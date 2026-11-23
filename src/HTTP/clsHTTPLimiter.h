#ifndef CLSHTTPLIMITER_H
#define CLSHTTPLIMITER_H
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rizweb {

// Per-IP protection: number of simultaneous connections and requests per second.
// 0 disables a limit. Thread safe; the table is split into shards so reactor threads
// rarely contend. Behind a reverse proxy every client has the proxy's address - limit there instead.
class HTTPLimiter
{
public:
    void setMaxConnectionsPerIp(uint32_t limit) { m_maxConnections = limit; }
    void setMaxRequestsPerSecond(uint32_t limit) { m_maxRequests = limit; }
    bool isEnabled() const { return m_maxConnections != 0 || m_maxRequests != 0; }

    bool acquireConnection(const std::string &ip);      // false = refuse the connection
    void releaseConnection(const std::string &ip);
    // false = over the limit; |retryAfterSeconds| tells when the window resets
    bool allowRequest(const std::string &ip, uint32_t &retryAfterSeconds);

private:
    struct Entry
    {
        uint32_t connections = 0;
        uint64_t windowStart = 0;
        uint32_t requests = 0;
    };
    struct Shard
    {
        std::mutex mutex;
        std::unordered_map<std::string, Entry> entries;
    };
    static constexpr size_t kShards = 16;
    Shard &shardFor(const std::string &ip) { return m_shards[std::hash<std::string>()(ip) % kShards]; }
    static uint64_t nowSeconds();
    static void purge(Shard &shard, uint64_t now);

    uint32_t m_maxConnections = 0;
    uint32_t m_maxRequests = 0;
    Shard m_shards[kShards];
};

} // namespace rizweb

#endif // CLSHTTPLIMITER_H
