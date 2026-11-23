#include "clsFileCache.h"
#include <functional>

namespace rizweb {

FileCache::Shard &FileCache::shardFor(const std::string &key)
{
    return m_shards[std::hash<std::string>()(key) % kShards];
}

FileCache::EntryPtr FileCache::find(const std::string &root, const std::string &urlPath)
{
    if (m_ttlSeconds == 0)
        return nullptr;
    const std::string key = makeKey(root, urlPath);
    Shard &shard = shardFor(key);
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.slots.find(key);
    if (it == shard.slots.end())
        return nullptr;
    if (std::chrono::steady_clock::now() - it->second.validatedAt >= std::chrono::seconds(m_ttlSeconds))
        return nullptr;                                 // stale: the caller rebuilds and put() replaces it
    return it->second.entry;
}

void FileCache::put(const std::string &root, const std::string &urlPath, EntryPtr entry)
{
    if (m_ttlSeconds == 0 || !entry)
        return;
    const std::string key = makeKey(root, urlPath);
    const size_t bytes = entry->content ? entry->content->size() : 0;
    Shard &shard = shardFor(key);
    std::lock_guard<std::mutex> lock(shard.mutex);
    if (shard.slots.size() >= kMaxEntriesPerShard || shard.contentBytes + bytes > kMaxContentBytesPerShard) {
        shard.slots.clear();                            // simple and bounded; hot entries come back on the next request
        shard.contentBytes = 0;
    }
    auto it = shard.slots.find(key);
    if (it != shard.slots.end() && it->second.entry && it->second.entry->content)
        shard.contentBytes -= it->second.entry->content->size();
    shard.slots[key] = Slot{std::move(entry), std::chrono::steady_clock::now()};
    shard.contentBytes += bytes;
}

} // namespace rizweb
