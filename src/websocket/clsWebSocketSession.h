#ifndef RIZWEB_CLSWEBSOCKETSESSION_H
#define RIZWEB_CLSWEBSOCKETSESSION_H

#include "clsWebSocketFrame.h"
#include "../http/clsHttpRequest.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rizweb {

// What a handler sees of one WebSocket connection. Only valid inside the callbacks, on the
// reactor thread that owns the connection (that is the netRector rule for every socket call).
class WebSocketSession
{
public:
    virtual ~WebSocketSession() = default;

    virtual bool sendText(std::string_view text) = 0;
    virtual bool sendBinary(std::string_view data) = 0;
    virtual bool sendPing(std::string_view data = std::string_view()) = 0;

    // Sends a close frame and closes once the send queue is flushed.
    // (Not called close(): the connection class also inherits netrector::TCPSocket::close.)
    virtual void disconnect(uint16_t code = 1000, std::string_view reason = std::string_view()) = 0;

    virtual const HttpRequest& request() const = 0;
    virtual bool isOpen() const = 0;

    // Free slot for the application (for example a user id); never touched by the library.
    void* userData = nullptr;
};

struct WebSocketHandlers
{
    // Optional extra check after the origin test, before the upgrade is accepted.
    std::function<bool(const HttpRequest&)> authorize;
    std::function<void(WebSocketSession&)> onOpen;
    std::function<void(WebSocketSession&, WsOpcode, std::string_view)> onMessage;
    // code 1005 means the peer sent no code, 1006 means the connection dropped without a close frame
    std::function<void(WebSocketSession&, uint16_t code)> onClose;

    size_t maxMessageBytes = 1024 * 1024;

    // A browser sends an Origin header. It must name the same host as the Host header
    // unless it is listed here (or allowAnyOrigin is set). Requests without Origin
    // (non-browser clients) are accepted.
    std::vector<std::string> allowedOrigins;
    bool allowAnyOrigin = false;
};

} // namespace rizweb

#endif // RIZWEB_CLSWEBSOCKETSESSION_H
