#include "clsHTTPFetchConnection.h"
#include "../Utils/clsStringUtils.h"
#include "../clsVersion.h"
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace rizweb {

const std::string *HTTPFetchResponse::header(const std::string &name) const
{
    for (const auto &item : headers)
        if (StringUtils::equalsIgnoreCase(item.first, name))
            return &item.second;
    return nullptr;
}

std::vector<std::string> HTTPFetchResponse::headerValues(const std::string &name) const
{
    std::vector<std::string> values;
    for (const auto &item : headers)
        if (StringUtils::equalsIgnoreCase(item.first, name))
            values.push_back(item.second);
    return values;
}

NetTlsContext *HTTPFetchShared::tlsContext(bool verify, std::string &error)
{
#if defined(RIZWEB_HAS_TLS)
    std::lock_guard<std::mutex> lock(mutex);
    std::unique_ptr<NetTlsContext> &slot = verify ? verifying : insecure;
    if (!slot)
        slot.reset(NetTlsContext::createClient(verify, verify && !caFile.empty() ? caFile.c_str() : nullptr, &error));
    return slot.get();
#else
    (void)verify;
    error = "this build has no TLS support";
    return nullptr;
#endif
}

// Refuses loopback, private, link-local and similar addresses (checked for every resolved address, so a DNS
// answer that points inside the network is caught too).
#if defined(RIZWEB_USE_NETRECTOR)
static bool publicAddressOnly(void *, const struct sockaddr *address, socklen_t)
{
    if (address->sa_family == AF_INET) {
        uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in *>(address)->sin_addr.s_addr);
        return !((ip >> 24) == 0 || (ip >> 24) == 10 || (ip >> 24) == 127 || (ip >> 16) == 0xA9FE || (ip >> 20) == 0xAC1 ||
                 (ip >> 16) == 0xC0A8 || (ip >> 22) == 0x191 /*100.64/10*/ || (ip >> 28) == 0xE || (ip >> 28) == 0xF);
    }
    if (address->sa_family == AF_INET6) {
        const unsigned char *b = reinterpret_cast<const sockaddr_in6 *>(address)->sin6_addr.s6_addr;
        static const unsigned char zero[16] = {0};
        if (std::memcmp(b, zero, 16) == 0 || (std::memcmp(b, zero, 15) == 0 && b[15] == 1))
            return false;
        if ((b[0] & 0xFE) == 0xFC || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) || b[0] == 0xFF)
            return false;
        static const unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(b, mapped, 12) == 0) {            // ::ffff:a.b.c.d
            sockaddr_in v4{};
            v4.sin_family = AF_INET;
            std::memcpy(&v4.sin_addr, b + 12, 4);
            return publicAddressOnly(nullptr, reinterpret_cast<const sockaddr *>(&v4), sizeof(v4));
        }
        return true;
    }
    return false;
}

#endif

// ---------------------------------------------------------------------------------------------

HTTPFetchConnection::HTTPFetchConnection(const HTTPUrl &url, const HTTPFetchRequest &request, std::shared_ptr<HTTPFetchShared> shared, Done done)
    : m_url(url), m_requestCopy(request), m_shared(std::move(shared)), m_done(std::move(done))
{
    m_isHead = StringUtils::equalsIgnoreCase(request.method, "HEAD");
    m_response.finalUrl = url.toString();
}

void HTTPFetchConnection::launch(NetReactor *reactor, const HTTPUrl &url, const HTTPFetchRequest &request,
                                 std::shared_ptr<HTTPFetchShared> shared, Done done)
{
    HTTPFetchConnection *connection = new HTTPFetchConnection(url, request, std::move(shared), std::move(done));
    std::string error;
    if (!connection->begin(reactor, error)) {
        connection->fail(error, false);   // the object still belongs to us: connectTo() returned false
        delete connection;
    }
}

bool HTTPFetchConnection::begin(NetReactor *reactor, std::string &error)
{
    for (const auto &header : m_requestCopy.headers) {
        if (header.first.empty() || header.first.find_first_of("\r\n:\0 ") != std::string::npos || header.second.find_first_of("\r\n") != std::string::npos ||
            header.second.find('\0') != std::string::npos) {
            error = "invalid request header";
            return false;
        }
    }
    for (char c : m_requestCopy.method)
        if (!isalpha(static_cast<unsigned char>(c))) {
            error = "invalid method";
            return false;
        }
    setReactor(reactor);
    if (m_url.https) {
        NetTlsContext *context = m_shared->tlsContext(m_requestCopy.verifyTLS, error);
        if (!context) {
            if (error.empty())
                error = "cannot create the TLS client context";
            return false;
        }
        if (!enableTLS(context, false, m_url.host.c_str())) {
            error = "cannot enable TLS";
            return false;
        }
    }
    if (m_requestCopy.blockPrivateNetworks) {
#if defined(RIZWEB_USE_NETRECTOR)
        setConnectFilter(&publicAddressOnly, nullptr);
#else
        error = "blockPrivateNetworks is not supported by the netRectorClient backend (refusing instead of connecting unchecked)";
        return false;
#endif
    }
    setIdleTimeout(m_requestCopy.timeoutSeconds ? m_requestCopy.timeoutSeconds : 30);
    if (!connectTo(m_url.host.c_str(), m_url.port)) {
        error = "cannot start the connection";
        return false;
    }
    return true;          // from here on the reactor owns this object; the result comes through callbacks
}

std::string HTTPFetchConnection::buildRequest() const
{
    std::string text;
    text.reserve(256 + m_requestCopy.body.size());
    text += m_requestCopy.method + " " + m_url.target + " HTTP/1.1\r\nHost: " + m_url.hostHeader() + "\r\n";
    bool hasAgent = false, hasAccept = false;
    for (const auto &header : m_requestCopy.headers) {
        std::string name = StringUtils::toLower(header.first);
        if (name == "host" || name == "content-length" || name == "connection" || name == "transfer-encoding")
            continue;
        hasAgent = hasAgent || name == "user-agent";
        hasAccept = hasAccept || name == "accept";
        text += header.first + ": " + header.second + "\r\n";
    }
    if (!hasAgent)
        text += "User-Agent: " + (m_shared->userAgent.empty() ? std::string("RizWEB/") + RIZWEB_VERSION : m_shared->userAgent) + "\r\n";
    if (!hasAccept)
        text += "Accept: */*\r\n";
    if (!m_requestCopy.body.empty() || m_requestCopy.method == "POST" || m_requestCopy.method == "PUT" || m_requestCopy.method == "PATCH")
        text += "Content-Length: " + std::to_string(m_requestCopy.body.size()) + "\r\n";
    text += "Connection: close\r\n\r\n";
    text += m_requestCopy.body;
    return text;
}

void HTTPFetchConnection::onConnected()
{
    std::string request = buildRequest();
    if (!send(request.data(), request.size()))
        fail("cannot send the request");
}

void HTTPFetchConnection::onReceiveData(const uint8_t *data, size_t length)
{
    const char *text = reinterpret_cast<const char *>(data);
    while (length > 0 && m_stage != Stage::Finished) {
        if (m_stage == Stage::Body) {
            consume(text, length);
            return;
        }
        size_t before = m_head.size();
        m_head.append(text, length);
        size_t end = m_head.find("\r\n\r\n", before >= 3 ? before - 3 : 0);
        if (end == std::string::npos) {
            if (m_head.size() > 64 * 1024)
                fail("response headers too large");
            return;
        }
        std::string rest = m_head.substr(end + 4);
        m_head.resize(end);
        if (!parseHead(m_head))
            return;
        m_head.clear();
        if (m_stage == Stage::Head) {        // interim 1xx response: the real one follows
            m_response = HTTPFetchResponse();
            m_response.finalUrl = m_url.toString();
        }
        if (rest.empty())
            return;
        if (m_stage == Stage::Body) {
            std::string copy = std::move(rest);
            consume(copy.data(), copy.size());
            return;
        }
        std::string copy = std::move(rest);
        onReceiveData(reinterpret_cast<const uint8_t *>(copy.data()), copy.size());
        return;
    }
}

bool HTTPFetchConnection::parseHead(const std::string &head)
{
    size_t lineEnd = head.find("\r\n");
    std::string_view statusLine = std::string_view(head).substr(0, lineEnd);
    if (!StringUtils::startsWith(statusLine, "HTTP/1.") || statusLine.size() < 12 || statusLine[8] != ' ') {
        fail("malformed status line");
        return false;
    }
    int status = 0;
    for (size_t i = 9; i < 12; i++) {
        if (statusLine[i] < '0' || statusLine[i] > '9') {
            fail("malformed status line");
            return false;
        }
        status = status * 10 + (statusLine[i] - '0');
    }
    m_response.status = status;
    m_response.reason = statusLine.size() > 13 ? std::string(statusLine.substr(13)) : std::string();

    size_t position = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
    while (position < head.size()) {
        size_t end = head.find("\r\n", position);
        if (end == std::string::npos)
            end = head.size();
        std::string_view line = std::string_view(head).substr(position, end - position);
        position = end + 2;
        size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            fail("malformed response header");
            return false;
        }
        m_response.headers.emplace_back(std::string(line.substr(0, colon)), std::string(StringUtils::trim(line.substr(colon + 1))));
    }

    if (status >= 100 && status < 200 && status != 101) {
        m_stage = Stage::Head;                       // interim response: wait for the next head
        return true;
    }
    const std::string *transfer = m_response.header("transfer-encoding");
    const std::string *length = m_response.header("content-length");
    if (m_isHead || status == 204 || status == 304 || status == 101) {
        m_framing = Framing::None;
    } else if (transfer && StringUtils::containsIgnoreCase(*transfer, "chunked")) {
        m_framing = Framing::Chunked;
        m_decoder.reset(new HTTPBodyDecoder(HTTPBodyDecoder::chunked()));
    } else if (length) {
        if (length->empty() || length->size() > 18 || length->find_first_not_of("0123456789") != std::string::npos) {
            fail("invalid Content-Length");
            return false;
        }
        m_total = strtoull(length->c_str(), nullptr, 10);
        m_framing = Framing::Length;
        m_decoder.reset(new HTTPBodyDecoder(HTTPBodyDecoder::contentLength(m_total)));
    } else {
        m_framing = Framing::UntilClose;
    }
    if (m_framing == Framing::Length && m_requestCopy.maxResponseBytes && m_total > m_requestCopy.maxResponseBytes) {
        fail("response too large");
        return false;
    }

    if (!m_requestCopy.downloadPath.empty() && !m_requestCopy.onBody && status >= 200 && status < 300 && m_framing != Framing::None) {
        if (!m_file.open(m_requestCopy.downloadPath)) {
            fail("cannot create the download file (it may exist already)");
            return false;
        }
    }
    m_stage = Stage::Body;
    if (m_framing == Framing::None)
        succeed();
    return true;
}

bool HTTPFetchConnection::emit(const char *data, size_t length)
{
    m_response.bodyBytes += length;
    bool keep = true;
    if (m_requestCopy.onBody)
        keep = m_requestCopy.onBody(data, length);
    else if (m_file.isOpen())
        keep = m_file.write(data, length);
    else
        m_response.body.append(data, length);
    if (m_requestCopy.onProgress)
        m_requestCopy.onProgress(m_response.bodyBytes, m_total);
    return keep;
}

void HTTPFetchConnection::consume(const char *data, size_t length)
{
    if (m_framing == Framing::UntilClose) {
        if (m_requestCopy.maxResponseBytes && m_response.bodyBytes + length > m_requestCopy.maxResponseBytes) {
            fail("response too large");
            return;
        }
        if (!emit(data, length))
            fail("the transfer was aborted");
        return;
    }
    bool aborted = false;
    HTTPBodyDecoder::Result result = m_decoder->feed(data, length, m_requestCopy.maxResponseBytes, [&](const char *piece, size_t size) {
        if (emit(piece, size))
            return true;
        aborted = true;
        return false;
    });
    switch (result.status) {
    case HTTPBodyDecoder::Status::NeedMore: break;
    case HTTPBodyDecoder::Status::Done: succeed(); break;
    case HTTPBodyDecoder::Status::TooLarge: fail("response too large"); break;
    case HTTPBodyDecoder::Status::Error: fail(aborted ? "the transfer was aborted" : "malformed response body"); break;
    }
}

void HTTPFetchConnection::report()
{
    if (m_reported)
        return;
    m_reported = true;
    m_stage = Stage::Finished;
    Done done = std::move(m_done);
    if (done)
        done(std::move(m_response));
}

void HTTPFetchConnection::succeed()
{
    if (m_reported)
        return;
    if (m_file.isOpen() && !m_file.close()) {
        fail("the download file cannot be written");
        return;
    }
    m_file.forget();
    report();
    if (getStatus() != Closed)
        close(true);
}

void HTTPFetchConnection::fail(const std::string &message, bool closeSocket)
{
    if (m_reported)
        return;
    m_file.discard();
    m_response.error = message;
    report();
    if (closeSocket && getStatus() != Closed)
        close(true);
}

void HTTPFetchConnection::onClose()
{
    if (m_reported)
        return;
    if (m_stage == Stage::Body && m_framing == Framing::UntilClose) {
        succeed();
        return;
    }
    switch (closeReason()) {
    case ConnectFailed:
        fail("cannot connect to " + m_url.host + ":" + std::to_string(m_url.port) + (m_url.https ? " (TCP connect failed, or the TLS handshake / certificate verification failed)" : ""));
        break;
    case ClosedByTimeout: fail(m_stage == Stage::Head && m_response.status == 0 ? "timeout" : "timeout while receiving the response"); break;
    case ClosedByError: fail(m_url.https ? "TLS or network error (certificate rejected?)" : "network error"); break;
    default: fail(m_stage == Stage::Body ? "connection closed before the response was complete" : "connection closed without a response"); break;
    }
}

} // namespace rizweb
