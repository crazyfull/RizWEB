#ifndef CLSWORKERPOOL_H
#define CLSWORKERPOOL_H
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rizweb {

// Fixed set of threads with a bounded job queue, for work that blocks (sqlite, a synchronous library, disk).
// submit() never blocks: when the queue is full it returns false so the caller can answer "503 busy" at once
// instead of piling up memory.
class WorkerPool
{
public:
    WorkerPool() = default;
    ~WorkerPool() { stop(); }
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;

    void start(size_t threads, size_t queueLimit);
    void stop();                                    // finishes the job in progress, drops queued jobs, joins
    bool submit(std::function<void()> job);         // false: pool not running or queue full
    size_t queued();

private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::function<void()>> m_queue;
    std::vector<std::thread> m_threads;
    size_t m_limit = 0;
    bool m_running = false;
};

} // namespace rizweb

#endif // CLSWORKERPOOL_H
