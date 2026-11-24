#include "clsDeadlineQueue.h"

namespace rizweb {

void DeadlineQueue::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
        return;
    m_running = true;
    m_thread = std::thread([this]() { run(); });
}

void DeadlineQueue::stop()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
            return;
        m_running = false;
        m_entries.clear();
        m_index.clear();
        thread = std::move(m_thread);
    }
    m_wake.notify_all();
    if (thread.joinable())
        thread.join();
}

uint64_t DeadlineQueue::add(std::chrono::milliseconds delay, std::function<void()> callback)
{
    uint64_t id;
    bool earliest;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
            return 0;
        id = m_nextId++;
        Clock::time_point when = Clock::now() + delay;
        earliest = m_entries.empty() || when < m_entries.begin()->first;
        m_index[id] = m_entries.emplace(when, Entry{id, std::move(callback)});
    }
    if (earliest)
        m_wake.notify_one();
    return id;
}

void DeadlineQueue::cancel(uint64_t id)
{
    if (id == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_index.find(id);
    if (found == m_index.end())
        return;
    m_entries.erase(found->second);
    m_index.erase(found);
}

void DeadlineQueue::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_running) {
        if (m_entries.empty()) {
            m_wake.wait(lock);
            continue;
        }
        auto first = m_entries.begin();
        if (first->first > Clock::now()) {
            m_wake.wait_until(lock, first->first);
            continue;
        }
        std::function<void()> callback = std::move(first->second.callback);
        m_index.erase(first->second.id);
        m_entries.erase(first);
        lock.unlock();
        callback();
        lock.lock();
    }
}

} // namespace rizweb
