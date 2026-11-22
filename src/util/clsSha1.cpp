#include "clsSha1.h"

#include <cstring>

namespace rizweb {

namespace {
inline uint32_t rotateLeft(uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}
} // namespace

Sha1::Sha1()
{
    m_state[0] = 0x67452301;
    m_state[1] = 0xEFCDAB89;
    m_state[2] = 0x98BADCFE;
    m_state[3] = 0x10325476;
    m_state[4] = 0xC3D2E1F0;
}

void Sha1::processBlock(const uint8_t* block)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
               | (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++)
        w[i] = rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3], e = m_state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }

        uint32_t temp = rotateLeft(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotateLeft(b, 30);
        b = a;
        a = temp;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
}

void Sha1::update(const void* data, size_t length)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    m_totalBytes += length;

    while (length > 0) {
        size_t take = 64 - m_bufferLength;
        if (take > length)
            take = length;

        std::memcpy(m_buffer + m_bufferLength, bytes, take);
        m_bufferLength += take;
        bytes += take;
        length -= take;

        if (m_bufferLength == 64) {
            processBlock(m_buffer);
            m_bufferLength = 0;
        }
    }
}

std::array<uint8_t, 20> Sha1::finish()
{
    const uint64_t totalBits = m_totalBytes * 8;

    const uint8_t padStart = 0x80;
    update(&padStart, 1);

    const uint8_t zero = 0;
    while (m_bufferLength != 56)
        update(&zero, 1);

    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; i++)
        lengthBytes[i] = static_cast<uint8_t>(totalBits >> (56 - i * 8));
    update(lengthBytes, 8);

    std::array<uint8_t, 20> digest{};
    for (int i = 0; i < 5; i++) {
        digest[i * 4] = static_cast<uint8_t>(m_state[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(m_state[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(m_state[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i]);
    }
    return digest;
}

std::array<uint8_t, 20> Sha1::hash(std::string_view data)
{
    Sha1 sha;
    sha.update(data.data(), data.size());
    return sha.finish();
}

} // namespace rizweb
