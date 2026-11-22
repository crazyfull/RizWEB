#ifndef CLSHTTPWSOCKETPARSER_H
#define CLSHTTPWSOCKETPARSER_H
#include "clsWSocketHeader.h"
#include <cstddef>
#include <string>
#include <string_view>

namespace rizweb {

// WebSocket protocol engine (RFC 6455) shared by every HTTPClient that switched protocol.
// It reassembles fragmented messages, answers pings, performs the closing handshake and
// enforces the message size limit. The owner supplies the transport through three hooks.
class HTTPWSocketParser
{
public:
    virtual ~HTTPWSocketParser() = default;

    // Feed bytes received after the upgrade.
    void webSocketParse(const char *data, size_t length);

    void sendText(std::string_view message) { sendFrame(WS_OPCODE_TEXT, message); }
    void sendBinary(const void *data, size_t length) { sendFrame(WS_OPCODE_BINARY, std::string_view(static_cast<const char *>(data), length)); }
    void sendPing(std::string_view payload = {}) { sendFrame(WS_OPCODE_PING, payload.substr(0, 125)); }
    void sendClose(uint16_t statusCode = 1000);

    void setMaxMessageBytes(size_t bytes) { m_maxMessage = bytes; }

protected:
    // transport hooks
    virtual void webSocketSendRaw(const std::string &bytes) = 0;
    virtual void webSocketCloseTransport() = 0;
    virtual void webSocketMessage(bool isBinary, const char *payload, size_t length) = 0;

private:
    void sendFrame(uint8_t opcode, std::string_view payload);
    void fail(uint16_t statusCode);

    std::string m_buffer;
    std::string m_message;
    bool m_messageIsBinary = false;
    bool m_inFragmentedMessage = false;
    bool m_closeSent = false;
    bool m_failed = false;
    size_t m_maxMessage = 1024 * 1024;
};

} // namespace rizweb

#endif // CLSHTTPWSOCKETPARSER_H
