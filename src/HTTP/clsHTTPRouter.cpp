#include "clsHTTPRouter.h"

namespace rizweb {

const char *HTTPRouter::methodName(HTTPMethod method)
{
    switch (method) {
    case HTTPMethod::Get: return "GET";
    case HTTPMethod::Head: return "HEAD";
    case HTTPMethod::Post: return "POST";
    case HTTPMethod::Put: return "PUT";
    case HTTPMethod::Delete: return "DELETE";
    case HTTPMethod::Options: return "OPTIONS";
    case HTTPMethod::Patch: return "PATCH";
    default: return "";
    }
}

std::vector<HTTPRouter::Segment> HTTPRouter::compile(const std::string &pattern)
{
    std::vector<Segment> segments;
    size_t start = 1;
    while (start <= pattern.size()) {
        size_t end = pattern.find('/', start);
        if (end == std::string::npos)
            end = pattern.size();
        std::string part = pattern.substr(start, end - start);
        if (!part.empty()) {
            if (part[0] == ':')
                segments.push_back({Segment::Parameter, part.substr(1)});
            else if (part[0] == '*')
                segments.push_back({Segment::Wildcard, "*"});
            else
                segments.push_back({Segment::Literal, part});
        }
        start = end + 1;
    }
    return segments;
}

HTTPRouter::Route &HTTPRouter::store(std::unique_ptr<Route> route)
{
    Route &ref = *route;
    int slot = index(route->method);
    std::string pattern = route->pattern.empty() || route->pattern[0] != '/' ? "/" + route->pattern : route->pattern;
    ref.pattern = pattern;
    bool dynamic = pattern.find(':') != std::string::npos || pattern.find('*') != std::string::npos;
    if (dynamic) {
        PatternRoute entry;
        entry.segments = compile(pattern);
        entry.route = std::move(route);
        m_patterns[slot].push_back(std::move(entry));
    } else {
        m_exact[slot][pattern] = std::move(route);
    }
    m_count++;
    return ref;
}

HTTPRouter::Route &HTTPRouter::add(HTTPMethod method, const std::string &pattern, RouteHandler handler)
{
    std::unique_ptr<Route> route(new Route);
    route->method = method;
    route->pattern = pattern;
    route->handler = std::move(handler);
    return store(std::move(route));
}

HTTPRouter::Route &HTTPRouter::addWebSocket(const std::string &pattern, WebSocketEndpoint endpoint)
{
    std::unique_ptr<Route> route(new Route);
    route->method = HTTPMethod::Get;
    route->pattern = pattern;
    route->webSocket = std::make_shared<const WebSocketEndpoint>(std::move(endpoint));
    return store(std::move(route));
}

bool HTTPRouter::matchSegments(const std::vector<Segment> &segments, const std::string &path, std::vector<std::pair<std::string, std::string>> &params)
{
    params.clear();
    size_t position = 1;
    for (size_t i = 0; i < segments.size(); i++) {
        const Segment &segment = segments[i];
        if (segment.kind == Segment::Wildcard) {
            params.emplace_back("*", position <= path.size() ? path.substr(position) : std::string());
            return true;
        }
        if (position > path.size())
            return false;
        size_t end = path.find('/', position);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(position, end - position);
        if (part.empty())
            return false;
        if (segment.kind == Segment::Literal) {
            if (part != segment.text)
                return false;
        } else {
            params.emplace_back(segment.text, std::move(part));
        }
        position = end + 1;
    }
    // everything consumed? ("/a/b" vs "/a/b/" are the same route)
    return position >= path.size();
}

const HTTPRouter::Route *HTTPRouter::find(HTTPMethod method, const std::string &path, std::vector<std::pair<std::string, std::string>> *params) const
{
    int slot = index(method);
    if (slot < 0 || slot >= kMethods)
        return nullptr;
    auto exact = m_exact[slot].find(path);
    if (exact == m_exact[slot].end() && path.size() > 1 && path.back() == '/')
        exact = m_exact[slot].find(path.substr(0, path.size() - 1));
    if (exact != m_exact[slot].end()) {
        if (params)
            params->clear();
        return exact->second.get();
    }
    std::vector<std::pair<std::string, std::string>> scratch;
    for (const PatternRoute &entry : m_patterns[slot]) {
        if (matchSegments(entry.segments, path, params ? *params : scratch))
            return entry.route.get();
    }
    return nullptr;
}

HTTPRouter::Result HTTPRouter::match(HTTPMethod method, const std::string &path, const Route *&route,
                                     std::vector<std::pair<std::string, std::string>> &params, std::string &allow) const
{
    route = find(method, path, &params);
    if (!route && method == HTTPMethod::Head)
        route = find(HTTPMethod::Get, path, &params);
    if (route)
        return Result::Found;

    allow.clear();
    static const HTTPMethod all[] = {HTTPMethod::Get, HTTPMethod::Post, HTTPMethod::Put, HTTPMethod::Delete, HTTPMethod::Patch};
    for (HTTPMethod other : all) {
        if (find(other, path, nullptr)) {
            if (!allow.empty())
                allow += ", ";
            allow += methodName(other);
            if (other == HTTPMethod::Get)
                allow += ", HEAD";
        }
    }
    return allow.empty() ? Result::NotFound : Result::MethodNotAllowed;
}

size_t HTTPRouter::bodyLimitFor(HTTPMethod method, const std::string &path) const
{
    const Route *route = find(method, path, nullptr);
    return route ? route->bodyLimit : 0;
}

} // namespace rizweb
