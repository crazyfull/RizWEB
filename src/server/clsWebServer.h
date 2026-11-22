#ifndef RIZWEB_CLSWEBSERVER_H
#define RIZWEB_CLSWEBSERVER_H

#include "clsServerConfig.h"
#include "../http/clsAuthGuard.h"
#include "../http/clsMimeTypes.h"
#include "../http/clsRouter.h"
#include "../http/clsSessionStore.h"
#include "../http/clsStaticFileHandler.h"
#include "../websocket/clsWebSocketSession.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace netrector {
class Server;
class TLSContext;
class TCPSocket;
} // namespace netrector

namespace rizweb {

class HttpConnection;

struct WebSocketRoute
{
    std::string path;
    WebSocketHandlers handlers;
};

// Small, fast HTTP/1.1 + WebSocket server on top of netRector (epoll reactors, BoringSSL, sendfile).
//
// Typical use (compare with Go's net/http):
//
//     rizweb::WebServer server;
//     server.get("/hello", [](const rizweb::HttpRequest&, rizweb::HttpResponse& res) {
//         res.sendText(200, "hello");
//     });
//     server.serveStatic("/", options);
//     if (!server.start(&error)) ...
//
// Threading: handlers run on the netRector reactor threads, possibly several at once, so they
// must be thread safe. Register routes, middleware and WebSocket handlers BEFORE start();
// the tables are read-only afterwards.
//
// Sharing reactors: construct with an existing netrector::Server to add RizWEB listeners to
// the reactors of an application that already runs netRector. In that mode start() only adds
// the listeners; the application starts and stops its own Server.
class WebServer
{
public:
    explicit WebServer(ServerConfig config = ServerConfig());
    WebServer(netrector::Server& sharedServer, ServerConfig config = ServerConfig());
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    ServerConfig& config() { return m_config; }
    const ServerConfig& config() const { return m_config; }

    // ---- routing (before start) ----
    void handle(std::string_view pattern, HttpHandler handler);                         // any method
    void get(std::string_view pattern, HttpHandler handler);                            // GET and HEAD
    void post(std::string_view pattern, HttpHandler handler);
    void put(std::string_view pattern, HttpHandler handler);
    void del(std::string_view pattern, HttpHandler handler);
    void use(Middleware middleware);                                                    // runs before routing, in order
    void setNotFoundHandler(HttpHandler handler);

    // Serves a directory below a URL prefix ("/", "/static/"). options.rootDirectory is required.
    void serveStatic(std::string_view prefix, StaticFileOptions options);

    void websocket(std::string_view path, WebSocketHandlers handlers);

    Router& router() { return m_router; }
    MimeTypes& mimeTypes() { return *m_mimeTypes; }
    std::shared_ptr<SessionStore> sessions() { return m_sessions; }

    // ---- lifecycle ----
    // Creates the listeners (and starts the reactors when the server owns them).
    // Returns false and fills *error when something cannot work (bad certificate, port in use, ...).
    bool start(std::string* error = nullptr);
    void stop();
    bool isRunning() const { return m_running.load(); }

    uint64_t activeConnections() const { return m_activeConnections.load(); }

    // ---- used by HttpConnection ----
    void dispatch(const HttpRequest& request, HttpResponse& response) const;
    const WebSocketRoute* findWebSocketRoute(std::string_view path) const;
    bool isStopping() const { return m_stopping.load(); }
    void connectionOpened() { m_activeConnections.fetch_add(1); }
    void connectionClosed() { m_activeConnections.fetch_sub(1); }

private:
    bool addListeners(std::string* error);
    static netrector::TCPSocket* acceptPlain(void* context);
    static netrector::TCPSocket* acceptTls(void* context);

    ServerConfig m_config;
    Router m_router;
    std::vector<Middleware> m_middleware;
    std::vector<WebSocketRoute> m_webSocketRoutes;
    HttpHandler m_notFound;
    std::shared_ptr<MimeTypes> m_mimeTypes;
    std::shared_ptr<SessionStore> m_sessions;

    // declaration order matters: the server (reactors, sockets) must be destroyed before the TLS context
    std::unique_ptr<netrector::TLSContext> m_tls;
    std::unique_ptr<netrector::Server> m_ownedServer;
    netrector::Server* m_server = nullptr;

    std::atomic<uint64_t> m_activeConnections{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopping{false};
};

} // namespace rizweb

#endif // RIZWEB_CLSWEBSERVER_H
