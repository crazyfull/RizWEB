#include "stringUtil.h"

#include <cstdio>

namespace rizweb {
namespace stringUtil {

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); i++) {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i]))
            return false;
    }
    return true;
}

std::string toLower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = toLowerAscii(c);
    return out;
}

std::string_view trim(std::string_view text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
        begin++;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        end--;
    return text.substr(begin, end - begin);
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool containsToken(std::string_view headerValue, std::string_view token)
{
    size_t pos = 0;
    while (pos <= headerValue.size()) {
        size_t comma = headerValue.find(',', pos);
        if (comma == std::string_view::npos)
            comma = headerValue.size();

        if (equalsIgnoreCase(trim(headerValue.substr(pos, comma - pos)), token))
            return true;

        pos = comma + 1;
    }
    return false;
}

bool isToken(std::string_view text)
{
    if (text.empty())
        return false;

    for (char c : text) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) {
            switch (c) {
            case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
            case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
                ok = true;
                break;
            default:
                break;
            }
        }
        if (!ok)
            return false;
    }
    return true;
}

bool hasControlChars(std::string_view text)
{
    for (unsigned char c : text) {
        if ((c < 0x20 && c != '\t') || c == 0x7f)
            return true;
    }
    return false;
}

bool isValidUtf8(std::string_view text)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
    const size_t n = text.size();
    size_t i = 0;

    while (i < n) {
        unsigned char c = p[i];
        if (c < 0x80) {
            i++;
            continue;
        }

        size_t extra = 0;
        uint32_t codePoint = 0;
        uint32_t minimum = 0;
        if ((c & 0xE0) == 0xC0) { extra = 1; codePoint = c & 0x1F; minimum = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; codePoint = c & 0x0F; minimum = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; codePoint = c & 0x07; minimum = 0x10000; }
        else return false;

        if (i + extra >= n)
            return false;

        for (size_t k = 1; k <= extra; k++) {
            if ((p[i + k] & 0xC0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (p[i + k] & 0x3F);
        }

        if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            return false;

        i += extra + 1;
    }
    return true;
}

std::string htmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string formatSize(uint64_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }

    char buffer[32];
    if (unit == 0)
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    return buffer;
}

} // namespace stringUtil
} // namespace rizweb
