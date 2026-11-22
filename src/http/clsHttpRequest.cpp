#include "clsHttpRequest.h"

#include "../util/clsUrlCodec.h"
#include "../util/stringUtil.h"

namespace rizweb {

std::string HttpRequest::host() const
{
    const std::string* value = headers.get("Host");
    if (!value)
        return std::string();

    std::string_view host = stringUtil::trim(*value);
    if (!host.empty() && host[0] == '[') {
        size_t close = host.find(']');
        return std::string(host.substr(0, close == std::string_view::npos ? host.size() : close + 1));
    }

    size_t colon = host.find(':');
    return std::string(host.substr(0, colon));
}

void HttpRequest::parseUrlEncoded(std::string_view text, std::vector<Param>& out)
{
    size_t pos = 0;
    while (pos < text.size()) {
        size_t amp = text.find('&', pos);
        if (amp == std::string_view::npos)
            amp = text.size();

        std::string_view pair = text.substr(pos, amp - pos);
        pos = amp + 1;
        if (pair.empty())
            continue;

        size_t equals = pair.find('=');
        std::string_view rawName = pair.substr(0, equals);
        std::string_view rawValue = (equals == std::string_view::npos) ? std::string_view() : pair.substr(equals + 1);

        std::string name;
        std::string value;
        if (UrlCodec::decode(rawName, name, true) && UrlCodec::decode(rawValue, value, true))
            out.emplace_back(std::move(name), std::move(value));
    }
}

const std::vector<HttpRequest::Param>& HttpRequest::queryParams() const
{
    if (!m_queryParsed) {
        parseUrlEncoded(rawQuery, m_queryParams);
        m_queryParsed = true;
    }
    return m_queryParams;
}

std::optional<std::string> HttpRequest::queryValue(std::string_view name) const
{
    for (const Param& param : queryParams()) {
        if (param.first == name)
            return param.second;
    }
    return std::nullopt;
}

const std::vector<HttpRequest::Param>& HttpRequest::formParams() const
{
    if (!m_formParsed) {
        const std::string* type = headers.get("Content-Type");
        if (type && stringUtil::startsWith(stringUtil::toLower(*type), "application/x-www-form-urlencoded"))
            parseUrlEncoded(body, m_formParams);
        m_formParsed = true;
    }
    return m_formParams;
}

std::optional<std::string> HttpRequest::formValue(std::string_view name) const
{
    for (const Param& param : formParams()) {
        if (param.first == name)
            return param.second;
    }
    return std::nullopt;
}

std::optional<std::string> HttpRequest::cookie(std::string_view name) const
{
    for (const HttpHeaders::Entry& entry : headers.entries()) {
        if (!stringUtil::equalsIgnoreCase(entry.first, "Cookie"))
            continue;

        std::string_view text = entry.second;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t semicolon = text.find(';', pos);
            if (semicolon == std::string_view::npos)
                semicolon = text.size();

            std::string_view pair = stringUtil::trim(text.substr(pos, semicolon - pos));
            pos = semicolon + 1;

            size_t equals = pair.find('=');
            if (equals == std::string_view::npos)
                continue;

            if (pair.substr(0, equals) == name) {
                std::string_view value = pair.substr(equals + 1);
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                return std::string(value);
            }
        }
    }
    return std::nullopt;
}

void HttpRequest::clear()
{
    method = HttpMethod::unknown;
    methodName.clear();
    target.clear();
    path.clear();
    rawQuery.clear();
    versionMinor = 1;
    headers.clear();
    body.clear();
    keepAlive = true;
    isSecure = false;
    remoteAddress.clear();
    remotePort = 0;
    m_queryParams.clear();
    m_queryParsed = false;
    m_formParams.clear();
    m_formParsed = false;
}

} // namespace rizweb
