#ifndef CLSDEADLINEQUEUE_H
#define CLSDEADLINEQUEUE_H
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace rizweb {

// One thread that runs callbacks at their deadline (used for the timeout of deferred responses). Callbacks must
// be short and must not block: they only hand work over to a reactor thread. add() and cancel() are thread safe.
class DeadlineQueue
{
public:
    using Clock = std::chrono::steady_clock;

    DeadlineQueue() = default;
    ~DeadlineQueue() { stop(); }
    DeadlineQueue(const DeadlineQueue &) = delete;
    DeadlineQueue &operator=(const DeadlineQueue &) = delete;

    void start();
    void stop();                                                    // pending entries are dropped
    uint64_t add(std::chrono::milliseconds delay, std::function<void()> callback);   // returns an id, 0 when stopped
    void cancel(uint64_t id);

private:
    void run();

    struct Entry
    {
        uint64_t id;
        std::function<void()> callback;
    };

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::multimap<Clock::time_point, Entry> m_entries;
    std::unordered_map<uint64_t, std::multimap<Clock::time_point, Entry>::iterator> m_index;
    std::thread m_thread;
    uint64_t m_nextId = 1;
    bool m_running = false;
};

} // namespace rizweb

#endif // CLSDEADLINEQUEUE_H
