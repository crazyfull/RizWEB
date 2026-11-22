#ifndef CLSRIZLOG_H
#define CLSRIZLOG_H
#include <cstdarg>

namespace rizweb {

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

class RizLog
{
public:
    static void setLevel(LogLevel level);
    static LogLevel level();
    static void write(LogLevel level, const char *format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
};

} // namespace rizweb

#define RIZLOG_DEBUG(...) ::rizweb::RizLog::write(::rizweb::LogLevel::Debug, __VA_ARGS__)
#define RIZLOG_INFO(...) ::rizweb::RizLog::write(::rizweb::LogLevel::Info, __VA_ARGS__)
#define RIZLOG_WARN(...) ::rizweb::RizLog::write(::rizweb::LogLevel::Warning, __VA_ARGS__)
#define RIZLOG_ERROR(...) ::rizweb::RizLog::write(::rizweb::LogLevel::Error, __VA_ARGS__)

#endif // CLSRIZLOG_H
