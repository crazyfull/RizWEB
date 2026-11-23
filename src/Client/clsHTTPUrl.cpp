#include "clsHTTPUrl.h"
#include "../Utils/clsStringUtils.h"
#include <vector>

namespace rizweb {

bool HTTPUrl::parse(const std::string &text, HTTPUrl &url, std::string &error)
{
    HTTPUrl result;
    std::string_view rest = text;
    if (StringUtils::startsWith(StringUtils::toLower(rest.substr(0, 8)), "https://")) {
        result.https = true;
        result.port = 443;
        rest.remove_prefix(8);
    } else if (StringUtils::startsWith(StringUtils::toLower(rest.substr(0, 7)), "http://")) {
        rest.remove_prefix(7);
    } else {
        error = "only http:// and https:// URLs are supported";
        return false;
    }
    for (char c : rest)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f || c == '\\') {
            error = "the URL contains spaces, control characters or backslashes";
            return false;
        }
    size_t hostEnd = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, hostEnd);
    std::string_view tail = hostEnd == std::string_view::npos ? std::string_view() : rest.substr(hostEnd);
    if (authority.find('@') != std::string_view::npos) {
        error = "user information in URLs is not supported (use an Authorization header)";
        return false;
    }
    std::string_view hostPart = authority, portPart;
    if (!authority.empty() && authority[0] == '[') {
        size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            error = "malformed IPv6 address";
            return false;
        }
        hostPart = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                error = "malformed host";
                return false;
            }
            portPart = authority.substr(close + 2);
        }
    } else {
        size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            hostPart = authority.substr(0, colon);
            portPart = authority.substr(colon + 1);
        }
    }
    if (hostPart.empty()) {
        error = "the URL has no host";
        return false;
    }
    if (!portPart.empty()) {
        unsigned long port = 0;
        for (char c : portPart) {
            if (c < '0' || c > '9' || portPart.size() > 5) {
                error = "invalid port";
                return false;
            }
            port = port * 10 + static_cast<unsigned>(c - '0');
        }
        if (port == 0 || port > 65535) {
            error = "invalid port";
            return false;
        }
        result.port = static_cast<uint16_t>(port);
    }
    result.host = StringUtils::toLower(hostPart);
    size_t fragment = tail.find('#');
    if (fragment != std::string_view::npos)
        tail = tail.substr(0, fragment);
    if (tail.empty())
        result.target = "/";
    else if (tail[0] == '?')
        result.target = "/" + std::string(tail);
    else
        result.target = std::string(tail);
    url = result;
    return true;
}

std::string HTTPUrl::hostHeader() const
{
    std::string name = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    bool defaultPort = (https && port == 443) || (!https && port == 80);
    return defaultPort ? name : name + ":" + std::to_string(port);
}

std::string HTTPUrl::toString() const
{
    return std::string(https ? "https://" : "http://") + hostHeader() + target;
}

static std::string normalizeDots(const std::string &path)
{
    std::vector<std::string> parts;
    size_t position = 0;
    while (position <= path.size()) {
        size_t slash = path.find('/', position);
        std::string part = path.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (part != "." && !part.empty()) {
            parts.push_back(part);
        }
        if (slash == std::string::npos)
            break;
        position = slash + 1;
    }
    std::string out;
    for (const std::string &part : parts)
        out += "/" + part;
    if (out.empty() || (!path.empty() && path.back() == '/'))
        out += "/";
    return out;
}

bool HTTPUrl::resolve(const std::string &location, HTTPUrl &result, std::string &error) const
{
    if (location.empty()) {
        error = "empty Location header";
        return false;
    }
    std::string lower = StringUtils::toLower(location.substr(0, 8));
    if (StringUtils::startsWith(lower, "http://") || StringUtils::startsWith(lower, "https://"))
        return parse(location, result, error);
    if (StringUtils::startsWith(location, "//"))
        return parse(std::string(https ? "https:" : "http:") + location, result, error);
    if (location.find("://") != std::string::npos || location.find(':') < location.find('/')) {
        error = "unsupported redirect target";
        return false;
    }
    result = *this;
    std::string_view rest = location;
    size_t fragment = rest.find('#');
    if (fragment != std::string_view::npos)
        rest = rest.substr(0, fragment);
    std::string path, query;
    size_t question = rest.find('?');
    std::string_view pathPart = rest.substr(0, question);
    if (question != std::string_view::npos)
        query = std::string(rest.substr(question));
    if (pathPart.empty()) {                          // "?x=1": keep the current path
        size_t currentQuery = target.find('?');
        path = target.substr(0, currentQuery);
    } else if (pathPart[0] == '/') {
        path = normalizeDots(std::string(pathPart));
    } else {
        std::string base = target.substr(0, target.find('?'));
        base = base.substr(0, base.rfind('/') + 1);
        path = normalizeDots(base + std::string(pathPart));
    }
    result.target = path + query;
    return true;
}

} // namespace rizweb
