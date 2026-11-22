#include "clsHTTPSecurity.h"
#include "clsHTTPCookie.h"
#include "../Utils/clsStringUtils.h"

namespace rizweb {

HTTPSecurity::HTTPSecurity() : m_enabled(false), m_securePath("/"), m_sessions(4096, 20 * 60), m_nonces(4096, 120) {}

void HTTPSecurity::setSecurePath(const std::string &path)
{
    std::string normalized = path.empty() ? "/" : path;
    if (normalized[0] != '/')
        normalized.insert(normalized.begin(), '/');
    if (normalized.back() != '/')
        normalized.push_back('/');
    m_securePath = normalized;
}

bool HTTPSecurity::isSecurePath(const std::string &urlPath) const
{
    // "/panel" (no slash) must be protected as well as "/panel/..."
    return StringUtils::startsWith(urlPath, m_securePath) || (urlPath + "/") == m_securePath;
}

bool HTTPSecurity::isSessionValid(const std::string *cookieHeader)
{
    return m_sessions.isValid(HTTPCookie::value(cookieHeader, kSessionCookie));
}

} // namespace rizweb
