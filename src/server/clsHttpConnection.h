#ifndef RIZWEB_CLSHTTPCONNECTION_H
#define RIZWEB_CLSHTTPCONNECTION_H

#include "../http/clsHttpRequestParser.h"
#include "../http/clsHttpResponse.h"
#include "../websocket/clsWebSocketFrame.h"
#include "../websocket/clsWebSocketSession.h"

#include <chrono>
#include <string>

#include "clsTCPSocket.h"   // netRector

namespace rizweb {

class WebServer;
struct WebSocketRoute;

// One client connection: HTTP/1.1 with keep-alive and pipelining, and after a successful
// upgrade a WebSocket. It is a netrector::TCPSocket, so the reactor owns and deletes it;
// every method runs on the reactor thread of this connection.
class HttpConnection : public netrector::TCPSocket, public WebSocketSession
{
public:
    explicit HttpConnection(WebServer& server);
    ~HttpConnection() override;

    // ---- netrector::TCPSocket ----
    void onAccepted() override;
    void onReceiveData(const uint8_t* data, size_t length) override;
    void onResume() override;
    void onClose() override;

    // ---- WebSocketSession ----
    bool sendText(std::string_view text) override;
    bool sendBinary(std::string_view data) override;
    bool sendPing(std::string_view data = std::string_view()) override;
    void disconnect(uint16_t code = 1000, std::string_view reason = std::string_view()) override;
    const HttpRequest& request() const override;
    bool isOpen() const override;

private:
    enum class Mode { http, webSocket, closing };

    void processHttp(const char* data, size_t length);
    void processWebSocket(const uint8_t* data, size_t length);
    void handleRequest();
    void handleWebSocketRequest(const WebSocketRoute& route);
    void rejectRequest(int status, std::string_view message);
    void sendResponse(HttpResponse& response, bool keepAlive);
    bool sendFileBody(std::string& head, UniqueFd file, off_t offset, size_t length);
    bool sendFrame(WsOpcode opcode, std::string_view payload);
    void abortConnection();

    static void releaseString(void* data, void* argument);
    static void releaseShared(void* data, void* argument);
    static void releaseFileTail(void* data, void* argument);

    WebServer& m_server;
    HttpRequestParser m_parser;
    Mode m_mode = Mode::http;
    std::string m_inbox;                // input held back while the send queue is full
    std::string m_remoteAddress;
    uint16_t m_remotePort = 0;
    int m_requestCount = 0;
    bool m_continueSent = false;
    std::chrono::steady_clock::time_point m_requestStart;

    // WebSocket state
    HttpRequest m_upgradeRequest;       // the handshake request, kept for WebSocketSession::request()
    const WebSocketRoute* m_route = nullptr;
    WebSocketFrameParser m_frames;
    bool m_opened = false;              // onOpen was called
    bool m_closeSent = false;
    bool m_closeNotified = false;
    uint16_t m_peerCloseCode = 1006;
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPCONNECTION_H
