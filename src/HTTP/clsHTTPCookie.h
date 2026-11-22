#ifndef CLSHTTPCOOKIE_H
#define CLSHTTPCOOKIE_H
#include <string>
#include <string_view>

namespace rizweb {

struct CookieOptions
{
    int maxAgeSeconds = 0;           // 0 = session cookie
    std::string path = "/";
    std::string domain;
    bool httpOnly = true;
    bool secure = false;
    std::string sameSite = "Lax";    // "Lax", "Strict", "None" or empty to omit
};

class HTTPCookie
{
public:
    // Value of cookie |name| inside a request "Cookie:" header, or an empty string.
    static std::string value(const std::string *cookieHeader, std::string_view name);
    // "name=value; Max-Age=..; Path=..; HttpOnly; ..." or an empty string when name/value contain illegal characters.
    static std::string buildSetCookie(std::string_view name, std::string_view value, const CookieOptions &options);
};

} // namespace rizweb

#endif // CLSHTTPCOOKIE_H
