#ifndef CLSHTTPROUTER_H
#define CLSHTTPROUTER_H
#include "clsHTTPRequest.h"
#include "clsHTTPUpload.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rizweb {

class HTTPClient;
class HTTPResponse;

using RouteHandler = std::function<void(HTTPClient &, HTTPRequest &, HTTPResponse &)>;

// Callbacks of one WebSocket endpoint (see WEBMaster::ws()).
struct WebSocketEndpoint
{
    std::function<void(HTTPClient &)> onOpen;
    std::function<void(HTTPClient &, bool isBinary, const char *payload, size_t length)> onMessage;
    std::function<void(HTTPClient &)> onClose;
};

// Method + pattern routing:
//   "/users"            exact match (hash lookup, the fast path)
//   "/users/:id"        ":name" matches one path segment      -> request.param("id")
//   "/files/*"          "*" matches the rest of the path      -> request.param("*")
// Exact routes win over parameter routes; parameter routes are tried in registration order.
// Register everything before WEBMaster::start(); lookups afterwards are read-only (thread safe).
class HTTPRouter
{
public:
    struct Route
    {
        HTTPMethod method = HTTPMethod::Get;
        std::string pattern;
        RouteHandler handler;
        std::shared_ptr<const WebSocketEndpoint> webSocket;   // set for ws() routes
        UploadFactory stream;                                 // set for stream() / upload() routes: the body is not buffered
        size_t bodyLimit = 0;                                 // 0 = server default
    };

    enum class Result { Found, NotFound, MethodNotAllowed };

    Route &add(HTTPMethod method, const std::string &pattern, RouteHandler handler);
    Route &addStream(HTTPMethod method, const std::string &pattern, UploadFactory factory);
    Route &addWebSocket(const std::string &pattern, WebSocketEndpoint endpoint);
    bool isEmpty() const { return m_count == 0; }

    // Finds the route for |method| + |path|. Fills |params| for ":name" / "*" parts.
    // HEAD falls back to GET. On MethodNotAllowed, |allow| lists the methods that would match.
    Result match(HTTPMethod method, const std::string &path, const Route *&route,
                 std::vector<std::pair<std::string, std::string>> &params, std::string &allow) const;
    // The streaming route of this request, or nullptr (used before the body is read).
    const Route *streamRouteFor(HTTPMethod method, const std::string &path) const;
    // Body limit configured for the route of this request (0 = none configured).
    size_t bodyLimitFor(HTTPMethod method, const std::string &path) const;

    static const char *methodName(HTTPMethod method);

private:
    struct Segment
    {
        enum Kind { Literal, Parameter, Wildcard } kind;
        std::string text;
    };
    struct PatternRoute
    {
        std::vector<Segment> segments;
        std::unique_ptr<Route> route;
    };

    static std::vector<Segment> compile(const std::string &pattern);
    static bool matchSegments(const std::vector<Segment> &segments, const std::string &path, std::vector<std::pair<std::string, std::string>> &params);
    const Route *find(HTTPMethod method, const std::string &path, std::vector<std::pair<std::string, std::string>> *params) const;
    Route &store(std::unique_ptr<Route> route);

    static constexpr int kMethods = 8;
    static int index(HTTPMethod method) { return static_cast<int>(method); }

    std::unordered_map<std::string, std::unique_ptr<Route>> m_exact[kMethods];
    std::vector<PatternRoute> m_patterns[kMethods];
    size_t m_count = 0;
};

} // namespace rizweb

#endif // CLSHTTPROUTER_H
