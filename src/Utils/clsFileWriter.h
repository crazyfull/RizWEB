#ifndef CLSFILEWRITER_H
#define CLSFILEWRITER_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace rizweb {

// Minimal unbuffered file writer (open(2)/write(2), _open/_write on Windows): bytes go from the caller's buffer
// straight to the kernel, which is what upload-to-disk wants. The file is created exclusively (never overwrites).
class FileWriter
{
public:
    FileWriter() = default;
    ~FileWriter();
    FileWriter(const FileWriter &) = delete;
    FileWriter &operator=(const FileWriter &) = delete;

    bool open(const std::string &path);          // false when it exists already or cannot be created
    bool write(const char *data, size_t length);
    bool close();                                // idempotent
    void discard();                              // close and delete the file
    void forget() { m_path.clear(); }           // keep the (closed) file, drop the reference to it
    bool isOpen() const { return m_fd >= 0; }
    uint64_t written() const { return m_written; }
    const std::string &path() const { return m_path; }

private:
    int m_fd = -1;
    uint64_t m_written = 0;
    std::string m_path;
};

} // namespace rizweb

#endif // CLSFILEWRITER_H
