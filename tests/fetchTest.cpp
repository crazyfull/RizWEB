// Integration tests: RizWEB's HTTP client against RizWEB's own server on localhost.
#include "Client/clsHTTPFetcher.h"
#include "Client/clsHTTPUrl.h"
#include "Utils/clsFileUtils.h"
#include "clsWEBMaster.h"
#include "testFramework.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace rizweb;

namespace {

constexpr uint16_t kHttpPort = 18741;
constexpr uint16_t kHttpsPort = 18742;

struct TestServer
{
    WEBMaster web;
    bool started = false;

    TestServer()
    {
        web.settings().httpPort = kHttpPort;
        web.settings().sslPort = kHttpsPort;
        web.settings().selfSignedTls = true;
        web.settings().locatedRoot = tempPath("");

        web.get("/hello", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.setContentType("text/plain"); res.write("hello client"); res.flush(200); });
        web.get("/big", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.setContentType("application/octet-stream"); res.write(std::string(300000, 'x')); res.flush(200); });
        web.post("/echo", [](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {
            res.setContentType("text/plain");
            res.setHeaderField("X-Method", req.methodName());
            res.write(req.body());
            res.flush(200);
        });
        web.get("/redirect", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.redirect("/hello", 302); });
        web.get("/loop", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.redirect("/loop", 302); });
        web.post("/seeother", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.redirect("/hello", 303); });
        web.get("/echoauth", [](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {
            const std::string *auth = req.headerField("authorization");
            res.write(auth ? *auth : "none");
            res.flush(200);
        });
        web.get("/cross", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.redirect("http://localhost:" + std::to_string(kHttpPort) + "/echoauth", 302); });
        started = web.start();
    }
    ~TestServer() { web.stop(); }
};

TestServer &server()
{
    static TestServer instance;
    return instance;
}

std::string url(const std::string &path) { return "http://127.0.0.1:" + std::to_string(kHttpPort) + path; }

} // namespace

TEST(urlParsing)
{
    HTTPUrl u;
    std::string error;
    CHECK(HTTPUrl::parse("https://Example.com:8443/a/b?x=1#frag", u, error));
    CHECK(u.https && u.host == "example.com" && u.port == 8443 && u.target == "/a/b?x=1");
    CHECK(HTTPUrl::parse("http://[::1]:80", u, error));
    CHECK(!u.https && u.host == "::1" && u.target == "/" && u.hostHeader() == "[::1]");
    CHECK(HTTPUrl::parse("http://h?q=1", u, error) && u.target == "/?q=1");
    CHECK(!HTTPUrl::parse("ftp://x", u, error));
    CHECK(!HTTPUrl::parse("http://user:pw@host/", u, error));
    CHECK(!HTTPUrl::parse("http://host:99999/", u, error));
    CHECK(!HTTPUrl::parse("http://ho st/", u, error));

    HTTPUrl base, out;
    HTTPUrl::parse("http://h.com/a/b/c?z=1", base, error);
    CHECK(base.resolve("d", out, error) && out.target == "/a/b/d");
    CHECK(base.resolve("../x?y=2", out, error) && out.target == "/a/x?y=2");
    CHECK(base.resolve("/root", out, error) && out.target == "/root");
    CHECK(base.resolve("//other.com/p", out, error) && out.host == "other.com" && !out.https);
    CHECK(base.resolve("https://s.com/q", out, error) && out.https && out.host == "s.com");
    CHECK(!base.resolve("javascript:alert(1)", out, error));
}

TEST(clientBlocking)
{
    CHECK(server().started);
    HTTPFetcher fetcher;
    HTTPFetchResponse r = fetcher.get(url("/hello"));
    CHECK(r.ok());
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, std::string("hello client"));
    CHECK(r.header("CONTENT-TYPE") != nullptr);

    r = fetcher.post(url("/echo"), "payload=1");
    CHECK_EQ(r.body, std::string("payload=1"));
    CHECK_EQ(*r.header("x-method"), std::string("POST"));

    r = fetcher.get(url("/big"));
    CHECK_EQ(r.body.size(), size_t(300000));

    r = fetcher.get(url("/missing-route-404"));
    CHECK(r.completed() && !r.ok());
    CHECK_EQ(r.status, 404);
}

TEST(clientRedirects)
{
    HTTPFetcher fetcher;
    HTTPFetchResponse r = fetcher.get(url("/redirect"));
    CHECK_EQ(r.body, std::string("hello client"));
    CHECK_EQ(r.redirects, 1);
    CHECK_EQ(r.finalUrl, url("/hello"));

    HTTPFetchRequest noFollow;
    noFollow.url = url("/redirect");
    noFollow.followRedirects = false;
    r = fetcher.fetch(noFollow);
    CHECK_EQ(r.status, 302);
    CHECK(r.header("location") != nullptr);

    r = fetcher.get(url("/loop"));
    CHECK(!r.error.empty());
    CHECK(r.error.find("too many redirects") != std::string::npos);

    HTTPFetchRequest seeOther;                      // 303 turns POST into GET
    seeOther.method = "POST";
    seeOther.url = url("/seeother");
    seeOther.body = "x=1";
    r = fetcher.fetch(seeOther);
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, std::string("hello client"));

    HTTPFetchRequest credentials;                   // Authorization is not sent to another origin (127.0.0.1 -> localhost)
    credentials.url = url("/cross");
    credentials.header("Authorization", "Bearer secret");
    r = fetcher.fetch(credentials);
    CHECK_EQ(r.body, std::string("none"));
    HTTPFetchRequest sameOrigin;
    sameOrigin.url = url("/echoauth");
    sameOrigin.header("Authorization", "Bearer secret");
    CHECK_EQ(fetcher.fetch(sameOrigin).body, std::string("Bearer secret"));
}

TEST(clientAsync)
{
    HTTPFetcher fetcher;
    std::promise<HTTPFetchResponse> promise;
    HTTPFetchRequest request;
    request.url = url("/hello");
    fetcher.fetchAsync(request, [&](HTTPFetchResponse &r) { promise.set_value(r); });
    auto future = promise.get_future();
    CHECK(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK_EQ(future.get().body, std::string("hello client"));

    // many in parallel
    std::atomic<int> done{0}, good{0};
    for (int i = 0; i < 50; i++) {
        HTTPFetchRequest r;
        r.url = url("/hello");
        fetcher.fetchAsync(r, [&](HTTPFetchResponse &resp) { good += resp.ok() ? 1 : 0; done++; });
    }
    for (int i = 0; i < 200 && done < 50; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_EQ(done.load(), 50);
    CHECK_EQ(good.load(), 50);

    // fetch() inside a callback is refused instead of deadlocking
    std::promise<std::string> nested;
    fetcher.fetchAsync(request, [&](HTTPFetchResponse &) { nested.set_value(fetcher.get(url("/hello")).error); });
    auto nestedFuture = nested.get_future();
    CHECK(nestedFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(nestedFuture.get().find("inside a fetch callback") != std::string::npos);
}

TEST(clientSharedHost)
{
    // the client runs on the reactor threads of the server's host
    NetHost host
#if defined(RIZWEB_USE_NETRECTOR)
        (1024, 2)
#endif
        ;
    WEBMaster web(host);
    web.settings().httpPort = kHttpPort + 10;
    web.settings().sslPort = 0;
    HTTPFetcher fetcher(host);
    web.get("/a", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.write("from a"); res.flush(200); });
    std::promise<std::string> result;
    web.get("/proxy", [&](HTTPClient &client, HTTPRequest &, HTTPResponse &res) {
        // async call from a handler, answer when the upstream replies
        HTTPFetchRequest r;
        r.url = "http://127.0.0.1:" + std::to_string(kHttpPort + 10) + "/a";
        uint64_t id = client.id();
        (void)id;
        fetcher.fetchAsync(r, [&result](HTTPFetchResponse &up) { result.set_value(up.body); });
        res.write("started");
        res.flush(200);
    });
    CHECK(web.start());
#if defined(RIZWEB_USE_NETRECTOR)
    std::thread runner([&]() { host.start(); });
    runner.join();
#else
    std::thread runner([&]() { host.run(); });
#endif
    HTTPFetcher outside;
    CHECK_EQ(outside.get("http://127.0.0.1:" + std::to_string(kHttpPort + 10) + "/proxy").body, std::string("started"));
    auto future = result.get_future();
    CHECK(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK_EQ(future.get(), std::string("from a"));
    web.stop();
#if !defined(RIZWEB_USE_NETRECTOR)
    host.stop();
    runner.join();
#endif
}

TEST(clientDownloadAndStreaming)
{
    HTTPFetcher fetcher;
    std::string path = tempPath("rizweb-download-test.bin");
    std::remove(path.c_str());
    HTTPFetchRequest request;
    request.url = url("/big");
    request.downloadPath = path;
    uint64_t lastProgress = 0;
    request.onProgress = [&](uint64_t received, uint64_t) { lastProgress = received; };
    HTTPFetchResponse r = fetcher.fetch(request);
    CHECK(r.ok());
    CHECK(r.body.empty());
    CHECK_EQ(r.bodyBytes, uint64_t(300000));
    CHECK_EQ(lastProgress, uint64_t(300000));
    CHECK_EQ(FileUtils::stat(path).size, 300000ull);
    r = fetcher.fetch(request);                      // exists already -> refused, nothing overwritten
    CHECK(!r.error.empty());
    std::remove(path.c_str());

    size_t seen = 0;
    HTTPFetchRequest streaming;
    streaming.url = url("/big");
    streaming.onBody = [&](const char *, size_t n) { seen += n; return true; };
    r = fetcher.fetch(streaming);
    CHECK(r.ok() && r.body.empty());
    CHECK_EQ(seen, size_t(300000));

    HTTPFetchRequest limited;
    limited.url = url("/big");
    limited.maxResponseBytes = 1000;
    CHECK(fetcher.fetch(limited).error.find("too large") != std::string::npos);

    HTTPFetchRequest aborted;
    aborted.url = url("/big");
    aborted.onBody = [](const char *, size_t) { return false; };
    CHECK(!fetcher.fetch(aborted).error.empty());
}

TEST(clientErrors)
{
    HTTPFetcher fetcher;
    HTTPFetchResponse r = fetcher.get("http://127.0.0.1:1/");
    CHECK(r.error.find("cannot connect") != std::string::npos);
    r = fetcher.get("ftp://example.com/");
    CHECK(!r.error.empty());
    HTTPFetchRequest injected;
    injected.url = url("/hello");
    injected.header("X-Evil", "a\r\nInjected: 1");
    CHECK(fetcher.fetch(injected).error.find("invalid request header") != std::string::npos);
    HTTPFetchRequest blocked;
    blocked.url = url("/hello");
    blocked.blockPrivateNetworks = true;
    CHECK(!fetcher.fetch(blocked).completed());       // 127.0.0.1 refused
}

#if defined(RIZWEB_HAS_TLS)
TEST(clientHttps)
{
    HTTPFetcher fetcher;
    HTTPFetchRequest insecure;
    insecure.url = "https://127.0.0.1:" + std::to_string(kHttpsPort) + "/hello";
    insecure.verifyTLS = false;
    HTTPFetchResponse r = fetcher.fetch(insecure);
    CHECK(r.ok());
    CHECK_EQ(r.body, std::string("hello client"));

    HTTPFetchRequest verifying = insecure;            // self-signed certificate must be rejected
    verifying.verifyTLS = true;
    r = fetcher.fetch(verifying);
    CHECK(!r.completed());
    CHECK(r.error.find("certificate verification") != std::string::npos);
}
#endif
