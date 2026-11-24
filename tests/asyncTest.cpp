// Deferred (async) responses: the handler returns at once, the answer is produced later from another thread.
#if !defined(_WIN32)           // uses POSIX sockets directly
#include "clsWEBMaster.h"
#include "testFramework.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace rizweb;

namespace {

constexpr uint16_t kPort = 18751;

struct AsyncServer
{
    WEBMaster web;
    std::atomic<int> reactorBlockedMs { 0 };

    AsyncServer()
    {
        web.settings().httpPort = kPort;
        web.settings().sslPort = 0;
        web.settings().asyncTimeoutSeconds = 2;
        web.get("/slow/:ms", [](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {
            int ms = std::atoi(req.param("ms").c_str());
            std::shared_ptr<AsyncResponse> async = res.defer();
            std::thread([async, ms]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                async->respond([ms](HTTPResponse &r) {
                    r.setContentType("text/plain");
                    r.write("slow ");
                    r.write(static_cast<long long>(ms));
                    r.flush(200);
                });
            }).detach();
        });
        web.get("/fast", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.write("fast"); res.flush(200); });
        web.get("/never", [](HTTPClient &, HTTPRequest &, HTTPResponse &res) { res.defer(); });
        web.start();
    }
    ~AsyncServer() { web.stop(); }
};

AsyncServer &server()
{
    static AsyncServer instance;
    return instance;
}

int connectLocal()
{
    server();
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    timeval tv { 5, 0 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

// reads until |count| responses ("HTTP/1.1 " occurrences) arrived or the peer closed / timed out
std::string readResponses(int fd, int count)
{
    std::string all;
    char buffer[4096];
    for (;;) {
        size_t found = 0;
        for (size_t p = all.find("HTTP/1.1 "); p != std::string::npos; p = all.find("HTTP/1.1 ", p + 1))
            ++found;
        if (static_cast<int>(found) >= count && all.find("\r\n\r\n") != std::string::npos) {
            // wait until the last body is complete: all responses here end with their known text
            if (all.size() > 0 && (all.rfind("fast") != std::string::npos || all.rfind("slow ") != std::string::npos))
                if (all.size() >= 20)
                    break;
        }
        ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        all.append(buffer, static_cast<size_t>(n));
    }
    return all;
}

} // namespace

TEST(asyncResponseAnswersLater)
{
    int fd = connectLocal();
    CHECK(fd >= 0);
    std::string request = "GET /slow/150 HTTP/1.1\r\nHost: x\r\n\r\n";
    auto start = std::chrono::steady_clock::now();
    ::send(fd, request.data(), request.size(), 0);
    std::string reply = readResponses(fd, 1);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(reply.find("200 OK") != std::string::npos);
    CHECK(reply.find("slow 150") != std::string::npos);
    CHECK(elapsed >= 140);
    ::close(fd);
}

TEST(asyncDoesNotBlockOtherConnections)
{
    int slow = connectLocal();
    int quick = connectLocal();
    CHECK(slow >= 0 && quick >= 0);
    std::string slowRequest = "GET /slow/400 HTTP/1.1\r\nHost: x\r\n\r\n";
    ::send(slow, slowRequest.data(), slowRequest.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto start = std::chrono::steady_clock::now();
    std::string fastRequest = "GET /fast HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    ::send(quick, fastRequest.data(), fastRequest.size(), 0);
    std::string reply = readResponses(quick, 1);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(reply.find("fast") != std::string::npos);
    CHECK(elapsed < 200);               // answered while the other connection is still waiting
    std::string slowReply = readResponses(slow, 1);
    CHECK(slowReply.find("slow 400") != std::string::npos);
    ::close(slow);
    ::close(quick);
}

TEST(asyncKeepsPipelinedOrder)
{
    int fd = connectLocal();
    CHECK(fd >= 0);
    std::string requests = "GET /slow/150 HTTP/1.1\r\nHost: x\r\n\r\nGET /fast HTTP/1.1\r\nHost: x\r\n\r\nGET /slow/10 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    ::send(fd, requests.data(), requests.size(), 0);
    std::string all;
    char buffer[4096];
    for (;;) {
        ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        all.append(buffer, static_cast<size_t>(n));
    }
    size_t a = all.find("slow 150");
    size_t b = all.find("fast");
    size_t c = all.find("slow 10");
    CHECK(a != std::string::npos && b != std::string::npos && c != std::string::npos);
    CHECK(a < b && b < c);
    ::close(fd);
}

TEST(asyncTimeoutClosesConnection)
{
    int fd = connectLocal();
    CHECK(fd >= 0);
    std::string request = "GET /never HTTP/1.1\r\nHost: x\r\n\r\n";
    ::send(fd, request.data(), request.size(), 0);
    char buffer[256];
    ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);      // blocks until the server gives up (AsyncTimeout = 2 s)
    CHECK(n <= 0);
    ::close(fd);
}

TEST(asyncClientHangsUpFirst)
{
    int fd = connectLocal();
    CHECK(fd >= 0);
    std::string request = "GET /slow/100 HTTP/1.1\r\nHost: x\r\n\r\n";
    ::send(fd, request.data(), request.size(), 0);
    ::close(fd);                        // the late respond() must find no client and do nothing
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int again = connectLocal();
    std::string fastRequest = "GET /fast HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    ::send(again, fastRequest.data(), fastRequest.size(), 0);
    CHECK(readResponses(again, 1).find("fast") != std::string::npos);
    ::close(again);
}

#endif // !_WIN32
