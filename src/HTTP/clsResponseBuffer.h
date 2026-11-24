#ifndef CLSRESPONSEBUFFER_H
#define CLSRESPONSEBUFFER_H
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rizweb {

// Growable byte buffer for building responses without std::string. Storage comes from a small per-thread pool
// of fixed-size blocks, so a short response (a JSON reply, an error page, the header block) never touches the
// heap once the pool is warm. A response that outgrows one block moves to the heap and is freed afterwards.
// The buffer gives its storage back on release() / destruction, from whichever thread that happens on.
class ResponseBuffer
{
public:
    static constexpr size_t kBlockSize = 4096;

    ResponseBuffer() = default;
    ~ResponseBuffer() { release(); }
    ResponseBuffer(const ResponseBuffer &) = delete;
    ResponseBuffer &operator=(const ResponseBuffer &) = delete;

    void append(const char *data, size_t length);
    void append(std::string_view text) { append(text.data(), text.size()); }
    void append(const char *text);
    void append(char c) { append(&c, 1); }
    void appendNumber(uint64_t value);
    void erase(size_t position, size_t length);
    void clear() { m_size = 0; }
    void release();                         // drops the content and returns the storage

    const char *data() const { return m_data; }
    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }
    std::string_view view() const { return std::string_view(m_data ? m_data : "", m_size); }

private:
    void grow(size_t needed);

    char *m_data = nullptr;
    size_t m_size = 0;
    size_t m_capacity = 0;
};

} // namespace rizweb

#endif // CLSRESPONSEBUFFER_H
