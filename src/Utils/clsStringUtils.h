#ifndef CLSSTRINGUTILS_H
#define CLSSTRINGUTILS_H
#include <string>
#include <string_view>

namespace rizweb {

class StringUtils
{
public:
    static char toLowerAscii(char c);
    static std::string toLower(std::string_view text);
    static bool equalsIgnoreCase(std::string_view a, std::string_view b);
    static bool startsWith(std::string_view text, std::string_view prefix);
    static bool endsWith(std::string_view text, std::string_view suffix);
    static bool containsIgnoreCase(std::string_view text, std::string_view needle);
    static std::string_view trim(std::string_view text);

    // Percent-decoding. Returns false for a malformed escape ("%", "%4", "%zz") or an encoded NUL.
    // plusAsSpace is for query strings / form bodies only, never for paths.
    static bool urlDecode(std::string_view in, std::string &out, bool plusAsSpace);
    static std::string urlEncode(std::string_view text, bool keepSlash = false);
    static std::string htmlEscape(std::string_view text);

    static std::string toHex(const unsigned char *data, size_t length);
    static std::string formatSize(unsigned long long bytes);
};

} // namespace rizweb

#endif // CLSSTRINGUTILS_H
