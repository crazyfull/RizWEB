#include "clsHTTPResponse.h"
#include <charconv>
#include "../Utils/clsEscape.h"
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
    m_headers.release();
    m_body.release();
    m_statusCode = 0;
    m_committed = false;
    m_deferred = false;
    m_head = request.method() == HTTPMethod::Head;
    m_keepAlive = request.keepAlive();
    m_http11 = request.isHttp11();
}

void HTTPResponse::writeEscaped(std::string_view untrusted)
{
    m_body.append(Escape::html(untrusted));
}

void HTTPResponse::write(long long number)
{
    char digits[24];
    auto result = std::to_chars(digits, digits + sizeof(digits), number);
    m_body.append(digits, static_cast<size_t>(result.ptr - digits));
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
    m_headers.append(name);
    m_headers.append(": ", 2);
    m_headers.append(value);
    m_headers.append("\r\n", 2);
}

void HTTPResponse::addHeaderField(std::string_view name, uint64_t value)
{
    if (name.empty() || hasIllegalChars(name))
        return;
    m_headers.append(name);
    m_headers.append(": ", 2);
    m_headers.appendNumber(value);
    m_headers.append("\r\n", 2);
}

void HTTPResponse::setHeaderField(std::string_view name, std::string_view value)
{
    // remove an existing line with the same name, then append
    size_t position = 0;
    while (position < m_headers.size()) {
        std::string_view rest = m_headers.view().substr(position);
        size_t end = rest.find("\r\n");
        if (end == std::string_view::npos)
            break;
        std::string_view line = rest.substr(0, end);
        size_t colon = line.find(':');
        if (colon != std::string_view::npos && StringUtils::equalsIgnoreCase(line.substr(0, colon), name)) {
            m_headers.erase(position, end + 2);
            continue;
        }
        position += end + 2;
    }
    addHeaderField(name, value);
}

void HTTPResponse::setCookie(std::string_view name, std::string_view value, const CookieOptions &options)
{
    std::string cookie = HTTPCookie::buildSetCookie(name, value, options);
    if (!cookie.empty())
        addHeaderField("Set-Cookie", cookie);
}

void HTTPResponse::buildHead(ResponseBuffer &out, int statusCode, bool hasLength, uint64_t length)
{
    out.append("HTTP/1.1 ", 9);
    out.appendNumber(static_cast<uint64_t>(statusCode));
    out.append(' ');
    out.append(HTTPStatusCode::reasonPhrase(statusCode));
    out.append("\r\nDate: ", 8);
    out.append(cachedDate());
    out.append("\r\nServer: " SERVER_NAME "\r\n");
    out.append(m_headers.view());
    if (hasLength) {
        out.append("Content-Length: ", 16);
        out.appendNumber(length);
        out.append("\r\n", 2);
    }
    if (statusCode != 101) {
        if (!m_keepAlive)
            out.append("Connection: close\r\n");
        else if (!m_http11)
            out.append("Connection: keep-alive\r\n");
    }
    out.append("\r\n", 2);
}

void HTTPResponse::finish()
{
    m_committed = true;
    m_headers.release();
    m_body.release();
    m_client.onResponseSent(m_keepAlive);
}

void HTTPResponse::flush(int statusCode)
{
    if (m_committed)
        return;
    m_statusCode = statusCode;
    bool bodyAllowed = !(statusCode == 204 || statusCode == 304 || (statusCode >= 100 && statusCode < 200));
    ResponseBuffer head;
    buildHead(head, statusCode, bodyAllowed, m_body.size());
    // head and body leave in one writev(2) (plain TCP) or one TLS record: nothing is concatenated first
    if (bodyAllowed && !m_head)
        m_client.sendParts(head.view(), m_body.view());
    else
        m_client.sendBuffer(head.view());
    finish();
}

void HTTPResponse::sendHeaders(int statusCode, bool hasLength, uint64_t length)
{
    if (m_committed)
        return;
    m_statusCode = statusCode;
    ResponseBuffer head;
    buildHead(head, statusCode, hasLength, length);
    m_client.sendBuffer(head.view());
    // The caller sends the body afterwards and calls HTTPClient::onResponseSent() itself.
    m_committed = true;
    m_headers.release();
    m_body.release();
}

void HTTPResponse::sendSharedBody(int statusCode, std::shared_ptr<const std::string> body)
{
    if (m_committed)
        return;
    m_statusCode = statusCode;
    ResponseBuffer head;
    buildHead(head, statusCode, true, body->size());
    m_client.sendBuffer(head.view());
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

std::shared_ptr<AsyncResponse> HTTPResponse::defer()
{
    if (m_committed || m_deferred)
        return nullptr;
    std::shared_ptr<AsyncResponse> handle = m_client.beginDeferred();
    if (handle)
        m_deferred = true;
    return handle;
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
    const char *reason = HTTPStatusCode::reasonPhrase(statusCode);
    m_body.clear();
    m_body.append("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>");
    m_body.appendNumber(static_cast<uint64_t>(statusCode));
    m_body.append(' ');
    m_body.append(reason);
    m_body.append("</title><style>*{box-sizing:border-box;caret-color:transparent !important;}body{font-family:system-ui,-apple-system,sans-serif;background:radial-gradient(circle at top,#1e293b 0%,#090d16 100%);color:#f8fafc;display:grid;place-items:center;min-height:100vh;text-align:center;padding:20px;}.error{width:100%;max-width:440px;text-align:center;}.code{margin:0;color:#3baff6;font-size:clamp(80px,20vw,120px);font-weight:800;line-height:1;}footer{position:fixed;bottom:20px;left:0;width:100%;color:#64748b;font-size:11px;text-align:center;}h1{margin:24px 0 10px;font-size:24px;font-weight:650;}p{margin:0;color:#94a3b8;font-size:15px;line-height:1.7;}@media (max-width:480px){h1{font-size:21px;}p{font-size:14px;}}</style></head><body><main class=\"error\"><div class=\"code\">");
    m_body.appendNumber(static_cast<uint64_t>(statusCode));
    m_body.append("</div><h1>");
    m_body.append(reason);
    m_body.append("</h1>");
    if (!detail.empty()) {
        m_body.append("<p>");
        m_body.append(Escape::html(detail));
        m_body.append("</p>");
    }
    m_body.append("</main><footer>Powered by RizWEB</footer></body></html>");
    setHeaderField("Content-Type", "text/html; charset=utf-8");
    flush(statusCode);
}

void HTTPResponse::authenticationFailed(std::string_view message)
{
    setHeaderField("Content-Type", "text/plain; charset=utf-8");
    m_body.clear();
    m_body.append(message.empty() ? std::string_view("Authentication failed!") : message);
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
    ResponseBuffer head;
    buildHead(head, 101, false, 0);
    m_client.sendBuffer(head.view());
    m_committed = true;
    m_headers.clear();
}

} // namespace rizweb
