#ifndef RIZWEB_CLSGZIPCACHE_H
#define RIZWEB_CLSGZIPCACHE_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rizweb {

// Keeps gzip compressed copies of static files so a hot file is compressed once, not per request.
// The key must change when the file changes (path + size + mtime). Thread safe.
// When the build has no zlib (RIZWEB_HAS_ZLIB undefined) compress() always fails and nothing is cached.
class GzipCache
{
public:
    explicit GzipCache(size_t maxBytes = 32u * 1024 * 1024);

    static bool isAvailable();
    static bool compress(const std::string& input, std::string& output);

    std::shared_ptr<const std::string> find(const std::string& key) const;
    void store(const std::string& key, std::shared_ptr<const std::string> value);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<const std::string>> m_items;
    size_t m_bytes = 0;
    size_t m_maxBytes;
};

} // namespace rizweb

#endif // RIZWEB_CLSGZIPCACHE_H
