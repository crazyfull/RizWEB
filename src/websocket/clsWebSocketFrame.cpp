#include "clsWebSocketFrame.h"

#include "../util/stringUtil.h"

#include <algorithm>

namespace rizweb {

namespace {

bool isControl(WsOpcode opcode)
{
    return static_cast<uint8_t>(opcode) >= 8;
}

bool isValidCloseCode(uint16_t code)
{
    if (code >= 3000 && code <= 4999)
        return true;
    switch (code) {
    case 1000: case 1001: case 1002: case 1003:
    case 1007: case 1008: case 1009: case 1010: case 1011:
        return true;
    default:
        return false;
    }
}

} // namespace

WebSocketFrameParser::WebSocketFrameParser(size_t maxMessageBytes, bool requireMask)
    : m_maxMessageBytes(maxMessageBytes), m_requireMask(requireMask)
{
}

bool WebSocketFrameParser::fail(uint16_t code)
{
    m_failed = true;
    m_errorCode = code;
    return false;
}

bool WebSocketFrameParser::parseHeader()
{
    const uint8_t* h = reinterpret_cast<const uint8_t*>(m_headerBytes.data());

    const bool fin = (h[0] & 0x80) != 0;
    if ((h[0] & 0x70) != 0)
        return fail(1002);   // reserved bits without a negotiated extension

    const uint8_t rawOpcode = h[0] & 0x0F;
    const bool validOpcode = rawOpcode <= 2 || (rawOpcode >= 8 && rawOpcode <= 10);
    if (!validOpcode)
        return fail(1002);
    const WsOpcode opcode = static_cast<WsOpcode>(rawOpcode);

    const bool masked = (h[1] & 0x80) != 0;
    if (m_requireMask && !masked)
        return fail(1002);

    uint64_t length = h[1] & 0x7F;
    size_t position = 2;
    if (length == 126) {
        length = (static_cast<uint64_t>(h[2]) << 8) | h[3];
        position = 4;
        if (length < 126)
            return fail(1002);   // not the shortest encoding
    } else if (length == 127) {
        length = 0;
        for (int i = 0; i < 8; i++)
            length = (length << 8) | h[2 + i];
        position = 10;
        if ((length >> 63) != 0 || length < 65536)
            return fail(1002);
    }

    if (masked) {
        for (int i = 0; i < 4; i++)
            m_maskKey[i] = h[position + i];
    }

    if (isControl(opcode)) {
        if (!fin || length > 125)
            return fail(1002);
    } else if (opcode == WsOpcode::continuation) {
        if (!m_fragmenting)
            return fail(1002);
    } else if (m_fragmenting) {
        return fail(1002);   // new data frame while a fragmented message is open
    }

    if (!isControl(opcode)) {
        uint64_t already = m_fragmenting ? m_assembly.size() : 0;
        if (length > m_maxMessageBytes || already + length > m_maxMessageBytes)
            return fail(1009);
    }

    m_frameOpcode = opcode;
    m_frameFin = fin;
    m_frameMasked = masked;
    m_maskIndex = 0;
    m_remaining = length;
    m_framePayload.clear();
    m_framePayload.reserve(static_cast<size_t>(std::min<uint64_t>(length, 64 * 1024)));
    return true;
}

bool WebSocketFrameParser::finishFrame(bool& messageReady)
{
    messageReady = false;

    if (isControl(m_frameOpcode)) {
        if (m_frameOpcode == WsOpcode::close) {
            if (m_framePayload.size() == 1)
                return fail(1002);
            if (m_framePayload.size() >= 2) {
                uint16_t code = static_cast<uint16_t>((static_cast<uint8_t>(m_framePayload[0]) << 8)
                                                      | static_cast<uint8_t>(m_framePayload[1]));
                if (!isValidCloseCode(code))
                    return fail(1002);
                if (!stringUtil::isValidUtf8(std::string_view(m_framePayload).substr(2)))
                    return fail(1007);
            }
        }
        m_message.opcode = m_frameOpcode;
        m_message.payload = std::move(m_framePayload);
        messageReady = true;
        return true;
    }

    if (m_frameOpcode != WsOpcode::continuation) {
        m_assemblyOpcode = m_frameOpcode;
        m_assembly = std::move(m_framePayload);
    } else {
        m_assembly += m_framePayload;
    }

    if (!m_frameFin) {
        m_fragmenting = true;
        return true;
    }

    m_fragmenting = false;
    if (m_assemblyOpcode == WsOpcode::text && !stringUtil::isValidUtf8(m_assembly))
        return fail(1007);

    m_message.opcode = m_assemblyOpcode;
    m_message.payload = std::move(m_assembly);
    m_assembly.clear();
    messageReady = true;
    return true;
}

WsParseStatus WebSocketFrameParser::feed(const uint8_t* data, size_t length, size_t& consumed)
{
    consumed = 0;
    if (m_failed)
        return WsParseStatus::error;

    size_t offset = 0;
    while (offset < length) {
        if (m_state == State::header) {
            // collect bytes until the header (2 + extended length + mask) is complete
            while (offset < length) {
                m_headerBytes += static_cast<char>(data[offset++]);

                size_t needed = 2;
                if (m_headerBytes.size() >= 2) {
                    uint8_t len7 = static_cast<uint8_t>(m_headerBytes[1]) & 0x7F;
                    needed += (len7 == 126) ? 2 : (len7 == 127 ? 8 : 0);
                    needed += (static_cast<uint8_t>(m_headerBytes[1]) & 0x80) ? 4 : 0;
                }
                if (m_headerBytes.size() >= 2 && m_headerBytes.size() == needed)
                    break;
            }

            size_t headerSize = m_headerBytes.size();
            bool complete = false;
            if (headerSize >= 2) {
                uint8_t len7 = static_cast<uint8_t>(m_headerBytes[1]) & 0x7F;
                size_t needed = 2 + ((len7 == 126) ? 2 : (len7 == 127 ? 8 : 0)) + ((static_cast<uint8_t>(m_headerBytes[1]) & 0x80) ? 4 : 0);
                complete = (headerSize == needed);
            }
            if (!complete)
                break;

            bool ok = parseHeader();
            m_headerBytes.clear();
            if (!ok) {
                consumed = offset;
                return WsParseStatus::error;
            }
            m_state = State::payload;
        }

        // payload (may be empty)
        size_t take = static_cast<size_t>(std::min<uint64_t>(m_remaining, length - offset));
        size_t start = m_framePayload.size();
        m_framePayload.append(reinterpret_cast<const char*>(data + offset), take);
        if (m_frameMasked) {
            for (size_t i = 0; i < take; i++)
                m_framePayload[start + i] = static_cast<char>(static_cast<uint8_t>(m_framePayload[start + i]) ^ m_maskKey[(m_maskIndex++) & 3]);
        }
        offset += take;
        m_remaining -= take;

        if (m_remaining == 0) {
            m_state = State::header;
            bool ready = false;
            if (!finishFrame(ready)) {
                consumed = offset;
                return WsParseStatus::error;
            }
            if (ready) {
                consumed = offset;
                return WsParseStatus::message;
            }
        }
    }

    consumed = offset;
    return WsParseStatus::needMore;
}

std::string WebSocketFrameBuilder::header(WsOpcode opcode, size_t payloadLength)
{
    std::string out;
    out += static_cast<char>(0x80 | static_cast<uint8_t>(opcode));
    if (payloadLength < 126) {
        out += static_cast<char>(payloadLength);
    } else if (payloadLength <= 0xFFFF) {
        out += static_cast<char>(126);
        out += static_cast<char>((payloadLength >> 8) & 0xFF);
        out += static_cast<char>(payloadLength & 0xFF);
    } else {
        out += static_cast<char>(127);
        for (int i = 7; i >= 0; i--)
            out += static_cast<char>((static_cast<uint64_t>(payloadLength) >> (i * 8)) & 0xFF);
    }
    return out;
}

std::string WebSocketFrameBuilder::build(WsOpcode opcode, std::string_view payload)
{
    std::string out = header(opcode, payload.size());
    out.append(payload);
    return out;
}

std::string WebSocketFrameBuilder::buildClose(uint16_t code, std::string_view reason)
{
    std::string payload;
    payload += static_cast<char>((code >> 8) & 0xFF);
    payload += static_cast<char>(code & 0xFF);
    payload.append(reason.substr(0, 123));
    return build(WsOpcode::close, payload);
}

} // namespace rizweb
