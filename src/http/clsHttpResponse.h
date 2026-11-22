#ifndef RIZWEB_CLSHTTPRESPONSE_H
#define RIZWEB_CLSHTTPRESPONSE_H

#include "clsHttpHeaders.h"
#include "../util/clsUniqueFd.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <sys/types.h>

namespace rizweb {

struct Cookie
{
    std::string name;
    std::string value;
    std::string path = "/";
    std::string domain;
    std::string sameSite = "Lax";   // "Strict", "Lax", "None" or empty to omit
    int maxAgeSeconds = -1;         // < 0: session cookie
    bool httpOnly = true;
    bool secure = false;
};

// What a handler fills in. The connection serializes and sends it afterwards.
class HttpResponse
{
public:
    HttpResponse() = default;

    void setStatus(int code) { m_status = code; }
    int status() const { return m_status; }

    // Header values containing CR/LF are refused (response splitting). Returns false then.
    bool setHeader(std::string_view name, std::string_view value);
    bool addHeader(std::string_view name, std::string_view value);
    const std::string* header(std::string_view name) const { return m_headers.get(name); }
    const HttpHeaders& headers() const { return m_headers; }

    void setContentType(std::string_view type) { setHeader("Content-Type", type); }
    bool setCookie(const Cookie& cookie);

    // ---- body (memory) ----
    void write(std::string_view data) { m_body.append(data); }
    void setBody(std::string body) { m_body = std::move(body); m_shared.reset(); }
    // Shared immutable body (used by the compressed file cache): sent without copying.
    void setSharedBody(std::shared_ptr<const std::string> body) { m_shared = std::move(body); m_body.clear(); }
    const std::string& body() const { return m_body; }
    std::string takeBody() { return std::move(m_body); }
    const std::shared_ptr<const std::string>& sharedBody() const { return m_shared; }

    // ---- body (file) ----
    // The response owns the descriptor; the connection sends [offset, offset+length) with sendfile.
    void setFileBody(UniqueFd fd, off_t offset, size_t length);
    bool hasFileBody() const { return m_file.valid(); }
    int fileDescriptor() const { return m_file.get(); }
    off_t fileOffset() const { return m_fileOffset; }
    UniqueFd releaseFile() { return std::move(m_file); }

    size_t bodyLength() const;

    // ---- shortcuts ----
    void sendText(int status, std::string_view text, std::string_view contentType = "text/plain; charset=utf-8");
    void sendHtml(int status, std::string_view html) { sendText(status, html, "text/html; charset=utf-8"); }
    void sendJson(int status, std::string_view json) { sendText(status, json, "application/json; charset=utf-8"); }

    // Small HTML error page; the message is escaped.
    void sendError(int status, std::string_view message = std::string_view());

    // Location may be a path or an absolute URL. CR/LF in it turn the response into a 500.
    void redirect(std::string_view location, int status = 302);

    struct HeadOptions
    {
        int versionMinor = 1;
        bool keepAlive = true;
        bool includeContentLength = true;   // false for 1xx/204/304
        std::string_view serverName;
    };

    // "HTTP/1.1 200 OK\r\n...headers...\r\n\r\n". Content-Length is the real body size,
    // so the same head is correct for HEAD requests.
    std::string serializeHead(const HeadOptions& options) const;

    void reset();

private:
    int m_status = 200;
    HttpHeaders m_headers;
    std::string m_body;
    std::shared_ptr<const std::string> m_shared;
    UniqueFd m_file;
    off_t m_fileOffset = 0;
    size_t m_fileLength = 0;
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPRESPONSE_H
