// RizWEB standalone server.
//   rizweb                 run with settings.ini (created next to the executable on first start)
//   rizweb -install        install + start a systemd service (Linux, root)
//   rizweb -uninstall      stop + remove the systemd service
//   rizweb -fetch URL      HTTP(S) client: print the status line, headers and body of URL (-k: do not verify TLS)
#include "Client/clsHTTPFetcher.h"
#include "clsWEBMaster.h"
#include "LinuxService/clsLinuxService.h"
#include "Utils/clsRizLog.h"
#include "Utils/clsFileUtils.h"
#include "Utils/clsEscape.h"
#include "Utils/clsStringUtils.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

using namespace rizweb;

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }

int main(int argc, char **argv)
{
    const char *serviceName = "rizweb";
    if (argc > 1) {
        if (!strcmp(argv[1], "-install"))
            return LinuxService::createService(serviceName, "RizWEB Server") ? 0 : 1;
        if (!strcmp(argv[1], "-uninstall"))
            return LinuxService::removeService(serviceName) ? 0 : 1;
        if (!strcmp(argv[1], "-fetch") && argc > 2) {
            HTTPFetcher fetcher;
            HTTPFetchRequest request;
            request.url = argv[2];
            request.verifyTLS = !(argc > 3 && !strcmp(argv[3], "-k"));
            HTTPFetchResponse response = fetcher.fetch(request);
            if (!response.completed()) {
                fprintf(stderr, "error: %s\n", response.error.c_str());
                return 1;
            }
            printf("HTTP %d %s  (%s, %d redirect(s))\n", response.status, response.reason.c_str(), response.finalUrl.c_str(), response.redirects);
            for (const auto &header : response.headers)
                printf("%s: %s\n", header.first.c_str(), header.second.c_str());
            printf("\n%s\n", response.body.c_str());
            return response.ok() ? 0 : 2;
        }
        printf("usage: %s [-install | -uninstall | -fetch URL [-k]]\n", argv[0]);
        return strcmp(argv[1], "-h") == 0 ? 0 : 1;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif

    RIZLOG_INFO("Welcome to RizWEB %s", RIZWEB_VERSION);

    WEBMaster server;
    server.loadSettings();

    for (const char *extension : {".png", ".css", ".js", ".woff", ".woff2", ".ico", ".ttf"})
        server.cacheFileTypes().add(extension, 10 * 60);
    for (const char *extension : {".html", ".htm", ".css", ".js", ".json", ".svg", ".txt", ".xml"})
        server.compressionTypes().add(extension);

    // Middleware: runs before routing; return true when it answered the request itself.
    server.use([](HTTPClient &, HTTPRequest &, HTTPResponse &response) {
        response.setHeaderField("X-Served-By", "rizweb");
        return false;
    });

    // Cross-origin access for the /api routes (preflight is answered automatically).
    server.cors().allowOrigin("*").allowMethods("GET, POST, OPTIONS").allowHeaders("Content-Type");

    // Go style routes: ":name" matches one segment, "*" matches the rest of the path.
    server.get("/ip", [&server](HTTPClient &client, HTTPRequest &, HTTPResponse &response) {
        response.setContentType("text/plain; charset=utf-8");
        response.write("your ip: " + client.clientIp() + "\nconnections: " + std::to_string(server.clients().count()) + "\n");
        response.flush(200);
    });
    server.get("/api/hello/:name", [](HTTPClient &, HTTPRequest &request, HTTPResponse &response) {
        response.setContentType("application/json; charset=utf-8");
        response.write("{\"hello\":\"" + Escape::json(request.param("name")) + "\"}");
        response.flush(200);
    });
    // --- waiting handlers: the two ways to keep reactor threads free -------------------------------------
    // 1) offload(): blocking work (sqlite, a synchronous library) runs on the worker pool.
    //    Here a 3 second sleep stands in for the database query.
    //    Try:  curl localhost:8080/api/slow-worker/42   (meanwhile other requests are answered at once)
    server.get("/api/slow-worker/:id", server.offload([](const HTTPRequest &request, WorkerReply &reply) {
        std::this_thread::sleep_for(std::chrono::seconds(3));            // <- blocking "query"
        reply.setContentType("application/json; charset=utf-8");
        reply.write("{\"id\":\"" + Escape::json(request.param("id")) + "\",\"via\":\"worker pool\"}");
        reply.flush(200);
    }));

    // 2) defer(): the handler returns at once and the answer is sent later, from any thread. Use it with
    //    something that calls you back (an async driver, HTTPFetcher::fetchAsync). The thread below only plays the
    //    role of that driver's callback; a real one would not need a thread of yours.
    //    Try:  curl localhost:8080/api/slow-async/42
    server.get("/api/slow-async/:id", [](HTTPClient &, HTTPRequest &request, HTTPResponse &response) {
        std::shared_ptr<AsyncResponse> async = response.defer();         // the reactor thread is free from here on
        if (!async)
            return response.internalServerError();
        std::string id = request.param("id");
        std::thread([async, id]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));        // <- "waiting for the database"
            async->respond([id](HTTPResponse &out) {                      // runs on the connection's own thread
                out.setContentType("application/json; charset=utf-8");
                out.write("{\"id\":\"" + Escape::json(id) + "\",\"via\":\"defer\"}");
                out.flush(200);
            });
        }).detach();
    });

    server.post("/api/echo", [](HTTPClient &, HTTPRequest &request, HTTPResponse &response) {
        response.setContentType(request.headerField("content-type") ? *request.headerField("content-type") : "application/octet-stream");
        response.write(request.body());
        response.flush(200);
    }).bodyLimit = 8 * 1024 * 1024;     // per-route body limit

    // File upload: multipart/form-data goes straight to ./upload while it arrives (nothing is buffered in memory).
    //   curl -F "file=@photo.png" -F "title=hello" "http://host:8080/upload?uploadId=abc"
    //   curl http://host:8080/upload/progress/abc        (JSON progress while the upload runs)
    UploadOptions uploadOptions;
    uploadOptions.directory = FileUtils::executableDirectory() + "/upload";
    uploadOptions.maxFileBytes = 512ull * 1024 * 1024;
    server.upload("/upload", uploadOptions, [](HTTPClient &, HTTPRequest &, HTTPResponse &response, UploadResult &result) {
        std::string json = "{\"files\":[";
        for (size_t i = 0; i < result.files.size(); i++) {
            const UploadedFile &file = result.files[i];
            json += std::string(i ? "," : "") + "{\"field\":\"" + Escape::json(file.fieldName) + "\",\"name\":\"" + Escape::json(file.fileName) +
                    "\",\"type\":\"" + Escape::json(file.contentType) + "\",\"size\":" + std::to_string(file.size) + "}";
        }
        json += "],\"fields\":" + std::to_string(result.fields.size()) + "}";
        response.setContentType("application/json; charset=utf-8");
        response.write(json);
        response.flush(200);
    });
    server.uploadProgress();

    // WebSocket endpoints: any number, each with its own callbacks.
    WebSocketEndpoint echo;
    echo.onOpen = [](HTTPClient &client) { client.sendText("welcome to rizweb"); };
    echo.onMessage = [](HTTPClient &client, bool isBinary, const char *payload, size_t length) {
        if (isBinary)
            client.sendBinary(payload, length);
        else
            client.sendText(std::string_view(payload, length));
    };
    server.ws("/ws", echo);

    // Login example (only used when IsUseAuthentication=true): replace with a real credential check.
    server.setAuthenticationHandler([](HTTPClient &client, HTTPRequest &) { client.authenticationResponse(false, "no accounts configured"); });

    if (!server.start())
        return 1;

    while (!g_stop)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    RIZLOG_INFO("stopping");
    server.stop();
    return 0;
}
