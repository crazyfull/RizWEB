#ifndef RIZWEB_CLSRANDOM_H
#define RIZWEB_CLSRANDOM_H

#include <cstddef>
#include <string>

namespace rizweb {

// Cryptographically secure random bytes from the kernel (getrandom, /dev/urandom as fallback).
class Random
{
public:
    // Returns false only if the kernel could not provide randomness; callers must not continue then.
    static bool fill(void* buffer, size_t length);

    // Random token as lowercase hex (2 characters per byte). Empty string on failure.
    static std::string hexToken(size_t byteCount);
};

} // namespace rizweb

#endif // RIZWEB_CLSRANDOM_H
