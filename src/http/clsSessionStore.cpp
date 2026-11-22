#include "clsSessionStore.h"

#include "../util/clsRandom.h"

namespace rizweb {

SessionStore::SessionStore(std::chrono::seconds timeToLive, size_t maxSessions)
    : m_timeToLive(timeToLive), m_maxSessions(maxSessions), m_lastPurge(std::chrono::steady_clock::now())
{
}

void SessionStore::purgeLocked(std::chrono::steady_clock::time_point now)
{
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second.expires <= now)
            it = m_sessions.erase(it);
        else
            ++it;
    }
    m_lastPurge = now;
}

std::string SessionStore::create(std::string userData)
{
    std::string token = Random::hexToken(24);
    if (token.empty())
        return std::string();

    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sessions.size() >= m_maxSessions) {
        purgeLocked(now);
        if (m_sessions.size() >= m_maxSessions)
            return std::string();
    }

    m_sessions[token] = Entry{now + m_timeToLive, std::move(userData)};
    return token;
}

bool SessionStore::validate(std::string_view token, std::string* userData)
{
    if (token.empty() || token.size() > 128)
        return false;

    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);

    if (now - m_lastPurge > std::chrono::seconds(60))
        purgeLocked(now);

    auto it = m_sessions.find(std::string(token));
    if (it == m_sessions.end())
        return false;

    if (it->second.expires <= now) {
        m_sessions.erase(it);
        return false;
    }

    it->second.expires = now + m_timeToLive;
    if (userData)
        *userData = it->second.userData;
    return true;
}

void SessionStore::destroy(std::string_view token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.erase(std::string(token));
}

void SessionStore::purgeExpired()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    purgeLocked(std::chrono::steady_clock::now());
}

size_t SessionStore::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessions.size();
}

} // namespace rizweb
