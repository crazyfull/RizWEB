#ifndef RIZWEB_CLSUNIQUEFD_H
#define RIZWEB_CLSUNIQUEFD_H

#include <unistd.h>
#include <utility>

namespace rizweb {

// Owns a file descriptor and closes it on destruction (move only).
class UniqueFd
{
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : m_fd(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : m_fd(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    int get() const { return m_fd; }
    bool valid() const { return m_fd >= 0; }

    int release()
    {
        int fd = m_fd;
        m_fd = -1;
        return fd;
    }

    void reset(int fd = -1)
    {
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = fd;
    }

private:
    int m_fd = -1;
};

} // namespace rizweb

#endif // RIZWEB_CLSUNIQUEFD_H
