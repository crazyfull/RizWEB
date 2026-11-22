// Drives HttpConnection + WebServer with a fake socket (no network, no netRector needed).
#include "../src/server/clsHttpConnection.h"
#include "../src/server/clsWebServer.h"
#include "../src/util/stringUtil.h"
#include "../src/websocket/clsWebSocketFrame.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace rizweb;

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_failed++;                                                                   \
            std::printf("  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                 \
    } while (0)

static void feed(HttpConnection& c, const std::string& text)
{
    c.onReceiveData(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

static size_t countOf(const std::string& text, const std::string& needle)
{
    size_t n = 0, pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        n++;
        pos += needle.size();
    }
    return n;
}

static std::string maskedFrame(uint8_t b0, const std::string& payload)
{
    const uint8_t key[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    std::string f;
    f += static_cast<char>(b0);
    f += static_cast<char>(0x80 | payload.size());   // payloads here are < 126 bytes
    f.append(reinterpret_cast<const char*>(key), 4);
    for (size_t i = 0; i < payload.size(); i++)
        f += static_cast<char>(payload[i] ^ key[i & 3]);
    return f;
}

static bool fdIsClosed(int fd)
{
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

static const char* kUpgrade =
    "GET /ws HTTP/1.1\r\nHost: site.test\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";

int main()
{
    ServerConfig config;
    config.httpPort = 0;
    WebServer server(config);

    int exceptionCalls = 0;
    server.get("/hello", [](const HttpRequest&, HttpResponse& res) { res.sendText(200, "hello"); });
    server.post("/echo", [](const HttpRequest& req, HttpResponse& res) { res.sendText(200, req.body); });
    server.get("/boom", [&](const HttpRequest&, HttpResponse&) { exceptionCalls++; throw std::runtime_error("boom"); });
    server.get("/big", [](const HttpRequest&, HttpResponse& res) { res.sendText(200, std::string(100000, 'B')); });
    server.get("/204", [](const HttpRequest&, HttpResponse& res) { res.setStatus(204); });

    char tmpl[] = "/tmp/rizweb_conn_XXXXXX";
    std::string base = mkdtemp(tmpl);
    std::string small(500, 's');
    std::string large(300000, 'L');
    for (size_t i = 0; i < large.size(); i++)
        large[i] = static_cast<char>('a' + (i * 7) % 26);   // not repetitive, so wrong offsets are noticed
    std::ofstream(base + "/small.txt") << small;
    std::ofstream(base + "/large.bin", std::ios::binary) << large;
    StaticFileOptions files;
    files.rootDirectory = base;
    files.enableGzip = false;
    server.serveStatic("/files/", files);

    std::vector<std::string> events;
    WebSocketHandlers ws;
    ws.onOpen = [&](WebSocketSession& s) { events.push_back("open:" + s.request().path); };
    ws.onMessage = [&](WebSocketSession& s, WsOpcode op, std::string_view data) {
        events.push_back("msg:" + std::string(data));
        if (data == "bye")
            s.disconnect(1000, "done");
        else if (op == WsOpcode::text)
            s.sendText("echo:" + std::string(data));
    };
    ws.onClose = [&](WebSocketSession&, uint16_t code) { events.push_back("close:" + std::to_string(code)); };
    server.websocket("/ws", ws);

    std::printf("simple request\n");
    {
        HttpConnection c(server);
        c.onAccepted();
        CHECK(c.idleTimeout == 20);
        feed(c, "GET /hello HTTP/1.1\r\nHost: site.test\r\n\r\n");
        CHECK(c.sent.find("HTTP/1.1 200 OK\r\n") == 0);
        CHECK(c.sent.find("Content-Length: 5\r\n") != std::string::npos);
        CHECK(c.sent.find("Server: RizWEB/") != std::string::npos);
        CHECK(c.sent.compare(c.sent.size() - 5, 5, "hello") == 0);
        CHECK(c.status == netrector::TCPSocket::Connected);   // keep-alive
        CHECK(c.sent.find("Connection:") == std::string::npos);
    }

    std::printf("pipelining and slicing\n");
    {
        HttpConnection c(server);
        feed(c, "GET /hello HTTP/1.1\r\nHost: a\r\n\r\nGET /hello HTTP/1.1\r\nHost: a\r\n\r\nPOST /echo HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\n\r\nabc");
        CHECK(countOf(c.sent, "HTTP/1.1 200 OK") == 3);
        CHECK(c.sent.compare(c.sent.size() - 3, 3, "abc") == 0);

        HttpConnection d(server);
        std::string req = "POST /echo HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\n\r\ndata";
        for (char ch : req)
            feed(d, std::string(1, ch));
        CHECK(d.sent.find("200 OK") != std::string::npos && d.sent.compare(d.sent.size() - 4, 4, "data") == 0);
    }

    std::printf("close semantics\n");
    {
        HttpConnection c(server);
        feed(c, "GET /hello HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\nGET /hello HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(countOf(c.sent, "HTTP/1.1 200") == 1);   // second request is ignored after close
        CHECK(c.sent.find("Connection: close\r\n") != std::string::npos);
        CHECK(c.status == netrector::TCPSocket::Closed && !c.forced);

        HttpConnection h10(server);
        feed(h10, "GET /hello HTTP/1.0\r\n\r\n");
        CHECK(h10.sent.find("HTTP/1.0 200 OK") == 0 && h10.status == netrector::TCPSocket::Closed);

        HttpConnection h10k(server);
        feed(h10k, "GET /hello HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
        CHECK(h10k.sent.find("Connection: keep-alive\r\n") != std::string::npos && h10k.status == netrector::TCPSocket::Connected);
    }

    std::printf("HEAD, 204, 404, 405, 500\n");
    {
        HttpConnection c(server);
        feed(c, "HEAD /hello HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.sent.find("Content-Length: 5") != std::string::npos);
        CHECK(c.sent.size() >= 4 && c.sent.compare(c.sent.size() - 4, 4, "\r\n\r\n") == 0);

        HttpConnection n(server);
        feed(n, "GET /204 HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(n.sent.find("204 No Content") != std::string::npos && n.sent.find("Content-Length") == std::string::npos);

        HttpConnection nf(server);
        feed(nf, "GET /nope HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(nf.sent.find("404 Not Found") != std::string::npos);

        HttpConnection m(server);
        feed(m, "DELETE /hello HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\n");
        CHECK(m.sent.find("405 Method Not Allowed") != std::string::npos && m.sent.find("Allow: GET, HEAD") != std::string::npos);

        HttpConnection o(server);
        feed(o, "OPTIONS /echo HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(o.sent.find("204 No Content") != std::string::npos && o.sent.find("Allow: POST, OPTIONS") != std::string::npos);

        HttpConnection b(server);
        feed(b, "GET /boom HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(b.sent.find("500 Internal Server Error") != std::string::npos && exceptionCalls == 1);
        CHECK(b.sent.find("boom") == std::string::npos);   // internals are not leaked
        CHECK(b.status == netrector::TCPSocket::Connected);

        HttpConnection u(server);
        feed(u, "BREW /hello HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(u.sent.find("501") != std::string::npos);
    }

    std::printf("malformed requests\n");
    {
        HttpConnection c(server);
        feed(c, "GET /../../etc/passwd HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.sent.find("400 Bad Request") != std::string::npos && c.status == netrector::TCPSocket::Closed);

        HttpConnection s(server);
        feed(s, "POST /echo HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n");
        CHECK(s.sent.find("400 Bad Request") != std::string::npos && s.status == netrector::TCPSocket::Closed);

        HttpConnection t(server);
        feed(t, "POST /echo HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999\r\n\r\n");
        CHECK(t.sent.find("413") != std::string::npos);

        HttpConnection e(server);
        feed(e, "POST /echo HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\nExpect: 100-continue\r\n\r\n");
        CHECK(e.sent == "HTTP/1.1 100 Continue\r\n\r\n");
        feed(e, "xyz");
        CHECK(e.sent.find("200 OK") != std::string::npos && e.sent.compare(e.sent.size() - 3, 3, "xyz") == 0);
    }

    std::printf("large body without copy\n");
    {
        HttpConnection c(server);
        feed(c, "GET /big HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.sentNoCopyCalls == 1);
        CHECK(c.sent.size() > 100000 && c.sent.compare(c.sent.size() - 10, 10, std::string(10, 'B')) == 0);
    }

    std::printf("static files over sendfile\n");
    {
        HttpConnection c(server);
        feed(c, "GET /files/small.txt HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.sendFileCalls == 0 && c.sent.compare(c.sent.size() - 500, 500, small) == 0);   // small: one send

        HttpConnection l(server);
        feed(l, "GET /files/large.bin HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(l.sendFileCalls == 1 && l.sentNoCopyCalls == 1);
        size_t headEnd = l.sent.find("\r\n\r\n") + 4;
        CHECK(l.sent.size() - headEnd == large.size());
        CHECK(l.sent.compare(headEnd, std::string::npos, large) == 0);
        CHECK(l.sent.find("Content-Length: 300000\r\n") != std::string::npos);

        // range inside the big file (tail logic must use the range, not the whole file)
        HttpConnection r(server);
        feed(r, "GET /files/large.bin HTTP/1.1\r\nHost: a\r\nRange: bytes=1000-201999\r\n\r\n");
        CHECK(r.sent.find("206 Partial Content") != std::string::npos);
        size_t rangeBody = r.sent.find("\r\n\r\n") + 4;
        CHECK(r.sent.compare(rangeBody, std::string::npos, large.substr(1000, 201000)) == 0);

        // conditional request -> 304 without a body
        HttpConnection first(server);
        feed(first, "GET /files/small.txt HTTP/1.1\r\nHost: a\r\n\r\n");
        size_t etagPos = first.sent.find("ETag: ") + 6;
        std::string etag = first.sent.substr(etagPos, first.sent.find("\r\n", etagPos) - etagPos);
        HttpConnection nm(server);
        feed(nm, "GET /files/small.txt HTTP/1.1\r\nHost: a\r\nIf-None-Match: " + etag + "\r\n\r\n");
        CHECK(nm.sent.find("304 Not Modified") != std::string::npos && nm.sent.find("Content-Length") == std::string::npos);
        CHECK(nm.sent.compare(nm.sent.size() - 4, 4, "\r\n\r\n") == 0);

        // "/files/%2e%2e/secret" normalizes to "/secret" (still inside the URL space) and is not routed
        HttpConnection t(server);
        feed(t, "GET /files/%2e%2e/secret HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(t.sent.find("404 Not Found") != std::string::npos);
        // climbing above the root is refused outright
        HttpConnection t2(server);
        feed(t2, "GET /files/%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(t2.sent.find("400 Bad Request") != std::string::npos);
    }

    // descriptors opened for the files above must all be gone again
    {
        int open = 0;
        for (int fd = 3; fd < 64; fd++)
            if (!fdIsClosed(fd)) open++;
        CHECK(open == 0);
    }

    std::printf("host filter\n");
    {
        WebServer vhost(config);
        vhost.config().hostName = "only.example";
        vhost.get("/x", [](const HttpRequest&, HttpResponse& res) { res.sendText(200, "x"); });
        HttpConnection a(vhost);
        feed(a, "GET /x HTTP/1.1\r\nHost: only.example:8080\r\n\r\n");
        CHECK(a.sent.find("200 OK") != std::string::npos);
        HttpConnection b(vhost);
        feed(b, "GET /x HTTP/1.1\r\nHost: other.example\r\n\r\n");
        CHECK(b.sent.find("404 Not Found") != std::string::npos);
    }

    std::printf("websocket\n");
    {
        HttpConnection c(server);
        feed(c, std::string(kUpgrade) + "\r\n");
        CHECK(c.sent.find("HTTP/1.1 101 Switching Protocols\r\n") == 0);
        CHECK(c.sent.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos);
        CHECK(c.sent.find("Upgrade: websocket\r\n") != std::string::npos && c.sent.find("Connection: Upgrade\r\n") != std::string::npos);
        CHECK(c.sent.find("Content-Length") == std::string::npos);
        CHECK(c.isOpen() && events.size() == 1 && events[0] == "open:/ws");

        c.sent.clear();
        feed(c, maskedFrame(0x81, "hi"));
        CHECK(c.sent == std::string("\x81\x07" "echo:hi", 9));

        // ping gets a pong with the same payload; two frames in one read
        c.sent.clear();
        feed(c, maskedFrame(0x89, "pp") + maskedFrame(0x81, "again"));
        CHECK(c.sent == std::string("\x8a\x02" "pp", 4) + std::string("\x81\x0a" "echo:again", 12));

        // the handler closes the session
        c.sent.clear();
        feed(c, maskedFrame(0x81, "bye"));
        CHECK(c.sent == std::string("\x88\x06\x03\xe8" "done", 8));
        CHECK(c.status == netrector::TCPSocket::Closed);
        CHECK(!c.isOpen());
        CHECK(events.back() == "close:1006");   // no close frame from the peer
    }
    {
        events.clear();
        HttpConnection c(server);
        // handshake and the first frame arrive in the same read
        feed(c, std::string(kUpgrade) + "\r\n" + maskedFrame(0x81, "fast"));
        CHECK(events.size() == 2 && events[1] == "msg:fast");
        CHECK(c.sent.find("echo:fast") != std::string::npos);

        // peer closes politely
        c.sent.clear();
        feed(c, maskedFrame(0x88, std::string("\x03\xe9", 2)));
        CHECK(c.sent == std::string("\x88\x02\x03\xe9", 4));
        CHECK(c.status == netrector::TCPSocket::Closed && events.back() == "close:1001");
    }
    {
        events.clear();
        HttpConnection c(server);
        feed(c, std::string(kUpgrade) + "\r\n");
        c.sent.clear();
        feed(c, std::string("\x81\x02xx", 4));    // unmasked client frame
        CHECK(c.sent == std::string("\x88\x02\x03\xea", 4) && c.status == netrector::TCPSocket::Closed);   // 1002
    }
    {
        // cross-site websocket is refused, same-origin accepted
        HttpConnection evil(server);
        feed(evil, std::string(kUpgrade) + "Origin: https://evil.test\r\n\r\n");
        CHECK(evil.sent.find("403 Forbidden") != std::string::npos && !evil.isOpen());

        HttpConnection good(server);
        feed(good, std::string(kUpgrade) + "Origin: https://site.test\r\n\r\n");
        CHECK(good.sent.find("HTTP/1.1 101 Switching Protocols") == 0);

        HttpConnection plain(server);
        feed(plain, "GET /ws HTTP/1.1\r\nHost: site.test\r\n\r\n");
        CHECK(plain.sent.find("426 Upgrade Required") != std::string::npos && plain.sent.find("Sec-WebSocket-Version: 13") != std::string::npos);

        HttpConnection badKey(server);
        feed(badKey, "GET /ws HTTP/1.1\r\nHost: site.test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: short\r\nSec-WebSocket-Version: 13\r\n\r\n");
        CHECK(badKey.sent.find("400 Bad Request") != std::string::npos);
    }

    std::printf("backpressure\n");
    {
        HttpConnection c(server);
        c.blocked = true;
        feed(c, "GET /hello HTTP/1.1\r\nHost: a\r\n\r\nGET /hello HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.sent.empty());           // held back, not answered
        c.blocked = false;
        c.onResume();
        CHECK(countOf(c.sent, "HTTP/1.1 200 OK") == 2);

        // a partly delivered request is not lost either
        HttpConnection d(server);
        feed(d, "GET /hello HTTP/1.1\r\nHost: a\r\n\r\n");
        d.blocked = true;
        feed(d, "GET /hello HTTP/1.1\r\nHost: a\r\n\r\n");
        d.blocked = false;
        feed(d, "GET /hello HTTP/1.1\r\nHost: a\r\n\r\n");   // new data joins the held back bytes
        CHECK(countOf(d.sent, "HTTP/1.1 200 OK") == 3);
    }

    std::printf("request limits\n");
    {
        WebServer small(config);
        small.config().maxRequestsPerConnection = 2;
        small.get("/x", [](const HttpRequest&, HttpResponse& res) { res.sendText(200, "x"); });
        HttpConnection c(small);
        feed(c, "GET /x HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.status == netrector::TCPSocket::Connected);
        feed(c, "GET /x HTTP/1.1\r\nHost: a\r\n\r\n");
        CHECK(c.status == netrector::TCPSocket::Closed && c.sent.rfind("Connection: close") != std::string::npos);

        WebServer tight(config);
        tight.config().maxHeaderBytes = 128;
        HttpConnection h(tight);
        feed(h, "GET /x HTTP/1.1\r\nHost: a\r\nX-Pad: " + std::string(300, 'p') + "\r\n\r\n");
        CHECK(h.sent.find("431") != std::string::npos && h.status == netrector::TCPSocket::Closed);
    }

    CHECK(server.activeConnections() == 0);

    std::string cleanup = "rm -rf " + base;
    if (std::system(cleanup.c_str()) != 0)
        std::printf("  (cleanup failed)\n");

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
