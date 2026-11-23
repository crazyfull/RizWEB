#include "clsHTTPCors.h"
#include "clsHTTPRequest.h"
#include "clsHTTPResponse.h"

namespace rizweb {

HTTPCors &HTTPCors::allowOrigin(const std::string &origin)
{
    m_enabled = true;
    if (origin == "*")
        m_any = true;
    else
        m_origins.push_back(origin);
    return *this;
}

HTTPCors &HTTPCors::allowOrigins(const std::vector<std::string> &origins)
{
    for (const std::string &origin : origins)
        allowOrigin(origin);
    return *this;
}

bool HTTPCors::isAllowed(const std::string &origin) const
{
    for (const std::string &item : m_origins)
        if (item == origin)
            return true;
    return false;
}

bool HTTPCors::apply(const HTTPRequest &request, HTTPResponse &response) const
{
    const std::string *origin = request.headerField("origin");
    if (!m_enabled || !origin)
        return false;
    if (!m_any && !isAllowed(*origin))
        return false;

    // credentials are not allowed together with "*": reflect the origin instead
    bool reflect = !m_any || m_credentials;
    response.setHeaderField("Access-Control-Allow-Origin", reflect ? *origin : "*");
    if (reflect)
        response.setHeaderField("Vary", "Origin");
    if (m_credentials)
        response.setHeaderField("Access-Control-Allow-Credentials", "true");
    if (!m_expose.empty())
        response.setHeaderField("Access-Control-Expose-Headers", m_expose);

    if (request.method() == HTTPMethod::Options && request.headerField("access-control-request-method")) {
        response.setHeaderField("Access-Control-Allow-Methods", m_methods);
        if (!m_headers.empty()) {
            response.setHeaderField("Access-Control-Allow-Headers", m_headers);
        } else if (const std::string *wanted = request.headerField("access-control-request-headers")) {
            response.setHeaderField("Access-Control-Allow-Headers", *wanted);
        }
        response.setHeaderField("Access-Control-Max-Age", std::to_string(m_maxAge));
        response.sendHeaders(204, false, 0);
        response.complete();
        return true;
    }
    return false;
}

} // namespace rizweb
