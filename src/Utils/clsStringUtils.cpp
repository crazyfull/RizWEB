#include "clsStringUtils.h"
#include <cstdio>

namespace rizweb {

char StringUtils::toLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

std::string StringUtils::toLower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = toLowerAscii(c);
    return out;
}

bool StringUtils::equalsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (toLowerAscii(a[i]) != toLowerAscii(b[i]))
            return false;
    return true;
}

bool StringUtils::startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtils::endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool StringUtils::containsIgnoreCase(std::string_view text, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > text.size())
        return false;
    for (size_t i = 0; i + needle.size() <= text.size(); i++)
        if (equalsIgnoreCase(text.substr(i, needle.size()), needle))
            return true;
    return false;
}

std::string_view StringUtils::trim(std::string_view text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
        begin++;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
        end--;
    return text.substr(begin, end - begin);
}

static int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool StringUtils::urlDecode(std::string_view in, std::string &out, bool plusAsSpace)
{
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '%') {
            if (i + 2 >= in.size())
                return false;
            int high = hexValue(in[i + 1]);
            int low = hexValue(in[i + 2]);
            if (high < 0 || low < 0)
                return false;
            char decoded = static_cast<char>(high * 16 + low);
            if (decoded == '\0')
                return false;
            out.push_back(decoded);
            i += 2;
        } else if (c == '+' && plusAsSpace) {
            out.push_back(' ');
        } else if (c == '\0') {
            return false;
        } else {
            out.push_back(c);
        }
    }
    return true;
}

std::string StringUtils::urlEncode(std::string_view text, bool keepSlash)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || (keepSlash && c == '/');
        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string StringUtils::htmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

std::string StringUtils::toHex(const unsigned char *data, size_t length)
{
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; i++) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 15]);
    }
    return out;
}

std::string StringUtils::formatSize(unsigned long long bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    char buffer[32];
    if (unit == 0)
        snprintf(buffer, sizeof(buffer), "%llu %s", bytes, units[unit]);
    else
        snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    return buffer;
}

} // namespace rizweb
