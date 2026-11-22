#include "clsLogger.h"

#include <atomic>
#include <cstdio>

namespace rizweb {

namespace {

std::atomic<Logger::Handler> g_handler{nullptr};
std::atomic<int> g_level{static_cast<int>(LogLevel::info)};

const char* levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::debug: return "DEBUG";
    case LogLevel::info: return "INFO";
    case LogLevel::warning: return "WARN";
    case LogLevel::error: return "ERROR";
    }
    return "?";
}

} // namespace

void Logger::setHandler(Handler handler)
{
    g_handler.store(handler);
}

void Logger::setLevel(LogLevel level)
{
    g_level.store(static_cast<int>(level));
}

LogLevel Logger::level()
{
    return static_cast<LogLevel>(g_level.load());
}

void Logger::log(LogLevel level, const char* format, ...)
{
    if (static_cast<int>(level) < g_level.load())
        return;

    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Handler handler = g_handler.load();
    if (handler) {
        handler(level, buffer);
        return;
    }

    std::fprintf(stderr, "[%s] %s\n", levelName(level), buffer);
}

} // namespace rizweb
