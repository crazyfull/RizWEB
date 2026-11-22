#include "clsAuthGuard.h"

#include "../util/clsUrlCodec.h"
#include "../util/stringUtil.h"

namespace rizweb {

AuthGuard::AuthGuard(std::shared_ptr<SessionStore> sessions, std::string loginPath)
    : m_sessions(std::move(sessions)), m_loginPath(std::move(loginPath))
{
}

void AuthGuard::protect(std::string prefix)
{
    m_protected.push_back(std::move(prefix));
}

void AuthGuard::allow(std::string prefix)
{
    m_allowed.push_back(std::move(prefix));
}

Middleware AuthGuard::middleware() const
{
    // copy the configuration: the middleware must stay valid after the guard object is gone
    auto sessions = m_sessions;
    auto protectedPrefixes = m_protected;
    auto allowedPrefixes = m_allowed;
    auto loginPath = m_loginPath;
    allowedPrefixes.push_back(loginPath);

    return [sessions, protectedPrefixes, allowedPrefixes, loginPath](const HttpRequest& request, HttpResponse& response) {
        bool needsAuth = false;
        for (const std::string& prefix : protectedPrefixes) {
            if (stringUtil::startsWith(request.path, prefix)) {
                needsAuth = true;
                break;
            }
        }
        if (!needsAuth)
            return true;

        for (const std::string& prefix : allowedPrefixes) {
            if (stringUtil::startsWith(request.path, prefix))
                return true;
        }

        std::optional<std::string> token = request.cookie(AuthGuard::cookieName());
        if (token && sessions->validate(*token))
            return true;

        if (request.method == HttpMethod::get || request.method == HttpMethod::head)
            response.redirect(loginPath, 302);
        else
            response.sendError(401, "Authentication required.");
        return false;
    };
}

bool AuthGuard::login(const HttpRequest& request, HttpResponse& response, std::string userData) const
{
    std::string token = m_sessions->create(std::move(userData));
    if (token.empty())
        return false;

    Cookie cookie;
    cookie.name = cookieName();
    cookie.value = token;
    cookie.secure = request.isSecure;
    cookie.sameSite = "Lax";
    cookie.maxAgeSeconds = static_cast<int>(m_sessions->timeToLive().count());
    return response.setCookie(cookie);
}

void AuthGuard::logout(const HttpRequest& request, HttpResponse& response) const
{
    if (std::optional<std::string> token = request.cookie(cookieName()))
        m_sessions->destroy(*token);

    Cookie cookie;
    cookie.name = cookieName();
    cookie.value = "deleted";
    cookie.secure = request.isSecure;
    cookie.maxAgeSeconds = 0;
    response.setCookie(cookie);
}

Middleware makeSameOriginGuard()
{
    return [](const HttpRequest& request, HttpResponse& response) {
        switch (request.method) {
        case HttpMethod::post:
        case HttpMethod::put:
        case HttpMethod::patch:
        case HttpMethod::del:
            break;
        default:
            return true;
        }

        const std::string* source = request.header("Origin");
        if (!source)
            source = request.header("Referer");
        if (!source)
            return true;   // non-browser client

        std::string_view text = *source;
        size_t scheme = text.find("://");
        if (scheme == std::string_view::npos) {
            response.sendError(403, "Cross-site request refused.");
            return false;
        }

        std::string_view authority = text.substr(scheme + 3);
        authority = authority.substr(0, authority.find_first_of("/?#"));

        const std::string* host = request.header("Host");
        if (!host || !stringUtil::equalsIgnoreCase(authority, *host)) {
            response.sendError(403, "Cross-site request refused.");
            return false;
        }
        return true;
    };
}

} // namespace rizweb
