#include "clsGzipCache.h"

#ifdef RIZWEB_HAS_ZLIB
#include <zlib.h>
#endif

namespace rizweb {

GzipCache::GzipCache(size_t maxBytes) : m_maxBytes(maxBytes)
{
}

bool GzipCache::isAvailable()
{
#ifdef RIZWEB_HAS_ZLIB
    return true;
#else
    return false;
#endif
}

bool GzipCache::compress(const std::string& input, std::string& output)
{
#ifdef RIZWEB_HAS_ZLIB
    z_stream stream{};
    // windowBits 15 + 16 selects the gzip container
    if (deflateInit2(&stream, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;

    output.assign(deflateBound(&stream, static_cast<uLong>(input.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(&output[0]);
    stream.avail_out = static_cast<uInt>(output.size());

    int result = deflate(&stream, Z_FINISH);
    size_t produced = stream.total_out;
    deflateEnd(&stream);

    if (result != Z_STREAM_END)
        return false;
    output.resize(produced);
    return true;
#else
    (void)input;
    (void)output;
    return false;
#endif
}

std::shared_ptr<const std::string> GzipCache::find(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_items.find(key);
    return it == m_items.end() ? nullptr : it->second;
}

void GzipCache::store(const std::string& key, std::shared_ptr<const std::string> value)
{
    if (!value || value->size() > m_maxBytes)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_bytes + value->size() > m_maxBytes) {
        // simple and predictable: start over instead of tracking LRU order
        m_items.clear();
        m_bytes = 0;
    }
    m_bytes += value->size();
    m_items[key] = std::move(value);
}

} // namespace rizweb
