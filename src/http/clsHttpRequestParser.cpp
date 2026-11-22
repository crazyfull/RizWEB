#include "clsHttpRequestParser.h"

#include "../util/clsUrlCodec.h"
#include "../util/stringUtil.h"

#include <algorithm>

namespace rizweb {

namespace {

constexpr size_t kMaxChunkLine = 1024;
constexpr size_t kMaxTrailerBytes = 8 * 1024;

HttpMethod methodFromName(std::string_view name)
{
    if (name == "GET") return HttpMethod::get;
    if (name == "HEAD") return HttpMethod::head;
    if (name == "POST") return HttpMethod::post;
    if (name == "PUT") return HttpMethod::put;
    if (name == "PATCH") return HttpMethod::patch;
    if (name == "DELETE") return HttpMethod::del;
    if (name == "OPTIONS") return HttpMethod::options;
    return HttpMethod::unknown;
}

// digits only, no sign, at most 18 digits (fits in 64 bit)
bool parseDecimal(std::string_view text, size_t& value)
{
    if (text.empty() || text.size() > 18)
        return false;

    size_t result = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return false;
        result = result * 10 + static_cast<size_t>(c - '0');
    }
    value = result;
    return true;
}

bool parseHex(std::string_view text, size_t& value)
{
    if (text.empty() || text.size() > 15)
        return false;

    size_t result = 0;
    for (char c : text) {
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        result = (result << 4) | static_cast<size_t>(digit);
    }
    value = result;
    return true;
}

} // namespace

HttpRequestParser::HttpRequestParser(HttpParserLimits limits) : m_limits(limits)
{
}

void HttpRequestParser::reset()
{
    m_request.clear();
    m_state = State::head;
    m_head.clear();
    m_line.clear();
    m_terminatorState = 0;
    m_crlfProgress = 0;
    m_remaining = 0;
    m_trailerBytes = 0;
    m_contentLength = 0;
    m_chunked = false;
    m_started = false;
    m_headComplete = false;
    m_expectContinue = false;
    m_errorStatus = 400;
}

bool HttpRequestParser::fail(int status)
{
    m_state = State::failed;
    m_errorStatus = status;
    return false;
}

ParseStatus HttpRequestParser::feed(const char* data, size_t length, size_t& consumed)
{
    consumed = 0;
    if (m_state == State::failed)
        return ParseStatus::error;
    if (m_state == State::done)
        return ParseStatus::complete;

    size_t offset = 0;

    while (offset < length && m_state != State::done && m_state != State::failed) {
        switch (m_state) {
        case State::head: {
            // ignore empty lines in front of the request line (RFC 7230, 3.5)
            if (m_head.empty()) {
                while (offset < length && (data[offset] == '\r' || data[offset] == '\n'))
                    offset++;
                if (offset == length)
                    break;
            }
            m_started = true;

            // find the end of the header block without copying more than needed
            size_t end = offset;
            bool found = false;
            while (end < length) {
                char c = data[end++];
                switch (m_terminatorState) {
                case 0: m_terminatorState = (c == '\r') ? 1 : 0; break;
                case 1: m_terminatorState = (c == '\n') ? 2 : (c == '\r' ? 1 : 0); break;
                case 2: m_terminatorState = (c == '\r') ? 3 : 0; break;
                default: m_terminatorState = (c == '\n') ? 4 : (c == '\r' ? 1 : 0); break;
                }
                if (m_terminatorState == 4) {
                    found = true;
                    break;
                }
            }

            if (m_head.size() + (end - offset) > m_limits.maxHeaderBytes) {
                fail(431);
                break;
            }

            m_head.append(data + offset, end - offset);
            offset = end;

            if (found) {
                m_terminatorState = 0;
                parseHead();
            }
            break;
        }

        case State::bodyFixed: {
            size_t take = std::min(m_remaining, length - offset);
            m_request.body.append(data + offset, take);
            offset += take;
            m_remaining -= take;
            if (m_remaining == 0)
                m_state = State::done;
            break;
        }

        case State::chunkSize: {
            char c = data[offset++];
            if (c != '\n') {
                m_line += c;
                if (m_line.size() > kMaxChunkLine)
                    fail(400);
                break;
            }
            if (m_line.empty() || m_line.back() != '\r') {
                fail(400);
                break;
            }
            m_line.pop_back();
            parseChunkSizeLine();
            m_line.clear();
            break;
        }

        case State::chunkData: {
            size_t take = std::min(m_remaining, length - offset);
            m_request.body.append(data + offset, take);
            offset += take;
            m_remaining -= take;
            if (m_remaining == 0) {
                m_state = State::chunkDataEnd;
                m_crlfProgress = 0;
            }
            break;
        }

        case State::chunkDataEnd: {
            char c = data[offset++];
            if (m_crlfProgress == 0 && c == '\r') {
                m_crlfProgress = 1;
            } else if (m_crlfProgress == 1 && c == '\n') {
                m_state = State::chunkSize;
                m_line.clear();
            } else {
                fail(400);
            }
            break;
        }

        case State::trailers: {
            char c = data[offset++];
            if (++m_trailerBytes > kMaxTrailerBytes) {
                fail(431);
                break;
            }
            if (c != '\n') {
                m_line += c;
                break;
            }
            if (m_line.empty() || m_line.back() != '\r') {
                fail(400);
                break;
            }
            // trailer fields are ignored; an empty line ends the message
            if (m_line.size() == 1)
                m_state = State::done;
            m_line.clear();
            break;
        }

        default:
            break;
        }
    }

    consumed = offset;
    if (m_state == State::failed)
        return ParseStatus::error;
    return m_state == State::done ? ParseStatus::complete : ParseStatus::needMore;
}

bool HttpRequestParser::parseHead()
{
    std::string_view head(m_head);

    size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string_view::npos)
        return fail(400);

    if (!parseRequestLine(head.substr(0, lineEnd)))
        return false;

    // everything after the request line, including the final empty line
    if (!parseHeaderLines(head.substr(lineEnd + 2)))
        return false;

    return finishHead();
}

bool HttpRequestParser::parseRequestLine(std::string_view line)
{
    size_t firstSpace = line.find(' ');
    if (firstSpace == std::string_view::npos)
        return fail(400);
    size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos)
        return fail(400);

    std::string_view method = line.substr(0, firstSpace);
    std::string_view target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::string_view version = line.substr(secondSpace + 1);

    if (!stringUtil::isToken(method))
        return fail(400);
    if (target.empty() || version.find(' ') != std::string_view::npos)
        return fail(400);
    if (target.size() > m_limits.maxUriBytes)
        return fail(414);

    for (unsigned char c : target) {
        if (c <= 0x20 || c == 0x7f)
            return fail(400);
    }

    if (version == "HTTP/1.1")
        m_request.versionMinor = 1;
    else if (version == "HTTP/1.0")
        m_request.versionMinor = 0;
    else if (stringUtil::startsWith(version, "HTTP/"))
        return fail(505);
    else
        return fail(400);

    m_request.methodName.assign(method);
    m_request.method = methodFromName(method);
    m_request.target.assign(target);

    if (target == "*") {
        if (m_request.method != HttpMethod::options)
            return fail(400);
        m_request.path = "*";
        return true;
    }

    if (target[0] != '/' || target.find('#') != std::string_view::npos)
        return fail(400);

    size_t question = target.find('?');
    std::string_view rawPath = target.substr(0, question);
    if (question != std::string_view::npos)
        m_request.rawQuery.assign(target.substr(question + 1));

    std::string decoded;
    if (!UrlCodec::decode(rawPath, decoded, false))
        return fail(400);
    if (stringUtil::hasControlChars(decoded))
        return fail(400);
    if (!UrlCodec::normalizePath(decoded, m_request.path))
        return fail(400);

    return true;
}

bool HttpRequestParser::parseHeaderLines(std::string_view lines)
{
    size_t pos = 0;
    size_t count = 0;

    while (pos < lines.size()) {
        size_t end = lines.find("\r\n", pos);
        if (end == std::string_view::npos)
            return fail(400);

        std::string_view line = lines.substr(pos, end - pos);
        pos = end + 2;

        if (line.empty())
            break; // end of the header block

        if (line[0] == ' ' || line[0] == '\t')
            return fail(400); // obsolete line folding

        size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0)
            return fail(400);

        std::string_view name = line.substr(0, colon);
        if (!stringUtil::isToken(name))
            return fail(400); // also rejects "Name : value"

        std::string_view value = stringUtil::trim(line.substr(colon + 1));
        if (stringUtil::hasControlChars(value))
            return fail(400);

        if (++count > m_limits.maxHeaderCount)
            return fail(431);

        m_request.headers.add(std::string(name), std::string(value));
    }
    return true;
}

bool HttpRequestParser::finishHead()
{
    const HttpHeaders& headers = m_request.headers;

    // Host: exactly one for HTTP/1.1
    size_t hostCount = 0;
    for (const HttpHeaders::Entry& entry : headers.entries()) {
        if (stringUtil::equalsIgnoreCase(entry.first, "Host"))
            hostCount++;
    }
    if (hostCount > 1 || (m_request.versionMinor >= 1 && hostCount == 0))
        return fail(400);

    // Transfer-Encoding / Content-Length (request smuggling defence)
    bool hasTransferEncoding = false;
    bool hasContentLength = false;
    bool haveLength = false;
    size_t length = 0;

    for (const HttpHeaders::Entry& entry : headers.entries()) {
        if (stringUtil::equalsIgnoreCase(entry.first, "Transfer-Encoding")) {
            hasTransferEncoding = true;
            if (!stringUtil::equalsIgnoreCase(stringUtil::trim(entry.second), "chunked"))
                return fail(501);
            m_chunked = true;
        } else if (stringUtil::equalsIgnoreCase(entry.first, "Content-Length")) {
            hasContentLength = true;
            std::string_view list = entry.second;
            size_t p = 0;
            while (p <= list.size()) {
                size_t comma = list.find(',', p);
                if (comma == std::string_view::npos)
                    comma = list.size();

                size_t value = 0;
                if (!parseDecimal(stringUtil::trim(list.substr(p, comma - p)), value))
                    return fail(400);
                if (haveLength && value != length)
                    return fail(400);

                length = value;
                haveLength = true;
                p = comma + 1;
            }
        }
    }

    if (hasTransferEncoding && (hasContentLength || m_request.versionMinor < 1))
        return fail(400);

    if (m_chunked)
        m_request.headers.remove("Transfer-Encoding");
    m_contentLength = haveLength ? length : 0;
    if (m_contentLength > m_limits.maxBodyBytes)
        return fail(413);

    // persistent connection
    if (m_request.versionMinor >= 1)
        m_request.keepAlive = !headers.hasToken("Connection", "close");
    else
        m_request.keepAlive = headers.hasToken("Connection", "keep-alive");

    bool bodyExpected = m_chunked || m_contentLength > 0;

    if (const std::string* expect = headers.get("Expect")) {
        if (stringUtil::equalsIgnoreCase(*expect, "100-continue")) {
            m_expectContinue = bodyExpected && m_request.versionMinor >= 1;
        } else {
            return fail(417);
        }
    }

    m_headComplete = true;
    if (m_chunked) {
        m_state = State::chunkSize;
        m_line.clear();
    } else if (m_contentLength > 0) {
        m_state = State::bodyFixed;
        m_remaining = m_contentLength;
        m_request.body.reserve(std::min<size_t>(m_contentLength, 1 << 20));
    } else {
        m_state = State::done;
    }
    return true;
}

bool HttpRequestParser::parseChunkSizeLine()
{
    std::string_view line(m_line);

    // chunk extensions are ignored
    size_t semicolon = line.find(';');
    std::string_view sizeText = stringUtil::trim(line.substr(0, semicolon));

    size_t size = 0;
    if (!parseHex(sizeText, size))
        return fail(400);

    if (size == 0) {
        m_state = State::trailers;
        m_trailerBytes = 0;
        return true;
    }

    if (size > m_limits.maxBodyBytes || m_request.body.size() + size > m_limits.maxBodyBytes)
        return fail(413);

    m_remaining = size;
    m_state = State::chunkData;
    return true;
}

} // namespace rizweb
