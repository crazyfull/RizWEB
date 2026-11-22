# RizWEB

A small, fast HTTP/1.1 and WebSocket server for Linux, written as a **portable C++17 library**
on top of [netRector](https://github.com/ArsPand/netRector) (epoll reactors, BoringSSL TLS,
`sendfile`). The API is modelled on Go's `net/http`: register handlers, serve a directory, start.

```cpp
#include "server/clsWebServer.h"
using namespace rizweb;

int main() {
    WebServer server;                                   // listens on :8080 by default

    server.get("/hello", [](const HttpRequest& req, HttpResponse& res) {
        res.sendText(200, "hello " + req.queryValue("name").value_or("world"));
    });

    StaticFileOptions site;
    site.rootDirectory = "www";
    server.serveStatic("/", site);

    std::string error;
    if (!server.start(&error)) return 1;
    // ... wait for a signal, then server.stop();
}
```

`WebServer` owns its netRector `Server` by default. An application that already runs netRector
(for example the RizWEB UI programs) can hand its own `netrector::Server` to
`WebServer(netrector::Server&, config)` so HTTP shares the same reactors and threads.

## Build

```sh
# TLS needs the patched BoringSSL that netRector builds (scripts/build_boringssl.sh in netRector)
cmake -S . -B build -DBORINGSSL_DIR=/path/to/boringssl          # add -DNETRECTOR_ENABLE_TLS=OFF for no TLS
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/bin/rizweb --root www --port 8080 --listing
```

netRector is fetched from `github.com/ArsPand/netRector` automatically. Use a local checkout with
`-DRIZWEB_NETRECTOR_DIR=/path/to/netRector`, or pin a branch with `-DRIZWEB_NETRECTOR_TAG=...`.
`libwrench` is no longer used anywhere.

## Layout

| Folder | Content |
|---|---|
| `src/util` | string helpers, URL codec, SHA-1, Base64, random, INI reader, logger, HTTP dates |
| `src/http` | request, parser, response, router, static files, gzip cache, sessions, auth guard |
| `src/websocket` | RFC 6455 frame codec, handshake, session API |
| `src/server` | `WebServer` (public facade), `HttpConnection` (the netRector socket), `ServerConfig` |
| `tests` | unit tests; the connection tests run against a fake `TCPSocket` (no network) |

`rizweb_core` (everything except `src/server/clsHttpConnection.cpp` and `clsWebServer.cpp`) does not
depend on netRector, so it is unit tested on its own.

## Features

* HTTP/1.1 keep-alive and pipelining, `Expect: 100-continue`, chunked request bodies, HEAD
* Go-style routing: exact paths, `/subtree/` prefixes, per-method handlers, 405 with `Allow`
* Middleware chain (`WebServer::use`), session store + `AuthGuard`, same-origin (CSRF) guard
* Static files: `sendfile`, ETag / Last-Modified, `304`, byte ranges, cached gzip, directory listing
* WebSocket: fragmentation, ping/pong, close handshake, UTF-8 validation, origin check
* TLS through BoringSSL, optional asynchronous private-key operations (`tlsKeyThreads`)
* Back-pressure: a client that does not read its responses cannot make the server queue unlimited work

## Threading

Handlers run on the netRector reactor threads, several at once, so they must be thread safe.
Register routes, middleware and WebSocket handlers **before** `start()`. A `WebSocketSession`
may only be used inside its callbacks (netRector rule: sockets belong to their reactor thread).

## Security notes (what changed from 1.x)

* The path is percent-decoded and normalized by the parser; anything that leaves the root is a 400.
  The static handler additionally resolves symlinks and refuses files outside the document root.
* Dot files are refused, directory listings are HTML escaped, listings are off by default.
* Smuggling defences: duplicate/conflicting `Content-Length`, `Content-Length` with
  `Transfer-Encoding`, obsolete folding, space before the colon, bare LF are rejected.
* Response splitting is impossible: header values with CR/LF are refused, redirects are validated.
* Sessions are 192 random bits from `getrandom`, stored server side, `HttpOnly; SameSite=Lax`
  (+ `Secure` over TLS). Passwords are never logged.
* Limits: header size, URI length, header count, body size, requests per connection, request deadline,
  idle timeout, send-queue size.
