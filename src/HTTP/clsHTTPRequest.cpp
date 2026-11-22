#include "clsHTTPRequest.h"
#include "../Utils/clsStringUtils.h"
#include <cstdlib>
#include <vector>

namespace rizweb {

static HTTPMethod methodFromName(std::string_view name)
{
    if (name == "GET") return HTTPMethod::Get;
    if (name == "HEAD") return HTTPMethod::Head;
    if (name == "POST") return HTTPMethod::Post;
    if (name == "PUT") return HTTPMethod::Put;
    if (name == "DELETE") return HTTPMethod::Delete;
    if (name == "OPTIONS") return HTTPMethod::Options;
    if (name == "PATCH") return HTTPMethod::Patch;
    return HTTPMethod::Unknown;
}

static bool isTokenChar(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    return c != '\0' && std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

bool HTTPRequest::isDirectoryRequest() const
{
    return !m_urlPath.empty() && m_urlPath.back() == '/';
}

// Decodes %XX and resolves "." / ".." segments. A ".." that would climb above "/" is an error
// (that is the classic path traversal), as are NUL bytes, backslashes and control characters.
bool HTTPRequest::normalizePath(std::string_view rawPath, std::string &out)
{
    std::string decoded;
    if (!StringUtils::urlDecode(rawPath, decoded, false))
        return false;
    for (char c : decoded)
        if (c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            return false;
    if (decoded.empty() || decoded[0] != '/')
        return false;

    std::vector<std::string_view> segments;
    size_t start = 1;
    while (start <= decoded.size()) {
        size_t end = decoded.find('/', start);
        if (end == std::string::npos)
            end = decoded.size();
        std::string_view segment(decoded.data() + start, end - start);
        if (segment == "..") {
            if (segments.empty())
                return false;
            segments.pop_back();
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        start = end + 1;
    }

    out = "/";
    for (size_t i = 0; i < segments.size(); i++) {
        out.append(segments[i]);
        if (i + 1 < segments.size())
            out.push_back('/');
    }
    bool trailingSlash = decoded.back() == '/' || StringUtils::endsWith(decoded, "/.") || StringUtils::endsWith(decoded, "/..");
    if (trailingSlash && out.size() > 1)
        out.push_back('/');
    return true;
}

void HTTPRequest::parseUrlEncodedFields(std::string_view text)
{
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('&', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view pair = text.substr(start, end - start);
        if (!pair.empty()) {
            size_t equal = pair.find('=');
            std::string name, value;
            bool ok = StringUtils::urlDecode(pair.substr(0, equal), name, true);
            if (equal != std::string_view::npos)
                ok = ok && StringUtils::urlDecode(pair.substr(equal + 1), value, true);
            if (ok && !name.empty())
                m_query.addField(name, value);
        }
        start = end + 1;
    }
}

HTTPParseResult HTTPRequest::parse(const char *data, size_t length, size_t maxBodyBytes)
{
    std::string_view input(data, length);
    size_t headerEnd = input.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        if (length > kMaxHeaderBytes)
            return HTTPParseResult::HeaderTooLarge;
        return HTTPParseResult::Incomplete;
    }
    if (headerEnd > kMaxHeaderBytes)
        return HTTPParseResult::HeaderTooLarge;

    std::string_view head = input.substr(0, headerEnd);

    // request line
    size_t lineEnd = head.find("\r\n");
    std::string_view requestLine = head.substr(0, lineEnd);
    size_t firstSpace = requestLine.find(' ');
    size_t lastSpace = requestLine.rfind(' ');
    if (firstSpace == std::string_view::npos || lastSpace == firstSpace)
        return HTTPParseResult::BadRequest;
    std::string_view methodText = requestLine.substr(0, firstSpace);
    std::string_view target = requestLine.substr(firstSpace + 1, lastSpace - firstSpace - 1);
    std::string_view version = requestLine.substr(lastSpace + 1);

    for (char c : methodText)
        if (!isTokenChar(c))
            return HTTPParseResult::BadRequest;
    if (methodText.empty() || methodText.size() > 16)
        return HTTPParseResult::BadRequest;
    if (target.size() > kMaxUrlLength)
        return HTTPParseResult::UriTooLong;
    if (version == "HTTP/1.1")
        m_http11 = true;
    else if (version == "HTTP/1.0")
        m_http11 = false;
    else
        return StringUtils::startsWith(version, "HTTP/") ? HTTPParseResult::VersionNotSupported : HTTPParseResult::BadRequest;

    m_methodName = std::string(methodText);
    m_method = methodFromName(methodText);

    // target: origin-form only ("/path?query"), plus "*" for OPTIONS
    if (target == "*" && m_method == HTTPMethod::Options) {
        m_urlPath = "*";
    } else {
        if (target.empty() || target[0] != '/')
            return HTTPParseResult::BadRequest;
        size_t question = target.find('?');
        std::string_view rawPath = target.substr(0, question);
        if (question != std::string_view::npos)
            m_queryString = std::string(target.substr(question + 1));
        if (!normalizePath(rawPath, m_urlPath))
            return HTTPParseResult::BadRequest;
        parseUrlEncodedFields(m_queryString);
    }

    // header fields
    size_t position = (lineEnd == std::string_view::npos) ? head.size() : lineEnd + 2;
    while (position < head.size()) {
        size_t end = head.find("\r\n", position);
        if (end == std::string_view::npos)
            end = head.size();
        std::string_view line = head.substr(position, end - position);
        position = end + 2;
        if (line.empty())
            continue;
        size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0)
            return HTTPParseResult::BadRequest;
        std::string_view name = line.substr(0, colon);
        for (char c : name)
            if (!isTokenChar(c))
                return HTTPParseResult::BadRequest;    // also rejects "Name : value" and folded lines
        std::string_view value = StringUtils::trim(line.substr(colon + 1));
        for (char c : value)
            if (c == '\0')
                return HTTPParseResult::BadRequest;
        if (!m_headers.addField(name, value))
            return HTTPParseResult::HeaderTooLarge;
    }

    // Host
    if (const std::string *host = m_headers.fieldByName("host")) {
        std::string_view text = *host;
        if (!text.empty() && text[0] == '[') {
            size_t close = text.find(']');
            m_host = StringUtils::toLower(text.substr(0, close == std::string_view::npos ? text.size() : close + 1));
        } else {
            m_host = StringUtils::toLower(text.substr(0, text.find(':')));
        }
    }

    // connection persistence
    const std::string *connection = m_headers.fieldByName("connection");
    if (m_http11)
        m_keepAlive = !(connection && StringUtils::containsIgnoreCase(*connection, "close"));
    else
        m_keepAlive = connection && StringUtils::containsIgnoreCase(*connection, "keep-alive");

    // body (Content-Length only; chunked uploads are refused instead of mis-parsed)
    size_t bodyLength = 0;
    const std::string *contentLength = m_headers.fieldByName("content-length");
    if (m_headers.fieldByName("transfer-encoding"))
        return contentLength ? HTTPParseResult::BadRequest : HTTPParseResult::NotImplemented;
    if (contentLength) {
        if (contentLength->empty() || contentLength->size() > 12)
            return HTTPParseResult::BadRequest;
        for (char c : *contentLength)
            if (c < '0' || c > '9')
                return HTTPParseResult::BadRequest;
        bodyLength = static_cast<size_t>(strtoull(contentLength->c_str(), nullptr, 10));
        if (bodyLength > maxBodyBytes)
            return HTTPParseResult::BodyTooLarge;
    }

    size_t bodyStart = headerEnd + 4;
    if (length - bodyStart < bodyLength)
        return HTTPParseResult::Incomplete;

    m_body.assign(data + bodyStart, bodyLength);
    m_consumed = bodyStart + bodyLength;

    const std::string *contentType = m_headers.fieldByName("content-type");
    if (bodyLength > 0 && contentType && StringUtils::startsWith(StringUtils::toLower(*contentType), "application/x-www-form-urlencoded"))
        parseUrlEncodedFields(m_body);

    return HTTPParseResult::Completed;
}

} // namespace rizweb
