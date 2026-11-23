#include "HTTP/clsHTTPRequest.h"
#include "testFramework.h"

using namespace rizweb;

static HTTPParseResult parseText(HTTPRequest &request, const std::string &text, size_t limit = 1024 * 1024)
{
    return request.parse(text.data(), text.size(), limit);
}

TEST(parserSimpleGet)
{
    HTTPRequest request;
    std::string text = "GET /index.html?a=1&b=hello%20world HTTP/1.1\r\nHost: Example.com:8080\r\nUser-Agent: t\r\n\r\n";
    CHECK(parseText(request, text) == HTTPParseResult::Completed);
    CHECK(request.method() == HTTPMethod::Get);
    CHECK_EQ(request.urlPath(), std::string("/index.html"));
    CHECK_EQ(request.host(), std::string("example.com"));
    CHECK_EQ(*request.queryField("b"), std::string("hello world"));
    CHECK(request.keepAlive());
    CHECK_EQ(request.consumedBytes(), text.size());
}

TEST(parserIncomplete)
{
    HTTPRequest request;
    CHECK(parseText(request, "GET / HTTP/1.1\r\nHost: x\r\n") == HTTPParseResult::Incomplete);
}

TEST(parserKeepAliveRules)
{
    HTTPRequest a, b, c;
    CHECK(parseText(a, "GET / HTTP/1.0\r\n\r\n") == HTTPParseResult::Completed);
    CHECK(!a.keepAlive());
    CHECK(parseText(b, "GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n") == HTTPParseResult::Completed);
    CHECK(b.keepAlive());
    CHECK(parseText(c, "GET / HTTP/1.1\r\nConnection: close\r\n\r\n") == HTTPParseResult::Completed);
    CHECK(!c.keepAlive());
}

TEST(parserRejectsTraversal)
{
    for (const char *path : {"/../etc/passwd", "/a/../../b", "/%2e%2e/x", "/%2E%2E%2Fx", "/a%00b", "/a\\b", "/%zz", "/%4"}) {
        HTTPRequest request;
        std::string text = std::string("GET ") + path + " HTTP/1.1\r\nHost: x\r\n\r\n";
        CHECK(parseText(request, text) == HTTPParseResult::BadRequest);
    }
}

TEST(parserNormalizesPath)
{
    HTTPRequest request;
    CHECK(parseText(request, "GET /a/./b//c/../d/ HTTP/1.1\r\n\r\n") == HTTPParseResult::Completed);
    CHECK_EQ(request.urlPath(), std::string("/a/b/d/"));
}

TEST(parserContentLength)
{
    HTTPRequest request;
    std::string text = "POST /x HTTP/1.1\r\nContent-Length: 5\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\na=b&c";
    CHECK(parseText(request, text) == HTTPParseResult::Completed);
    CHECK_EQ(request.body(), std::string("a=b&c"));
    CHECK_EQ(*request.queryField("a"), std::string("b"));
    HTTPRequest partial;
    CHECK(parseText(partial, text.substr(0, text.size() - 2)) == HTTPParseResult::Incomplete);
}

TEST(parserBodyLimit)
{
    HTTPRequest request;
    CHECK(parseText(request, "POST /x HTTP/1.1\r\nContent-Length: 2000\r\n\r\n", 1000) == HTTPParseResult::BodyTooLarge);

    // per-path limit
    BodyLimitFunction limit = [](HTTPMethod, const std::string &path) { return path == "/upload" ? size_t(5000) : size_t(10); };
    HTTPRequest big;
    std::string text = "POST /upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc";
    CHECK(big.parse(text.data(), text.size(), 10, &limit) == HTTPParseResult::Completed);
    HTTPRequest small;
    text = "POST /other HTTP/1.1\r\nContent-Length: 30\r\n\r\n";
    CHECK(small.parse(text.data(), text.size(), 10, &limit) == HTTPParseResult::BodyTooLarge);
}

TEST(parserChunkedBody)
{
    HTTPRequest request;
    std::string text = "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\nTrailer: v\r\n\r\nGET /next";
    CHECK(parseText(request, text) == HTTPParseResult::Completed);
    CHECK_EQ(request.body(), std::string("Wikipedia"));
    CHECK_EQ(text.substr(request.consumedBytes()), std::string("GET /next"));

    HTTPRequest partial;
    CHECK(parseText(partial, "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWi") == HTTPParseResult::Incomplete);
    HTTPRequest bad;
    CHECK(parseText(bad, "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n") == HTTPParseResult::BadRequest);
    HTTPRequest tooLarge;
    CHECK(parseText(tooLarge, "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n10\r\n0123456789abcdef\r\n0\r\n\r\n", 8) == HTTPParseResult::BodyTooLarge);
}

TEST(parserRejectsSmuggling)
{
    HTTPRequest both;
    CHECK(parseText(both, "POST / HTTP/1.1\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n") == HTTPParseResult::BadRequest);
    HTTPRequest gzip;
    CHECK(parseText(gzip, "POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n") == HTTPParseResult::NotImplemented);
    HTTPRequest spaced;
    CHECK(parseText(spaced, "GET / HTTP/1.1\r\nHost : x\r\n\r\n") == HTTPParseResult::BadRequest);
    HTTPRequest badLength;
    CHECK(parseText(badLength, "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n") == HTTPParseResult::BadRequest);
}

TEST(parserLimits)
{
    HTTPRequest longUrl;
    CHECK(parseText(longUrl, "GET /" + std::string(9000, 'a') + " HTTP/1.1\r\n\r\n") == HTTPParseResult::UriTooLong);
    HTTPRequest hugeHeader;
    CHECK(parseText(hugeHeader, "GET / HTTP/1.1\r\nX: " + std::string(20000, 'a')) == HTTPParseResult::HeaderTooLarge);
    HTTPRequest version;
    CHECK(parseText(version, "GET / HTTP/2.0\r\n\r\n") == HTTPParseResult::VersionNotSupported);
}
