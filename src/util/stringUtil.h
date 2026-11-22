#ifndef RIZWEB_STRINGUTIL_H
#define RIZWEB_STRINGUTIL_H

#include <cstdint>
#include <string>
#include <string_view>

namespace rizweb {
namespace stringUtil {

inline char toLowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b);
std::string toLower(std::string_view text);
std::string_view trim(std::string_view text);
bool startsWith(std::string_view text, std::string_view prefix);
bool endsWith(std::string_view text, std::string_view suffix);

// true when a comma separated header value (for example "keep-alive, Upgrade") holds the token
bool containsToken(std::string_view headerValue, std::string_view token);

// RFC 7230 token characters (method names, header names)
bool isToken(std::string_view text);

// true when the text holds NUL, CR, LF or other control characters (HT is allowed)
bool hasControlChars(std::string_view text);

bool isValidUtf8(std::string_view text);

std::string htmlEscape(std::string_view text);
std::string formatSize(uint64_t bytes);

} // namespace stringUtil
} // namespace rizweb

#endif // RIZWEB_STRINGUTIL_H
