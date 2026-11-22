#ifndef RIZWEB_CLSLOGGER_H
#define RIZWEB_CLSLOGGER_H

#include <cstdarg>

namespace rizweb {

enum class LogLevel { debug = 0, info = 1, warning = 2, error = 3 };

// Minimal logger. The application can redirect everything with setHandler().
// Passwords, cookies and other secrets must never be passed to it.
class Logger
{
public:
    using Handler = void (*)(LogLevel level, const char* message);

    static void setHandler(Handler handler);
    static void setLevel(LogLevel level);
    static LogLevel level();

    static void log(LogLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));
};

} // namespace rizweb

#define RIZWEB_LOG_DEBUG(...) ::rizweb::Logger::log(::rizweb::LogLevel::debug, __VA_ARGS__)
#define RIZWEB_LOG_INFO(...) ::rizweb::Logger::log(::rizweb::LogLevel::info, __VA_ARGS__)
#define RIZWEB_LOG_WARNING(...) ::rizweb::Logger::log(::rizweb::LogLevel::warning, __VA_ARGS__)
#define RIZWEB_LOG_ERROR(...) ::rizweb::Logger::log(::rizweb::LogLevel::error, __VA_ARGS__)

#endif // RIZWEB_CLSLOGGER_H
