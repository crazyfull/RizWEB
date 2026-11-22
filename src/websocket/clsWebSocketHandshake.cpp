#include "clsWebSocketHandshake.h"

#include "../util/clsBase64.h"
#include "../util/clsSha1.h"

namespace rizweb {

std::string WebSocketHandshake::acceptKey(std::string_view clientKey)
{
    std::string joined(clientKey);
    joined += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::array<uint8_t, 20> digest = Sha1::hash(joined);
    return Base64::encode(digest.data(), digest.size());
}

bool WebSocketHandshake::isValidClientKey(std::string_view clientKey)
{
    std::string decoded;
    return clientKey.size() == 24 && Base64::decode(clientKey, decoded) && decoded.size() == 16;
}

} // namespace rizweb
