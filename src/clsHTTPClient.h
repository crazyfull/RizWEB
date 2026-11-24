#ifndef CLSHTTPCLIENT_H
#define CLSHTTPCLIENT_H
#include "HTTP/clsHTTPAsyncResponse.h"
#include "HTTP/clsHTTPBodyDecoder.h"
#include "HTTP/clsHTTPRequest.h"
#include "HTTP/clsHTTPResponse.h"
#include "HTTP/clsHTTPRouter.h"
#include "HTTP/clsHTTPUpload.h"
#include "HTTP/clsHTTPUploadTracker.h"
#include "HTTP/clsHTTPWSocketParser.h"
#include "clsNetBackend.h"
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rizweb {

class WEBMaster;

// One client connection. It lives on a single reactor thread; every callback and method runs there.
// Use HTTPClientList::post() to reach a client from another thread.
class HTTPClient : public NetSocket, public HTTPWSocketParser
{
public:
    HTTPClient(WEBMaster &master, bool isTls);
    ~HTTPClient() override;

    uint32_t id() const { return m_id; }
    bool isWebSocket() const { return m_webSocket; }
    const std::string &clientIp() const { return m_ip; }
    // Long-lived connections (for example a status page that is polled rarely) can opt out of the idle timeout.
    void setIdleTimeoutEnabled(bool enabled);
    void setArgument(void *argument) { m_argument = argument; }
    void *argument() const { return m_argument; }

    // Login helper for the authentication callback: creates the session and answers the request.
    void authenticationResponse(bool successful, const std::string &message = std::string());

    // --- used by HTTPResponse / static file handler ---
    void sendBuffer(std::string_view data);
    void sendParts(std::string_view first, std::string_view second);   // one writev / one TLS record
    void sendShared(std::shared_ptr<const std::string> body);
    bool streamFile(const std::string &path, uint64_t offset, uint64_t length, bool keepAlive);
    void onResponseSent(bool keepAlive);

    // --- deferred (async) responses, see HTTPResponse::defer() / AsyncResponse ---
    std::shared_ptr<AsyncResponse> beginDeferred();
    void completeDeferred(uint64_t token, const AsyncResponse::Writer &writer);   // reactor thread only

protected:
    // NetSocket hooks
    void onAccepted() override;
    void onReceiveData(const uint8_t *data, size_t length) override;
    void onResume() override;
    void onClose() override;

    // HTTPWSocketParser hooks
    void webSocketSendRaw(const std::string &bytes) override;
    void webSocketCloseTransport() override { close(); }
    void webSocketMessage(bool isBinary, const char *payload, size_t length) override;

private:
    struct FileStream;
    struct UploadSession;

    void handleInput(const char *data, size_t length);
    void dispatch();
    void handleWebSocketUpgrade(const std::shared_ptr<const WebSocketEndpoint> &endpoint);
    void startUpload(const HTTPRouter::Route &route, std::vector<std::pair<std::string, std::string>> params);
    size_t feedUpload(const char *data, size_t length);
    void finishUpload();
    void abortUpload(int status, const char *message);
    void handleLogin();
    void parseError(HTTPParseResult result);
    void pumpStream();
    void finishStream();
    void sweepRetiredFiles(bool force);
    static void releaseShared(void *data, void *argument);

    WEBMaster &m_master;
    uint32_t m_id;
    bool m_tls;
    bool m_registered = false;
    bool m_limited = false;              // holds a connection slot of the per-IP limiter
    std::string m_ip;
    std::shared_ptr<const WebSocketEndpoint> m_endpoint;   // WebSocket route that accepted the upgrade
    bool m_webSocket = false;
    bool m_deferred = false;             // the answer to the current request is produced later; pipelined input waits
    uint64_t m_deferToken = 0;
    uint64_t m_deferTimer = 0;           // DeadlineQueue entry that answers 504 when nobody does
    bool m_streamRequest = false;        // the current request belongs to a streaming route (cannot be deferred)
    bool m_busy = false;                 // a file is being streamed; pipelined input waits
    void *m_argument = nullptr;
    std::string m_input;
    HTTPRequest m_request;
    HTTPResponse m_response;
    std::unique_ptr<FileStream> m_stream;
    std::unique_ptr<UploadSession> m_upload;     // a streaming route is receiving its body
    bool m_continueSent = false;                 // "100 Continue" already sent for the request being received
    bool m_requestKeepAlive = true;              // keep-alive wish of the request while an upload is being decided
    std::vector<int> m_retiredFds;       // fds still referenced by the send queue (sendFile)
};

} // namespace rizweb

#endif // CLSHTTPCLIENT_H
