#include "clsHTTPAsyncResponse.h"
#include "../clsHTTPClient.h"
#include "../clsHTTPClientList.h"
#include <string>

namespace rizweb {

bool AsyncResponse::respond(Writer writer)
{
    const uint64_t token = m_token;
    return m_clients.post(m_clientId, [token, writer = std::move(writer)](HTTPClient &client) {
        client.completeDeferred(token, writer);
    });
}

bool AsyncResponse::send(int statusCode, std::string_view contentType, std::string_view body)
{
    std::string type(contentType);
    std::string data(body);
    return respond([statusCode, type, data](HTTPResponse &response) {
        response.setContentType(type);
        response.write(data);
        response.flush(statusCode);
    });
}

bool AsyncResponse::sendError(int statusCode, std::string_view detail)
{
    std::string text(detail);
    return respond([statusCode, text](HTTPResponse &response) { response.sendError(statusCode, text); });
}

} // namespace rizweb
