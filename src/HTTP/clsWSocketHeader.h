#ifndef CLSWSOCKETHEADER_H
#define CLSWSOCKETHEADER_H
#include <cstddef>
#include <cstdint>

namespace rizweb {

enum WebSocketOpcode : uint8_t
{
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
};

struct WSFrameHeader
{
    bool final = false;
    bool rsv = false;           // any RSV bit set (no extensions are negotiated, so this is an error)
    bool masked = false;
    uint8_t opcode = 0;
    uint8_t maskKey[4] = {0, 0, 0, 0};
    uint64_t payloadLength = 0;
    size_t headerLength = 0;
};

// RFC 6455 frame header encoding / decoding.
class WSocketHeader
{
public:
    static constexpr size_t kMaxEncodedHeader = 10;     // server frames are never masked

    // 1 = header decoded, 0 = need more bytes, -1 = invalid
    static int decode(const uint8_t *data, size_t length, WSFrameHeader &out);
    static size_t encode(uint8_t *out, bool final, uint8_t opcode, uint64_t payloadLength);
    static void unmask(uint8_t *payload, size_t length, const uint8_t *key);
};

} // namespace rizweb

#endif // CLSWSOCKETHEADER_H
