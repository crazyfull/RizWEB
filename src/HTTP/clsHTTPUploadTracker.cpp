#include "clsHTTPUploadTracker.h"

namespace rizweb {

bool UploadTracker::isValidId(const std::string &id)
{
    if (id.empty() || id.size() > 64)
        return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
            return false;
    return true;
}

void UploadTracker::purgeLocked()
{
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        Entry &entry = it->second;
        bool stale;
        if (entry.progress->state.load() == UploadProgress::Uploading) {
            stale = now - entry.progress->started > std::chrono::hours(6);
        } else {
            if (entry.finishedAt == std::chrono::steady_clock::time_point{})
                entry.finishedAt = now;                 // first time we see it finished: keep it for a minute
            stale = now - entry.finishedAt > std::chrono::seconds(60);
        }
        it = stale ? m_entries.erase(it) : std::next(it);
    }
}

std::shared_ptr<UploadProgress> UploadTracker::begin(const std::string &id, uint64_t total)
{
    if (!isValidId(id))
        return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_entries.size() >= kMaxEntries)
        purgeLocked();
    if (m_entries.size() >= kMaxEntries)
        return nullptr;
    auto progress = std::make_shared<UploadProgress>();
    progress->total = total;
    m_entries[id] = Entry{progress, {}};
    return progress;
}

std::shared_ptr<UploadProgress> UploadTracker::find(const std::string &id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(id);
    if (it == m_entries.end())
        return nullptr;
    if (it->second.progress->state.load() != UploadProgress::Uploading && it->second.finishedAt == std::chrono::steady_clock::time_point{})
        it->second.finishedAt = std::chrono::steady_clock::now();
    return it->second.progress;
}

std::string UploadTracker::toJson(const std::string &id)
{
    auto progress = find(id);
    if (!progress)
        return {};
    uint64_t received = progress->received.load(), total = progress->total.load();
    int state = progress->state.load();
    unsigned percent = total ? static_cast<unsigned>(received * 100 / total) : (state == UploadProgress::Done ? 100u : 0u);
    const char *name = state == UploadProgress::Done ? "done" : state == UploadProgress::Failed ? "failed" : "uploading";
    return "{\"id\":\"" + id + "\",\"received\":" + std::to_string(received) + ",\"total\":" + std::to_string(total) +
           ",\"percent\":" + std::to_string(percent) + ",\"state\":\"" + name + "\"}";
}

} // namespace rizweb
