#include "HTTP/clsCompressionTypeList.h"
#include "HTTP/clsHTTPLimiter.h"
#include "HTTP/clsHTTPRouter.h"
#include "HTTP/clsHTTPSecurity.h"
#include "HTTP/clsHTTPCookie.h"
#include "HTTP/clsHTTPWSocketParser.h"
#include "HTTP/clsMIMETypes.h"
#include "Utils/clsCrypto.h"
#include "Utils/clsFileUtils.h"
#include "Utils/clsIniFile.h"
#include "Utils/clsStringUtils.h"
#include "testFramework.h"

using namespace rizweb;

TEST(urlDecoding)
{
    std::string out;
    CHECK(StringUtils::urlDecode("a%20b+c", out, true));
    CHECK_EQ(out, std::string("a b c"));
    CHECK(StringUtils::urlDecode("a+b", out, false));
    CHECK_EQ(out, std::string("a+b"));
    CHECK(!StringUtils::urlDecode("%", out, false));
    CHECK(!StringUtils::urlDecode("abc%4", out, false));
    CHECK(!StringUtils::urlDecode("%00", out, false));
    CHECK_EQ(StringUtils::urlEncode("a b/<c>", true), std::string("a%20b/%3Cc%3E"));
    CHECK_EQ(StringUtils::htmlEscape("<a href=\"x\">&'"), std::string("&lt;a href=&quot;x&quot;&gt;&amp;&#39;"));
}

TEST(cryptoVectors)
{
    std::string digest = Crypto::sha1("abc", 3);
    CHECK_EQ(StringUtils::toHex(reinterpret_cast<const unsigned char *>(digest.data()), digest.size()), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    CHECK_EQ(Crypto::base64Encode("Man", 3), std::string("TWFu"));
    CHECK_EQ(Crypto::base64Encode("Ma", 2), std::string("TWE="));
    // RFC 6455 section 1.3 handshake example
    std::string accept = std::string("dGhlIHNhbXBsZSBub25jZQ==") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string hash = Crypto::sha1(accept.data(), accept.size());
    CHECK_EQ(Crypto::base64Encode(hash.data(), hash.size()), std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    CHECK_EQ(Crypto::randomHex(16).size(), size_t(32));
    CHECK(Crypto::randomHex(16) != Crypto::randomHex(16));
    CHECK(Crypto::secureEquals("abc", "abc"));
    CHECK(!Crypto::secureEquals("abc", "abd"));
}

TEST(httpDates)
{
    CHECK_EQ(FileUtils::formatHttpDate(784111777), std::string("Sun, 06 Nov 1994 08:49:37 GMT"));
    time_t parsed = 0;
    CHECK(FileUtils::parseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT", parsed));
    CHECK_EQ(static_cast<long long>(parsed), 784111777LL);
    CHECK(!FileUtils::parseHttpDate("yesterday", parsed));
}

TEST(iniFile)
{
    std::string path = tempPath("rizweb-test.ini");
    FileUtils::writeFile(path, "# c\n[a]\nx = 5\nflag=true\n name = hello world \n");
    IniFile ini;
    CHECK(ini.load(path));
    CHECK_EQ(ini.getLong("a", "x", 0), 5L);
    CHECK(ini.getBool("a", "flag", false));
    CHECK_EQ(ini.getString("a", "name"), std::string("hello world"));
    CHECK_EQ(ini.getLong("a", "missing", 9), 9L);
}

TEST(mimeAndCompressionLists)
{
    MIMETypes mime;
    CHECK_EQ(mime.typeForPath("/a/b/site.CSS"), std::string("text/css; charset=utf-8"));
    CHECK_EQ(mime.typeForPath("/a.unknown"), std::string("application/octet-stream"));
    CHECK_EQ(mime.typeForPath("/dir.d/file"), std::string("application/octet-stream"));
    CompressionTypeList list;
    list.add("html");
    CHECK(list.needsCompression("/x.html"));
    CHECK(!list.needsCompression("/x.png"));
}

TEST(cookies)
{
    std::string header = "a=1; sID=abc123; b=2";
    CHECK_EQ(HTTPCookie::value(&header, "sID"), std::string("abc123"));
    CHECK_EQ(HTTPCookie::value(&header, "zzz"), std::string(""));
    CookieOptions options;
    options.secure = true;
    options.maxAgeSeconds = 60;
    CHECK_EQ(HTTPCookie::buildSetCookie("sID", "v", options), std::string("sID=v; Max-Age=60; Path=/; HttpOnly; Secure; SameSite=Lax"));
    CHECK_EQ(HTTPCookie::buildSetCookie("a", "bad\r\nSet-Cookie: x=1", options), std::string(""));
}

TEST(sessionsAndSecurity)
{
    HTTPSession store(2, 60);
    std::string a = store.create();
    std::string b = store.create();
    CHECK(!a.empty() && !b.empty() && a != b);
    CHECK(store.create().empty());          // full
    CHECK(store.isValid(a));
    CHECK(store.consume(a));
    CHECK(!store.isValid(a));
    CHECK(!store.consume(a));
    CHECK(!store.isValid("nonsense"));

    HTTPSecurity security;
    security.setSecurePath("panel");
    CHECK_EQ(security.securePath(), std::string("/panel/"));
    CHECK(security.isSecurePath("/panel/x"));
    CHECK(security.isSecurePath("/panel"));
    CHECK(!security.isSecurePath("/panelx"));
    CHECK(!security.isSecurePath("/other"));
}

TEST(routerMatching)
{
    HTTPRouter router;
    int hit = 0;
    router.add(HTTPMethod::Get, "/users", [&](HTTPClient &, HTTPRequest &, HTTPResponse &) { hit = 1; });
    router.add(HTTPMethod::Get, "/users/:id", [&](HTTPClient &, HTTPRequest &, HTTPResponse &) { hit = 2; });
    router.add(HTTPMethod::Get, "/users/me", [&](HTTPClient &, HTTPRequest &, HTTPResponse &) { hit = 3; });
    router.add(HTTPMethod::Post, "/users/:id/avatar", [&](HTTPClient &, HTTPRequest &, HTTPResponse &) { hit = 4; }).bodyLimit = 777;
    router.add(HTTPMethod::Get, "/files/*", [&](HTTPClient &, HTTPRequest &, HTTPResponse &) { hit = 5; });
    (void)hit;

    const HTTPRouter::Route *route = nullptr;
    std::vector<std::pair<std::string, std::string>> params;
    std::string allow;

    CHECK(router.match(HTTPMethod::Get, "/users", route, params, allow) == HTTPRouter::Result::Found);
    CHECK_EQ(route->pattern, std::string("/users"));
    CHECK(router.match(HTTPMethod::Get, "/users/", route, params, allow) == HTTPRouter::Result::Found);
    CHECK(router.match(HTTPMethod::Get, "/users/42", route, params, allow) == HTTPRouter::Result::Found);
    CHECK_EQ(route->pattern, std::string("/users/:id"));
    CHECK_EQ(params.size(), size_t(1));
    CHECK_EQ(params[0].second, std::string("42"));
    CHECK(router.match(HTTPMethod::Get, "/users/me", route, params, allow) == HTTPRouter::Result::Found);
    CHECK_EQ(route->pattern, std::string("/users/me"));       // exact beats ":id"
    CHECK(router.match(HTTPMethod::Head, "/users/42", route, params, allow) == HTTPRouter::Result::Found);   // HEAD -> GET
    CHECK(router.match(HTTPMethod::Get, "/files/a/b/c.txt", route, params, allow) == HTTPRouter::Result::Found);
    CHECK_EQ(params[0].first, std::string("*"));
    CHECK_EQ(params[0].second, std::string("a/b/c.txt"));
    CHECK(router.match(HTTPMethod::Delete, "/users/42", route, params, allow) == HTTPRouter::Result::MethodNotAllowed);
    CHECK_EQ(allow, std::string("GET, HEAD"));
    CHECK(router.match(HTTPMethod::Get, "/nothing", route, params, allow) == HTTPRouter::Result::NotFound);
    CHECK(router.match(HTTPMethod::Get, "/users/1/extra", route, params, allow) == HTTPRouter::Result::NotFound);
    CHECK_EQ(router.bodyLimitFor(HTTPMethod::Post, "/users/9/avatar"), size_t(777));

    HTTPRequest request;
    request.setParams({{"id", "7"}});
    CHECK_EQ(request.param("id"), std::string("7"));
    CHECK_EQ(request.param("none"), std::string(""));
}

TEST(limiter)
{
    HTTPLimiter limiter;
    CHECK(!limiter.isEnabled());
    limiter.setMaxConnectionsPerIp(2);
    limiter.setMaxRequestsPerSecond(3);
    CHECK(limiter.isEnabled());
    CHECK(limiter.acquireConnection("1.1.1.1"));
    CHECK(limiter.acquireConnection("1.1.1.1"));
    CHECK(!limiter.acquireConnection("1.1.1.1"));
    CHECK(limiter.acquireConnection("2.2.2.2"));
    limiter.releaseConnection("1.1.1.1");
    CHECK(limiter.acquireConnection("1.1.1.1"));

    uint32_t retry = 0;
    int allowed = 0;
    for (int i = 0; i < 10; i++)
        allowed += limiter.allowRequest("9.9.9.9", retry) ? 1 : 0;
    CHECK(allowed >= 3 && allowed <= 6);    // 3 per window; a second boundary may let a few more through
    CHECK_EQ(retry, 1u);
}

namespace {
struct FakeSocket : HTTPWSocketParser
{
    std::vector<std::string> sent;
    std::vector<std::string> messages;
    bool closed = false;
    void webSocketSendRaw(const std::string &bytes) override { sent.push_back(bytes); }
    void webSocketCloseTransport() override { closed = true; }
    void webSocketMessage(bool, const char *payload, size_t length) override { messages.emplace_back(payload, length); }
};

std::string maskedFrame(uint8_t opcode, const std::string &payload, bool fin = true)
{
    const uint8_t key[4] = {1, 2, 3, 4};
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80 : 0) | opcode));
    if (payload.size() < 126) {
        out.push_back(static_cast<char>(0x80 | payload.size()));
    } else {
        out.push_back(static_cast<char>(0x80 | 126));
        out.push_back(static_cast<char>(payload.size() >> 8));
        out.push_back(static_cast<char>(payload.size() & 0xFF));
    }
    out.append(reinterpret_cast<const char *>(key), 4);
    for (size_t i = 0; i < payload.size(); i++)
        out.push_back(static_cast<char>(payload[i] ^ key[i & 3]));
    return out;
}
} // namespace

TEST(webSocketParser)
{
    FakeSocket socket;
    std::string frame = maskedFrame(WS_OPCODE_TEXT, "hello");
    socket.webSocketParse(frame.data(), 3);                   // split delivery
    CHECK(socket.messages.empty());
    socket.webSocketParse(frame.data() + 3, frame.size() - 3);
    CHECK_EQ(socket.messages.size(), size_t(1));
    CHECK_EQ(socket.messages[0], std::string("hello"));

    std::string fragments = maskedFrame(WS_OPCODE_TEXT, "ab", false) + maskedFrame(WS_OPCODE_CONTINUATION, "cd");
    socket.webSocketParse(fragments.data(), fragments.size());
    CHECK_EQ(socket.messages.back(), std::string("abcd"));

    std::string ping = maskedFrame(WS_OPCODE_PING, "x");
    socket.webSocketParse(ping.data(), ping.size());
    CHECK_EQ(socket.sent.back(), std::string("\x8a\x01x", 3));

    std::string close = maskedFrame(WS_OPCODE_CLOSE, std::string("\x03\xe8", 2));
    socket.webSocketParse(close.data(), close.size());
    CHECK(socket.closed);
}

TEST(webSocketRejectsBadFrames)
{
    FakeSocket unmasked;
    const char raw[] = {static_cast<char>(0x81), 0x01, 'x'};         // client frames must be masked
    unmasked.webSocketParse(raw, sizeof(raw));
    CHECK(unmasked.closed);
    CHECK(unmasked.messages.empty());

    FakeSocket tooBig;
    tooBig.setMaxMessageBytes(10);
    std::string frame = maskedFrame(WS_OPCODE_TEXT, std::string(50, 'a'));
    tooBig.webSocketParse(frame.data(), frame.size());
    CHECK(tooBig.closed);
    CHECK_EQ(tooBig.sent.back().substr(0, 4), std::string("\x88\x02\x03\xf1", 4));   // 1009
}
