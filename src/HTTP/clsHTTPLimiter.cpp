#include "clsHTTPLimiter.h"
#include <chrono>

namespace rizweb {

uint64_t HTTPLimiter::nowSeconds()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void HTTPLimiter::purge(Shard &shard, uint64_t now)
{
    for (auto it = shard.entries.begin(); it != shard.entries.end();) {
        if (it->second.connections == 0 && it->second.windowStart + 1 < now)
            it = shard.entries.erase(it);
        else
            ++it;
    }
}

bool HTTPLimiter::acquireConnection(const std::string &ip)
{
    if (m_maxConnections == 0)
        return true;
    Shard &shard = shardFor(ip);
    std::lock_guard<std::mutex> lock(shard.mutex);
    if (shard.entries.size() > 8192)
        purge(shard, nowSeconds());
    Entry &entry = shard.entries[ip];
    if (entry.connections >= m_maxConnections)
        return false;
    entry.connections++;
    return true;
}

void HTTPLimiter::releaseConnection(const std::string &ip)
{
    if (m_maxConnections == 0)
        return;
    Shard &shard = shardFor(ip);
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.entries.find(ip);
    if (it == shard.entries.end())
        return;
    if (it->second.connections > 0)
        it->second.connections--;
    if (it->second.connections == 0 && it->second.requests == 0)
        shard.entries.erase(it);
}

bool HTTPLimiter::allowRequest(const std::string &ip, uint32_t &retryAfterSeconds)
{
    if (m_maxRequests == 0)
        return true;
    uint64_t now = nowSeconds();
    Shard &shard = shardFor(ip);
    std::lock_guard<std::mutex> lock(shard.mutex);
    Entry &entry = shard.entries[ip];
    if (entry.windowStart != now) {          // fixed one-second window
        entry.windowStart = now;
        entry.requests = 0;
    }
    if (entry.requests >= m_maxRequests) {
        retryAfterSeconds = 1;
        return false;
    }
    entry.requests++;
    return true;
}

} // namespace rizweb
