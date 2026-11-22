#include "clsWSocketHeader.h"

namespace rizweb {

int WSocketHeader::decode(const uint8_t *data, size_t length, WSFrameHeader &out)
{
    if (length < 2)
        return 0;
    out.final = (data[0] & 0x80) != 0;
    out.rsv = (data[0] & 0x70) != 0;
    out.opcode = data[0] & 0x0F;
    out.masked = (data[1] & 0x80) != 0;
    uint64_t payload = data[1] & 0x7F;
    size_t offset = 2;

    if (payload == 126) {
        if (length < 4)
            return 0;
        payload = (uint64_t(data[2]) << 8) | data[3];
        offset = 4;
    } else if (payload == 127) {
        if (length < 10)
            return 0;
        payload = 0;
        for (int i = 0; i < 8; i++)
            payload = (payload << 8) | data[2 + i];
        if (payload >> 63)
            return -1;
        offset = 10;
    }

    if (out.masked) {
        if (length < offset + 4)
            return 0;
        for (int i = 0; i < 4; i++)
            out.maskKey[i] = data[offset + i];
        offset += 4;
    }
    out.payloadLength = payload;
    out.headerLength = offset;
    return 1;
}

size_t WSocketHeader::encode(uint8_t *out, bool final, uint8_t opcode, uint64_t payloadLength)
{
    out[0] = static_cast<uint8_t>((final ? 0x80 : 0) | (opcode & 0x0F));
    if (payloadLength < 126) {
        out[1] = static_cast<uint8_t>(payloadLength);
        return 2;
    }
    if (payloadLength <= 0xFFFF) {
        out[1] = 126;
        out[2] = static_cast<uint8_t>(payloadLength >> 8);
        out[3] = static_cast<uint8_t>(payloadLength);
        return 4;
    }
    out[1] = 127;
    for (int i = 0; i < 8; i++)
        out[2 + i] = static_cast<uint8_t>(payloadLength >> (56 - i * 8));
    return 10;
}

void WSocketHeader::unmask(uint8_t *payload, size_t length, const uint8_t *key)
{
    for (size_t i = 0; i < length; i++)
        payload[i] ^= key[i & 3];
}

} // namespace rizweb
