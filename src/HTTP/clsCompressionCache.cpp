#include "clsCompressionCache.h"
#include "../Utils/clsFileUtils.h"

#if defined(RIZWEB_HAS_ZLIB)
#include <zlib.h>
#endif

namespace rizweb {

bool CompressionCache::isAvailable()
{
#if defined(RIZWEB_HAS_ZLIB)
    return true;
#else
    return false;
#endif
}

#if defined(RIZWEB_HAS_ZLIB)
static bool compressBuffer(const std::string &in, ContentEncoding encoding, std::string &out)
{
    z_stream stream{};
    int windowBits = (encoding == ContentEncoding::Gzip) ? MAX_WBITS + 16 : MAX_WBITS;
    if (deflateInit2(&stream, Z_BEST_SPEED + 3, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    out.resize(deflateBound(&stream, static_cast<uLong>(in.size())) + 32);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.data()));
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = reinterpret_cast<Bytef *>(&out[0]);
    stream.avail_out = static_cast<uInt>(out.size());
    int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END;
}
#endif

std::shared_ptr<const std::string> CompressionCache::get(const std::string &path, uint64_t size, time_t modified, ContentEncoding encoding)
{
#if defined(RIZWEB_HAS_ZLIB)
    if (encoding == ContentEncoding::None || size > kMaxFileSize)
        return nullptr;

    std::string key = path;
    key += '\n';
    key += std::to_string(size);
    key += ':';
    key += std::to_string(static_cast<long long>(modified));
    key += (encoding == ContentEncoding::Gzip) ? 'g' : 'd';

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it != m_entries.end())
            return it->second;
    }

    std::string raw;
    if (!FileUtils::readFile(path, raw, kMaxFileSize))
        return nullptr;
    auto packed = std::make_shared<std::string>();
    if (!compressBuffer(raw, encoding, *packed) || packed->size() >= raw.size())
        return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_bytes + packed->size() > m_maxBytes) {
        m_entries.clear();
        m_bytes = 0;
    }
    m_bytes += packed->size();
    m_entries[key] = packed;
    return packed;
#else
    (void)path; (void)size; (void)modified; (void)encoding;
    return nullptr;
#endif
}

} // namespace rizweb
