#include "clsRizLog.h"
#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace rizweb {

static std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};

void RizLog::setLevel(LogLevel level) { g_level.store(static_cast<int>(level)); }
LogLevel RizLog::level() { return static_cast<LogLevel>(g_level.load()); }

void RizLog::write(LogLevel level, const char *format, ...)
{
    if (static_cast<int>(level) < g_level.load())
        return;

    static std::mutex mutex;
    static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    char stamp[32];
    time_t now = time(nullptr);
    struct tm parts;
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &parts);

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(mutex);
    fprintf(level == LogLevel::Error ? stderr : stdout, "%s [%s] %s\n", stamp, names[static_cast<int>(level)], message);
    fflush(level == LogLevel::Error ? stderr : stdout);
}

} // namespace rizweb
