#ifndef RIZWEB_CLSBASE64_H
#define RIZWEB_CLSBASE64_H

#include <cstdint>
#include <string>
#include <string_view>

namespace rizweb {

class Base64
{
public:
    static std::string encode(const void* data, size_t length);
    static std::string encode(std::string_view data);

    // Strict decoder (padding required, no whitespace). Returns false on malformed input.
    static bool decode(std::string_view input, std::string& output);
};

} // namespace rizweb

#endif // RIZWEB_CLSBASE64_H
