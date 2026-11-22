#include "clsHTTPSession.h"
#include "../Utils/clsCrypto.h"
#include <chrono>

namespace rizweb {

HTTPSession::HTTPSession(size_t maxEntries, uint32_t lifetimeSeconds) : m_maxEntries(maxEntries), m_lifetime(lifetimeSeconds) {}

uint64_t HTTPSession::nowSeconds()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void HTTPSession::setLifetime(uint32_t seconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lifetime = seconds;
}

uint32_t HTTPSession::lifetime() const { return m_lifetime; }

void HTTPSession::removeExpiredLocked(uint64_t now)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (now >= it->second)
            it = m_entries.erase(it);
        else
            ++it;
    }
    m_nextCleaning = now + 60;
}

std::string HTTPSession::create()
{
    std::string token = Crypto::randomHex(16);
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t now = nowSeconds();
    if (m_entries.size() >= m_maxEntries)
        removeExpiredLocked(now);
    if (m_entries.size() >= m_maxEntries)
        return {};
    m_entries[token] = now + m_lifetime;
    return token;
}

bool HTTPSession::isValid(const std::string &token)
{
    if (token.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t now = nowSeconds();
    if (now >= m_nextCleaning)
        removeExpiredLocked(now);
    auto it = m_entries.find(token);
    return it != m_entries.end() && now < it->second;
}

bool HTTPSession::consume(const std::string &token)
{
    if (token.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(token);
    if (it == m_entries.end())
        return false;
    bool valid = nowSeconds() < it->second;
    m_entries.erase(it);
    return valid;
}

void HTTPSession::remove(const std::string &token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(token);
}

void HTTPSession::removeExpired()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    removeExpiredLocked(nowSeconds());
}

size_t HTTPSession::count()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

} // namespace rizweb
