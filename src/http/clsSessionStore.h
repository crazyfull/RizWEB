#ifndef RIZWEB_CLSSESSIONSTORE_H
#define RIZWEB_CLSSESSIONSTORE_H

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rizweb {

// Server side sessions. Tokens are 192 random bits from the kernel and are only ever
// stored in memory; the cookie carries nothing but the token. Thread safe (all reactor
// threads share one store).
class SessionStore
{
public:
    explicit SessionStore(std::chrono::seconds timeToLive = std::chrono::minutes(20), size_t maxSessions = 100000);

    // Returns an empty string when the store is full or randomness is unavailable.
    std::string create(std::string userData = std::string());

    // true when the token is known and not expired; the expiry slides forward on every hit.
    bool validate(std::string_view token, std::string* userData = nullptr);

    void destroy(std::string_view token);
    void purgeExpired();
    size_t size() const;
    std::chrono::seconds timeToLive() const { return m_timeToLive; }

private:
    struct Entry
    {
        std::chrono::steady_clock::time_point expires;
        std::string userData;
    };

    void purgeLocked(std::chrono::steady_clock::time_point now);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Entry> m_sessions;
    std::chrono::seconds m_timeToLive;
    size_t m_maxSessions;
    std::chrono::steady_clock::time_point m_lastPurge;
};

} // namespace rizweb

#endif // RIZWEB_CLSSESSIONSTORE_H
