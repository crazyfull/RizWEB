#ifndef RIZWEB_CLSAUTHGUARD_H
#define RIZWEB_CLSAUTHGUARD_H

#include "clsHttpRequest.h"
#include "clsHttpResponse.h"
#include "clsSessionStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rizweb {

// A middleware returns true to continue with routing, false when it already filled the response.
using Middleware = std::function<bool(const HttpRequest&, HttpResponse&)>;

// Session based protection of path prefixes, plus the cookie helpers that go with it.
class AuthGuard
{
public:
    AuthGuard(std::shared_ptr<SessionStore> sessions, std::string loginPath = "/auth/");

    // Paths below these prefixes need a valid session ("/" protects everything).
    void protect(std::string prefix);

    // Paths that stay public even below a protected prefix (the login page, its assets).
    void allow(std::string prefix);

    Middleware middleware() const;

    // Creates a session and sets the cookie. false when the store is full.
    bool login(const HttpRequest& request, HttpResponse& response, std::string userData = std::string()) const;
    void logout(const HttpRequest& request, HttpResponse& response) const;

    static const char* cookieName() { return "rizweb_sid"; }

private:
    std::shared_ptr<SessionStore> m_sessions;
    std::string m_loginPath;
    std::vector<std::string> m_protected;
    std::vector<std::string> m_allowed;
};

// Refuses state changing requests (POST, PUT, PATCH, DELETE) whose Origin (or Referer)
// names a different host than the Host header. Together with SameSite cookies this
// stops cross site request forgery without tokens.
Middleware makeSameOriginGuard();

} // namespace rizweb

#endif // RIZWEB_CLSAUTHGUARD_H
