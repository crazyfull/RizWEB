#ifndef CLSHTTPREQUEST_H
#define CLSHTTPREQUEST_H
#include "clsHTTPHeaderfields.h"
#include <cstddef>
#include <string>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace rizweb {

enum class HTTPMethod
{
    Unknown,
    Get,
    Head,
    Post,
    Put,
    Delete,
    Options,
    Patch
};

enum class HTTPParseResult
{
    Completed,          // one full request was parsed; consumedBytes() tells how much input it used
    Incomplete,         // need more data
    BadRequest,         // 400
    UriTooLong,         // 414
    HeaderTooLarge,     // 431
    BodyTooLarge,       // 413
    NotImplemented,     // 501 (unsupported Transfer-Encoding)
    VersionNotSupported // 505
};

// Per-request body limit: called once the headers are parsed (method, decoded path) and the request has a body.
using BodyLimitFunction = std::function<size_t(HTTPMethod, const std::string &urlPath)>;

class HTTPRequest
{
public:
    static constexpr size_t kMaxHeaderBytes = 16 * 1024;
    static constexpr size_t kMaxUrlLength = 8 * 1024;

    // Parses the first request found in [data, data + length). Bodies with Content-Length and
    // Transfer-Encoding: chunked are supported. |limitFor| (optional) overrides maxBodyBytes per path.
    HTTPParseResult parse(const char *data, size_t length, size_t maxBodyBytes, const BodyLimitFunction *limitFor = nullptr);
    size_t consumedBytes() const { return m_consumed; }

    HTTPMethod method() const { return m_method; }
    const std::string &methodName() const { return m_methodName; }
    // Decoded path with "." / ".." resolved ("/a/b/" keeps its trailing slash). Never escapes the root.
    const std::string &urlPath() const { return m_urlPath; }
    const std::string &queryString() const { return m_queryString; }
    const std::string &host() const { return m_host; }        // lower case, without port
    const std::string &body() const { return m_body; }
    bool isHttp11() const { return m_http11; }
    bool keepAlive() const { return m_keepAlive; }
    bool isDirectoryRequest() const;                          // path ends with '/'

    const std::string *headerField(std::string_view name) const { return m_headers.fieldByName(name); }
    const std::string *queryField(std::string_view name) const { return m_query.fieldByName(name); }
    const HTTPHeaderfields &headerFields() const { return m_headers; }
    const HTTPHeaderfields &queryFields() const { return m_query; }

    // Route parameters: ":id" segments and the "*" wildcard of the matched route (set by the router).
    std::string param(std::string_view name) const;
    void setParams(std::vector<std::pair<std::string, std::string>> params) { m_params = std::move(params); }

    // Filesystem path chosen by the server for this request (set by the HTTP client).
    const std::string &filePath() const { return m_filePath; }
    void setFilePath(const std::string &path) { m_filePath = path; }
    void overrideUrlPath(const std::string &path) { m_urlPath = path; }

private:
    HTTPMethod m_method = HTTPMethod::Unknown;
    std::string m_methodName;
    std::string m_urlPath;
    std::string m_queryString;
    std::string m_host;
    std::string m_body;
    std::string m_filePath;
    HTTPHeaderfields m_headers;
    HTTPHeaderfields m_query;
    std::vector<std::pair<std::string, std::string>> m_params;
    bool m_http11 = true;
    bool m_keepAlive = true;
    size_t m_consumed = 0;

    static bool normalizePath(std::string_view rawPath, std::string &out);
    void parseUrlEncodedFields(std::string_view text);
    // 1 = complete (body + consumed set), 0 = need more data, -1 = malformed, -2 = too large
    static int decodeChunked(const char *data, size_t length, size_t limit, std::string &body, size_t &used);
};

} // namespace rizweb

#endif // CLSHTTPREQUEST_H
