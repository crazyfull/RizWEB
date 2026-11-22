#ifndef CLSHTTPREQUEST_H
#define CLSHTTPREQUEST_H
#include "clsHTTPHeaderfields.h"
#include <cstddef>
#include <string>
#include <string_view>

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
    NotImplemented,     // 501 (Transfer-Encoding / chunked request bodies)
    VersionNotSupported // 505
};

class HTTPRequest
{
public:
    static constexpr size_t kMaxHeaderBytes = 16 * 1024;
    static constexpr size_t kMaxUrlLength = 8 * 1024;

    // Parses the first request found in [data, data + length).
    HTTPParseResult parse(const char *data, size_t length, size_t maxBodyBytes);
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
    bool m_http11 = true;
    bool m_keepAlive = true;
    size_t m_consumed = 0;

    static bool normalizePath(std::string_view rawPath, std::string &out);
    void parseUrlEncodedFields(std::string_view text);
};

} // namespace rizweb

#endif // CLSHTTPREQUEST_H
