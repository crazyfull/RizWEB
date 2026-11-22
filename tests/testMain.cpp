// Unit tests for the network independent core (no netRector needed).
#include "../src/http/clsAuthGuard.h"
#include "../src/http/clsHttpRequestParser.h"
#include "../src/http/clsHttpResponse.h"
#include "../src/http/clsRouter.h"
#include "../src/http/clsStaticFileHandler.h"
#include "../src/util/clsBase64.h"
#include "../src/util/clsIniFile.h"
#include "../src/util/clsSha1.h"
#include "../src/util/clsUrlCodec.h"
#include "../src/util/stringUtil.h"
#include "../src/websocket/clsWebSocketFrame.h"
#include "../src/websocket/clsWebSocketHandshake.h"

#include <cstdio>
#include <cstdlib>
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

static ParseStatus parseAll(HttpRequestParser& parser, const std::string& text, size_t* used = nullptr)
{
    size_t consumed = 0;
    ParseStatus status = parser.feed(text.data(), text.size(), consumed);
    if (used)
        *used = consumed;
    return status;
}

static void testUrlCodec()
{
    std::printf("url codec\n");
    std::string out;
    CHECK(UrlCodec::decode("a%20b%2Fc", out, false) && out == "a b/c");
    CHECK(UrlCodec::decode("a+b", out, true) && out == "a b");
    CHECK(UrlCodec::decode("a+b", out, false) && out == "a+b");
    CHECK(!UrlCodec::decode("%zz", out, false));
    CHECK(!UrlCodec::decode("abc%2", out, false));
    CHECK(!UrlCodec::decode("%", out, false));

    CHECK(UrlCodec::normalizePath("/a/b/../c", out) && out == "/a/c");
    CHECK(UrlCodec::normalizePath("/a//b/./c/", out) && out == "/a/b/c/");
    CHECK(UrlCodec::normalizePath("/", out) && out == "/");
    CHECK(UrlCodec::normalizePath("/a/..", out) && out == "/");
    CHECK(!UrlCodec::normalizePath("/..", out));
    CHECK(!UrlCodec::normalizePath("/../etc/passwd", out));
    CHECK(!UrlCodec::normalizePath("/a/../../b", out));
    CHECK(!UrlCodec::normalizePath(std::string("/a\0b", 4), out));
    CHECK(!UrlCodec::normalizePath("/a\\..\\b", out));
    CHECK(!UrlCodec::normalizePath("relative", out));
    CHECK(UrlCodec::encodePath("/a b/é") == "/a%20b/%C3%A9");
}

static void testHashing()
{
    std::printf("sha1 / base64 / websocket accept\n");
    auto d = Sha1::hash("abc");
    CHECK(Base64::encode(d.data(), d.size()) == "qZk+NkcGgWq6PiVxeFDCbJzQ2J0=");
    CHECK(WebSocketHandshake::acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    CHECK(WebSocketHandshake::isValidClientKey("dGhlIHNhbXBsZSBub25jZQ=="));
    CHECK(!WebSocketHandshake::isValidClientKey("short"));

    std::string out;
    CHECK(Base64::decode("Zm9vYg==", out) && out == "foob");
    CHECK(Base64::decode("Zm9v", out) && out == "foo");
    CHECK(!Base64::decode("Zm9", out));
    CHECK(!Base64::decode("Zm=v", out));

    // incremental update must equal one-shot hashing across the 64 byte block boundary
    std::string big(1000, 'a');
    Sha1 part;
    part.update(big.data(), 7);
    part.update(big.data() + 7, 993);
    CHECK(part.finish() == Sha1::hash(big));
}

static void testParserBasics()
{
    std::printf("parser basics\n");
    HttpRequestParser p;
    size_t used = 0;
    std::string req = "GET /a%20b/../c?x=1&y=%41+b HTTP/1.1\r\nHost: Example.com:8080\r\nUser-Agent: t\r\n\r\nGET /next";
    CHECK(parseAll(p, req, &used) == ParseStatus::complete);
    CHECK(used == req.size() - 9);   // the pipelined start of the next request is untouched
    const HttpRequest& r = p.request();
    CHECK(r.method == HttpMethod::get);
    CHECK(r.path == "/c");
    CHECK(r.rawQuery == "x=1&y=%41+b");
    CHECK(r.queryValue("y").value_or("") == "A b");
    CHECK(r.host() == "Example.com");
    CHECK(r.keepAlive);
    CHECK(r.header("user-agent") && *r.header("USER-AGENT") == "t");

    // HTTP/1.0 closes by default
    p.reset();
    CHECK(parseAll(p, "GET / HTTP/1.0\r\n\r\n") == ParseStatus::complete);
    CHECK(!p.request().keepAlive);
    p.reset();
    CHECK(parseAll(p, "GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n") == ParseStatus::complete);
    CHECK(!p.request().keepAlive);

    // leading empty lines are tolerated
    p.reset();
    CHECK(parseAll(p, "\r\n\r\nGET / HTTP/1.1\r\nHost: a\r\n\r\n") == ParseStatus::complete);
}

static void testParserSlicing()
{
    std::printf("parser fed byte by byte\n");
    const std::string req = "POST /submit HTTP/1.1\r\nHost: a\r\nContent-Length: 11\r\n\r\nuser=a&b=%26";
    // body is 12 bytes here on purpose: parser must stop after 11
    HttpRequestParser p;
    ParseStatus st = ParseStatus::needMore;
    size_t total = 0;
    for (size_t i = 0; i < req.size() && st == ParseStatus::needMore; i++) {
        size_t used = 0;
        st = p.feed(req.data() + i, 1, used);
        total += used;
    }
    CHECK(st == ParseStatus::complete);
    CHECK(total == req.size() - 1);
    CHECK(p.request().body == "user=a&b=%2");
    CHECK(p.isHeadComplete());
}

static void testParserChunked()
{
    std::printf("parser chunked\n");
    HttpRequestParser p;
    std::string req = "POST /c HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "5\r\nhello\r\n6;ext=1\r\n world\r\n0\r\nX-Trailer: 1\r\n\r\nGET /";
    size_t used = 0;
    CHECK(parseAll(p, req, &used) == ParseStatus::complete);
    CHECK(p.request().body == "hello world");
    CHECK(used == req.size() - 5);

    // byte by byte
    HttpRequestParser q;
    ParseStatus st = ParseStatus::needMore;
    for (size_t i = 0; i < req.size() - 5 && st == ParseStatus::needMore; i++) {
        size_t u = 0;
        st = q.feed(req.data() + i, 1, u);
    }
    CHECK(st == ParseStatus::complete && q.request().body == "hello world");

    HttpRequestParser bad;
    CHECK(parseAll(bad, "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n") == ParseStatus::error);
    HttpRequestParser bad2;
    CHECK(parseAll(bad2, "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabcXX") == ParseStatus::error);
}

static void testParserRejects()
{
    std::printf("parser rejects hostile input\n");
    struct Case { const char* text; int status; };
    const Case cases[] = {
        {"GET /../etc/passwd HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET /%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET /a%00b HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET /a%0d%0aSet-Cookie:x HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET /bad%zz HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET /a\\..\\b HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET http://evil/ HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET /#frag HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"GET / HTTP/1.1\r\n\r\n", 400},                                            // no Host
        {"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 400},                      // two Host
        {"GET / HTTP/2.0\r\nHost: a\r\n\r\n", 505},
        {"GET / JUNK\r\nHost: a\r\n\r\n", 400},
        {"GET  / HTTP/1.1\r\nHost: a\r\n\r\n", 400},                                // double space
        {"GET / HTTP/1.1\r\nHost: a\r\nX-A : b\r\n\r\n", 400},                      // space before colon
        {"GET / HTTP/1.1\r\nHost: a\r\n folded: x\r\n\r\n", 400},                   // obs-fold
        {"GET / HTTP/1.1\r\nHost: a\r\nX: a\x01" "b\r\n\r\n", 400},                 // control char
        {"GET / HTTP/1.1\r\nHost: a\r\nX: a\nb\r\n\r\n", 400},                       // bare LF inside a value
        {"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n", 400},
        {"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n", 400},
        {"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1e3\r\n\r\n", 400},
        {"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n", 400},
        {"POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: gzip, chunked\r\n\r\n", 501},
        {"POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999\r\n\r\n", 413},
        {"POST / HTTP/1.1\r\nHost: a\r\nExpect: weird\r\n\r\n", 417},
        {"G(T / HTTP/1.1\r\nHost: a\r\n\r\n", 400},
    };
    for (const Case& c : cases) {
        HttpRequestParser p;
        ParseStatus st = parseAll(p, c.text);
        if (c.status == 0) {
            CHECK(st == ParseStatus::needMore);
        } else {
            CHECK(st == ParseStatus::error);
            CHECK(p.errorStatus() == c.status);
            if (st != ParseStatus::error || p.errorStatus() != c.status)
                std::printf("    case: %.40s\n", c.text);
        }
    }

    // duplicate equal Content-Length is allowed (RFC 7230 3.3.2)
    HttpRequestParser ok;
    CHECK(parseAll(ok, "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 2, 2\r\n\r\nhi") == ParseStatus::complete);

    // limits
    HttpParserLimits tiny;
    tiny.maxHeaderBytes = 64;
    HttpRequestParser big(tiny);
    CHECK(parseAll(big, "GET / HTTP/1.1\r\nHost: a\r\nX-Long: " + std::string(200, 'x') + "\r\n\r\n") == ParseStatus::error);
    CHECK(big.errorStatus() == 431);

    HttpParserLimits uri;
    uri.maxUriBytes = 16;
    HttpRequestParser longUri(uri);
    CHECK(parseAll(longUri, "GET /" + std::string(40, 'a') + " HTTP/1.1\r\nHost: a\r\n\r\n") == ParseStatus::error);
    CHECK(longUri.errorStatus() == 414);

    HttpParserLimits body;
    body.maxBodyBytes = 4;
    HttpRequestParser chunkTooBig(body);
    CHECK(parseAll(chunkTooBig, "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n3\r\ndef\r\n") == ParseStatus::error);
    CHECK(chunkTooBig.errorStatus() == 413);

    // slow header that never ends is cut at the limit, not buffered forever
    HttpRequestParser slow(tiny);
    ParseStatus st = ParseStatus::needMore;
    for (int i = 0; i < 100 && st == ParseStatus::needMore; i++) {
        size_t u = 0;
        st = slow.feed("a", 1, u);
    }
    CHECK(st == ParseStatus::error && slow.errorStatus() == 431);
}

static void testExpectContinue()
{
    std::printf("expect 100-continue\n");
    HttpRequestParser p;
    CHECK(parseAll(p, "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\nExpect: 100-continue\r\n\r\n") == ParseStatus::needMore);
    CHECK(p.isHeadComplete() && p.expectsContinue());
}

static void testRequestHelpers()
{
    std::printf("request helpers\n");
    HttpRequestParser p;
    CHECK(parseAll(p, "POST /f HTTP/1.1\r\nHost: [::1]:80\r\nContent-Type: application/x-www-form-urlencoded\r\n"
                      "Cookie: a=1; rizweb_sid=\"tok\"; b=2\r\nContent-Length: 17\r\n\r\nuser=a%26b&pass=x") == ParseStatus::complete);
    const HttpRequest& r = p.request();
    CHECK(r.host() == "[::1]");
    CHECK(r.formValue("user").value_or("") == "a&b");
    CHECK(r.formValue("pass").value_or("") == "x");
    CHECK(!r.formValue("none").has_value());
    CHECK(r.cookie("rizweb_sid").value_or("") == "tok");
    CHECK(r.cookie("a").value_or("") == "1");
    CHECK(!r.cookie("c").has_value());
}

static void testResponse()
{
    std::printf("response\n");
    HttpResponse res;
    CHECK(!res.setHeader("X-Bad", "a\r\nSet-Cookie: x=1"));
    CHECK(!res.setHeader("Bad Name", "v"));
    res.setHeader("Content-Type", "text/plain");
    res.write("hello");
    HttpResponse::HeadOptions opt;
    opt.serverName = "RizWEB/test";
    std::string head = res.serializeHead(opt);
    CHECK(head.find("HTTP/1.1 200 OK\r\n") == 0);
    CHECK(head.find("Content-Length: 5\r\n") != std::string::npos);
    CHECK(head.find("Server: RizWEB/test\r\n") != std::string::npos);
    CHECK(head.find("Date: ") != std::string::npos);
    CHECK(head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0);
    CHECK(head.find("Set-Cookie") == std::string::npos);

    opt.keepAlive = false;
    CHECK(res.serializeHead(opt).find("Connection: close\r\n") != std::string::npos);

    HttpResponse redirect;
    redirect.redirect("/ok");
    CHECK(redirect.status() == 302 && *redirect.header("Location") == "/ok");
    HttpResponse evil;
    evil.redirect("/x\r\nSet-Cookie: a=b");
    CHECK(evil.status() == 500 && !evil.header("Set-Cookie"));

    HttpResponse cookies;
    Cookie c;
    c.name = "sid";
    c.value = "abc123";
    c.secure = true;
    c.maxAgeSeconds = 60;
    CHECK(cookies.setCookie(c));
    const std::string& line = *cookies.header("Set-Cookie");
    CHECK(line.find("sid=abc123") == 0 && line.find("HttpOnly") != std::string::npos
          && line.find("SameSite=Lax") != std::string::npos && line.find("Secure") != std::string::npos);
    c.value = "a;b";
    CHECK(!cookies.setCookie(c));

    HttpResponse err;
    err.sendError(404, "<script>alert(1)</script>");
    CHECK(err.body().find("<script>") == std::string::npos && err.body().find("&lt;script&gt;") != std::string::npos);
}

static void testRouter()
{
    std::printf("router\n");
    Router router;
    int hit = 0;
    router.add("/api/time", Router::methodBit(HttpMethod::get), [&](const HttpRequest&, HttpResponse&) { hit = 1; });
    router.add("/api/time", Router::methodBit(HttpMethod::post), [&](const HttpRequest&, HttpResponse&) { hit = 2; });
    router.add("/static/", Router::kAnyMethod, [&](const HttpRequest&, HttpResponse&) { hit = 3; });
    router.add("/static/deep/", Router::kAnyMethod, [&](const HttpRequest&, HttpResponse&) { hit = 4; });

    HttpRequest req;
    HttpResponse res;
    auto m = router.find("/api/time", HttpMethod::get);
    CHECK(m.handler);
    (*m.handler)(req, res);
    CHECK(hit == 1);
    m = router.find("/api/time", HttpMethod::head);   // HEAD follows GET
    CHECK(m.handler);
    m = router.find("/api/time", HttpMethod::post);
    (*m.handler)(req, res);
    CHECK(hit == 2);
    m = router.find("/api/time", HttpMethod::del);
    CHECK(!m.handler && m.pathExists && (m.allowedMethods & Router::methodBit(HttpMethod::get)));
    m = router.find("/nothing", HttpMethod::get);
    CHECK(!m.handler && !m.pathExists);
    m = router.find("/static/a.css", HttpMethod::get);
    (*m.handler)(req, res);
    CHECK(hit == 3);
    m = router.find("/static/deep/x", HttpMethod::get);   // longest prefix wins
    (*m.handler)(req, res);
    CHECK(hit == 4);
    m = router.find("/static", HttpMethod::get);           // tree root without slash
    CHECK(m.handler);
    m = router.find("/staticfoo", HttpMethod::get);
    CHECK(!m.handler);
}

static void testWebSocketFrames()
{
    std::printf("websocket frames\n");
    auto maskedFrame = [](uint8_t b0, const std::string& payload, bool mask = true) {
        std::string f;
        f += static_cast<char>(b0);
        const uint8_t key[4] = {0x12, 0x34, 0x56, 0x78};
        if (payload.size() < 126) {
            f += static_cast<char>((mask ? 0x80 : 0) | payload.size());
        } else {
            f += static_cast<char>((mask ? 0x80 : 0) | 126);
            f += static_cast<char>(payload.size() >> 8);
            f += static_cast<char>(payload.size() & 0xFF);
        }
        if (mask) {
            f.append(reinterpret_cast<const char*>(key), 4);
            for (size_t i = 0; i < payload.size(); i++)
                f += static_cast<char>(payload[i] ^ key[i & 3]);
        } else {
            f += payload;
        }
        return f;
    };
    auto feed = [](WebSocketFrameParser& p, const std::string& bytes, size_t& used) {
        return p.feed(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), used);
    };

    WebSocketFrameParser p;
    size_t used = 0;
    std::string two = maskedFrame(0x81, "hello") + maskedFrame(0x82, "bin");
    CHECK(feed(p, two, used) == WsParseStatus::message);
    CHECK(p.message().opcode == WsOpcode::text && p.message().payload == "hello");
    std::string rest = two.substr(used);
    CHECK(feed(p, rest, used) == WsParseStatus::message);
    CHECK(p.message().opcode == WsOpcode::binary && p.message().payload == "bin");

    // fragmentation with a ping in the middle
    WebSocketFrameParser frag;
    std::string stream = maskedFrame(0x01, "Hel") + maskedFrame(0x89, "p") + maskedFrame(0x80, "lo");
    size_t pos = 0;
    int messages = 0;
    std::string seen;
    while (pos < stream.size()) {
        std::string slice = stream.substr(pos);
        WsParseStatus st = feed(frag, slice, used);
        pos += used;
        if (st == WsParseStatus::message) {
            messages++;
            seen += (frag.message().opcode == WsOpcode::ping ? "[ping:" : "[text:") + frag.message().payload + "]";
        } else {
            break;
        }
    }
    CHECK(messages == 2 && seen == "[ping:p][text:Hello]");

    // byte by byte
    WebSocketFrameParser slow;
    std::string one = maskedFrame(0x81, std::string(200, 'z'));
    WsParseStatus st = WsParseStatus::needMore;
    for (size_t i = 0; i < one.size() && st == WsParseStatus::needMore; i++)
        st = slow.feed(reinterpret_cast<const uint8_t*>(one.data()) + i, 1, used);
    CHECK(st == WsParseStatus::message && slow.message().payload == std::string(200, 'z'));

    // violations
    WebSocketFrameParser a;
    CHECK(feed(a, maskedFrame(0x81, "x", false), used) == WsParseStatus::error && a.errorCode() == 1002);   // unmasked
    WebSocketFrameParser b;
    CHECK(feed(b, maskedFrame(0xC1, "x"), used) == WsParseStatus::error);                                    // RSV1
    WebSocketFrameParser c;
    CHECK(feed(c, maskedFrame(0x83, "x"), used) == WsParseStatus::error);                                    // bad opcode
    WebSocketFrameParser d;
    CHECK(feed(d, maskedFrame(0x80, "x"), used) == WsParseStatus::error);                                    // lone continuation
    WebSocketFrameParser e;
    CHECK(feed(e, maskedFrame(0x09, "x"), used) == WsParseStatus::error);                                    // fragmented ping
    WebSocketFrameParser f;
    CHECK(feed(f, maskedFrame(0x89, std::string(126, 'x')), used) == WsParseStatus::error);                  // long control
    WebSocketFrameParser g;
    CHECK(feed(g, maskedFrame(0x01, "a") + maskedFrame(0x01, "b"), used) == WsParseStatus::needMore || g.errorCode() == 1002);
    WebSocketFrameParser h;
    CHECK(feed(h, maskedFrame(0x81, "\xff\xfe"), used) == WsParseStatus::error && h.errorCode() == 1007);    // bad UTF-8
    WebSocketFrameParser small(10);
    CHECK(feed(small, maskedFrame(0x82, std::string(11, 'x')), used) == WsParseStatus::error && small.errorCode() == 1009);
    WebSocketFrameParser closeBad;
    CHECK(feed(closeBad, maskedFrame(0x88, std::string("\x03\xed", 2)), used) == WsParseStatus::error);       // code 1005 on the wire
    WebSocketFrameParser closeOk;
    CHECK(feed(closeOk, maskedFrame(0x88, std::string("\x03\xe8", 2) + "bye"), used) == WsParseStatus::message);
    CHECK(closeOk.message().opcode == WsOpcode::close);

    // builder
    CHECK(WebSocketFrameBuilder::build(WsOpcode::text, "hi") == std::string("\x81\x02hi", 4));
    std::string big = WebSocketFrameBuilder::header(WsOpcode::binary, 70000);
    CHECK(big.size() == 10 && static_cast<uint8_t>(big[1]) == 127);
    std::string mid = WebSocketFrameBuilder::header(WsOpcode::binary, 300);
    CHECK(mid.size() == 4 && static_cast<uint8_t>(mid[1]) == 126 && static_cast<uint8_t>(mid[2]) == 1 && static_cast<uint8_t>(mid[3]) == 44);
    CHECK(WebSocketFrameBuilder::buildClose(1000, "ok") == std::string("\x88\x04\x03\xe8ok", 6));
}

static void writeFile(const std::string& path, const std::string& content)
{
    std::ofstream(path, std::ios::binary) << content;
}

static void testStaticFiles()
{
    std::printf("static files\n");
    char tmpl[] = "/tmp/rizweb_test_XXXXXX";
    std::string base = mkdtemp(tmpl);
    std::string root = base + "/www";
    mkdir(root.c_str(), 0755);
    mkdir((root + "/docs").c_str(), 0755);
    mkdir((root + "/withindex").c_str(), 0755);
    writeFile(root + "/hello.txt", "0123456789");
    writeFile(root + "/page.html", std::string(2000, 'h'));
    writeFile(root + "/.secret", "nope");
    writeFile(root + "/docs/<img src=x onerror=alert(1)>.txt", "x");
    writeFile(root + "/docs/a:b.txt", "x");
    writeFile(root + "/withindex/index.html", "<p>idx</p>");
    writeFile(base + "/outside.txt", "outside");
    symlink((base + "/outside.txt").c_str(), (root + "/link.txt").c_str());
    symlink(base.c_str(), (root + "/dirlink").c_str());

    StaticFileOptions options;
    options.rootDirectory = root;
    options.directoryListing = true;
    options.cacheSecondsByExtension["txt"] = 600;
    StaticFileHandler handler(options, nullptr);

    auto fetch = [&](const std::string& path, const std::string& extra = "", HttpMethod method = HttpMethod::get) {
        HttpRequestParser p;
        std::string req = std::string(method == HttpMethod::head ? "HEAD " : "GET ") + path + " HTTP/1.1\r\nHost: a\r\n" + extra + "\r\n";
        size_t used;
        p.feed(req.data(), req.size(), used);
        HttpResponse res;
        handler.serve(p.request(), res, p.request().path);
        return res;
    };

    HttpResponse r = fetch("/hello.txt");
    CHECK(r.status() == 200 && r.hasFileBody() && r.bodyLength() == 10);
    CHECK(*r.header("Content-Type") == "text/plain; charset=utf-8");
    CHECK(*r.header("Cache-Control") == "public, max-age=600");
    CHECK(*r.header("X-Content-Type-Options") == "nosniff");
    std::string etag = *r.header("ETag");

    r = fetch("/hello.txt", "If-None-Match: " + etag + "\r\n");
    CHECK(r.status() == 304 && !r.hasFileBody());
    r = fetch("/hello.txt", "If-None-Match: \"other\"\r\n");
    CHECK(r.status() == 200);
    r = fetch("/hello.txt", "If-Modified-Since: " + *fetch("/hello.txt").header("Last-Modified") + "\r\n");
    CHECK(r.status() == 304);

    r = fetch("/hello.txt", "Range: bytes=2-4\r\n");
    CHECK(r.status() == 206 && r.bodyLength() == 3 && r.fileOffset() == 2);
    CHECK(*r.header("Content-Range") == "bytes 2-4/10");
    r = fetch("/hello.txt", "Range: bytes=-3\r\n");
    CHECK(r.status() == 206 && r.fileOffset() == 7 && r.bodyLength() == 3);
    r = fetch("/hello.txt", "Range: bytes=8-\r\n");
    CHECK(r.status() == 206 && r.fileOffset() == 8 && r.bodyLength() == 2);
    r = fetch("/hello.txt", "Range: bytes=5-999\r\n");
    CHECK(r.status() == 206 && r.bodyLength() == 5);
    r = fetch("/hello.txt", "Range: bytes=50-60\r\n");
    CHECK(r.status() == 416 && *r.header("Content-Range") == "bytes */10");
    r = fetch("/hello.txt", "Range: bytes=0-1,3-4\r\n");
    CHECK(r.status() == 200 && r.bodyLength() == 10);
    r = fetch("/hello.txt", "Range: bytes=abc\r\n");
    CHECK(r.status() == 200);

    // path safety
    r = fetch("/.secret");
    CHECK(r.status() == 403);
    r = fetch("/link.txt");               // symlink leaving the root
    CHECK(r.status() == 404 || r.status() == 403);
    CHECK(!r.hasFileBody());
    r = fetch("/dirlink/outside.txt");
    CHECK(r.status() == 404 || r.status() == 403);
    r = fetch("/missing");
    CHECK(r.status() == 404);

    // directories
    r = fetch("/docs");
    CHECK(r.status() == 301 && *r.header("Location") == "/docs/");
    r = fetch("/withindex/");
    CHECK(r.status() == 200 && r.hasFileBody() && r.bodyLength() == 10);
    r = fetch("/docs/");
    CHECK(r.status() == 200 && r.body().find("onerror=alert(1)>") == std::string::npos);
    CHECK(r.body().find("&lt;img src=x onerror=alert(1)&gt;.txt") != std::string::npos);
    CHECK(r.body().find("href=\"./a%3Ab.txt\"") != std::string::npos);
    CHECK(r.body().find(".secret") == std::string::npos);
    r = fetch("/hello.txt/");              // file addressed as directory
    CHECK(r.status() == 404 || r.status() == 403);

    // methods
    HttpRequestParser p;
    std::string post = "POST /hello.txt HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\n";
    size_t used;
    p.feed(post.data(), post.size(), used);
    HttpResponse res;
    handler.serve(p.request(), res, "/hello.txt");
    CHECK(res.status() == 405 && *res.header("Allow") == "GET, HEAD");

    // gzip (only when built with zlib)
    if (GzipCache::isAvailable()) {
        r = fetch("/page.html", "Accept-Encoding: gzip, deflate\r\n");
        CHECK(r.status() == 200 && r.header("Content-Encoding") && *r.header("Content-Encoding") == "gzip");
        CHECK(r.sharedBody() && r.sharedBody()->size() < 2000);
        CHECK(static_cast<uint8_t>((*r.sharedBody())[0]) == 0x1f && static_cast<uint8_t>((*r.sharedBody())[1]) == 0x8b);
        CHECK(*r.header("Vary") == "Accept-Encoding");
        r = fetch("/page.html", "Accept-Encoding: gzip;q=0\r\n");
        CHECK(!r.header("Content-Encoding") && r.hasFileBody());
        r = fetch("/page.html");
        CHECK(!r.header("Content-Encoding"));
        r = fetch("/page.html", "Accept-Encoding: gzip\r\nRange: bytes=0-9\r\n");
        CHECK(r.status() == 206 && !r.header("Content-Encoding"));
    }

    std::string cleanup = "rm -rf " + base;
    if (std::system(cleanup.c_str()) != 0)
        std::printf("  (cleanup failed)\n");
}

static void testAuthAndOrigin()
{
    std::printf("auth guard / same origin\n");
    auto sessions = std::make_shared<SessionStore>(std::chrono::seconds(60), 4);
    AuthGuard guard(sessions, "/auth/");
    guard.protect("/admin/");
    guard.allow("/admin/public/");
    Middleware mw = guard.middleware();

    auto make = [](const std::string& text) {
        auto parser = std::make_shared<HttpRequestParser>();
        size_t used;
        parser->feed(text.data(), text.size(), used);
        return parser;
    };

    auto anon = make("GET /admin/x HTTP/1.1\r\nHost: a\r\n\r\n");
    HttpResponse res;
    CHECK(!mw(anon->request(), res) && res.status() == 302 && *res.header("Location") == "/auth/");
    HttpResponse res2;
    CHECK(mw(make("GET /home HTTP/1.1\r\nHost: a\r\n\r\n")->request(), res2));
    HttpResponse res3;
    CHECK(mw(make("GET /admin/public/a HTTP/1.1\r\nHost: a\r\n\r\n")->request(), res3));
    HttpResponse res4;
    CHECK(!mw(make("POST /admin/x HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\n")->request(), res4) && res4.status() == 401);

    HttpResponse loginRes;
    auto loginReq = make("POST /auth/ HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\n");
    CHECK(guard.login(loginReq->request(), loginRes));
    std::string setCookie = *loginRes.header("Set-Cookie");
    std::string token = setCookie.substr(setCookie.find('=') + 1, 48);
    CHECK(token.size() == 48);

    HttpResponse res5;
    CHECK(mw(make("GET /admin/x HTTP/1.1\r\nHost: a\r\nCookie: rizweb_sid=" + token + "\r\n\r\n")->request(), res5));
    HttpResponse res6;
    CHECK(!mw(make("GET /admin/x HTTP/1.1\r\nHost: a\r\nCookie: rizweb_sid=" + std::string(48, '0') + "\r\n\r\n")->request(), res6));

    auto logoutReq = make("GET /admin/x HTTP/1.1\r\nHost: a\r\nCookie: rizweb_sid=" + token + "\r\n\r\n");
    HttpResponse out;
    guard.logout(logoutReq->request(), out);
    CHECK(out.header("Set-Cookie") && out.header("Set-Cookie")->find("Max-Age=0") != std::string::npos);
    HttpResponse res7;
    CHECK(!mw(logoutReq->request(), res7));

    // session limit
    SessionStore tiny(std::chrono::seconds(60), 2);
    CHECK(!tiny.create().empty() && !tiny.create().empty() && tiny.create().empty());
    SessionStore expired(std::chrono::seconds(0), 10);
    std::string t = expired.create();
    CHECK(!t.empty() && !expired.validate(t));

    Middleware sameOrigin = makeSameOriginGuard();
    HttpResponse o1;
    CHECK(sameOrigin(make("POST /x HTTP/1.1\r\nHost: site.com\r\nOrigin: https://site.com\r\nContent-Length: 0\r\n\r\n")->request(), o1));
    HttpResponse o2;
    CHECK(!sameOrigin(make("POST /x HTTP/1.1\r\nHost: site.com\r\nOrigin: https://evil.com\r\nContent-Length: 0\r\n\r\n")->request(), o2) && o2.status() == 403);
    HttpResponse o3;
    CHECK(sameOrigin(make("GET /x HTTP/1.1\r\nHost: site.com\r\nOrigin: https://evil.com\r\n\r\n")->request(), o3));
    HttpResponse o4;
    CHECK(sameOrigin(make("POST /x HTTP/1.1\r\nHost: site.com\r\nContent-Length: 0\r\n\r\n")->request(), o4));
}

static void testIni()
{
    std::printf("ini\n");
    IniFile ini;
    ini.loadFromText("\xEF\xBB\xBF; comment\n[httpServer]\nPort = 8081\r\nListenIP=127.0.0.1\nIsDirectoryBrowsing = true\nName=\"quoted value\"\n[sslSetting]\nSSLPort=bad\n");
    CHECK(ini.getInt("httpServer", "Port", 1) == 8081);
    CHECK(ini.getString("httpServer", "ListenIP") == "127.0.0.1");
    CHECK(ini.getBool("httpServer", "IsDirectoryBrowsing", false));
    CHECK(ini.getString("httpServer", "Name") == "quoted value");
    CHECK(ini.getInt("sslSetting", "SSLPort", 443) == 443);
    CHECK(ini.getInt("none", "x", 7) == 7);
}

int main()
{
    testUrlCodec();
    testHashing();
    testParserBasics();
    testParserSlicing();
    testParserChunked();
    testParserRejects();
    testExpectContinue();
    testRequestHelpers();
    testResponse();
    testRouter();
    testWebSocketFrames();
    testStaticFiles();
    testAuthAndOrigin();
    testIni();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
