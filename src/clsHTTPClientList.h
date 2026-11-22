#ifndef CLSHTTPCLIENTLIST_H
#define CLSHTTPCLIENTLIST_H
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace rizweb {

class HTTPClient;

// Registry of the live connections. The reactors own the sockets; this list only lets the
// application count clients or run code on a client's own thread.
class HTTPClientList
{
public:
    void add(HTTPClient *client);
    void remove(HTTPClient *client);
    size_t count();

    // Runs |task| on the reactor thread of client |id|, if that client still exists when the task runs.
    // Safe to call from any thread. Returns false when the client is unknown.
    bool post(uint32_t id, std::function<void(HTTPClient &)> task);
    // Same for every connected client (for example a WebSocket broadcast).
    void postToAll(const std::function<void(HTTPClient &)> &task);

private:
    std::mutex m_mutex;
    std::unordered_map<uint32_t, HTTPClient *> m_clients;
};

} // namespace rizweb

#endif // CLSHTTPCLIENTLIST_H
