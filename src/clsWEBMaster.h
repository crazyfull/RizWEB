#ifndef CLSWEBMASTER_H
#define CLSWEBMASTER_H
#include "HTTP/HTTPSettings.h"
#include "HTTP/clsCacheFileType.h"
#include "HTTP/clsCompressionTypeList.h"
#include "HTTP/clsHTTPRequest.h"
#include "HTTP/clsHTTPResponse.h"
#include "HTTP/clsHTTPSecurity.h"
#include "HTTP/clsHTTPStaticFiles.h"
#include "HTTP/clsMIMETypes.h"
#include "clsHTTPClient.h"
#include "clsHTTPClientList.h"
#include "clsNetBackend.h"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#define RIZWEB_VERSION "1.4.0"

namespace rizweb {

// The web server. Two ways to use it:
//
//   WEBMaster server;                 // owns its network host (netRector Server / netRectorClient EventLoop)
//   WEBMaster server(myHost);         // shares the reactor of the application: RizWEB only adds listeners
//
// Handlers are called on reactor threads (several threads with netRector), so shared state used by
// handlers must be thread safe. A handler must answer the request before it returns.
class WEBMaster
{
public:
    // true = the request is handled (answered); false = continue with the static file handler
    using HttpHandler = std::function<bool(HTTPClient &, HTTPRequest &, HTTPResponse &)>;
    using AuthenticationHandler = std::function<void(HTTPClient &, HTTPRequest &)>;
    using ClientHandler = std::function<void(HTTPClient &)>;
    using WebSocketMessageHandler = std::function<void(HTTPClient &, bool isBinary, const char *payload, size_t length)>;

    // maxConnections: size hint of the connection table; shards: reactor threads (0 = CPU count). Ignored by netRectorClient.
    explicit WEBMaster(int maxConnections = 65536, int shards = 0);
    explicit WEBMaster(NetHost &sharedHost);
    ~WEBMaster();
    WEBMaster(const WEBMaster &) = delete;
    WEBMaster &operator=(const WEBMaster &) = delete;

    // configuration (before start())
    HTTPSettings &settings() { return m_settings; }
    const HTTPSettings &settings() const { return m_settings; }
    HTTPSecurity &security() { return m_security; }
    MIMETypes &mimeTypes() { return m_mimeTypes; }
    CacheFileType &cacheFileTypes() { return m_cacheFileTypes; }
    CompressionTypeList &compressionTypes() { return m_compressionTypes; }
    HTTPClientList &clients() { return m_clients; }
    HTTPStaticFiles &staticFiles() { return m_staticFiles; }

    // Reads settings.ini (created with defaults when missing) and the MIME database.
    void loadSettings(const std::string &path = std::string());

    void setHttpHandler(HttpHandler handler) { m_httpHandler = std::move(handler); }
    void setAuthenticationHandler(AuthenticationHandler handler) { m_authHandler = std::move(handler); }
    void setWebSocketConnectedHandler(ClientHandler handler) { m_wsConnected = std::move(handler); }
    void setWebSocketMessageHandler(WebSocketMessageHandler handler) { m_wsMessage = std::move(handler); }
    void setWebSocketClosedHandler(ClientHandler handler) { m_wsClosed = std::move(handler); }
    void setHttpClosedHandler(ClientHandler handler) { m_httpClosed = std::move(handler); }

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    // used by HTTPClient
    bool notifyHttpRequest(HTTPClient &c, HTTPRequest &q, HTTPResponse &r) { return m_httpHandler && m_httpHandler(c, q, r); }
    bool notifyAuthentication(HTTPClient &c, HTTPRequest &q);
    void notifyWebSocketConnected(HTTPClient &c) { if (m_wsConnected) m_wsConnected(c); }
    void notifyWebSocketMessage(HTTPClient &c, bool b, const char *p, size_t n) { if (m_wsMessage) m_wsMessage(c, b, p, n); }
    void notifyWebSocketClosed(HTTPClient &c) { if (m_wsClosed) m_wsClosed(c); }
    void notifyHttpClosed(HTTPClient &c) { if (m_httpClosed) m_httpClosed(c); }

private:
    static NetSocket *acceptPlain(void *master);
    static NetSocket *acceptTls(void *master);
    bool createTlsContext();

    HTTPSettings m_settings;
    HTTPSecurity m_security;
    MIMETypes m_mimeTypes;
    CacheFileType m_cacheFileTypes;
    CompressionTypeList m_compressionTypes;
    HTTPClientList m_clients;
    HTTPStaticFiles m_staticFiles;

    HttpHandler m_httpHandler;
    AuthenticationHandler m_authHandler;
    ClientHandler m_wsConnected;
    ClientHandler m_wsClosed;
    ClientHandler m_httpClosed;
    WebSocketMessageHandler m_wsMessage;

    std::unique_ptr<NetHost> m_ownedHost;
    NetHost *m_host;
    std::unique_ptr<NetListeners> m_listeners;
    std::unique_ptr<NetTlsContext> m_tls;
    bool m_ownsHost;
    bool m_running = false;
#if defined(RIZWEB_USE_NETRECTOR_CLIENT)
    std::thread m_loopThread;
#endif
};

} // namespace rizweb

#endif // CLSWEBMASTER_H
