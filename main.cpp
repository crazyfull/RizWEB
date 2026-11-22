// RizWEB standalone server.
//   rizweb                 run with settings.ini (created next to the executable on first start)
//   rizweb -install        install + start a systemd service (Linux, root)
//   rizweb -uninstall      stop + remove the systemd service
#include "clsWEBMaster.h"
#include "LinuxService/clsLinuxService.h"
#include "Utils/clsRizLog.h"
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

    // Example handler: /ip answers with the caller address. Return false to fall through to the file server.
    server.setHttpHandler([&server](HTTPClient &client, HTTPRequest &request, HTTPResponse &response) {
        if (request.urlPath() == "/ip") {
            response.setContentType("text/plain; charset=utf-8");
            response.write("your ip: " + client.clientIp() + "\nconnections: " + std::to_string(server.clients().count()) + "\n");
            response.flush(200);
            return true;
        }
        return false;
    });

    // Example WebSocket endpoint at /ws: echoes every message.
    server.setWebSocketConnectedHandler([](HTTPClient &client) { client.sendText("welcome to rizweb"); });
    server.setWebSocketMessageHandler([](HTTPClient &client, bool isBinary, const char *payload, size_t length) {
        if (isBinary)
            client.sendBinary(payload, length);
        else
            client.sendText(std::string_view(payload, length));
    });

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
