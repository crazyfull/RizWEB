#include "clsBase64.h"

namespace rizweb {

namespace {
const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
} // namespace

std::string Base64::encode(const void* data, size_t length)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < length; i += 3) {
        uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8) | bytes[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }

    if (i + 1 == length) {
        uint32_t v = static_cast<uint32_t>(bytes[i]) << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == length) {
        uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string Base64::encode(std::string_view data)
{
    return encode(data.data(), data.size());
}

bool Base64::decode(std::string_view input, std::string& output)
{
    output.clear();
    if (input.size() % 4 != 0)
        return false;

    output.reserve(input.size() / 4 * 3);
    for (size_t i = 0; i < input.size(); i += 4) {
        int a = decodeChar(input[i]);
        int b = decodeChar(input[i + 1]);
        if (a < 0 || b < 0)
            return false;

        bool pad2 = input[i + 2] == '=';
        bool pad3 = input[i + 3] == '=';
        bool lastGroup = (i + 4 == input.size());
        if ((pad2 || pad3) && !lastGroup)
            return false;
        if (pad2 && !pad3)
            return false;

        int c = pad2 ? 0 : decodeChar(input[i + 2]);
        int d = pad3 ? 0 : decodeChar(input[i + 3]);
        if (c < 0 || d < 0)
            return false;

        uint32_t v = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12)
                     | (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
        output += static_cast<char>((v >> 16) & 0xFF);
        if (!pad2)
            output += static_cast<char>((v >> 8) & 0xFF);
        if (!pad3)
            output += static_cast<char>(v & 0xFF);
    }
    return true;
}

} // namespace rizweb
