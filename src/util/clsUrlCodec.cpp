#include "clsUrlCodec.h"

#include <vector>

namespace rizweb {

namespace {

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isUnreserved(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
           || c == '-' || c == '.' || c == '_' || c == '~';
}

std::string encode(std::string_view input, bool keepSlash)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        if (isUnreserved(c) || (keepSlash && c == '/')) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

} // namespace

bool UrlCodec::decode(std::string_view input, std::string& output, bool plusAsSpace)
{
    output.clear();
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        if (c == '%') {
            if (i + 2 >= input.size())
                return false;

            int high = hexValue(input[i + 1]);
            int low = hexValue(input[i + 2]);
            if (high < 0 || low < 0)
                return false;

            output += static_cast<char>((high << 4) | low);
            i += 2;
        } else if (c == '+' && plusAsSpace) {
            output += ' ';
        } else {
            output += c;
        }
    }
    return true;
}

std::string UrlCodec::encodePath(std::string_view input)
{
    return encode(input, true);
}

std::string UrlCodec::encodeComponent(std::string_view input)
{
    return encode(input, false);
}

bool UrlCodec::normalizePath(std::string_view decodedPath, std::string& output)
{
    output.clear();
    if (decodedPath.empty() || decodedPath[0] != '/')
        return false;

    if (decodedPath.find('\0') != std::string_view::npos || decodedPath.find('\\') != std::string_view::npos)
        return false;

    std::vector<std::string_view> segments;
    size_t pos = 1;
    bool trailingSlash = false;

    while (pos <= decodedPath.size()) {
        size_t slash = decodedPath.find('/', pos);
        bool last = (slash == std::string_view::npos);
        std::string_view segment = decodedPath.substr(pos, last ? std::string_view::npos : slash - pos);

        if (segment == "..") {
            if (segments.empty())
                return false;
            segments.pop_back();
            trailingSlash = true;
        } else if (segment == "." || segment.empty()) {
            trailingSlash = true;
        } else {
            segments.push_back(segment);
            trailingSlash = false;
        }

        if (last)
            break;
        pos = slash + 1;
    }

    output = "/";
    for (size_t i = 0; i < segments.size(); i++) {
        output.append(segments[i]);
        if (i + 1 < segments.size())
            output += '/';
    }

    if (!segments.empty() && trailingSlash)
        output += '/';

    return true;
}

} // namespace rizweb
