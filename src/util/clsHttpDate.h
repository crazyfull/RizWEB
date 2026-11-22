#ifndef RIZWEB_CLSHTTPDATE_H
#define RIZWEB_CLSHTTPDATE_H

#include <ctime>
#include <string>
#include <string_view>

namespace rizweb {

// IMF-fixdate helpers ("Sun, 06 Nov 1994 08:49:37 GMT").
class HttpDate
{
public:
    static std::string format(time_t value);

    // Current time, formatted. Cached per thread and refreshed once per second.
    static const std::string& now();

    // Returns false when the text is not a valid IMF-fixdate.
    static bool parse(std::string_view text, time_t& value);
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPDATE_H
