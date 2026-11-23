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

Routes work like Go's `http.ServeMux` (`:name` matches one path segment, `*` the rest). Handlers answer
by writing to `HTTPResponse`; a path with a handler for other methods only gets `405` with an `Allow` header.

```cpp
#include "clsWEBMaster.h"
using namespace rizweb;

WEBMaster web;                                   // owns its network host
web.settings().httpPort = 8080;

web.use([](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {   // middleware chain, runs before routing
    return false;                                                 // true = "I answered, stop here"
});
web.cors().allowOrigin("*").allowMethods("GET, POST").allowHeaders("Content-Type");   // preflight handled for you

web.get("/api/users/:id", [](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {
    res.setContentType("application/json");
    res.write("{\"id\":\"" + StringUtils::htmlEscape(req.param("id")) + "\"}");
    res.flush(200);
});
web.post("/upload", uploadHandler).bodyLimit = 64 * 1024 * 1024;  // per-route body limit (default: MaxBodyBytes)

WebSocketEndpoint chat;                          // any number of endpoints, each with its own callbacks
chat.onMessage = [](HTTPClient &c, bool binary, const char *data, size_t len) { c.sendText(std::string_view(data, len)); };
web.ws("/chat", chat);

web.start();
```

Requests that match no route fall through to the optional `setHttpHandler`, then to static files.
Request bodies with `Content-Length` or `Transfer-Encoding: chunked` are supported.

Sharing the reactor of an application that already uses netRector / netRectorClient:

```cpp
netrector::Server server(65536);     // netrector::EventLoop on Windows / macOS
rizweb::WEBMaster web(server);       // RizWEB only adds its listeners
web.start();
server.start();
```

In a CMake project that already builds netRector (or netRectorClient), `add_subdirectory(RizWEB)` reuses
that target instead of building a second copy. `-DRIZWEB_BUILD_SHARED=ON` builds a shared library.

Handlers run on reactor threads (several with netRector): protect shared state, answer before returning,
and use `web.clients().post(id, fn)` to talk to a client from another thread.

## Limits and hardening

`settings.ini`: `FileCacheSeconds` (static file cache), `MaxBodyBytes`, `MaxConnectionsPerIp` (extra connections are closed), `MaxRequestsPerSecondPerIp`
(`429` + `Retry-After`); 0 disables a limit. Smuggling (`Content-Length` + `Transfer-Encoding`) and unsupported
transfer codings are rejected.

## Tests and benchmark

```sh
cmake --build build && ctest --test-dir build --output-on-failure     # 21 unit tests: parser, chunked, router, limiter, WebSocket codec, ...
python3 tools/bench.py 127.0.0.1 8080 /ip 32 5                        # dependency-free load generator; use wrk for real numbers
```

### RizWEB vs nginx 1.24 (static files)

`tools/benchVsNginx.sh` serves identical files from both servers. Result on a 2-core cloud VM, wrk on the same
machine (so the load generator takes CPU from both), localhost, plain HTTP, nginx with `open_file_cache`, one run each:

| file | conns | RizWEB req/s | nginx req/s | RizWEB p99 | nginx p99 |
|---|---|---|---|---|---|
| 554 B | 64 | 71.7k | 225.3k | 4.1 ms | 3.2 ms |
| 554 B | 512 | 92.1k | 161.6k | 10.3 ms | 8.3 ms |
| 100 KB | 64 | 63.3k | 65.7k | 7.2 ms | 39.0 ms |
| 100 KB | 512 | 59.4k | 57.4k | 15.4 ms | 27.0 ms |
| 1 MB | 64 | 5.3k | 5.6k | 14.6 ms | 17.4 ms |
| 1 MB | 512 | 4.9k | 7.1k | 123.9 ms | 127.5 ms |

Large files are on par (both bound by sendfile / loopback).

**Small-file path.** The first run showed RizWEB 1.7-3x slower on the 554 B file: every request did `realpath`
(7 `readlink` calls), `open`/`fstat`/`read`/`close` and three `lseek`s. Resolved files (real path, size, mtime,
ETag, content type and, for files up to 64 KB, the content) are now cached for `FileCacheSeconds` (default 1 s, 0 = off);
a changed file is picked up within that time, hidden files / symlink escapes are still refused. Re-measured, 554 B file:
215k req/s at 64 connections and 224k at 512 (before: 72k / 92k; nginx: 225k / 162k).

### 10k+ concurrent connections

`tools/benchHighConnections.sh` (wrk, keep-alive, 554 B file, 12 s per run, same VM, nginx `worker_connections 16384`,
one run each; sandbox limit of 20000 open files per process):

| connections | server | total requests | req/s | p99 | RSS |
|---|---|---|---|---|---|
| 1,000 | RizWEB | 2.92 M | 241.9k | 8.5 ms | 6 MB |
| 1,000 | nginx | 2.32 M | 191.8k | 10.7 ms | 21 MB |
| 10,000 | RizWEB | 1.94 M | 159.9k | 71 ms | 23 MB |
| 10,000 | nginx | 1.68 M | 139.4k | 84 ms | 26 MB |
| 15,000 | RizWEB | 1.65 M | 136.1k | 125 ms | 32 MB |
| 15,000 | nginx | 1.52 M | 125.1k | 125 ms | 28 MB |
| 19,000 (2 wrk) | RizWEB | 1.44 M | 119.1k | 179 ms | 39 MB |
| 19,000 (2 wrk) | nginx | 1.28 M | 105.7k | 184 ms | (111 MB*) |

\* nginx RSS at that point includes memory retained from the earlier runs of the same process.
No connect/read/timeout errors in these runs. One connection per request (`Connection: close`, 1,000 clients):
RizWEB 200k req/s (p99 9.9 ms), nginx 172k req/s (p99 24 ms). 100 KB file: 6,000 connections 53.4k vs 57.1k req/s,
9,000 connections 45.4k vs 47.5k req/s (RizWEB / nginx), no errors.

Limits found while testing: every connection and every file being sent needs a descriptor, so 10,000 simultaneous
downloads of a file above 64 KB need ~20,000 descriptors. With the sandbox hard limit of 20000, RizWEB logged
`accept4: too many open files` and ~420 connections were reset at 10,000 large-file connections (nginx shares open
descriptors between requests and did not). RizWEB now raises its soft open-file limit to the hard limit at start-up;
sharing descriptors between streams of one file is not implemented. Not measured: TLS under load, connections from many
client IPs, machines with more than 2 cores.

## Docker

`docker build -t rizweb .` produces a static image (the Dockerfile has not been built in CI yet).

## Layout

```
src/clsNetBackend.*     the only place that knows netRector vs netRectorClient
src/clsWEBMaster.*      server: settings, listeners, TLS, handlers
src/clsHTTPClient.*     one connection (request loop, WebSocket, file streaming)
src/clsHTTPClientList.* live connection registry
src/HTTP/               request, router, CORS, limiter, response, static files, cookies, sessions, security, MIME, cache, compression, WebSocket
src/Utils/              strings, crypto helpers, ini, files, log
src/LinuxService/       systemd service helper
```
