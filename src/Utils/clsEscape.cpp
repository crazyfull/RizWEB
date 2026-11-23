#include "clsEscape.h"
#include "clsStringUtils.h"

namespace rizweb {

std::string Escape::html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        case '`': out += "&#96;"; break;
        case '\0': break;
        default: out.push_back(c);
        }
    }
    return out;
}

std::string Escape::htmlAttribute(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 && c != '\t') {           // control characters (also CR / LF / NUL) have no place in an attribute
            continue;
        }
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        case '`': out += "&#96;"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

namespace {

// Length of the valid UTF-8 sequence at |p| (0 when invalid).
size_t utf8Length(const unsigned char *p, size_t left)
{
    if (p[0] < 0x80)
        return 1;
    if (p[0] >= 0xC2 && p[0] <= 0xDF && left >= 2 && (p[1] & 0xC0) == 0x80)
        return 2;
    if (p[0] >= 0xE0 && p[0] <= 0xEF && left >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        if (p[0] == 0xE0 && p[1] < 0xA0) return 0;        // overlong
        if (p[0] == 0xED && p[1] >= 0xA0) return 0;        // surrogate
        return 3;
    }
    if (p[0] >= 0xF0 && p[0] <= 0xF4 && left >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        if (p[0] == 0xF0 && p[1] < 0x90) return 0;
        if (p[0] == 0xF4 && p[1] >= 0x90) return 0;
        return 4;
    }
    return 0;
}

std::string escapeScriptString(std::string_view text, bool javaScript)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    const unsigned char *data = reinterpret_cast<const unsigned char *>(text.data());
    for (size_t i = 0; i < text.size();) {
        unsigned char c = data[i];
        size_t length = utf8Length(data + i, text.size() - i);
        if (length == 0) {                      // invalid UTF-8 becomes U+FFFD
            out += "\\ufffd";
            i++;
            continue;
        }
        if (length > 1) {
            // U+2028 / U+2029 end a line in JavaScript
            if (length == 3 && c == 0xE2 && data[i + 1] == 0x80 && (data[i + 2] == 0xA8 || data[i + 2] == 0xA9))
                out += data[i + 2] == 0xA8 ? "\\u2028" : "\\u2029";
            else
                out.append(text.substr(i, length));
            i += length;
            continue;
        }
        i++;
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '<': case '>': case '&':           // keeps "</script>" and "<!--" harmless when the text is embedded in HTML
            out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); break;
        case '\'':
        case '`':
            if (javaScript) { out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
            else out.push_back(static_cast<char>(c));
            break;
        case '$':
            if (javaScript) out += "\\u0024"; else out.push_back('$');
            break;
        default:
            if (c < 0x20 || c == 0x7f) {
                out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

} // namespace

std::string Escape::json(std::string_view text) { return escapeScriptString(text, false); }
std::string Escape::jsString(std::string_view text) { return escapeScriptString(text, true); }
std::string Escape::url(std::string_view text) { return StringUtils::urlEncode(text, false); }

std::string Escape::shellArg(std::string_view text)
{
    std::string out = "'";
    for (char c : text) {
        if (c == '\0')
            continue;
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string Escape::headerValue(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        if (c != '\r' && c != '\n' && c != '\0')
            out.push_back(c);
    return out;
}

} // namespace rizweb
