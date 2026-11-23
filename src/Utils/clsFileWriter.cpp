#include "clsFileWriter.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rizweb {

FileWriter::~FileWriter()
{
    close();
}

bool FileWriter::open(const std::string &path)
{
    close();
#if defined(_WIN32)
    m_fd = ::_open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    m_fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
#endif
    if (m_fd < 0)
        return false;
    m_path = path;
    m_written = 0;
    return true;
}

bool FileWriter::write(const char *data, size_t length)
{
    while (length > 0) {
#if defined(_WIN32)
        int done = ::_write(m_fd, data, static_cast<unsigned>(length > 0x40000000 ? 0x40000000 : length));
#else
        ssize_t done = ::write(m_fd, data, length);
#endif
        if (done < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += done;
        length -= static_cast<size_t>(done);
        m_written += static_cast<uint64_t>(done);
    }
    return true;
}

bool FileWriter::close()
{
    if (m_fd < 0)
        return true;
#if defined(_WIN32)
    int result = ::_close(m_fd);
#else
    int result = ::close(m_fd);
#endif
    m_fd = -1;
    return result == 0;
}

void FileWriter::discard()
{
    bool wasOpen = m_fd >= 0 || !m_path.empty();
    close();
    if (wasOpen && !m_path.empty())
        std::remove(m_path.c_str());
    m_path.clear();
}

} // namespace rizweb
