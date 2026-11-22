# RizWEB

Small, fast, fully static C++17 web server and embeddable HTTP/WebSocket API.

* Linux: built on [netRector](https://github.com/crazyfull/netRector) (epoll, one reactor thread per CPU)
* Windows / macOS: built on [netRectorClient](https://github.com/crazyfull/netRectorClient) (same API, portable event loop)
* TLS (HTTPS / WSS): BoringSSL (the patched build netRector uses)
* No other dependency (zlib is optional, for gzip/deflate)

## Features

HTTP/1.1 keep-alive and pipelining, GET/HEAD static files, Range, ETag / Last-Modified / 304, gzip/deflate
(compressed once, cached in memory), directory listing, WebSocket (RFC 6455), optional session login,
sendfile(2) on Linux, back-pressure aware streaming of big files.
Security: paths are resolved with realpath and cannot leave the web root, dot-files are never served,
HTML output is escaped, request/body/WebSocket size limits, same-origin check for WebSocket upgrades,
random server-side session tokens (HttpOnly, SameSite, Secure on HTTPS).

## Build

```sh
# 1. BoringSSL (patched, once): netRector/scripts/build_boringssl.sh /opt/bssl
# 2.
cmake -S . -B build -DNETRECTOR_DIR=/path/to/netRector -DRIZWEB_BORINGSSL_DIR=/opt/bssl
cmake --build build
./build/rizweb
```

`NETRECTOR_DIR` must point to netRector (Linux) or netRectorClient (Windows/macOS); without it CMake
fetches the right one from GitHub. `-DRIZWEB_ENABLE_TLS=OFF` builds without TLS.
Releases for all systems are built by the manual **Release (manual)** GitHub Action.

## Server

`settings.ini` is created next to the executable on first start (`Port`, `SSLPort`, `SSLCertPath`,
`SSLKeyPath`, `SelfSigned`, `LocatedRoot`, `DomainName`, ...). `rizweb -install` / `-uninstall` manage a systemd service.

## Use as a library

```cpp
#include "clsWEBMaster.h"
using namespace rizweb;

WEBMaster web;                       // owns its network host
web.settings().httpPort = 8080;
web.setHttpHandler([](HTTPClient &c, HTTPRequest &req, HTTPResponse &res) {
    if (req.urlPath() != "/api/ping") return false;      // false -> static files
    res.setContentType("application/json");
    res.write("{\"ok\":true}");
    res.flush(200);
    return true;
});
web.setWebSocketMessageHandler([](HTTPClient &c, bool binary, const char *data, size_t len) {
    c.sendText(std::string_view(data, len));
});
web.start();
```

Sharing the reactor of an application that already uses netRector / netRectorClient:

```cpp
netrector::Server server(65536);     // netrector::EventLoop on Windows / macOS
rizweb::WEBMaster web(server);       // RizWEB only adds its listeners
web.start();
server.start();
```

In a CMake project that already builds netRector (or netRectorClient), `add_subdirectory(RizWEB)` reuses
that target instead of building a second copy.

Handlers run on reactor threads (several with netRector): protect shared state, answer before returning,
and use `web.clients().post(id, fn)` to talk to a client from another thread.

## Layout

```
src/clsNetBackend.*     the only place that knows netRector vs netRectorClient
src/clsWEBMaster.*      server: settings, listeners, TLS, handlers
src/clsHTTPClient.*     one connection (request loop, WebSocket, file streaming)
src/clsHTTPClientList.* live connection registry
src/HTTP/               request, response, static files, cookies, sessions, security, MIME, cache, compression, WebSocket
src/Utils/              strings, crypto helpers, ini, files, log
src/LinuxService/       systemd service helper
```
