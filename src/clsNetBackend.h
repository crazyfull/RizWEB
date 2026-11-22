#ifndef CLSNETBACKEND_H
#define CLSNETBACKEND_H
// Single place that knows which network library RizWEB is built on:
//   Linux            -> netRector        (epoll, sharded Server)     RIZWEB_USE_NETRECTOR
//   Windows / macOS  -> netRectorClient  (portable EventLoop)        RIZWEB_USE_NETRECTOR_CLIENT
// Both libraries expose the same TCPSocket / TLSContext API, so the rest of RizWEB never
// includes a backend header directly.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(RIZWEB_USE_NETRECTOR)
#include "clsServer.h"
#include "clsTCPSocket.h"
#include "clsTLSContext.h"
#elif defined(RIZWEB_USE_NETRECTOR_CLIENT)
#include "netRector.h"
#else
#error "Define RIZWEB_USE_NETRECTOR (Linux) or RIZWEB_USE_NETRECTOR_CLIENT (other systems)"
#endif

namespace rizweb {

using NetSocket = netrector::TCPSocket;
using NetTlsContext = netrector::TLSContext;
using NetAcceptFactory = netrector::TCPSocket *(*)(void *);

#if defined(RIZWEB_USE_NETRECTOR)
using NetHost = netrector::Server;      // owns N reactor shards, one thread each
using NetReactor = netrector::EpollReactor;
constexpr bool kBackendHasSendFile = true;
#else
using NetHost = netrector::EventLoop;   // one loop = one thread
using NetReactor = netrector::EventLoop;
constexpr bool kBackendHasSendFile = false;
#endif

// Adds TCP listeners to a NetHost and keeps whatever the backend needs alive.
// It works with a host created by RizWEB and with a host owned by the application
// (so RizWEB and the application can share the same reactor threads).
class NetListeners
{
public:
    explicit NetListeners(NetHost &host);
    ~NetListeners();
    NetListeners(const NetListeners &) = delete;
    NetListeners &operator=(const NetListeners &) = delete;

    // Safe from any thread. When the host is already running on another thread (netRectorClient),
    // the listener is created on the loop thread and the call returns true once it is queued.
    bool listen(uint16_t port, const char *bindIp, NetAcceptFactory factory, void *context);
    void stopAll();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rizweb

#endif // CLSNETBACKEND_H
