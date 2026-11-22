#include "clsRandom.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

namespace rizweb {

bool Random::fill(void* buffer, size_t length)
{
    unsigned char* out = static_cast<unsigned char*>(buffer);
    size_t done = 0;

    while (done < length) {
        ssize_t n = ::getrandom(out + done, length - done, 0);
        if (n > 0) {
            done += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    if (done == length)
        return true;

    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;

    while (done < length) {
        ssize_t n = ::read(fd, out + done, length - done);
        if (n > 0) {
            done += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    ::close(fd);
    return done == length;
}

std::string Random::hexToken(size_t byteCount)
{
    std::string raw(byteCount, '\0');
    if (!fill(&raw[0], byteCount))
        return std::string();

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(byteCount * 2);
    for (unsigned char c : raw) {
        out += hex[c >> 4];
        out += hex[c & 0x0F];
    }
    return out;
}

} // namespace rizweb
