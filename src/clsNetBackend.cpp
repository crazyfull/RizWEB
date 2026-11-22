#include "clsNetBackend.h"
#include "Utils/clsRizLog.h"

namespace rizweb {

#if defined(RIZWEB_USE_NETRECTOR)

struct NetListeners::Impl
{
    NetHost *host;
};

NetListeners::NetListeners(NetHost &host) : m_impl(new Impl{&host}) {}
NetListeners::~NetListeners() = default;

bool NetListeners::listen(uint16_t port, const char *bindIp, NetAcceptFactory factory, void *context)
{
    return m_impl->host->AddNewListener(port, bindIp, factory, context);
}

void NetListeners::stopAll()
{
    // netRector closes its listeners in Server::stop(), which is owned by the host.
}

#else

struct NetListeners::Impl
{
    NetHost *host;
    std::vector<std::unique_ptr<netrector::Listener>> listeners;
};

NetListeners::NetListeners(NetHost &host) : m_impl(new Impl{&host, {}}) {}
NetListeners::~NetListeners() { stopAll(); }

bool NetListeners::listen(uint16_t port, const char *bindIp, NetAcceptFactory factory, void *context)
{
    // The loop is not running yet when RizWEB owns it, so the listener can be started directly.
    std::unique_ptr<netrector::Listener> listener(new netrector::Listener(m_impl->host));
    if (!listener->start(port, bindIp, factory, context)) {
        RIZLOG_ERROR("cannot listen on port %u (error %d)", (unsigned)port, listener->lastError());
        return false;
    }
    m_impl->listeners.push_back(std::move(listener));
    return true;
}

void NetListeners::stopAll()
{
    for (auto &listener : m_impl->listeners)
        listener->stop();
    m_impl->listeners.clear();
}

#endif

} // namespace rizweb
