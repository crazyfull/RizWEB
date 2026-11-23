#ifndef CLSFILECACHE_H
#define CLSFILECACHE_H
#include <array>
#include <chrono>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rizweb {

// Short-lived cache of resolved static files: for a hot URL a request needs no realpath / stat / open / read.
// An entry holds everything needed to answer (real path, size, mtime, validators, content type and, for small
// files, the content itself). Entries are trusted for |ttlSeconds| (default 1 s) and then rebuilt from disk, so a
// changed or replaced file is picked up within that time. Thread safe (sharded locks), memory bounded.
class FileCache
{
public:
    struct Entry
    {
        std::string path;                              // real path inside the web root
        unsigned long long size = 0;
        time_t modified = 0;
        std::string etag;
        std::string lastModified;
        std::string contentType;
        std::string cacheControl;                      // empty when none configured
        std::shared_ptr<const std::string> content;    // set for small files only
    };
    using EntryPtr = std::shared_ptr<const Entry>;

    explicit FileCache(unsigned ttlSeconds = 1) : m_ttlSeconds(ttlSeconds) {}

    void setTtlSeconds(unsigned seconds) { m_ttlSeconds = seconds; }
    bool isEnabled() const { return m_ttlSeconds > 0; }

    // Fresh entry for (root, urlPath) or nullptr.
    EntryPtr find(const std::string &root, const std::string &urlPath);
    void put(const std::string &root, const std::string &urlPath, EntryPtr entry);

private:
    static constexpr size_t kShards = 16;
    static constexpr size_t kMaxEntriesPerShard = 1024;
    static constexpr size_t kMaxContentBytesPerShard = 4u * 1024 * 1024;

    struct Slot
    {
        EntryPtr entry;
        std::chrono::steady_clock::time_point validatedAt;
    };
    struct Shard
    {
        std::mutex mutex;
        std::unordered_map<std::string, Slot> slots;
        size_t contentBytes = 0;
    };

    Shard &shardFor(const std::string &key);
    static std::string makeKey(const std::string &root, const std::string &urlPath) { return root + '\n' + urlPath; }

    unsigned m_ttlSeconds;
    std::array<Shard, kShards> m_shards;
};

} // namespace rizweb

#endif // CLSFILECACHE_H
