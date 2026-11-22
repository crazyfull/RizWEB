// RizWEB example application: static file server, a few API routes, a WebSocket echo endpoint
// and an optional password protected area. It also shows how little code a program needs
// when it uses RizWEB as a library.
#include "http/clsAuthGuard.h"
#include "server/clsWebServer.h"
#include "util/clsIniFile.h"
#include "util/clsLogger.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>

using namespace rizweb;

namespace {

void printUsage(const char* program)
{
    std::printf(
        "RizWEB %s\n\n"
        "usage: %s [options]\n"
        "  --config <file>        settings.ini ([httpServer] / [sslSetting] sections)\n"
        "  --root <dir>           document root            (default: www)\n"
        "  --resource <dir>       resource directory       (default: resource, served at /resource/)\n"
        "  --listen <ip>          bind address             (default: 0.0.0.0, \"*\" = IPv4 + IPv6)\n"
        "  --port <n>             HTTP port                (default: 8080)\n"
        "  --https-port <n>       HTTPS port, needs --cert and --key (or --self-signed)\n"
        "  --cert <file> --key <file>   PEM certificate chain and private key\n"
        "  --self-signed          temporary self-signed certificate (tests only)\n"
        "  --threads <n>          reactor threads          (default: CPU cores)\n"
        "  --listing              allow directory listings\n"
        "  --auth <user:password> protect /private/ with a login form\n"
        "  --verbose              debug log\n",
        RIZWEB_VERSION, program);
}

// compares without leaking where the first difference is
bool constantTimeEquals(const std::string& a, const std::string& b)
{
    unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string jsonEscape(const std::string& text)
{
    std::string out;
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
            out += buffer;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

const char* kLoginPage =
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Login</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"></head><body>"
    "<h1>Login</h1>%s<form method=\"post\" action=\"/auth/\">"
    "<p><input name=\"user\" placeholder=\"user\" autocomplete=\"username\"></p>"
    "<p><input name=\"pass\" type=\"password\" placeholder=\"password\" autocomplete=\"current-password\"></p>"
    "<p><button type=\"submit\">Sign in</button></p></form></body></html>";

} // namespace

int main(int argc, char** argv)
{
    ServerConfig config;
    std::string authUser, authPassword;
    std::string configFile;

    // two passes: the ini file first, then the command line overrides it
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            configFile = argv[++i];
    }
    if (!configFile.empty()) {
        IniFile ini;
        if (!ini.load(configFile)) {
            std::fprintf(stderr, "cannot read config file %s\n", configFile.c_str());
            return 1;
        }
        config.loadFromIni(ini);
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--config") {
            next();
        } else if (arg == "--root" || arg == "--resource" || arg == "--listen" || arg == "--cert" || arg == "--key"
                   || arg == "--port" || arg == "--https-port" || arg == "--threads" || arg == "--auth") {
            const char* value = next();
            if (!value) {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                return 1;
            }
            if (arg == "--root") config.documentRoot = value;
            else if (arg == "--resource") config.resourceRoot = value;
            else if (arg == "--listen") config.listenIp = value;
            else if (arg == "--cert") config.tlsCertFile = value;
            else if (arg == "--key") config.tlsKeyFile = value;
            else if (arg == "--port") config.httpPort = static_cast<uint16_t>(std::atoi(value));
            else if (arg == "--https-port") config.httpsPort = static_cast<uint16_t>(std::atoi(value));
            else if (arg == "--threads") config.threads = std::atoi(value);
            else {
                std::string pair = value;
                size_t colon = pair.find(':');
                if (colon == std::string::npos || colon == 0) {
                    std::fprintf(stderr, "--auth expects user:password\n");
                    return 1;
                }
                authUser = pair.substr(0, colon);
                authPassword = pair.substr(colon + 1);
            }
        } else if (arg == "--self-signed") {
            config.tlsSelfSigned = true;
        } else if (arg == "--listing") {
            config.directoryListing = true;
        } else if (arg == "--verbose") {
            Logger::setLevel(LogLevel::debug);
        } else {
            std::fprintf(stderr, "unknown option %s\n\n", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    if (config.tlsSelfSigned && config.httpsPort == 0)
        config.httpsPort = 8443;

    WebServer server(config);

    // ---- API routes ----
    server.get("/ip", [](const HttpRequest& request, HttpResponse& response) {
        response.sendJson(200, "{\"ip\":\"" + jsonEscape(request.remoteAddress) + "\"}");
    });

    server.get("/api/time", [](const HttpRequest&, HttpResponse& response) {
        response.sendJson(200, "{\"unix\":" + std::to_string(static_cast<long long>(std::time(nullptr))) + "}");
        response.setHeader("Cache-Control", "no-store");
    });

    server.get("/api/stats", [&server](const HttpRequest&, HttpResponse& response) {
        response.sendJson(200, "{\"connections\":" + std::to_string(server.activeConnections()) + "}");
    });

    // ---- WebSocket echo ----
    WebSocketHandlers echo;
    echo.onMessage = [](WebSocketSession& session, WsOpcode opcode, std::string_view data) {
        if (opcode == WsOpcode::text)
            session.sendText(data);
        else
            session.sendBinary(data);
    };
    server.websocket("/ws/echo", echo);

    // ---- optional protected area ----
    if (!authUser.empty()) {
        auto guard = std::make_shared<AuthGuard>(server.sessions(), "/auth/");
        guard->protect("/private/");
        server.use(makeSameOriginGuard());
        server.use(guard->middleware());

        server.get("/auth/", [](const HttpRequest&, HttpResponse& response) {
            char page[1024];
            std::snprintf(page, sizeof(page), kLoginPage, "");
            response.sendHtml(200, page);
        });

        server.post("/auth/", [guard, authUser, authPassword](const HttpRequest& request, HttpResponse& response) {
            std::string user = request.formValue("user").value_or("");
            std::string pass = request.formValue("pass").value_or("");

            // evaluate both so the time does not reveal which one was wrong
            bool userOk = constantTimeEquals(user, authUser);
            bool passOk = constantTimeEquals(pass, authPassword);

            if (userOk && passOk && guard->login(request, response, user)) {
                response.redirect("/private/", 303);
                return;
            }

            char page[1024];
            std::snprintf(page, sizeof(page), kLoginPage, "<p>Wrong user name or password.</p>");
            response.sendHtml(401, page);
        });

        server.post("/auth/logout", [guard](const HttpRequest& request, HttpResponse& response) {
            guard->logout(request, response);
            response.redirect("/auth/", 303);
        });
    }

    // ---- files ----
    StaticFileOptions resources;
    resources.rootDirectory = config.resourceRoot;
    resources.cacheSecondsByExtension = {{"css", 3600}, {"png", 86400}, {"ico", 86400}, {"svg", 86400}, {"js", 3600}};
    server.serveStatic("/resource/", resources);

    StaticFileOptions site;
    site.rootDirectory = config.documentRoot;
    site.directoryListing = config.directoryListing;
    server.serveStatic("/", site);

    // Block the signals before the reactor threads start so that they inherit the mask and
    // this thread is the only one that receives them.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    std::string error;
    if (!server.start(&error)) {
        std::fprintf(stderr, "cannot start: %s\n", error.c_str());
        return 1;
    }

    int received = 0;
    sigwait(&signals, &received);
    RIZWEB_LOG_INFO("signal %d, shutting down", received);

    server.stop();
    return 0;
}
