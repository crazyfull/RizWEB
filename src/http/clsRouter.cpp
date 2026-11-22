#include "clsRouter.h"

#include "../util/stringUtil.h"

#include <algorithm>

namespace rizweb {

unsigned Router::methodBit(HttpMethod method)
{
    switch (method) {
    case HttpMethod::get: return 1u << 0;
    case HttpMethod::head: return 1u << 1;
    case HttpMethod::post: return 1u << 2;
    case HttpMethod::put: return 1u << 3;
    case HttpMethod::patch: return 1u << 4;
    case HttpMethod::del: return 1u << 5;
    case HttpMethod::options: return 1u << 6;
    default: return 0;
    }
}

void Router::add(std::string_view pattern, unsigned methodMask, HttpHandler handler)
{
    if (pattern.empty() || pattern[0] != '/' || !handler)
        return;

    // GET routes answer HEAD too
    if (methodMask & methodBit(HttpMethod::get))
        methodMask |= methodBit(HttpMethod::head);

    Route route{std::string(pattern), methodMask, std::move(handler)};

    if (stringUtil::endsWith(pattern, "/")) {
        m_prefix.push_back(std::move(route));
        std::stable_sort(m_prefix.begin(), m_prefix.end(),
                         [](const Route& a, const Route& b) { return a.pattern.size() > b.pattern.size(); });
    } else {
        m_exact[route.pattern].push_back(std::move(route));
    }
}

Router::Match Router::find(std::string_view path, HttpMethod method) const
{
    Match result;
    const unsigned bit = methodBit(method);

    auto consider = [&](const Route& route) {
        result.pathExists = true;
        result.allowedMethods |= route.methodMask;
        if (!result.handler && (route.methodMask & bit))
            result.handler = &route.handler;
    };

    auto exact = m_exact.find(std::string(path));
    if (exact != m_exact.end()) {
        for (const Route& route : exact->second)
            consider(route);
        if (result.handler)
            return result;
    }

    for (const Route& route : m_prefix) {
        // "/static/" also matches "/static" so that the tree root is reachable
        bool matches = stringUtil::startsWith(path, route.pattern)
                       || path == std::string_view(route.pattern).substr(0, route.pattern.size() - 1);
        if (!matches)
            continue;
        consider(route);
        if (result.handler)
            return result;
    }
    return result;
}

} // namespace rizweb
