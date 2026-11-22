#include "clsHTTPWSocketParser.h"

namespace rizweb {

void HTTPWSocketParser::sendFrame(uint8_t opcode, std::string_view payload)
{
    uint8_t header[WSocketHeader::kMaxEncodedHeader];
    size_t headerLength = WSocketHeader::encode(header, true, opcode, payload.size());
    std::string frame;
    frame.reserve(headerLength + payload.size());
    frame.append(reinterpret_cast<const char *>(header), headerLength);
    frame.append(payload);
    webSocketSendRaw(frame);
}

void HTTPWSocketParser::sendClose(uint16_t statusCode)
{
    if (m_closeSent)
        return;
    m_closeSent = true;
    char payload[2] = {static_cast<char>(statusCode >> 8), static_cast<char>(statusCode & 0xFF)};
    sendFrame(WS_OPCODE_CLOSE, std::string_view(payload, 2));
}

void HTTPWSocketParser::fail(uint16_t statusCode)
{
    m_failed = true;
    sendClose(statusCode);
    webSocketCloseTransport();
}

void HTTPWSocketParser::webSocketParse(const char *data, size_t length)
{
    if (m_failed)
        return;
    m_buffer.append(data, length);

    size_t offset = 0;
    while (offset < m_buffer.size()) {
        WSFrameHeader header;
        int state = WSocketHeader::decode(reinterpret_cast<const uint8_t *>(m_buffer.data()) + offset, m_buffer.size() - offset, header);
        if (state == 0)
            break;
        if (state < 0 || header.rsv || !header.masked) {     // clients must mask every frame
            fail(1002);
            return;
        }
        bool control = (header.opcode & 0x08) != 0;
        if (control && (!header.final || header.payloadLength > 125)) {
            fail(1002);
            return;
        }
        if (header.payloadLength > m_maxMessage) {
            fail(1009);
            return;
        }
        size_t total = header.headerLength + static_cast<size_t>(header.payloadLength);
        if (m_buffer.size() - offset < total)
            break;

        uint8_t *payload = reinterpret_cast<uint8_t *>(&m_buffer[offset + header.headerLength]);
        size_t payloadLength = static_cast<size_t>(header.payloadLength);
        WSocketHeader::unmask(payload, payloadLength, header.maskKey);
        const char *text = reinterpret_cast<const char *>(payload);

        switch (header.opcode) {
        case WS_OPCODE_PING:
            sendFrame(WS_OPCODE_PONG, std::string_view(text, payloadLength));
            break;
        case WS_OPCODE_PONG:
            break;
        case WS_OPCODE_CLOSE:
            sendClose(payloadLength >= 2 ? static_cast<uint16_t>((uint8_t(text[0]) << 8) | uint8_t(text[1])) : 1000);
            m_failed = true;
            webSocketCloseTransport();
            return;
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            if (m_inFragmentedMessage) {
                fail(1002);
                return;
            }
            m_messageIsBinary = header.opcode == WS_OPCODE_BINARY;
            if (header.final) {
                webSocketMessage(m_messageIsBinary, text, payloadLength);
            } else {
                m_inFragmentedMessage = true;
                m_message.assign(text, payloadLength);
            }
            break;
        case WS_OPCODE_CONTINUATION:
            if (!m_inFragmentedMessage || m_message.size() + payloadLength > m_maxMessage) {
                fail(m_inFragmentedMessage ? 1009 : 1002);
                return;
            }
            m_message.append(text, payloadLength);
            if (header.final) {
                m_inFragmentedMessage = false;
                std::string complete;
                complete.swap(m_message);
                webSocketMessage(m_messageIsBinary, complete.data(), complete.size());
            }
            break;
        default:
            fail(1002);
            return;
        }
        offset += total;
        if (m_failed)
            return;
    }
    m_buffer.erase(0, offset);
}

} // namespace rizweb
