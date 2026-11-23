#ifndef CLSHTTPFETCHTYPES_H
#define CLSHTTPFETCHTYPES_H
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rizweb {

using HTTPHeaderList = std::vector<std::pair<std::string, std::string>>;

// One outgoing HTTP/1.1 request (see HTTPFetcher).
struct HTTPFetchRequest
{
    std::string method = "GET";
    std::string url;                          // http://host[:port]/path?query or https://...
    HTTPHeaderList headers;                   // Host, Content-Length and Connection are set automatically
    std::string body;

    uint32_t timeoutSeconds = 30;             // connect + TLS handshake + inactivity timeout
    uint64_t maxResponseBytes = 64ull * 1024 * 1024;   // decoded body limit, 0 = unlimited
    bool followRedirects = true;              // 301/302/303/307/308; https -> http is never followed
    int maxRedirects = 5;
    bool verifyTLS = true;                    // false accepts any certificate (testing only)
    bool blockPrivateNetworks = false;        // refuse loopback / private / link-local addresses (SSRF protection for user supplied URLs; netRector backend only, the request fails on netRectorClient)

    // Streaming: when set, the body is passed on in pieces instead of being stored in the response.
    // Return false to abort the transfer.
    std::function<bool(const char *data, size_t length)> onBody;
    // Download straight to a new file (2xx responses only); response.body stays empty. Fails when the file exists.
    std::string downloadPath;
    // Called while the body arrives (total = 0 when the size is unknown).
    std::function<void(uint64_t received, uint64_t total)> onProgress;

    HTTPFetchRequest &header(const std::string &name, const std::string &value)
    {
        headers.emplace_back(name, value);
        return *this;
    }
};

struct HTTPFetchResponse
{
    int status = 0;                           // 0 when no response was received (see error)
    std::string reason;
    HTTPHeaderList headers;                   // in the order received, names as sent
    std::string body;
    uint64_t bodyBytes = 0;                   // bytes of body received (also when streamed / downloaded)
    std::string error;                        // "" when a response was received completely
    std::string finalUrl;                     // URL of the last request (after redirects)
    int redirects = 0;

    bool completed() const { return error.empty() && status > 0; }
    bool ok() const { return completed() && status >= 200 && status < 300; }
    // First header with this name (case-insensitive) or nullptr.
    const std::string *header(const std::string &name) const;
    std::vector<std::string> headerValues(const std::string &name) const;     // e.g. every Set-Cookie
};

} // namespace rizweb

#endif // CLSHTTPFETCHTYPES_H
