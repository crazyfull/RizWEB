#include "clsHttpConnection.h"

#include "clsWebServer.h"
#include "../http/clsHttpStatus.h"
#include "../util/clsLogger.h"
#include "../util/stringUtil.h"
#include "../websocket/clsWebSocketHandshake.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <sys/uio.h>
#include <unistd.h>

namespace rizweb {

namespace {

// Bodies up to this size are joined with the head and sent with one send().
constexpr size_t kInlineBodyLimit = 16 * 1024;

// Files up to this size are read into memory (one syscall, one send). Bigger files use sendfile.
constexpr size_t kSmallFileLimit = 64 * 1024;

// Last bytes of a big file are sent from memory behind the sendfile part. netRector releases
// that buffer only after every earlier byte of the queue went out, which tells us when the
// descriptor may be closed (TCPSocket::sendFile has no completion callback of its own).
constexpr size_t kFileTailSize = 1024;

struct FileTail
{
    int fd;
    char bytes[kFileTailSize];
};

ParseStatus feedParser(HttpRequestParser& parser, const char* data, size_t length, size_t& used)
{
    return parser.feed(data, length, used);
}

} // namespace

HttpConnection::HttpConnection(WebServer& server)
    : m_server(server),
      m_parser([&server] {
          HttpParserLimits limits;
          limits.maxHeaderBytes = server.config().maxHeaderBytes;
          limits.maxBodyBytes = server.config().maxBodyBytes;
          return limits;
      }()),
      m_frames(1024 * 1024, true)
{
    m_server.connectionOpened();
    if (m_server.config().maxSendQueueBytes > 0)
        setMaxSendQueue(m_server.config().maxSendQueueBytes);
}

HttpConnection::~HttpConnection()
{
    m_server.connectionClosed();
}

void HttpConnection::onAccepted()
{
    getPeerAddress(m_remoteAddress, m_remotePort);
    // Idle limit of a connection that waits for its next request (also covers a client that never sends).
    setIdleTimeout(static_cast<uint32_t>(std::max(0, m_server.config().keepAliveSeconds)));
}

void HttpConnection::onReceiveData(const uint8_t* data, size_t length)
{
    if (m_mode == Mode::webSocket) {
        processWebSocket(data, length);
        return;
    }
    if (m_mode == Mode::closing)
        return;

    if (m_inbox.empty()) {
        processHttp(reinterpret_cast<const char*>(data), length);
        return;
    }

    m_inbox.append(reinterpret_cast<const char*>(data), length);
    std::string pending;
    pending.swap(m_inbox);
    processHttp(pending.data(), pending.size());
}

void HttpConnection::onResume()
{
    TCPSocket::onResume();   // default: resume reading

    if (m_mode == Mode::http && !m_inbox.empty() && getStatus() == Connected) {
        std::string pending;
        pending.swap(m_inbox);
        processHttp(pending.data(), pending.size());
    }
}

void HttpConnection::processHttp(const char* data, size_t length)
{
    const ServerConfig& config = m_server.config();

    while (length > 0 && m_mode == Mode::http) {
        // A client that does not read our answers must not make us pile up work:
        // keep the rest of its pipeline until the send queue drains (onResume).
        if (isWriteBlocked()) {
            m_inbox.assign(data, length);
            return;
        }

        if (!m_parser.hasStarted()) {
            m_requestStart = std::chrono::steady_clock::now();
        } else if (config.requestTimeoutSeconds > 0
                   && std::chrono::steady_clock::now() - m_requestStart > std::chrono::seconds(config.requestTimeoutSeconds)) {
            rejectRequest(408, "The request was not completed in time.");
            return;
        }

        size_t used = 0;
        ParseStatus parseStatus = feedParser(m_parser, data, length, used);
        data += used;
        length -= used;

        if (parseStatus == ParseStatus::needMore) {
            if (m_parser.isHeadComplete() && m_parser.expectsContinue() && !m_continueSent) {
                m_continueSent = true;
                static const char interim[] = "HTTP/1.1 100 Continue\r\n\r\n";
                send(interim, sizeof(interim) - 1);
            }
            return;
        }

        if (parseStatus == ParseStatus::error) {
            rejectRequest(m_parser.errorStatus(), std::string_view());
            return;
        }

        handleRequest();
        m_parser.reset();
        m_continueSent = false;

        // bytes that followed a successful upgrade request belong to the WebSocket
        if (m_mode == Mode::webSocket && length > 0) {
            processWebSocket(reinterpret_cast<const uint8_t*>(data), length);
            return;
        }
    }
}

void HttpConnection::rejectRequest(int statusCode, std::string_view message)
{
    m_mode = Mode::closing;
    HttpResponse response;
    response.sendError(statusCode, message);
    sendResponse(response, false);
}

void HttpConnection::handleRequest()
{
    HttpRequest& request = m_parser.request();
    request.isSecure = isTLS();
    request.remoteAddress = m_remoteAddress;
    request.remotePort = m_remotePort;
    m_requestCount++;

    bool keepAlive = request.keepAlive && !m_server.isStopping()
                     && m_requestCount < m_server.config().maxRequestsPerConnection;

    if (const WebSocketRoute* route = m_server.findWebSocketRoute(request.path)) {
        handleWebSocketRequest(*route);
        return;
    }

    HttpResponse response;
    m_server.dispatch(request, response);
    sendResponse(response, keepAlive);
}

void HttpConnection::sendResponse(HttpResponse& response, bool keepAlive)
{
    const HttpRequest& request = m_parser.request();
    const int statusCode = response.status();
    const bool bodyAllowed = HttpStatus::allowsBody(statusCode);
    const bool headOnly = request.method == HttpMethod::head;

    HttpResponse::HeadOptions options;
    options.versionMinor = request.versionMinor;
    options.keepAlive = keepAlive;
    options.includeContentLength = bodyAllowed;
    options.serverName = m_server.config().serverName;

    if (!bodyAllowed)
        response.releaseFile();   // 204 / 304 never carry a body

    const size_t bodyLength = bodyAllowed ? response.bodyLength() : 0;
    std::string head = response.serializeHead(options);
    bool ok = true;

    if (headOnly || bodyLength == 0) {
        ok = send(head.data(), head.size());
    } else if (response.hasFileBody()) {
        off_t offset = response.fileOffset();
        ok = sendFileBody(head, response.releaseFile(), offset, bodyLength);
    } else if (response.sharedBody()) {
        auto* holder = new std::shared_ptr<const std::string>(response.sharedBody());
        ok = send(head.data(), head.size());
        if (ok)
            ok = sendNoCopy((*holder)->data(), (*holder)->size(), &HttpConnection::releaseShared, holder);
        else
            delete holder;
    } else if (bodyLength <= kInlineBodyLimit) {
        head += response.body();
        ok = send(head.data(), head.size());
    } else {
        auto* holder = new std::string(response.takeBody());
        ok = send(head.data(), head.size());
        if (ok)
            ok = sendNoCopy(holder->data(), holder->size(), &HttpConnection::releaseString, holder);
        else
            delete holder;
    }

    if (!ok) {
        abortConnection();
        return;
    }

    if (!keepAlive) {
        m_mode = Mode::closing;
        close(false);   // flushes the queue first, then FIN
    }
}

void HttpConnection::abortConnection()
{
    m_mode = Mode::closing;
    close(true);
}

void HttpConnection::releaseString(void*, void* argument)
{
    delete static_cast<std::string*>(argument);
}

void HttpConnection::releaseShared(void*, void* argument)
{
    delete static_cast<std::shared_ptr<const std::string>*>(argument);
}

void HttpConnection::releaseFileTail(void*, void* argument)
{
    FileTail* tail = static_cast<FileTail*>(argument);
    ::close(tail->fd);
    delete tail;
}

bool HttpConnection::sendFileBody(std::string& head, UniqueFd file, off_t offset, size_t length)
{
    if (length <= kSmallFileLimit) {
        const size_t headSize = head.size();
        head.resize(headSize + length);

        size_t done = 0;
        while (done < length) {
            ssize_t n = ::pread(file.get(), &head[headSize + done], length - done, offset + static_cast<off_t>(done));
            if (n <= 0)
                return false;
            done += static_cast<size_t>(n);
        }
        return send(head.data(), head.size());
    }

    if (!send(head.data(), head.size()))
        return false;

    std::unique_ptr<FileTail> tail(new FileTail);
    tail->fd = file.get();

    size_t have = 0;
    const off_t tailOffset = offset + static_cast<off_t>(length - kFileTailSize);
    while (have < kFileTailSize) {
        ssize_t n = ::pread(file.get(), tail->bytes + have, kFileTailSize - have, tailOffset + static_cast<off_t>(have));
        if (n <= 0)
            return false;   // file shrank: unique_ptr and UniqueFd clean up
        have += static_cast<size_t>(n);
    }

    if (!sendFile(file.get(), offset, length - kFileTailSize))
        return false;       // descriptor is still ours and closes with `file`

    // From here the tail buffer owns the descriptor. sendNoCopy() runs the release callback itself
    // when it fails, so nothing is left to free on either outcome.
    FileTail* owned = tail.release();
    file.release();
    return sendNoCopy(owned->bytes, kFileTailSize, &HttpConnection::releaseFileTail, owned);
}

// ----------------------------------------------------------------------------
// WebSocket
// ----------------------------------------------------------------------------

namespace {

bool originAllowed(const HttpRequest& request, const WebSocketHandlers& handlers)
{
    const std::string* origin = request.header("Origin");
    if (!origin || handlers.allowAnyOrigin)
        return true;

    for (const std::string& allowed : handlers.allowedOrigins) {
        if (stringUtil::equalsIgnoreCase(*origin, allowed))
            return true;
    }

    // same site: the authority of the Origin must equal the Host header
    std::string_view text = *origin;
    size_t scheme = text.find("://");
    if (scheme == std::string_view::npos)
        return false;
    std::string_view authority = text.substr(scheme + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));

    const std::string* host = request.header("Host");
    return host && stringUtil::equalsIgnoreCase(authority, *host);
}

} // namespace

void HttpConnection::handleWebSocketRequest(const WebSocketRoute& route)
{
    HttpRequest& request = m_parser.request();

    auto refuse = [this](int statusCode, std::string_view message, bool offerVersion) {
        HttpResponse response;
        response.sendError(statusCode, message);
        if (offerVersion)
            response.setHeader("Sec-WebSocket-Version", "13");
        m_mode = Mode::closing;
        sendResponse(response, false);
    };

    if (request.method != HttpMethod::get || request.versionMinor < 1) {
        refuse(400, "WebSocket needs GET and HTTP/1.1.", false);
        return;
    }
    if (!request.headers.hasToken("Upgrade", "websocket") || !request.headers.hasToken("Connection", "upgrade")) {
        refuse(426, "This endpoint speaks WebSocket.", true);
        return;
    }

    const std::string* version = request.header("Sec-WebSocket-Version");
    if (!version || stringUtil::trim(*version) != "13") {
        refuse(426, "Unsupported WebSocket version.", true);
        return;
    }

    const std::string* key = request.header("Sec-WebSocket-Key");
    if (!key || !WebSocketHandshake::isValidClientKey(stringUtil::trim(*key))) {
        refuse(400, "Invalid Sec-WebSocket-Key.", false);
        return;
    }

    if (!originAllowed(request, route.handlers)) {
        refuse(403, "Origin not allowed.", false);
        return;
    }

    if (route.handlers.authorize) {
        bool allowed = false;
        try {
            allowed = route.handlers.authorize(request);
        } catch (const std::exception& error) {
            RIZWEB_LOG_ERROR("websocket authorize handler threw: %s", error.what());
        }
        if (!allowed) {
            refuse(403, "Not authorized.", false);
            return;
        }
    }

    HttpResponse response;
    response.setStatus(101);
    response.setHeader("Upgrade", "websocket");
    response.setHeader("Connection", "Upgrade");
    response.setHeader("Sec-WebSocket-Accept", WebSocketHandshake::acceptKey(stringUtil::trim(*key)));

    m_upgradeRequest = std::move(request);   // the parser is reset after this call
    sendResponse(response, true);
    if (getStatus() != Connected)
        return;

    m_route = &route;
    m_mode = Mode::webSocket;
    m_frames = WebSocketFrameParser(route.handlers.maxMessageBytes, true);
    setIdleTimeout(static_cast<uint32_t>(std::max(0, m_server.config().webSocketIdleSeconds)));
    m_opened = true;

    if (route.handlers.onOpen) {
        try {
            route.handlers.onOpen(*this);
        } catch (const std::exception& error) {
            RIZWEB_LOG_ERROR("websocket onOpen handler threw: %s", error.what());
            disconnect(1011, "internal error");
        }
    }
}

bool HttpConnection::sendFrame(WsOpcode opcode, std::string_view payload)
{
    if (!isOpen())
        return false;

    std::string header = WebSocketFrameBuilder::header(opcode, payload.size());
    if (payload.empty())
        return send(header.data(), header.size());

    struct iovec parts[2];
    parts[0].iov_base = &header[0];
    parts[0].iov_len = header.size();
    parts[1].iov_base = const_cast<char*>(payload.data());
    parts[1].iov_len = payload.size();
    return sendv(parts, 2);
}

bool HttpConnection::sendText(std::string_view text)
{
    return sendFrame(WsOpcode::text, text);
}

bool HttpConnection::sendBinary(std::string_view data)
{
    return sendFrame(WsOpcode::binary, data);
}

bool HttpConnection::sendPing(std::string_view data)
{
    return sendFrame(WsOpcode::ping, data.substr(0, 125));
}

void HttpConnection::disconnect(uint16_t code, std::string_view reason)
{
    if (m_mode != Mode::webSocket || m_closeSent)
        return;

    m_closeSent = true;
    std::string frame = WebSocketFrameBuilder::buildClose(code, reason);
    send(frame.data(), frame.size());
    m_mode = Mode::closing;
    close(false);
}

const HttpRequest& HttpConnection::request() const
{
    return m_opened ? m_upgradeRequest : m_parser.request();
}

bool HttpConnection::isOpen() const
{
    return m_mode == Mode::webSocket && !m_closeSent && getStatus() == Connected;
}

void HttpConnection::processWebSocket(const uint8_t* data, size_t length)
{
    while (length > 0 && m_mode == Mode::webSocket) {
        size_t used = 0;
        WsParseStatus parseStatus = m_frames.feed(data, length, used);
        data += used;
        length -= used;

        if (parseStatus == WsParseStatus::needMore)
            return;

        if (parseStatus == WsParseStatus::error) {
            disconnect(m_frames.errorCode());
            return;
        }

        WsMessage& message = m_frames.message();
        switch (message.opcode) {
        case WsOpcode::text:
        case WsOpcode::binary:
            if (m_route && m_route->handlers.onMessage) {
                try {
                    m_route->handlers.onMessage(*this, message.opcode, message.payload);
                } catch (const std::exception& error) {
                    RIZWEB_LOG_ERROR("websocket onMessage handler threw: %s", error.what());
                    disconnect(1011, "internal error");
                }
            }
            break;

        case WsOpcode::ping:
            sendFrame(WsOpcode::pong, message.payload);
            break;

        case WsOpcode::close: {
            if (message.payload.size() >= 2) {
                m_peerCloseCode = static_cast<uint16_t>((static_cast<uint8_t>(message.payload[0]) << 8)
                                                        | static_cast<uint8_t>(message.payload[1]));
            } else {
                m_peerCloseCode = 1005;
            }
            if (!m_closeSent) {
                m_closeSent = true;
                std::string reply = message.payload.size() >= 2
                                        ? WebSocketFrameBuilder::buildClose(m_peerCloseCode)
                                        : WebSocketFrameBuilder::build(WsOpcode::close, std::string_view());
                send(reply.data(), reply.size());
            }
            m_mode = Mode::closing;
            close(false);
            return;
        }

        default:
            break;
        }
    }
}

void HttpConnection::onClose()
{
    if (m_opened && !m_closeNotified && m_route && m_route->handlers.onClose) {
        m_closeNotified = true;
        try {
            m_route->handlers.onClose(*this, m_peerCloseCode);
        } catch (const std::exception& error) {
            RIZWEB_LOG_ERROR("websocket onClose handler threw: %s", error.what());
        }
    }
}

} // namespace rizweb
