#ifndef RIZWEB_CLSHTTPREQUEST_H
#define RIZWEB_CLSHTTPREQUEST_H

#include "clsHttpHeaders.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rizweb {

enum class HttpMethod { unknown, get, head, post, put, patch, del, options };

// A parsed request. The parser fills it, handlers only read it.
// All strings are owned, so a handler may keep them after it returns.
class HttpRequest
{
public:
    using Param = std::pair<std::string, std::string>;

    HttpMethod method = HttpMethod::unknown;
    std::string methodName;      // as sent, for example "GET"
    std::string target;          // raw request target, "/a%20b?x=1"
    std::string path;            // percent decoded and normalized, "/a b"
    std::string rawQuery;        // text after '?', still encoded
    int versionMinor = 1;        // HTTP/1.x
    HttpHeaders headers;
    std::string body;
    bool keepAlive = true;

    // filled by the connection
    bool isSecure = false;
    std::string remoteAddress;
    uint16_t remotePort = 0;

    const std::string* header(std::string_view name) const { return headers.get(name); }

    // Host header without the port ("example.com", "[::1]")
    std::string host() const;

    // Decoded query parameters (parsed on first use).
    const std::vector<Param>& queryParams() const;
    std::optional<std::string> queryValue(std::string_view name) const;

    // Decoded fields of an application/x-www-form-urlencoded body.
    const std::vector<Param>& formParams() const;
    std::optional<std::string> formValue(std::string_view name) const;

    std::optional<std::string> cookie(std::string_view name) const;

    bool isMethod(HttpMethod expected) const { return method == expected; }

    void clear();

private:
    static void parseUrlEncoded(std::string_view text, std::vector<Param>& out);

    mutable std::vector<Param> m_queryParams;
    mutable bool m_queryParsed = false;
    mutable std::vector<Param> m_formParams;
    mutable bool m_formParsed = false;
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPREQUEST_H
