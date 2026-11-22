#ifndef CLSHTTPRESPONSE_H
#define CLSHTTPRESPONSE_H
#include "clsHTTPCookie.h"
#include "clsHTTPRequest.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rizweb {

class HTTPClient;

// One response per request. Headers and body are collected, then flush() writes them to the socket
// in a single send. Large files use sendHeaders() + HTTPClient::streamFile().
class HTTPResponse
{
public:
    explicit HTTPResponse(HTTPClient &client);

    // Called by the client before every request.
    void begin(const HTTPRequest &request);

    // headers
    void addHeaderField(std::string_view name, std::string_view value);
    void setHeaderField(std::string_view name, std::string_view value);      // replaces an existing field
    void addHeaderField(std::string_view name, uint64_t value);
    void setCookie(std::string_view name, std::string_view value, const CookieOptions &options = CookieOptions());
    void setContentType(std::string_view type) { setHeaderField("Content-Type", type); }

    // body
    void write(std::string_view data) { m_body.append(data); }
    void write(const std::string &data) { m_body.append(data); }
    void write(const char *data) { m_body.append(data); }
    void write(long long number) { m_body.append(std::to_string(number)); }

    // sending
    void flush(int statusCode);                                     // headers + body, then done
    void sendHeaders(int statusCode, bool hasLength, uint64_t length); // headers only (HEAD, 304, file streams)
    void redirect(std::string_view url, int statusCode = 302);
    void sendSharedBody(int statusCode, std::shared_ptr<const std::string> body);  // zero-copy cached body
    void abort();                                                   // drop the connection now

    // error pages (the Content-Length is always the real length)
    void sendError(int statusCode, std::string_view detail = {});
    void notFound() { sendError(404, "The requested URL was not found on this server."); }
    void accessDenied() { sendError(403, "Access to this resource is denied."); }
    void badRequest(std::string_view detail = {}) { sendError(400, detail); }
    void internalServerError(std::string_view detail = {}) { sendError(500, detail); }
    void siteNotFound() { sendError(404, "Site not found."); }
    void authenticationFailed(std::string_view message = {});
    void methodNotAllowed(std::string_view allow);
    void switchProtocol(const std::string &webSocketKey);

    int statusCode() const { return m_statusCode; }
    bool isCommitted() const { return m_committed; }
    bool isHeadRequest() const { return m_head; }
    bool keepAlive() const { return m_keepAlive; }
    void forceClose() { m_keepAlive = false; }

private:
    void buildHead(std::string &out, int statusCode, bool hasLength, uint64_t length);
    void finish();

    HTTPClient &m_client;
    std::string m_headers;
    std::string m_body;
    int m_statusCode = 0;
    bool m_committed = false;
    bool m_head = false;
    bool m_keepAlive = true;
    bool m_http11 = true;
};

} // namespace rizweb

#endif // CLSHTTPRESPONSE_H
