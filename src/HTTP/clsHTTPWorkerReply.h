#ifndef CLSHTTPWORKERREPLY_H
#define CLSHTTPWORKERREPLY_H
#include "clsHTTPAsyncResponse.h"
#include "clsResponseBuffer.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rizweb {

// The response object of a handler that runs on a worker thread (WEBMaster::offload). It only collects status,
// headers and body; flush() hands them to the connection's reactor thread, which sends them. It is used by one
// worker thread and must not be kept after the handler returns.
class WorkerReply
{
public:
    explicit WorkerReply(std::shared_ptr<AsyncResponse> async) : m_async(std::move(async)) {}

    void setContentType(std::string_view type) { setHeaderField("Content-Type", type); }
    void setHeaderField(std::string_view name, std::string_view value);     // replaces a field of the same name
    void write(std::string_view data) { m_body.append(data); }
    void write(const char *data) { m_body.append(data); }
    void writeEscaped(std::string_view untrusted);                          // HTML-escaped
    void flush(int statusCode);                                             // sends; later calls are ignored
    void redirect(std::string_view url, int statusCode = 302);
    void sendError(int statusCode, std::string_view detail = {});
    bool sent() const { return m_sent; }

private:
    std::shared_ptr<AsyncResponse> m_async;
    std::vector<std::pair<std::string, std::string>> m_headers;
    ResponseBuffer m_body;
    bool m_sent = false;
};

} // namespace rizweb

#endif // CLSHTTPWORKERREPLY_H
