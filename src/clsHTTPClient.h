#ifndef CLSHTTPCLIENT_H
#define CLSHTTPCLIENT_H
#include "HTTP/clsHTTPRequest.h"
#include "HTTP/clsHTTPResponse.h"
#include "HTTP/clsHTTPWSocketParser.h"
#include "clsNetBackend.h"
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
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
    std::string clientIp() const;
    // Long-lived connections (for example a status page that is polled rarely) can opt out of the idle timeout.
    void setIdleTimeoutEnabled(bool enabled);
    void setArgument(void *argument) { m_argument = argument; }
    void *argument() const { return m_argument; }

    // Login helper for the authentication callback: creates the session and answers the request.
    void authenticationResponse(bool successful, const std::string &message = std::string());

    // --- used by HTTPResponse / static file handler ---
    void sendBuffer(const std::string &data);
    void sendShared(std::shared_ptr<const std::string> body);
    bool streamFile(const std::string &path, uint64_t offset, uint64_t length, bool keepAlive);
    void onResponseSent(bool keepAlive);

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

    void handleInput(const char *data, size_t length);
    void dispatch();
    void handleWebSocketUpgrade();
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
    bool m_webSocket = false;
    bool m_busy = false;                 // a file is being streamed; pipelined input waits
    void *m_argument = nullptr;
    std::string m_input;
    HTTPRequest m_request;
    HTTPResponse m_response;
    std::unique_ptr<FileStream> m_stream;
    std::vector<int> m_retiredFds;       // fds still referenced by the send queue (sendFile)
};

} // namespace rizweb

#endif // CLSHTTPCLIENT_H
