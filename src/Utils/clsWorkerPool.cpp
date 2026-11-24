#include "clsWorkerPool.h"

namespace rizweb {

void WorkerPool::start(size_t threads, size_t queueLimit)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running || threads == 0)
        return;
    m_running = true;
    m_limit = queueLimit;
    for (size_t i = 0; i < threads; ++i)
        m_threads.emplace_back([this]() { run(); });
}

void WorkerPool::stop()
{
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
            return;
        m_running = false;
        m_queue.clear();
        threads.swap(m_threads);
    }
    m_wake.notify_all();
    for (std::thread &thread : threads)
        if (thread.joinable())
            thread.join();
}

bool WorkerPool::submit(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || (m_limit != 0 && m_queue.size() >= m_limit))
            return false;
        m_queue.push_back(std::move(job));
    }
    m_wake.notify_one();
    return true;
}

size_t WorkerPool::queued()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void WorkerPool::run()
{
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this]() { return !m_running || !m_queue.empty(); });
            if (!m_running)
                return;
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        job();
    }
}

} // namespace rizweb
