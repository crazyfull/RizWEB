#ifndef CLSHTTPCORS_H
#define CLSHTTPCORS_H
#include <string>
#include <vector>

namespace rizweb {

class HTTPRequest;
class HTTPResponse;

// Cross-origin resource sharing. Disabled until allowOrigin() / allowOrigins() is called.
//   web.cors().allowOrigin("*");
//   web.cors().allowOrigins({"https://app.example.com"}).allowCredentials(true);
class HTTPCors
{
public:
    HTTPCors &allowOrigin(const std::string &origin);                 // "*" or one origin
    HTTPCors &allowOrigins(const std::vector<std::string> &origins);
    HTTPCors &allowMethods(const std::string &methods) { m_methods = methods; return *this; }
    HTTPCors &allowHeaders(const std::string &headers) { m_headers = headers; return *this; }    // empty = echo the request
    HTTPCors &exposeHeaders(const std::string &headers) { m_expose = headers; return *this; }
    HTTPCors &allowCredentials(bool enabled) { m_credentials = enabled; return *this; }
    HTTPCors &maxAge(unsigned seconds) { m_maxAge = seconds; return *this; }

    bool isEnabled() const { return m_enabled; }
    // Adds the CORS headers for a request that carries an Origin header. A preflight
    // (OPTIONS + Access-Control-Request-Method) is answered with 204 and true is returned.
    bool apply(const HTTPRequest &request, HTTPResponse &response) const;

private:
    bool isAllowed(const std::string &origin) const;

    bool m_enabled = false;
    bool m_any = false;
    bool m_credentials = false;
    unsigned m_maxAge = 600;
    std::vector<std::string> m_origins;
    std::string m_methods = "GET, POST, PUT, PATCH, DELETE, OPTIONS";
    std::string m_headers;
    std::string m_expose;
};

} // namespace rizweb

#endif // CLSHTTPCORS_H
