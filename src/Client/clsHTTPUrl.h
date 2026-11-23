#ifndef CLSHTTPURL_H
#define CLSHTTPURL_H
#include <cstdint>
#include <string>

namespace rizweb {

// http / https URL split into the parts a connection needs.
struct HTTPUrl
{
    bool https = false;
    std::string host;               // without brackets for IPv6 literals
    uint16_t port = 80;
    std::string target = "/";       // path + query, always starts with '/'

    static bool parse(const std::string &text, HTTPUrl &url, std::string &error);
    // Resolves a Location header (absolute, "//host/..", "/abs", or relative) against this URL.
    bool resolve(const std::string &location, HTTPUrl &result, std::string &error) const;
    std::string toString() const;
    std::string hostHeader() const;     // host[:port], port omitted when default
    bool sameOrigin(const HTTPUrl &other) const { return https == other.https && host == other.host && port == other.port; }
};

} // namespace rizweb

#endif // CLSHTTPURL_H
