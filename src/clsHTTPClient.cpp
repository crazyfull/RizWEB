#include "clsHTTPClient.h"
#include "HTTP/clsHTTPCookie.h"
#include "HTTP/clsHTTPSecurity.h"
#include "HTTP/clsHTTPStaticFiles.h"
#include "Utils/clsCrypto.h"
#include "Utils/clsRizLog.h"
#include "Utils/clsStringUtils.h"
#include "clsWEBMaster.h"
#include <algorithm>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rizweb {

static constexpr uint64_t kStreamChunk = 256 * 1024;
static constexpr size_t kMaxPendingInput = 64 * 1024;

struct HTTPClient::FileStream
{
    uint64_t offset = 0;
    uint64_t remaining = 0;
    bool keepAlive = true;
    int fd = -1;                    // netRector: sendfile(2)
    std::ifstream file;             // netRectorClient: read + send
    std::vector<char> buffer;
};

HTTPClient::HTTPClient(WEBMaster &master, bool isTls)
    : m_master(master), m_id(Crypto::randomNumber(1, UINT32_MAX)), m_tls(isTls), m_response(*this)
{
    setMaxMessageBytes(master.settings().maxWebSocketMessageBytes);
}

HTTPClient::~HTTPClient()
{
    sweepRetiredFiles(true);
    if (m_stream && m_stream->fd >= 0) {
#if !defined(_WIN32)
        ::close(m_stream->fd);
#endif
    }
}

void HTTPClient::setIdleTimeoutEnabled(bool enabled)
{
    setIdleTimeout(enabled ? m_master.settings().keepAliveSeconds : 0);
}

// ---------------------------------------------------------------------------------------------
// socket events
// ---------------------------------------------------------------------------------------------

void HTTPClient::onAccepted()
{
    uint16_t port = 0;
    getPeerAddress(m_ip, port);
    if (m_master.limiter().isEnabled()) {
        if (!m_master.limiter().acquireConnection(m_ip)) {
            close(true);                    // too many connections from this address
            return;
        }
        m_limited = true;
    }
    setIdleTimeout(m_master.settings().keepAliveSeconds);
    m_master.clients().add(this);
    m_registered = true;
}

void HTTPClient::onClose()
{
    if (m_limited) {
        m_master.limiter().releaseConnection(m_ip);
        m_limited = false;
    }
    if (m_registered) {
        m_master.clients().remove(this);
        m_registered = false;
    }
    if (m_webSocket) {
        if (m_endpoint && m_endpoint->onClose)
            m_endpoint->onClose(*this);
        else if (!m_endpoint)
            m_master.notifyWebSocketClosed(*this);
    } else
        m_master.notifyHttpClosed(*this);
    sweepRetiredFiles(true);
}

void HTTPClient::onResume()
{
    NetSocket::onResume();
    pumpStream();
    sweepRetiredFiles(false);
}

void HTTPClient::onReceiveData(const uint8_t *data, size_t length)
{
    const char *text = reinterpret_cast<const char *>(data);
    if (m_webSocket) {
        webSocketParse(text, length);
        return;
    }
    sweepRetiredFiles(false);
    if (m_busy) {                       // a file is streaming: keep pipelined requests for later
        if (m_input.size() + length > kMaxPendingInput) {
            close(true);
            return;
        }
        m_input.append(text, length);
        return;
    }
    if (m_input.empty()) {
        handleInput(text, length);
    } else {
        m_input.append(text, length);
        handleInput(m_input.data(), m_input.size());
    }
}

// ---------------------------------------------------------------------------------------------
// request loop (supports pipelining; every request is answered in order)
// ---------------------------------------------------------------------------------------------

void HTTPClient::handleInput(const char *data, size_t length)
{
    const char *position = data;
    size_t left = length;

    while (left > 0 && !m_busy && !m_webSocket && getStatus() == Connected) {
        m_request = HTTPRequest();
        HTTPParseResult result = m_request.parse(position, left, m_master.settings().maxBodyBytes, &m_master.bodyLimitFunction());
        if (result == HTTPParseResult::Incomplete)
            break;
        if (result != HTTPParseResult::Completed) {
            parseError(result);
            m_input.clear();
            return;
        }
        position += m_request.consumedBytes();
        left -= m_request.consumedBytes();

        m_response.begin(m_request);
        dispatch();

        if (!m_request.keepAlive() || !m_response.isCommitted())
            break;
    }

    // keep what was not processed (partial request, or requests waiting for a file stream)
    if (left > 0 && getStatus() == Connected && m_request.keepAlive()) {
        std::string rest(position, left);
        m_input.swap(rest);
    } else {
        m_input.clear();
    }
}

void HTTPClient::parseError(HTTPParseResult result)
{
    int status = 400;
    switch (result) {
    case HTTPParseResult::UriTooLong: status = 414; break;
    case HTTPParseResult::HeaderTooLarge: status = 431; break;
    case HTTPParseResult::BodyTooLarge: status = 413; break;
    case HTTPParseResult::NotImplemented: status = 501; break;
    case HTTPParseResult::VersionNotSupported: status = 505; break;
    default: break;
    }
    m_response.begin(HTTPRequest());
    m_response.forceClose();
    m_response.sendError(status);
}

void HTTPClient::dispatch()
{
    const std::string &path = m_request.urlPath();
    const HTTPSettings &settings = m_master.settings();
    HTTPSecurity &security = m_master.security();

    // 1. Host filtering
    if (!settings.serverHostName.empty() && m_request.host() != StringUtils::toLower(settings.serverHostName)) {
        m_response.siteNotFound();
        return;
    }

    // 2. per-IP request rate
    if (m_limited || m_master.limiter().isEnabled()) {
        uint32_t retryAfter = 1;
        if (!m_master.limiter().allowRequest(m_ip, retryAfter)) {
            m_response.setHeaderField("Retry-After", std::to_string(retryAfter));
            m_response.sendError(429, "Too many requests.");
            return;
        }
    }

    // 3. CORS (answers preflight requests itself)
    if (m_master.cors().isEnabled() && m_master.cors().apply(m_request, m_response))
        return;

    // 4. built-in areas and login protection
    std::string root = settings.locatedRoot;
    std::string relative = path;
    bool staticOnly = false;

    if (StringUtils::startsWith(path, "/resource/") || path == "/resource") {
        root = settings.resourceRoot;
        relative = path.size() > 9 ? path.substr(9) : "/";
        staticOnly = true;
    } else if (path == "/auth") {
        m_response.redirect("/auth/", 301);
        return;
    } else if (StringUtils::startsWith(path, "/auth/")) {
        root = settings.authRoot;
        relative = path.substr(5);
        staticOnly = true;
        if (m_request.method() == HTTPMethod::Post) {
            handleLogin();
            return;
        }
        // login form nonce: the POST must come from a page we served recently
        std::string nonce = security.loginNonces().create();
        CookieOptions options;
        options.maxAgeSeconds = 120;
        options.path = "/auth/";
        options.secure = m_tls;
        m_response.setCookie(HTTPSecurity::kNonceCookie, nonce, options);
    } else if (security.isAuthenticationEnabled() && security.isSecurePath(path)) {
        if (!security.isSessionValid(m_request.headerField("cookie"))) {
            if (m_request.headerField("upgrade"))
                m_response.authenticationFailed();
            else
                m_response.redirect("/auth/", 302);
            return;
        }
    }

    // 5. middleware chain
    for (const WEBMaster::Middleware &middleware : m_master.middleware()) {
        if (middleware(*this, m_request, m_response) || m_response.isCommitted())
            return;
    }

    // 6. routes
    if (!staticOnly && !m_master.router().isEmpty()) {
        const HTTPRouter::Route *route = nullptr;
        std::vector<std::pair<std::string, std::string>> params;
        std::string allow;
        switch (m_master.router().match(m_request.method(), path, route, params, allow)) {
        case HTTPRouter::Result::Found:
            if (route->webSocket) {
                handleWebSocketUpgrade(route->webSocket);
                return;
            }
            m_request.setParams(std::move(params));
            route->handler(*this, m_request, m_response);
            if (!m_response.isCommitted())
                m_response.internalServerError("The handler did not produce a response.");
            return;
        case HTTPRouter::Result::MethodNotAllowed:
            // a static file may still live at this path; only refuse when there is none
            if (m_request.method() != HTTPMethod::Get && m_request.method() != HTTPMethod::Head) {
                m_response.methodNotAllowed(allow);
                return;
            }
            break;
        case HTTPRouter::Result::NotFound:
            break;
        }
    }

    // 7. legacy single WebSocket path and catch-all handler
    if (path == settings.webSocketPath && !staticOnly) {
        handleWebSocketUpgrade(nullptr);
        return;
    }
    if (m_master.notifyHttpRequest(*this, m_request, m_response) || m_response.isCommitted())
        return;

    // 8. files
    m_master.staticFiles().serve(*this, m_request, m_response, root, relative);
}

void HTTPClient::handleLogin()
{
    HTTPSecurity &security = m_master.security();
    std::string nonce = HTTPCookie::value(m_request.headerField("cookie"), HTTPSecurity::kNonceCookie);
    if (!security.loginNonces().consume(nonce)) {
        m_response.authenticationFailed("timeout");
        return;
    }
    if (!m_master.notifyAuthentication(*this, m_request))
        authenticationResponse(false, "Unmanaged authentication");
}

void HTTPClient::authenticationResponse(bool successful, const std::string &message)
{
    if (m_response.isCommitted())
        return;
    if (!successful) {
        m_response.authenticationFailed(message);
        return;
    }
    std::string token = m_master.security().sessions().create();
    if (token.empty()) {
        m_response.sendError(503, "Too many sessions, try again later.");
        return;
    }
    CookieOptions options;
    options.maxAgeSeconds = static_cast<int>(m_master.security().sessions().lifetime());
    options.secure = m_tls;
    m_response.setCookie(HTTPSecurity::kSessionCookie, token, options);
    m_response.setContentType("text/plain; charset=utf-8");
    m_response.write(message.empty() ? "success" : message);
    m_response.flush(200);
}

void HTTPClient::handleWebSocketUpgrade(const std::shared_ptr<const WebSocketEndpoint> &endpoint)
{
    const std::string *upgrade = m_request.headerField("upgrade");
    const std::string *connection = m_request.headerField("connection");
    const std::string *key = m_request.headerField("sec-websocket-key");
    const std::string *version = m_request.headerField("sec-websocket-version");
    bool isUpgrade = upgrade && StringUtils::containsIgnoreCase(*upgrade, "websocket") && connection && StringUtils::containsIgnoreCase(*connection, "upgrade");

    if (!isUpgrade) {
        m_response.sendError(426, "This endpoint speaks WebSocket.");
        return;
    }
    if (m_request.method() != HTTPMethod::Get || !key || key->empty() || !version || *version != "13") {
        m_response.setHeaderField("Sec-WebSocket-Version", "13");
        m_response.badRequest("Invalid WebSocket handshake.");
        return;
    }

    // Cross-site WebSocket hijacking guard: a browser always sends Origin; it must name this host.
    if (m_master.settings().webSocketSameOriginOnly) {
        if (const std::string *origin = m_request.headerField("origin")) {
            std::string_view text = *origin;
            size_t scheme = text.find("://");
            if (scheme != std::string_view::npos)
                text.remove_prefix(scheme + 3);
            std::string originHost = text.empty() || text[0] != '[' ? StringUtils::toLower(text.substr(0, text.find(':'))) : StringUtils::toLower(text.substr(0, text.find(']') + 1));
            if (originHost != m_request.host()) {
                m_response.accessDenied();
                return;
            }
        }
    }

    m_response.switchProtocol(*key);
    m_webSocket = true;
    m_endpoint = endpoint;
    setIdleTimeout(0);              // the application decides about pings / timeouts
    if (!endpoint)
        m_master.notifyWebSocketConnected(*this);
    else if (endpoint->onOpen)
        endpoint->onOpen(*this);
}

void HTTPClient::webSocketMessage(bool isBinary, const char *payload, size_t length)
{
    if (!m_endpoint)
        m_master.notifyWebSocketMessage(*this, isBinary, payload, length);
    else if (m_endpoint->onMessage)
        m_endpoint->onMessage(*this, isBinary, payload, length);
}

void HTTPClient::webSocketSendRaw(const std::string &bytes)
{
    sendBuffer(bytes);
}

// ---------------------------------------------------------------------------------------------
// sending
// ---------------------------------------------------------------------------------------------

void HTTPClient::sendBuffer(const std::string &data)
{
    if (!data.empty() && !send(data.data(), data.size()))
        RIZLOG_DEBUG("client %u: send failed (connection closing or queue full)", m_id);
}

void HTTPClient::releaseShared(void *, void *argument)
{
    delete static_cast<std::shared_ptr<const std::string> *>(argument);
}

void HTTPClient::sendShared(std::shared_ptr<const std::string> body)
{
    const std::string *raw = body.get();
    auto *holder = new std::shared_ptr<const std::string>(std::move(body));
    sendNoCopy(raw->data(), raw->size(), &HTTPClient::releaseShared, holder);
}

void HTTPClient::onResponseSent(bool keepAlive)
{
    if (!keepAlive)
        close();                        // graceful: the send queue is flushed first
}

bool HTTPClient::streamFile(const std::string &path, uint64_t offset, uint64_t length, bool keepAlive)
{
    std::unique_ptr<FileStream> stream(new FileStream);
    stream->offset = offset;
    stream->remaining = length;
    stream->keepAlive = keepAlive;
#if defined(RIZWEB_USE_NETRECTOR)
    stream->fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (stream->fd < 0)
        return false;
#else
    stream->file.open(path, std::ios::binary);
    if (!stream->file)
        return false;
    stream->file.seekg(static_cast<std::streamoff>(offset));
    stream->buffer.resize(64 * 1024);
#endif
    m_stream = std::move(stream);
    m_busy = true;
    pumpStream();
    return true;
}

// Sends the file in chunks and stops when the socket reports back-pressure; onResume() continues.
void HTTPClient::pumpStream()
{
    while (m_stream && m_stream->remaining > 0) {
        if (getStatus() != Connected) {
            finishStream();
            return;
        }
        if (isWriteBlocked())
            return;
        uint64_t chunk = std::min<uint64_t>(m_stream->remaining, kStreamChunk);
#if defined(RIZWEB_USE_NETRECTOR)
        if (!sendFile(m_stream->fd, static_cast<off_t>(m_stream->offset), static_cast<size_t>(chunk))) {
            close(true);
            finishStream();
            return;
        }
#else
        chunk = std::min<uint64_t>(chunk, m_stream->buffer.size());
        m_stream->file.read(m_stream->buffer.data(), static_cast<std::streamsize>(chunk));
        std::streamsize got = m_stream->file.gcount();
        if (got <= 0 || !send(m_stream->buffer.data(), static_cast<size_t>(got))) {
            close(true);
            finishStream();
            return;
        }
        chunk = static_cast<uint64_t>(got);
#endif
        m_stream->offset += chunk;
        m_stream->remaining -= chunk;
    }
    if (m_stream)
        finishStream();
}

void HTTPClient::finishStream()
{
    bool keepAlive = m_stream->keepAlive;
#if !defined(_WIN32)
    if (m_stream->fd >= 0) {
        // the send queue may still reference this fd (sendfile): close it once the queue is empty
        m_retiredFds.push_back(m_stream->fd);
        m_stream->fd = -1;
    }
#endif
    m_stream.reset();
    m_busy = false;
    onResponseSent(keepAlive);

    // requests that arrived while the file was streaming
    if (!m_input.empty() && getStatus() == Connected) {
        std::string pending;
        pending.swap(m_input);
        handleInput(pending.data(), pending.size());
    }
}

void HTTPClient::sweepRetiredFiles(bool force)
{
#if !defined(_WIN32)
    if (m_retiredFds.empty() || (!force && pendingBytes() > 0))
        return;
    for (int fd : m_retiredFds)
        ::close(fd);
    m_retiredFds.clear();
#else
    (void)force;
#endif
}

} // namespace rizweb
