#ifndef RIZWEB_CLSROUTER_H
#define RIZWEB_CLSROUTER_H

#include "clsHttpRequest.h"
#include "clsHttpResponse.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rizweb {

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

// Go style patterns: "/api/time" matches that exact path, "/static/" matches the whole subtree.
// The longest matching subtree wins. Register all routes before the server starts; matching
// is read-only afterwards and therefore safe from every reactor thread.
class Router
{
public:
    static constexpr unsigned kAnyMethod = 0xFFu;

    static unsigned methodBit(HttpMethod method);

    // methodMask: OR of methodBit() values (kAnyMethod = all). HEAD is allowed wherever GET is.
    void add(std::string_view pattern, unsigned methodMask, HttpHandler handler);

    struct Match
    {
        const HttpHandler* handler = nullptr;
        bool pathExists = false;      // a route matched the path, but not the method
        unsigned allowedMethods = 0;  // for the Allow header of a 405
    };

    Match find(std::string_view path, HttpMethod method) const;

    bool hasRoutes() const { return !m_exact.empty() || !m_prefix.empty(); }

private:
    struct Route
    {
        std::string pattern;
        unsigned methodMask;
        HttpHandler handler;
    };

    std::unordered_map<std::string, std::vector<Route>> m_exact;
    std::vector<Route> m_prefix;   // sorted by pattern length, longest first
};

} // namespace rizweb

#endif // RIZWEB_CLSROUTER_H
