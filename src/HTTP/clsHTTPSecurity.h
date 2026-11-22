#ifndef CLSHTTPSECURITY_H
#define CLSHTTPSECURITY_H
#include "clsHTTPSession.h"
#include <string>

namespace rizweb {

// Optional login protection: every URL under securePath() needs a valid session cookie ("sID"),
// otherwise the client is redirected to /auth/.
class HTTPSecurity
{
public:
    static constexpr const char *kSessionCookie = "sID";
    static constexpr const char *kNonceCookie = "sdAuth";

    HTTPSecurity();

    bool isAuthenticationEnabled() const { return m_enabled; }
    void setAuthenticationEnabled(bool enabled) { m_enabled = enabled; }
    const std::string &securePath() const { return m_securePath; }
    void setSecurePath(const std::string &path);      // normalized to start and end with '/'
    bool isSecurePath(const std::string &urlPath) const;

    HTTPSession &sessions() { return m_sessions; }
    HTTPSession &loginNonces() { return m_nonces; }
    bool isSessionValid(const std::string *cookieHeader);

private:
    bool m_enabled;
    std::string m_securePath;
    HTTPSession m_sessions;
    HTTPSession m_nonces;
};

} // namespace rizweb

#endif // CLSHTTPSECURITY_H
