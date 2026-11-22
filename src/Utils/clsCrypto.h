#ifndef CLSCRYPTO_H
#define CLSCRYPTO_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace rizweb {

class Crypto
{
public:
    // Cryptographically secure random bytes (BoringSSL when built with TLS, otherwise the OS source).
    static void randomBytes(void *out, size_t length);
    static std::string randomHex(size_t byteCount);
    static uint32_t randomNumber(uint32_t minimum, uint32_t maximum);

    static std::string sha1(const void *data, size_t length);     // raw 20 bytes
    static std::string base64Encode(const void *data, size_t length);

    // Constant-time comparison, for tokens.
    static bool secureEquals(const std::string &a, const std::string &b);
};

} // namespace rizweb

#endif // CLSCRYPTO_H
