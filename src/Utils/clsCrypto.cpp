#include "clsCrypto.h"
#include "clsStringUtils.h"
#include <cstring>
#include <random>

#if defined(RIZWEB_HAS_TLS)
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

namespace rizweb {

void Crypto::randomBytes(void *out, size_t length)
{
#if defined(RIZWEB_HAS_TLS)
    RAND_bytes(static_cast<uint8_t *>(out), length);
#else
    static thread_local std::random_device device;     // OS entropy source
    uint8_t *bytes = static_cast<uint8_t *>(out);
    for (size_t i = 0; i < length; i += 4) {
        uint32_t value = device();
        size_t chunk = (length - i < 4) ? (length - i) : 4;
        memcpy(bytes + i, &value, chunk);
    }
#endif
}

std::string Crypto::randomHex(size_t byteCount)
{
    std::string raw(byteCount, '\0');
    randomBytes(&raw[0], byteCount);
    return StringUtils::toHex(reinterpret_cast<const unsigned char *>(raw.data()), raw.size());
}

uint32_t Crypto::randomNumber(uint32_t minimum, uint32_t maximum)
{
    if (maximum <= minimum)
        return minimum;
    uint32_t range = maximum - minimum + 1;
    uint32_t value;
    randomBytes(&value, sizeof(value));
    return minimum + (range == 0 ? value : value % range);
}

#if !defined(RIZWEB_HAS_TLS)
namespace {
struct Sha1State
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t block[64];
    size_t used = 0;
    uint64_t total = 0;

    static uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void process()
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) | (uint32_t(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
        for (int i = 16; i < 80; i++)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const uint8_t *data, size_t length)
    {
        total += length;
        for (size_t i = 0; i < length; i++) {
            block[used++] = data[i];
            if (used == 64) {
                process();
                used = 0;
            }
        }
    }

    std::string finish()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (used != 56)
            update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++)
            len[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        update(len, 8);
        std::string out(20, '\0');
        for (int i = 0; i < 5; i++) {
            out[i * 4] = static_cast<char>(h[i] >> 24);
            out[i * 4 + 1] = static_cast<char>(h[i] >> 16);
            out[i * 4 + 2] = static_cast<char>(h[i] >> 8);
            out[i * 4 + 3] = static_cast<char>(h[i]);
        }
        return out;
    }
};
} // namespace
#endif

std::string Crypto::sha1(const void *data, size_t length)
{
#if defined(RIZWEB_HAS_TLS)
    std::string out(SHA_DIGEST_LENGTH, '\0');
    SHA1(static_cast<const uint8_t *>(data), length, reinterpret_cast<uint8_t *>(&out[0]));
    return out;
#else
    Sha1State state;
    state.update(static_cast<const uint8_t *>(data), length);
    return state.finish();
#endif
}

std::string Crypto::base64Encode(const void *data, size_t length)
{
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *in = static_cast<const uint8_t *>(data);
    std::string out;
    out.reserve((length + 2) / 3 * 4);
    for (size_t i = 0; i < length; i += 3) {
        uint32_t value = uint32_t(in[i]) << 16;
        if (i + 1 < length)
            value |= uint32_t(in[i + 1]) << 8;
        if (i + 2 < length)
            value |= in[i + 2];
        out.push_back(table[(value >> 18) & 63]);
        out.push_back(table[(value >> 12) & 63]);
        out.push_back(i + 1 < length ? table[(value >> 6) & 63] : '=');
        out.push_back(i + 2 < length ? table[value & 63] : '=');
    }
    return out;
}

bool Crypto::secureEquals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace rizweb
