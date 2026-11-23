#ifndef CLSHTTPUPLOADTRACKER_H
#define CLSHTTPUPLOADTRACKER_H
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rizweb {

// Server-side progress of one upload. Updated by the connection, readable from any thread.
struct UploadProgress
{
    enum State { Uploading = 0, Done = 1, Failed = 2 };
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> total{0};         // 0 = unknown (chunked)
    std::atomic<int> state{Uploading};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

// Registry of uploads that carry an id (?uploadId=... or X-Upload-Id header, [A-Za-z0-9_-]{1,64}). A client
// can poll GET <progress route>/<id> (WEBMaster::uploadProgress()) while it uploads. Finished entries are
// dropped after a minute; the table is bounded.
class UploadTracker
{
public:
    static bool isValidId(const std::string &id);
    std::shared_ptr<UploadProgress> begin(const std::string &id, uint64_t total);
    std::shared_ptr<UploadProgress> find(const std::string &id);
    // {"id":"..","received":N,"total":N,"percent":N,"state":"uploading|done|failed"} or "" when unknown
    std::string toJson(const std::string &id);

private:
    void purgeLocked();
    static constexpr size_t kMaxEntries = 4096;
    std::mutex m_mutex;
    struct Entry
    {
        std::shared_ptr<UploadProgress> progress;
        std::chrono::steady_clock::time_point finishedAt{};
    };
    std::unordered_map<std::string, Entry> m_entries;
};

} // namespace rizweb

#endif // CLSHTTPUPLOADTRACKER_H
