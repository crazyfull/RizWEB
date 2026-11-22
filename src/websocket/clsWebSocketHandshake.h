#ifndef RIZWEB_CLSWEBSOCKETHANDSHAKE_H
#define RIZWEB_CLSWEBSOCKETHANDSHAKE_H

#include <string>
#include <string_view>

namespace rizweb {

class WebSocketHandshake
{
public:
    // Sec-WebSocket-Accept value for a client key (RFC 6455, 4.2.2).
    static std::string acceptKey(std::string_view clientKey);

    // The client key must be the base64 of exactly 16 bytes.
    static bool isValidClientKey(std::string_view clientKey);
};

} // namespace rizweb

#endif // RIZWEB_CLSWEBSOCKETHANDSHAKE_H
