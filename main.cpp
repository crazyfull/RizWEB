// RizWEB standalone server.
//   rizweb                 run with settings.ini (created next to the executable on first start)
//   rizweb -install        install + start a systemd service (Linux, root)
//   rizweb -uninstall      stop + remove the systemd service
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
        printf("usage: %s [-install | -uninstall]\n", argv[0]);
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
