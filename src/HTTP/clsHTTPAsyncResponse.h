#ifndef CLSHTTPASYNCRESPONSE_H
#define CLSHTTPASYNCRESPONSE_H
#include <cstdint>
#include <functional>
#include <string_view>

namespace rizweb {

class HTTPClient;
class HTTPClientList;
class HTTPResponse;

// Handle for an answer that is produced later. A handler calls HTTPResponse::defer(), returns at once (the reactor
// thread is free for other connections) and keeps this handle, for example inside an HTTPFetcher::fetchAsync
// callback. respond() may be called from any thread, exactly once; the writer runs on the reactor thread of the
// connection and writes into the real response. Requests that were pipelined behind this one wait and are
// answered afterwards, so the order of the responses is always the order of the requests.
//
// The handle must not outlive the WEBMaster it came from. If the connection is gone (client hung up, or
// AsyncTimeout expired) respond() returns false and nothing runs.
class AsyncResponse
{
public:
    using Writer = std::function<void(HTTPResponse &)>;

    // Runs |writer| on the connection's thread. The writer must finish the response (flush(), sendError(), ...).
    bool respond(Writer writer);

    // Shortcuts for a complete answer.
    bool send(int statusCode, std::string_view contentType, std::string_view body);
    bool sendError(int statusCode, std::string_view detail = {});

private:
    friend class HTTPClient;
    AsyncResponse(HTTPClientList &clients, uint32_t clientId, uint64_t token) : m_clients(clients), m_clientId(clientId), m_token(token) {}

    HTTPClientList &m_clients;
    uint32_t m_clientId;
    uint64_t m_token;
};

} // namespace rizweb

#endif // CLSHTTPASYNCRESPONSE_H
