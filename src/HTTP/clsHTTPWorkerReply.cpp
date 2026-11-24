#include "clsHTTPWorkerReply.h"
#include "../Utils/clsEscape.h"
#include "clsHTTPResponse.h"
#include <algorithm>
#include <cctype>

namespace rizweb {

void WorkerReply::setHeaderField(std::string_view name, std::string_view value)
{
    for (auto &field : m_headers) {
        if (field.first.size() == name.size() && std::equal(name.begin(), name.end(), field.first.begin(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
            field.second.assign(value);
            return;
        }
    }
    m_headers.emplace_back(std::string(name), std::string(value));
}

void WorkerReply::writeEscaped(std::string_view untrusted)
{
    m_body.append(Escape::html(untrusted));
}

void WorkerReply::flush(int statusCode)
{
    if (m_sent)
        return;
    m_sent = true;
    struct Payload
    {
        std::vector<std::pair<std::string, std::string>> headers;
        ResponseBuffer body;
    };
    auto payload = std::make_shared<Payload>();
    payload->headers.swap(m_headers);
    payload->body.append(m_body.view());
    m_body.release();
    m_async->respond([payload, statusCode](HTTPResponse &response) {
        for (const auto &field : payload->headers)
            response.setHeaderField(field.first, field.second);
        response.write(payload->body.view());
        response.flush(statusCode);
    });
}

void WorkerReply::redirect(std::string_view url, int statusCode)
{
    if (m_sent)
        return;
    m_sent = true;
    std::string target(url);
    m_async->respond([target, statusCode](HTTPResponse &response) { response.redirect(target, statusCode); });
}

void WorkerReply::sendError(int statusCode, std::string_view detail)
{
    if (m_sent)
        return;
    m_sent = true;
    m_async->sendError(statusCode, detail);
}

} // namespace rizweb
