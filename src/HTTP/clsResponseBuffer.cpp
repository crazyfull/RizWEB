#include "clsResponseBuffer.h"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace rizweb {

namespace {

constexpr size_t kPooledBlocks = 64;            // per thread; 64 x 4 KB

struct BlockPool
{
    std::vector<char *> blocks;
    ~BlockPool()
    {
        for (char *block : blocks)
            ::free(block);
    }
};

BlockPool &pool()
{
    thread_local BlockPool instance;
    return instance;
}

} // namespace

void ResponseBuffer::grow(size_t needed)
{
    if (!m_data && needed <= kBlockSize) {
        BlockPool &p = pool();
        if (!p.blocks.empty()) {
            m_data = p.blocks.back();
            p.blocks.pop_back();
        } else {
            m_data = static_cast<char *>(::malloc(kBlockSize));
            if (!m_data)
                throw std::bad_alloc();
        }
        m_capacity = kBlockSize;
        return;
    }
    size_t capacity = m_capacity ? m_capacity : kBlockSize;
    while (capacity < needed)
        capacity *= 2;
    char *bigger = static_cast<char *>(::malloc(capacity));
    if (!bigger)
        throw std::bad_alloc();
    if (m_size)
        ::memcpy(bigger, m_data, m_size);
    if (m_capacity == kBlockSize) {                 // the pooled block we outgrew goes back
        BlockPool &p = pool();
        if (p.blocks.size() < kPooledBlocks)
            p.blocks.push_back(m_data);
        else
            ::free(m_data);
    } else {
        ::free(m_data);
    }
    m_data = bigger;
    m_capacity = capacity;
}

void ResponseBuffer::append(const char *data, size_t length)
{
    if (length == 0)
        return;
    if (m_size + length > m_capacity)
        grow(m_size + length);
    ::memcpy(m_data + m_size, data, length);
    m_size += length;
}

void ResponseBuffer::append(const char *text)
{
    append(text, ::strlen(text));
}

void ResponseBuffer::appendNumber(uint64_t value)
{
    char digits[24];
    auto result = std::to_chars(digits, digits + sizeof(digits), value);
    append(digits, static_cast<size_t>(result.ptr - digits));
}

void ResponseBuffer::erase(size_t position, size_t length)
{
    if (position >= m_size)
        return;
    if (position + length > m_size)
        length = m_size - position;
    ::memmove(m_data + position, m_data + position + length, m_size - position - length);
    m_size -= length;
}

void ResponseBuffer::release()
{
    m_size = 0;
    if (!m_data)
        return;
    if (m_capacity == kBlockSize) {
        BlockPool &p = pool();
        if (p.blocks.size() < kPooledBlocks) {
            p.blocks.push_back(m_data);
            m_data = nullptr;
            m_capacity = 0;
            return;
        }
    }
    ::free(m_data);
    m_data = nullptr;
    m_capacity = 0;
}

} // namespace rizweb
