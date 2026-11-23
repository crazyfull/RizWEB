#include "clsHTTPResponse.h"
#include "clsHTTPStatusCode.h"
#include "../Utils/clsCrypto.h"
#include "../Utils/clsFileUtils.h"
#include "../Utils/clsStringUtils.h"
#include "../clsHTTPClient.h"

#ifndef SERVER_NAME
#define SERVER_NAME "RizWEB"
#endif

namespace rizweb {

static const std::string &cachedDate()
{
    thread_local time_t last = 0;
    thread_local std::string text;
    time_t now = time(nullptr);
    if (now != last) {
        last = now;
        text = FileUtils::formatHttpDate(now);
    }
    return text;
}

HTTPResponse::HTTPResponse(HTTPClient &client) : m_client(client) {}

void HTTPResponse::begin(const HTTPRequest &request)
{
    m_headers.clear();
    m_body.clear();
    m_statusCode = 0;
    m_committed = false;
    m_head = request.method() == HTTPMethod::Head;
    m_keepAlive = request.keepAlive();
    m_http11 = request.isHttp11();
}

static bool hasIllegalChars(std::string_view text)
{
    for (char c : text)
        if (c == '\r' || c == '\n' || c == '\0')
            return true;
    return false;
}

void HTTPResponse::addHeaderField(std::string_view name, std::string_view value)
{
    if (name.empty() || hasIllegalChars(name) || hasIllegalChars(value))   // blocks header injection
        return;
    m_headers.append(name).append(": ").append(value).append("\r\n");
}

void HTTPResponse::addHeaderField(std::string_view name, uint64_t value)
{
    addHeaderField(name, std::to_string(value));
}

void HTTPResponse::setHeaderField(std::string_view name, std::string_view value)
{
    // remove an existing line with the same name, then append
    size_t position = 0;
    while (position < m_headers.size()) {
        size_t end = m_headers.find("\r\n", position);
        if (end == std::string::npos)
            break;
        std::string_view line(m_headers.data() + position, end - position);
        size_t colon = line.find(':');
        if (colon != std::string_view::npos && StringUtils::equalsIgnoreCase(line.substr(0, colon), name)) {
            m_headers.erase(position, end + 2 - position);
            continue;
        }
        position = end + 2;
    }
    addHeaderField(name, value);
}

void HTTPResponse::setCookie(std::string_view name, std::string_view value, const CookieOptions &options)
{
    std::string cookie = HTTPCookie::buildSetCookie(name, value, options);
    if (!cookie.empty())
        addHeaderField("Set-Cookie", cookie);
}

void HTTPResponse::buildHead(std::string &out, int statusCode, bool hasLength, uint64_t length)
{
    out.reserve(256 + m_headers.size());
    out.append("HTTP/1.1 ").append(std::to_string(statusCode)).append(" ").append(HTTPStatusCode::reasonPhrase(statusCode)).append("\r\n");
    out.append("Date: ").append(cachedDate()).append("\r\n");
    out.append("Server: " SERVER_NAME "\r\n");
    out.append(m_headers);
    if (hasLength)
        out.append("Content-Length: ").append(std::to_string(length)).append("\r\n");
    if (statusCode != 101) {
        if (!m_keepAlive)
            out.append("Connection: close\r\n");
        else if (!m_http11)
            out.append("Connection: keep-alive\r\n");
    }
    out.append("\r\n");
}

void HTTPResponse::finish()
{
    m_committed = true;
    m_headers.clear();
    m_body.clear();
    m_client.onResponseSent(m_keepAlive);
}

void HTTPResponse::flush(int statusCode)
{
    if (m_committed)
        return;
    m_statusCode = statusCode;
    bool bodyAllowed = !(statusCode == 204 || statusCode == 304 || (statusCode >= 100 && statusCode < 200));
    std::string out;
    buildHead(out, statusCode, bodyAllowed, m_body.size());
    if (bodyAllowed && !m_head)
        out.append(m_body);
    m_client.sendBuffer(out);
    finish();
}

void HTTPResponse::sendHeaders(int statusCode, bool hasLength, uint64_t length)
{
    if (m_committed)
        return;
    m_statusCode = statusCode;
    std::string out;
    buildHead(out, statusCode, hasLength, length);
    m_client.sendBuffer(out);
    // The caller sends the body afterwards and calls HTTPClient::onResponseSent() itself.
    m_committed = true;
    m_headers.clear();
    m_body.clear();
}

void HTTPResponse::sendSharedBody(int statusCode, std::shared_ptr<const std::string> body)
{
    if (m_committed)
        return;
    m_statusCode = statusCode;
    std::string out;
    buildHead(out, statusCode, true, body->size());
    m_client.sendBuffer(out);
    if (!m_head)
        m_client.sendShared(std::move(body));
    finish();
}

void HTTPResponse::redirect(std::string_view url, int statusCode)
{
    setHeaderField("Location", url);
    sendError(statusCode, {});
}

void HTTPResponse::complete()
{
    m_client.onResponseSent(m_keepAlive);
}

void HTTPResponse::abort()
{
    m_committed = true;
    m_client.close(true);
}

void HTTPResponse::sendError(int statusCode, std::string_view detail)
{
    if (m_committed)
        return;
    std::string title = std::to_string(statusCode) + " " + HTTPStatusCode::reasonPhrase(statusCode);
    std::string page = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + title +
                       "</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"></head><body><h1>" + title + "</h1>";
    if (!detail.empty())
        page += "<p>" + StringUtils::htmlEscape(detail) + "</p>";
    page += "<hr></body></html>";
    setHeaderField("Content-Type", "text/html; charset=utf-8");
    m_body = std::move(page);
    flush(statusCode);
}

void HTTPResponse::authenticationFailed(std::string_view message)
{
    setHeaderField("Content-Type", "text/plain; charset=utf-8");
    m_body.assign(message.empty() ? "Authentication failed!" : message);
    flush(401);
}

void HTTPResponse::methodNotAllowed(std::string_view allow)
{
    setHeaderField("Allow", allow);
    sendError(405, {});
}

void HTTPResponse::switchProtocol(const std::string &webSocketKey)
{
    std::string accept = Crypto::base64Encode(Crypto::sha1((webSocketKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), webSocketKey.size() + 36).data(), 20);
    addHeaderField("Upgrade", "websocket");
    addHeaderField("Connection", "Upgrade");
    addHeaderField("Sec-WebSocket-Accept", accept);
    m_body.clear();
    m_statusCode = 101;
    std::string out;
    buildHead(out, 101, false, 0);
    m_client.sendBuffer(out);
    m_committed = true;
    m_headers.clear();
}

} // namespace rizweb
