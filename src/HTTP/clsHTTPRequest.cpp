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

bool HTTPRequest::expectsContinue() const
{
    const std::string *expect = m_headers.fieldByName("expect");
    return expect && StringUtils::equalsIgnoreCase(StringUtils::trim(*expect), "100-continue");
}

std::string HTTPRequest::param(std::string_view name) const
{
    for (const auto &item : m_params)
        if (item.first == name)
            return item.second;
    return {};
}

// Chunked body: size-line CRLF data CRLF ... 0 CRLF [trailers] CRLF. Trailers are skipped.
int HTTPRequest::decodeChunked(const char *data, size_t length, size_t limit, std::string &body, size_t &used)
{
    size_t position = 0;
    body.clear();
    while (true) {
        size_t lineEnd = std::string_view(data + position, length - position).find("\r\n");
        if (lineEnd == std::string_view::npos)
            return (length - position > 80) ? -1 : 0;
        std::string_view line(data + position, lineEnd);
        size_t extension = line.find(';');
        if (extension != std::string_view::npos)
            line = line.substr(0, extension);
        line = StringUtils::trim(line);
        if (line.empty() || line.size() > 8)
            return -1;
        size_t chunk = 0;
        for (char c : line) {
            int digit = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (digit < 0)
                return -1;
            chunk = chunk * 16 + static_cast<size_t>(digit);
        }
        position += lineEnd + 2;
        if (chunk == 0) {
            // trailer section ends with an empty line
            while (true) {
                size_t end = std::string_view(data + position, length - position).find("\r\n");
                if (end == std::string_view::npos)
                    return (length - position > 8192) ? -1 : 0;
                position += end + 2;
                if (end == 0) {
                    used = position;
                    return 1;
                }
            }
        }
        if (body.size() + chunk > limit)
            return -2;
        if (length - position < chunk + 2)
            return 0;
        body.append(data + position, chunk);
        position += chunk;
        if (data[position] != '\r' || data[position + 1] != '\n')
            return -1;
        position += 2;
    }
}

HTTPParseResult HTTPRequest::parse(const char *data, size_t length, size_t maxBodyBytes, const BodyLimitFunction *limitFor)
{
    HTTPParseResult head = parseHead(data, length);
    if (head != HTTPParseResult::Completed)
        return head;
    return parseBody(data, length, maxBodyBytes, limitFor);
}

HTTPParseResult HTTPRequest::parseHead(const char *data, size_t length)
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
    m_headBytes = headerEnd + 4;

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

    // body framing: Content-Length or chunked (both together is a smuggling attempt)
    const std::string *contentLength = m_headers.fieldByName("content-length");
    const std::string *transferEncoding = m_headers.fieldByName("transfer-encoding");
    if (transferEncoding) {
        if (contentLength || !StringUtils::equalsIgnoreCase(StringUtils::trim(*transferEncoding), "chunked"))
            return contentLength ? HTTPParseResult::BadRequest : HTTPParseResult::NotImplemented;
        m_bodyKind = BodyKind::Chunked;
    } else if (contentLength) {
        if (contentLength->empty() || contentLength->size() > 18)
            return HTTPParseResult::BadRequest;
        for (char c : *contentLength)
            if (c < '0' || c > '9')
                return HTTPParseResult::BadRequest;
        m_contentLength = strtoull(contentLength->c_str(), nullptr, 10);
        m_bodyKind = m_contentLength > 0 ? BodyKind::ContentLength : BodyKind::None;
    }
    return HTTPParseResult::Completed;
}

HTTPParseResult HTTPRequest::parseBody(const char *data, size_t length, size_t maxBodyBytes, const BodyLimitFunction *limitFor)
{
    const size_t bodyStart = m_headBytes;
    size_t limit = maxBodyBytes;
    size_t bodyLength = 0;
    if (m_bodyKind != BodyKind::None && limitFor && *limitFor)
        limit = (*limitFor)(m_method, m_urlPath);

    if (m_bodyKind == BodyKind::Chunked) {
        size_t used = 0;
        int state = decodeChunked(data + bodyStart, length - bodyStart, limit, m_body, used);
        if (state == 0)
            return HTTPParseResult::Incomplete;
        if (state == -2)
            return HTTPParseResult::BodyTooLarge;
        if (state < 0)
            return HTTPParseResult::BadRequest;
        m_consumed = bodyStart + used;
        bodyLength = m_body.size();
    } else {
        bodyLength = static_cast<size_t>(m_contentLength);
        if (m_contentLength > limit)
            return HTTPParseResult::BodyTooLarge;
        if (length - bodyStart < bodyLength)
            return HTTPParseResult::Incomplete;
        m_body.assign(data + bodyStart, bodyLength);
        m_consumed = bodyStart + bodyLength;
    }

    const std::string *contentType = m_headers.fieldByName("content-type");
    if (bodyLength > 0 && contentType && StringUtils::startsWith(StringUtils::toLower(*contentType), "application/x-www-form-urlencoded"))
        parseUrlEncodedFields(m_body);

    return HTTPParseResult::Completed;
}

} // namespace rizweb
