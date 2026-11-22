#include "clsHTTPCookie.h"
#include "../Utils/clsStringUtils.h"

namespace rizweb {

std::string HTTPCookie::value(const std::string *cookieHeader, std::string_view name)
{
    if (!cookieHeader)
        return {};
    std::string_view text = *cookieHeader;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view pair = StringUtils::trim(text.substr(start, end - start));
        size_t equal = pair.find('=');
        if (equal != std::string_view::npos && pair.substr(0, equal) == name)
            return std::string(pair.substr(equal + 1));
        start = end + 1;
    }
    return {};
}

static bool isSafe(std::string_view text)
{
    for (char c : text)
        if (c == ';' || c == ',' || c == ' ' || c == '\r' || c == '\n' || c == '\0')
            return false;
    return true;
}

std::string HTTPCookie::buildSetCookie(std::string_view name, std::string_view value, const CookieOptions &options)
{
    if (name.empty() || !isSafe(name) || !isSafe(value) || !isSafe(options.path) || !isSafe(options.domain))
        return {};
    std::string out;
    out.append(name).append("=").append(value);
    if (options.maxAgeSeconds > 0)
        out.append("; Max-Age=").append(std::to_string(options.maxAgeSeconds));
    if (!options.path.empty())
        out.append("; Path=").append(options.path);
    if (!options.domain.empty())
        out.append("; Domain=").append(options.domain);
    if (options.httpOnly)
        out.append("; HttpOnly");
    if (options.secure || options.sameSite == "None")
        out.append("; Secure");
    if (!options.sameSite.empty())
        out.append("; SameSite=").append(options.sameSite);
    return out;
}

} // namespace rizweb
