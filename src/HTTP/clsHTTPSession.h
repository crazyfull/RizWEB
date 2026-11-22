#ifndef CLSHTTPSESSION_H
#define CLSHTTPSESSION_H
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rizweb {

// Server-side store of random tokens with an expiry. Used for login sessions and for the short-lived
// login-form nonce. Thread safe (connections run on different reactor threads).
class HTTPSession
{
public:
    explicit HTTPSession(size_t maxEntries = 4096, uint32_t lifetimeSeconds = 20 * 60);

    void setLifetime(uint32_t seconds);
    uint32_t lifetime() const;

    // New 128-bit random token (hex), or an empty string when the store is full.
    std::string create();
    bool isValid(const std::string &token);
    // Valid and removed in one step (one-time tokens).
    bool consume(const std::string &token);
    void remove(const std::string &token);
    void removeExpired();
    size_t count();

private:
    static uint64_t nowSeconds();
    void removeExpiredLocked(uint64_t now);

    std::mutex m_mutex;
    std::unordered_map<std::string, uint64_t> m_entries;   // token -> expiry (steady clock seconds)
    size_t m_maxEntries;
    uint32_t m_lifetime;
    uint64_t m_nextCleaning = 0;
};

} // namespace rizweb

#endif // CLSHTTPSESSION_H
