#ifndef RIZWEB_CLSSHA1_H
#define RIZWEB_CLSSHA1_H

#include <array>
#include <cstdint>
#include <string_view>

namespace rizweb {

// Small SHA-1, used only for the WebSocket handshake (RFC 6455). Not for security purposes.
class Sha1
{
public:
    Sha1();
    void update(const void* data, size_t length);
    std::array<uint8_t, 20> finish();

    static std::array<uint8_t, 20> hash(std::string_view data);

private:
    void processBlock(const uint8_t* block);

    uint32_t m_state[5];
    uint64_t m_totalBytes = 0;
    uint8_t m_buffer[64];
    size_t m_bufferLength = 0;
};

} // namespace rizweb

#endif // RIZWEB_CLSSHA1_H
