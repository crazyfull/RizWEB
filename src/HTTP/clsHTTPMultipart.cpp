#include "clsHTTPMultipart.h"
#include "../Utils/clsStringUtils.h"
#include <algorithm>
#include <string_view>

namespace rizweb {

HTTPMultipartParser::HTTPMultipartParser(const std::string &boundary) : m_delimiter("\r\n--" + boundary)
{
    m_carry = "\r\n";        // the first delimiter has no CRLF in front of it
}

HTTPMultipartParser::Status HTTPMultipartParser::fail(const char *message)
{
    m_error = message;
    return Status::Error;
}

bool HTTPMultipartParser::boundaryFromContentType(const std::string &contentType, std::string &boundary)
{
    std::string_view text = contentType;
    size_t semicolon = text.find(';');
    if (!StringUtils::equalsIgnoreCase(StringUtils::trim(text.substr(0, semicolon)), "multipart/form-data") || semicolon == std::string_view::npos)
        return false;
    std::string lower = StringUtils::toLower(text);
    size_t at = lower.find("boundary=", semicolon);
    if (at == std::string::npos)
        return false;
    std::string_view value = text.substr(at + 9);
    std::string result;
    if (!value.empty() && value[0] == '"') {
        size_t close = value.find('"', 1);
        if (close == std::string_view::npos)
            return false;
        result = std::string(value.substr(1, close - 1));
    } else {
        size_t end = value.find_first_of("; \t");
        result = std::string(value.substr(0, end));
    }
    if (result.empty() || result.size() > 70)
        return false;
    for (char c : result)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            return false;
    boundary = result;
    return true;
}

std::string HTTPMultipartParser::sanitizeFileName(const std::string &name)
{
    size_t slash = name.find_last_of("/\\");
    std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
    std::string clean;
    for (char c : base)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|')
            clean.push_back(c);
    while (!clean.empty() && (clean.back() == '.' || clean.back() == ' '))
        clean.pop_back();
    size_t start = clean.find_first_not_of(". ");
    clean = start == std::string::npos ? std::string() : clean.substr(start);
    if (clean.size() > 255) {
        // keep the extension, cut the middle (never inside a UTF-8 sequence)
        size_t dot = clean.rfind('.');
        std::string extension = (dot != std::string::npos && clean.size() - dot <= 16) ? clean.substr(dot) : std::string();
        size_t keep = 255 - extension.size();
        while (keep > 0 && (static_cast<unsigned char>(clean[keep]) & 0xC0) == 0x80)
            keep--;
        clean = clean.substr(0, keep) + extension;
    }
    return clean.empty() ? std::string("file") : clean;
}

// ---------------------------------------------------------------------------------------------

namespace {

// Parses "form-data; name="a"; filename="b.txt"" style parameters. Handles quotes, \" escapes and filename* (RFC 5987).
void parseDisposition(std::string_view value, MultipartPart &part)
{
    size_t position = value.find(';');
    while (position != std::string_view::npos && position < value.size()) {
        position++;
        while (position < value.size() && (value[position] == ' ' || value[position] == '\t'))
            position++;
        size_t equal = value.find('=', position);
        if (equal == std::string_view::npos)
            break;
        std::string key = StringUtils::toLower(StringUtils::trim(value.substr(position, equal - position)));
        std::string text;
        size_t next;
        if (equal + 1 < value.size() && value[equal + 1] == '"') {
            size_t i = equal + 2;
            for (; i < value.size() && value[i] != '"'; i++) {
                if (value[i] == '\\' && i + 1 < value.size())
                    i++;
                text.push_back(value[i]);
            }
            next = value.find(';', i);
        } else {
            next = value.find(';', equal + 1);
            text = std::string(StringUtils::trim(value.substr(equal + 1, next == std::string_view::npos ? std::string_view::npos : next - equal - 1)));
        }
        if (key == "name") {
            part.name = text;
        } else if (key == "filename") {
            if (part.fileName.empty())
                part.fileName = text;
            part.isFile = true;
        } else if (key == "filename*") {
            size_t quote = text.find('\'');
            size_t second = quote == std::string::npos ? std::string::npos : text.find('\'', quote + 1);
            if (second != std::string::npos) {
                std::string decoded;
                if (StringUtils::urlDecode(text.substr(second + 1), decoded, false)) {
                    part.fileName = decoded;
                    part.isFile = true;
                }
            }
        }
        position = next;
    }
}

} // namespace

bool HTTPMultipartParser::parsePartHeaders(const std::string &block, MultipartPart &part)
{
    bool hasDisposition = false;
    size_t position = 0;
    while (position < block.size()) {
        size_t end = block.find("\r\n", position);
        if (end == std::string::npos)
            end = block.size();
        std::string_view line(block.data() + position, end - position);
        position = end + 2;
        size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            return false;
        std::string name = StringUtils::toLower(StringUtils::trim(line.substr(0, colon)));
        std::string_view value = StringUtils::trim(line.substr(colon + 1));
        if (name == "content-disposition") {
            hasDisposition = true;
            if (!StringUtils::startsWith(StringUtils::toLower(value), "form-data"))
                return false;
            parseDisposition(value, part);
        } else if (name == "content-type") {
            part.contentType = std::string(value);
        }
    }
    return hasDisposition;
}

HTTPMultipartParser::Status HTTPMultipartParser::feed(const char *data, size_t length, MultipartHandler &handler)
{
    const size_t delimiterSize = m_delimiter.size();
    while (true) {
        switch (m_state) {
        case State::Epilogue:
            return Status::Finished;

        case State::Preamble:
        case State::Body: {
            const bool inBody = m_state == State::Body;
            bool found = false;

            // 1. delimiter split between the saved tail and the new data
            if (!m_carry.empty() && length > 0) {
                std::string stitched = m_carry;
                stitched.append(data, std::min(length, delimiterSize - 1));
                size_t at = stitched.find(m_delimiter);
                if (at != std::string::npos) {
                    if (inBody && at > 0 && !handler.onPartData(stitched.data(), at))
                        return fail("aborted");
                    size_t used = at + delimiterSize - m_carry.size();
                    data += used;
                    length -= used;
                    m_carry.clear();
                    found = true;
                } else if (length < delimiterSize - 1) {
                    // too little data to decide: everything is a possible delimiter prefix; keep the tail, pass on the rest
                    size_t keep = std::min(stitched.size(), delimiterSize - 1);
                    if (inBody && stitched.size() > keep && !handler.onPartData(stitched.data(), stitched.size() - keep))
                        return fail("aborted");
                    m_carry.assign(stitched, stitched.size() - keep, keep);
                    return Status::NeedMore;
                } else {
                    if (inBody && !handler.onPartData(m_carry.data(), m_carry.size()))
                        return fail("aborted");
                    m_carry.clear();
                }
            } else if (!m_carry.empty()) {
                // no new data: the tail stays (nothing can be decided yet)
                return Status::NeedMore;
            }

            // 2. delimiter inside the new data (zero copy)
            if (!found) {
                size_t at = std::string_view(data, length).find(m_delimiter);
                if (at != std::string_view::npos) {
                    if (inBody && at > 0 && !handler.onPartData(data, at))
                        return fail("aborted");
                    data += at + delimiterSize;
                    length -= at + delimiterSize;
                    found = true;
                } else {
                    // 3. keep a possible delimiter prefix, hand out the rest
                    size_t keep = std::min(length, delimiterSize - 1);
                    if (inBody && length > keep && !handler.onPartData(data, length - keep))
                        return fail("aborted");
                    m_carry.assign(data + length - keep, keep);
                    return Status::NeedMore;
                }
            }

            if (inBody && !handler.onPartEnd())
                return fail("aborted");
            m_state = State::AfterDelimiter;
            m_carry.clear();
            break;
        }

        case State::AfterDelimiter:
            while (m_carry.size() < 2 && length > 0) {
                m_carry.push_back(*data++);
                length--;
            }
            if (m_carry.size() < 2)
                return Status::NeedMore;
            if (m_carry == "\r\n") {
                m_state = State::Headers;
            } else if (m_carry == "--") {
                m_state = State::Epilogue;
            } else {
                return fail("malformed boundary line");
            }
            m_carry.clear();
            break;

        case State::Headers: {
            if (length == 0)
                return Status::NeedMore;
            size_t before = m_carry.size();
            size_t room = kMaxPartHeaderBytes + 4 - before;
            size_t take = std::min(length, room);
            m_carry.append(data, take);
            size_t from = before >= 3 ? before - 3 : 0;
            size_t end = m_carry.find("\r\n\r\n", from);
            if (end == std::string::npos) {
                if (m_carry.size() >= kMaxPartHeaderBytes + 4)
                    return fail("part headers too large");
                data += take;
                length -= take;
                return Status::NeedMore;
            }
            size_t used = end + 4 - before;
            data += used;
            length -= used;
            MultipartPart part;
            std::string block = m_carry.substr(0, end);
            m_carry.clear();
            if (!parsePartHeaders(block, part))
                return fail("malformed part headers");
            m_state = State::Body;
            if (!handler.onPartBegin(part))
                return fail("aborted");
            break;
        }
        }
    }
}

} // namespace rizweb
