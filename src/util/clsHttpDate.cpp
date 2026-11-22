#include "clsHttpDate.h"

#include <cstring>

namespace rizweb {

std::string HttpDate::format(time_t value)
{
    struct tm parts;
    gmtime_r(&value, &parts);

    char buffer[40];
    size_t n = std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &parts);
    return std::string(buffer, n);
}

const std::string& HttpDate::now()
{
    thread_local time_t cachedSecond = 0;
    thread_local std::string cachedText;

    time_t current = std::time(nullptr);
    if (current != cachedSecond || cachedText.empty()) {
        cachedSecond = current;
        cachedText = format(current);
    }
    return cachedText;
}

bool HttpDate::parse(std::string_view text, time_t& value)
{
    if (text.size() != 29 || text.size() >= 64)
        return false;

    std::string copy(text);
    struct tm parts;
    std::memset(&parts, 0, sizeof(parts));

    const char* end = strptime(copy.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &parts);
    if (!end || *end != '\0')
        return false;

    value = timegm(&parts);
    return value != static_cast<time_t>(-1);
}

} // namespace rizweb
