#ifndef RIZWEB_CLSWEBSOCKETFRAME_H
#define RIZWEB_CLSWEBSOCKETFRAME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rizweb {

enum class WsOpcode : uint8_t
{
    continuation = 0,
    text = 1,
    binary = 2,
    close = 8,
    ping = 9,
    pong = 10
};

struct WsMessage
{
    WsOpcode opcode = WsOpcode::text;   // text, binary or a control frame; fragments are already joined
    std::string payload;
};

enum class WsParseStatus { needMore, message, error };

// Incremental RFC 6455 frame reader for the server side.
//
// feed() returns WsParseStatus::message as soon as one complete message (or control frame)
// is available and reports how many bytes it used; call it again with the rest.
// Enforced: reserved bits zero, client frames masked, control frames short and unfragmented,
// correct fragment order, 63 bit lengths, message size limit, valid UTF-8 in text messages
// and valid close frames. On error errorCode() holds the close code to answer with.
class WebSocketFrameParser
{
public:
    explicit WebSocketFrameParser(size_t maxMessageBytes = 1024 * 1024, bool requireMask = true);

    WsParseStatus feed(const uint8_t* data, size_t length, size_t& consumed);

    WsMessage& message() { return m_message; }
    uint16_t errorCode() const { return m_errorCode; }

private:
    enum class State { header, payload };

    bool parseHeader();
    bool finishFrame(bool& messageReady);
    bool fail(uint16_t code);

    size_t m_maxMessageBytes;
    bool m_requireMask;
    State m_state = State::header;
    std::string m_headerBytes;
    WsOpcode m_frameOpcode = WsOpcode::text;
    bool m_frameFin = true;
    bool m_frameMasked = false;
    uint8_t m_maskKey[4] = {0, 0, 0, 0};
    size_t m_maskIndex = 0;
    uint64_t m_remaining = 0;
    std::string m_framePayload;
    bool m_fragmenting = false;
    WsOpcode m_assemblyOpcode = WsOpcode::text;
    std::string m_assembly;
    WsMessage m_message;
    uint16_t m_errorCode = 1002;
    bool m_failed = false;
};

// Builds server-to-client frames (never masked, always FIN).
class WebSocketFrameBuilder
{
public:
    // Frame header for a payload of the given size ("\x81\x05" for a short text frame).
    static std::string header(WsOpcode opcode, size_t payloadLength);
    static std::string build(WsOpcode opcode, std::string_view payload);

    // Close frame: 2 byte big endian code followed by an optional reason.
    static std::string buildClose(uint16_t code, std::string_view reason = std::string_view());
};

} // namespace rizweb

#endif // RIZWEB_CLSWEBSOCKETFRAME_H
