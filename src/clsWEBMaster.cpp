#include "clsWEBMaster.h"
#include "Utils/clsRizLog.h"

namespace rizweb {

WEBMaster::WEBMaster(int maxConnections, int shards)
    : m_staticFiles(*this), m_host(nullptr), m_ownsHost(true)
{
#if defined(RIZWEB_USE_NETRECTOR)
    unsigned count = shards > 0 ? static_cast<unsigned>(shards) : std::max(1u, std::thread::hardware_concurrency());
    m_ownedHost.reset(new NetHost(maxConnections, static_cast<int>(count)));
#else
    (void)maxConnections; (void)shards;
    m_ownedHost.reset(new NetHost());
#endif
    m_host = m_ownedHost.get();
}

WEBMaster::WEBMaster(NetHost &sharedHost)
    : m_staticFiles(*this), m_host(&sharedHost), m_ownsHost(false)
{
}

WEBMaster::~WEBMaster()
{
    stop();
}

void WEBMaster::loadSettings(const std::string &path)
{
    m_settings.load(path);
    m_security.setAuthenticationEnabled(m_settings.authenticationEnabled);
    m_security.setSecurePath(m_settings.securePath);
    m_security.sessions().setLifetime(m_settings.sessionExpirationMinutes * 60);
    if (!m_mimeTypes.loadDatabase(m_settings.mimeTypesPath))
        RIZLOG_DEBUG("no MIME database at %s, using the built-in table", m_settings.mimeTypesPath.c_str());
}

bool WEBMaster::notifyAuthentication(HTTPClient &client, HTTPRequest &request)
{
    if (!m_authHandler)
        return false;
    m_authHandler(client, request);
    return true;
}

NetSocket *WEBMaster::acceptPlain(void *master)
{
    return new HTTPClient(*static_cast<WEBMaster *>(master), false);
}

NetSocket *WEBMaster::acceptTls(void *master)
{
    WEBMaster *self = static_cast<WEBMaster *>(master);
    std::unique_ptr<HTTPClient> client(new HTTPClient(*self, true));
    if (!self->m_tls || !client->enableTLS(self->m_tls.get()))
        return nullptr;                    // refuse the connection
    return client.release();
}

bool WEBMaster::createTlsContext()
{
    bool hasFiles = !m_settings.sslCertPath.empty() && !m_settings.sslKeyPath.empty();
    if (!hasFiles && !m_settings.selfSignedTls)
        return false;
    if (!NetTlsContext::isSupported()) {
        RIZLOG_WARN("TLS requested but this build has no BoringSSL support (configure with -DRIZWEB_ENABLE_TLS=ON)");
        return false;
    }
    std::string error;
    if (hasFiles)
        m_tls.reset(NetTlsContext::createServer(m_settings.sslCertPath.c_str(), m_settings.sslKeyPath.c_str(), &error));
    else
        m_tls.reset(NetTlsContext::createServerSelfSigned(m_settings.serverHostName.empty() ? "localhost" : m_settings.serverHostName.c_str(), &error));
    if (!m_tls) {
        RIZLOG_ERROR("TLS disabled: %s", error.c_str());
        return false;
    }
    m_tls->setALPN({"http/1.1"});
    return true;
}

bool WEBMaster::start()
{
    if (m_running)
        return true;
    if (m_settings.httpPort == 0 && m_settings.sslPort == 0) {
        RIZLOG_ERROR("no listener configured");
        return false;
    }
    m_limiter.setMaxConnectionsPerIp(m_settings.maxConnectionsPerIp);
    m_limiter.setMaxRequestsPerSecond(m_settings.maxRequestsPerSecondPerIp);
    m_bodyLimit = [this](HTTPMethod method, const std::string &path) {
        size_t routeLimit = m_router.bodyLimitFor(method, path);
        return routeLimit ? routeLimit : m_settings.maxBodyBytes;
    };
    m_listeners.reset(new NetListeners(*m_host));
    const char *ip = m_settings.listenIp.empty() ? nullptr : m_settings.listenIp.c_str();

    if (createTlsContext() && !m_listeners->listen(m_settings.sslPort, ip, &WEBMaster::acceptTls, this)) {
        RIZLOG_ERROR("cannot listen on TLS port %u", (unsigned)m_settings.sslPort);
        return false;
    }
    if (m_settings.httpPort != 0 && !m_listeners->listen(m_settings.httpPort, ip, &WEBMaster::acceptPlain, this)) {
        RIZLOG_ERROR("cannot listen on port %u", (unsigned)m_settings.httpPort);
        return false;
    }

    if (m_ownsHost) {
#if defined(RIZWEB_USE_NETRECTOR)
        if (!m_host->start()) {
            RIZLOG_ERROR("cannot start the network server");
            return false;
        }
#else
        m_loopThread = std::thread([this]() { m_host->run(); });
#endif
    }
    m_running = true;
    RIZLOG_INFO("RizWEB %s listening (http:%u, https:%s)", RIZWEB_VERSION, (unsigned)m_settings.httpPort, m_tls ? std::to_string(m_settings.sslPort).c_str() : "off");
    return true;
}

void WEBMaster::stop()
{
    if (!m_running && !m_listeners)
        return;
    if (m_ownsHost) {
#if defined(RIZWEB_USE_NETRECTOR)
        m_host->stop();                    // closes every connection and listener, then joins the shards
#else
        m_host->stop();
        if (m_loopThread.joinable())
            m_loopThread.join();
#endif
    }
    m_listeners.reset();                   // netRectorClient listeners are stopped here
    m_tls.reset();                         // only after the host stopped: sockets use the context
    m_running = false;
}

} // namespace rizweb
