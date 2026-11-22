#include "clsWebServer.h"

#include "clsHttpConnection.h"
#include "../http/clsHttpStatus.h"
#include "../util/clsLogger.h"
#include "../util/stringUtil.h"

#include <algorithm>
#include <exception>
#include <sys/resource.h>
#include <thread>

#include "clsServer.h"        // netRector
#include "clsTLSContext.h"    // netRector

namespace rizweb {

namespace {

// Raises the soft descriptor limit to the hard limit and returns the usable number.
int raiseDescriptorLimit(int wanted)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
        return wanted;

    if (limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        setrlimit(RLIMIT_NOFILE, &limit);
        getrlimit(RLIMIT_NOFILE, &limit);
    }

    rlim_t usable = limit.rlim_cur > 1048576 ? 1048576 : limit.rlim_cur;
    return static_cast<int>(std::min<rlim_t>(usable, static_cast<rlim_t>(wanted)));
}

void setError(std::string* error, const std::string& text)
{
    if (error)
        *error = text;
}

std::string allowHeaderValue(unsigned methods)
{
    struct Item { HttpMethod method; const char* name; };
    static const Item items[] = {{HttpMethod::get, "GET"},   {HttpMethod::head, "HEAD"}, {HttpMethod::post, "POST"},
                                 {HttpMethod::put, "PUT"},   {HttpMethod::patch, "PATCH"}, {HttpMethod::del, "DELETE"},
                                 {HttpMethod::options, "OPTIONS"}};
    std::string out;
    for (const Item& item : items) {
        if (methods & Router::methodBit(item.method)) {
            if (!out.empty())
                out += ", ";
            out += item.name;
        }
    }
    return out;
}

} // namespace

WebServer::WebServer(ServerConfig config)
    : m_config(std::move(config)),
      m_mimeTypes(std::make_shared<MimeTypes>()),
      m_sessions(std::make_shared<SessionStore>(std::chrono::minutes(m_config.sessionMinutes)))
{
}

WebServer::WebServer(netrector::Server& sharedServer, ServerConfig config) : WebServer(std::move(config))
{
    m_server = &sharedServer;
}

WebServer::~WebServer()
{
    stop();
}

// ---------------------------------------------------------------------------
// routing
// ---------------------------------------------------------------------------

void WebServer::handle(std::string_view pattern, HttpHandler handler)
{
    m_router.add(pattern, Router::kAnyMethod, std::move(handler));
}

void WebServer::get(std::string_view pattern, HttpHandler handler)
{
    m_router.add(pattern, Router::methodBit(HttpMethod::get), std::move(handler));
}

void WebServer::post(std::string_view pattern, HttpHandler handler)
{
    m_router.add(pattern, Router::methodBit(HttpMethod::post), std::move(handler));
}

void WebServer::put(std::string_view pattern, HttpHandler handler)
{
    m_router.add(pattern, Router::methodBit(HttpMethod::put), std::move(handler));
}

void WebServer::del(std::string_view pattern, HttpHandler handler)
{
    m_router.add(pattern, Router::methodBit(HttpMethod::del), std::move(handler));
}

void WebServer::use(Middleware middleware)
{
    if (middleware)
        m_middleware.push_back(std::move(middleware));
}

void WebServer::setNotFoundHandler(HttpHandler handler)
{
    m_notFound = std::move(handler);
}

void WebServer::serveStatic(std::string_view prefix, StaticFileOptions options)
{
    std::string pattern(prefix);
    if (pattern.empty() || pattern[0] != '/')
        pattern.insert(pattern.begin(), '/');
    if (pattern.back() != '/')
        pattern += '/';

    options.enableGzip = options.enableGzip && m_config.enableGzip;
    auto handler = std::make_shared<StaticFileHandler>(std::move(options), m_mimeTypes);
    const size_t prefixLength = pattern.size() - 1;   // keep the slash that starts the sub path

    m_router.add(pattern, Router::methodBit(HttpMethod::get),
                 [handler, prefixLength](const HttpRequest& request, HttpResponse& response) {
                     std::string_view sub = request.path;
                     sub.remove_prefix(std::min(prefixLength, sub.size()));
                     if (sub.empty())
                         sub = "/";
                     handler->serve(request, response, sub);
                 });
}

void WebServer::websocket(std::string_view path, WebSocketHandlers handlers)
{
    m_webSocketRoutes.push_back(WebSocketRoute{std::string(path), std::move(handlers)});
}

const WebSocketRoute* WebServer::findWebSocketRoute(std::string_view path) const
{
    for (const WebSocketRoute& route : m_webSocketRoutes) {
        if (route.path == path)
            return &route;
    }
    return nullptr;
}

void WebServer::dispatch(const HttpRequest& request, HttpResponse& response) const
{
    try {
        if (!m_config.hostName.empty() && !stringUtil::equalsIgnoreCase(request.host(), m_config.hostName)) {
            response.sendError(404, "Site not found.");
            return;
        }

        if (request.method == HttpMethod::unknown) {
            response.sendError(501);
            return;
        }

        if (request.method == HttpMethod::options && request.path == "*") {
            response.setStatus(200);
            response.setHeader("Allow", "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS");
            return;
        }

        for (const Middleware& middleware : m_middleware) {
            if (!middleware(request, response))
                return;
        }

        Router::Match match = m_router.find(request.path, request.method);
        if (match.handler) {
            (*match.handler)(request, response);
            return;
        }

        if (match.pathExists) {
            if (request.method == HttpMethod::options) {
                response.setStatus(204);
                response.setHeader("Allow", allowHeaderValue(match.allowedMethods | Router::methodBit(HttpMethod::options)));
                return;
            }
            response.sendError(405);
            response.setHeader("Allow", allowHeaderValue(match.allowedMethods));
            return;
        }

        if (m_notFound) {
            response.setStatus(404);
            m_notFound(request, response);
            return;
        }
        response.sendError(404, "The requested URL was not found on this server.");
    } catch (const std::exception& error) {
        RIZWEB_LOG_ERROR("handler for %s threw: %s", request.path.c_str(), error.what());
        response.reset();
        response.sendError(500);
    } catch (...) {
        RIZWEB_LOG_ERROR("handler for %s threw an unknown exception", request.path.c_str());
        response.reset();
        response.sendError(500);
    }
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

netrector::TCPSocket* WebServer::acceptPlain(void* context)
{
    return new HttpConnection(*static_cast<WebServer*>(context));
}

netrector::TCPSocket* WebServer::acceptTls(void* context)
{
    WebServer* server = static_cast<WebServer*>(context);
    HttpConnection* connection = new HttpConnection(*server);
    if (!connection->enableTLS(server->m_tls.get())) {
        // never serve a client in clear text on a TLS port; the reactor closes the descriptor
        // when the factory returns nullptr
        delete connection;
        return nullptr;
    }
    return connection;
}

bool WebServer::addListeners(std::string* error)
{
    if (m_config.httpPort == 0 && m_config.httpsPort == 0) {
        setError(error, "no listener configured (httpPort and httpsPort are both 0)");
        return false;
    }

    const char* bindIp = m_config.listenIp.empty() ? nullptr : m_config.listenIp.c_str();

    if (m_config.httpsPort != 0) {
        std::string tlsError;
        if (!netrector::TLSContext::isSupported()) {
            setError(error, "netRector was built without TLS (BoringSSL not found)");
            return false;
        }

        if (m_config.tlsSelfSigned && m_config.tlsCertFile.empty())
            m_tls.reset(netrector::TLSContext::createServerSelfSigned("localhost", &tlsError));
        else if (!m_config.tlsCertFile.empty() && !m_config.tlsKeyFile.empty())
            m_tls.reset(netrector::TLSContext::createServer(m_config.tlsCertFile.c_str(), m_config.tlsKeyFile.c_str(), &tlsError));
        else
            tlsError = "httpsPort is set but tlsCertFile / tlsKeyFile are missing";

        if (!m_tls) {
            setError(error, "TLS setup failed: " + tlsError);
            return false;
        }

        if (m_config.tlsKeyThreads > 0 && !m_tls->enableAsyncPrivateKey(m_config.tlsKeyThreads)) {
            setError(error, "enableAsyncPrivateKey failed");
            return false;
        }

        // HTTP/1.1 only; announcing h2 without speaking it would break browsers
        m_tls->setALPN({"http/1.1"});

        if (!m_server->AddNewListener(m_config.httpsPort, bindIp, &WebServer::acceptTls, this)) {
            setError(error, "cannot listen on TLS port " + std::to_string(m_config.httpsPort));
            return false;
        }
    }

    if (m_config.httpPort != 0) {
        if (!m_server->AddNewListener(m_config.httpPort, bindIp, &WebServer::acceptPlain, this)) {
            setError(error, "cannot listen on port " + std::to_string(m_config.httpPort));
            return false;
        }
    }
    return true;
}

bool WebServer::start(std::string* error)
{
    if (m_running.load()) {
        setError(error, "already running");
        return false;
    }

    if (!m_config.mimeTypesFile.empty() && !m_mimeTypes->loadFile(m_config.mimeTypesFile))
        RIZWEB_LOG_WARNING("mime types file \"%s\" could not be read", m_config.mimeTypesFile.c_str());

    const bool ownsServer = (m_server == nullptr);
    if (ownsServer) {
        int threads = m_config.threads > 0 ? m_config.threads : static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        int maxFd = raiseDescriptorLimit(m_config.maxConnections);
        m_ownedServer.reset(new netrector::Server(maxFd, threads));
        m_ownedServer->setUseGarbageCollector(false);
        m_server = m_ownedServer.get();
    }

    m_stopping = false;
    if (!addListeners(error)) {
        if (ownsServer) {
            m_ownedServer.reset();
            m_server = nullptr;
        }
        return false;
    }

    if (ownsServer && !m_ownedServer->start()) {
        setError(error, "netRector server could not start");
        m_ownedServer.reset();
        m_server = nullptr;
        return false;
    }

    m_running = true;
    RIZWEB_LOG_INFO("%s listening on %s (http %u, https %u)", m_config.serverName.c_str(), m_config.listenIp.c_str(),
                    static_cast<unsigned>(m_config.httpPort), static_cast<unsigned>(m_config.httpsPort));
    return true;
}

void WebServer::stop()
{
    m_stopping = true;
    if (m_ownedServer && m_running.load())
        m_ownedServer->stop();
    m_running = false;
}

} // namespace rizweb
