#include "clsHttpResponse.h"

#include "../util/clsHttpDate.h"
#include "clsHttpStatus.h"
#include "../util/stringUtil.h"

namespace rizweb {

bool HttpResponse::setHeader(std::string_view name, std::string_view value)
{
    if (!stringUtil::isToken(name) || stringUtil::hasControlChars(value))
        return false;
    m_headers.set(name, std::string(value));
    return true;
}

bool HttpResponse::addHeader(std::string_view name, std::string_view value)
{
    if (!stringUtil::isToken(name) || stringUtil::hasControlChars(value))
        return false;
    m_headers.add(std::string(name), std::string(value));
    return true;
}

bool HttpResponse::setCookie(const Cookie& cookie)
{
    if (!stringUtil::isToken(cookie.name))
        return false;

    // cookie-octets: no control characters, whitespace, quotes, comma, semicolon or backslash
    for (unsigned char c : cookie.value) {
        if (c <= 0x20 || c >= 0x7f || c == '"' || c == ',' || c == ';' || c == '\\')
            return false;
    }
    if (stringUtil::hasControlChars(cookie.path) || cookie.path.find(';') != std::string::npos)
        return false;
    if (stringUtil::hasControlChars(cookie.domain) || cookie.domain.find(';') != std::string::npos)
        return false;

    std::string text = cookie.name + "=" + cookie.value;
    if (cookie.maxAgeSeconds >= 0)
        text += "; Max-Age=" + std::to_string(cookie.maxAgeSeconds);
    if (!cookie.path.empty())
        text += "; Path=" + cookie.path;
    if (!cookie.domain.empty())
        text += "; Domain=" + cookie.domain;
    if (!cookie.sameSite.empty())
        text += "; SameSite=" + cookie.sameSite;
    if (cookie.httpOnly)
        text += "; HttpOnly";
    if (cookie.secure)
        text += "; Secure";

    m_headers.add("Set-Cookie", std::move(text));
    return true;
}

void HttpResponse::setFileBody(UniqueFd fd, off_t offset, size_t length)
{
    m_file = std::move(fd);
    m_fileOffset = offset;
    m_fileLength = length;
    m_body.clear();
    m_shared.reset();
}

size_t HttpResponse::bodyLength() const
{
    if (m_file.valid())
        return m_fileLength;
    return m_shared ? m_shared->size() : m_body.size();
}

void HttpResponse::sendText(int status, std::string_view text, std::string_view contentType)
{
    m_status = status;
    setHeader("Content-Type", contentType);
    setBody(std::string(text));
}

void HttpResponse::sendError(int status, std::string_view message)
{
    std::string title = std::to_string(status) + " " + HttpStatus::reason(status);
    std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + title
                       + "</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"></head><body><h1>"
                       + title + "</h1>";
    if (!message.empty())
        html += "<p>" + stringUtil::htmlEscape(message) + "</p>";
    html += "<hr></body></html>";

    m_file.reset();
    sendHtml(status, html);
}

void HttpResponse::redirect(std::string_view location, int status)
{
    if (!setHeader("Location", location)) {
        sendError(500, "invalid redirect target");
        return;
    }
    m_status = status;
    m_file.reset();
    m_shared.reset();
    m_body.clear();
}

std::string HttpResponse::serializeHead(const HeadOptions& options) const
{
    std::string head;
    head.reserve(256 + m_headers.size() * 32);

    head += (options.versionMinor >= 1) ? "HTTP/1.1 " : "HTTP/1.0 ";
    head += std::to_string(m_status);
    head += ' ';
    head += HttpStatus::reason(m_status);
    head += "\r\nDate: ";
    head += HttpDate::now();
    head += "\r\n";

    if (!options.serverName.empty()) {
        head += "Server: ";
        head += options.serverName;
        head += "\r\n";
    }

    for (const HttpHeaders::Entry& entry : m_headers.entries()) {
        // these are owned by the connection
        if (stringUtil::equalsIgnoreCase(entry.first, "Content-Length")
            || (m_status != 101 && stringUtil::equalsIgnoreCase(entry.first, "Connection"))
            || stringUtil::equalsIgnoreCase(entry.first, "Transfer-Encoding")
            || stringUtil::equalsIgnoreCase(entry.first, "Date")
            || stringUtil::equalsIgnoreCase(entry.first, "Server"))
            continue;

        head += entry.first;
        head += ": ";
        head += entry.second;
        head += "\r\n";
    }

    if (options.includeContentLength) {
        head += "Content-Length: ";
        head += std::to_string(bodyLength());
        head += "\r\n";
    }

    if (m_status != 101) {
        if (!options.keepAlive)
            head += "Connection: close\r\n";
        else if (options.versionMinor == 0)
            head += "Connection: keep-alive\r\n";
    }

    head += "\r\n";
    return head;
}

void HttpResponse::reset()
{
    m_status = 200;
    m_headers.clear();
    m_body.clear();
    m_shared.reset();
    m_file.reset();
    m_fileOffset = 0;
    m_fileLength = 0;
}

} // namespace rizweb
