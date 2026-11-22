#ifndef CLSCOMPRESSIONCACHE_H
#define CLSCOMPRESSIONCACHE_H
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rizweb {

enum class ContentEncoding { None, Gzip, Deflate };

// Compresses a file once and serves the result from memory afterwards, so the event loop never
// runs zlib per request. Entries are keyed by path + size + mtime and the whole cache is dropped
// when it grows past the byte limit. Thread safe. Without zlib every call returns nullptr.
class CompressionCache
{
public:
    static constexpr size_t kMaxFileSize = 8u * 1024 * 1024;
    explicit CompressionCache(size_t maxBytes = 64u * 1024 * 1024) : m_maxBytes(maxBytes) {}

    static bool isAvailable();
    std::shared_ptr<const std::string> get(const std::string &path, uint64_t size, time_t modified, ContentEncoding encoding);

private:
    std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<const std::string>> m_entries;
    size_t m_bytes = 0;
    size_t m_maxBytes;
};

} // namespace rizweb

#endif // CLSCOMPRESSIONCACHE_H
