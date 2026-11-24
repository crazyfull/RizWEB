# RizWEB

Small, fast, fully static C++17 web server **and** embeddable HTTP / WebSocket server + client API
(think Go's `net/http`, for C++ programs that already use a reactor).

* **Linux**: [netRector](https://github.com/crazyfull/netRector) (epoll, one reactor thread per CPU, sendfile)
* **Windows / macOS**: [netRectorClient](https://github.com/crazyfull/netRectorClient) (same API, portable event loop)
* **TLS** (HTTPS / WSS / HTTPS client): BoringSSL, the **stock upstream** one - RizWEB needs no patch
* zlib is optional (gzip / deflate). No other dependency.

The backend is chosen at configure time; the source of RizWEB is the same everywhere
(`src/clsNetBackend.*` is the only file that knows the difference).

## Features

| Area | What you get |
|---|---|
| HTTP/1.1 | keep-alive, pipelining, `100-continue`, `Content-Length` and chunked request bodies, HEAD, smuggling checks |
| Static files | `realpath` confinement to the web root, dot-files hidden, Range, ETag / Last-Modified / 304, gzip / deflate (compressed once, cached), `Cache-Control` per file type, escaped directory listing, sendfile(2) on Linux, back-pressure aware streaming, small in-memory file cache (`FileCacheSeconds`) |
| Responses | built in pooled buffers (no `std::string` for short or dynamic replies), head + body sent in one `writev` / one TLS record; **async answers**: `res.defer()` frees the reactor thread and the reply is produced later from any thread |
| Blocking work | `web.offload(...)`: handlers that block (sqlite, synchronous libraries) run on a worker thread pool with a bounded queue (`503` when full); the reactor threads never wait |
| Routing | Go-like: `get / post / put / patch / del`, `:name` parameters, `*` wildcard, automatic `405` + `Allow`, fall-through to a default handler and then static files |
| Middleware | chain that runs before routing; return `true` to answer and stop |
| CORS | `web.cors()`: origins, methods, headers, credentials, max-age; preflight is answered for you |
| WebSocket | RFC 6455, any number of endpoints (`web.ws("/chat", endpoint)`), text / binary, size limit, same-origin check, `ws://` and `wss://` |
| Uploads | `multipart/form-data` **and** raw / chunked bodies streamed straight from the receive buffer to the disk (no copy, no buffering - see below), per-part file name + `Content-Type`, progress endpoint |
| Input validation | `Validator`: required, length, integer / number / bool, one-of, slug, uuid, e-mail, URL, IP, UTF-8, safe file name, custom rules, JSON error report |
| Output escaping | `Escape::html / htmlAttribute / json / jsString / url / shellArg / headerValue`, `HTTPResponse::writeEscaped` |
| Security headers | `X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy`, optional `Content-Security-Policy`, optional HSTS (HTTPS only) |
| Auth | optional login area (`SecurePath`) with random server-side sessions (HttpOnly, SameSite, Secure on HTTPS) |
| Limits | request line / header / body size, per-route body limit, upload size, WebSocket message size, connections per IP, requests per second per IP (`429`), keep-alive timeout |
| HTTP client | `HTTPFetcher`: blocking and async, HTTP and HTTPS (certificate + host name verified), redirects, streaming, download to file, progress, timeouts, size limit, SSRF filter |
| Embedding | shares the reactor of an application that already uses netRector / netRectorClient; static or shared library; `find_package(RizWEB)` |
| Service | `rizweb -install` / `-uninstall` (systemd, Linux) |

### Uploads (zero copy, progress, chunked)

The request head is parsed first; the route, per-IP limits, authentication, CORS and middleware are applied **before**
the body is read, and `Expect: 100-continue` is answered only when the route accepts. After that the body
(`Content-Length` or `Transfer-Encoding: chunked`) is decoded incrementally and the multipart parser works directly on the
socket's receive buffer: file bytes go to `write(2)` without being copied into a body string, and a part that is split
between two reads is stitched by carrying only the delimiter-sized tail. Measured: a 400 MB upload took 0.41 s with 5.7 MB
resident memory. File names from the client are never used as paths: files are stored under a random name in
`UploadOptions::directory` (the original name is reported to the handler).

```sh
curl -F "file=@photo.png" -F "title=hello" "http://localhost:8080/upload?uploadId=abc"
curl http://localhost:8080/upload/progress/abc     # {"id":"abc","received":1048576,"total":5242880,"percent":20,"state":"uploading"}
curl -H "Transfer-Encoding: chunked" -F "file=@big.iso" http://localhost:8080/upload   # chunked request body
```

## Build

```sh
git clone https://github.com/crazyfull/RizWEB && cd RizWEB
# BoringSSL (stock, once)
git clone https://github.com/google/boringssl && cmake -GNinja -S boringssl -B boringssl/build -DCMAKE_BUILD_TYPE=Release && ninja -C boringssl/build crypto ssl
# RizWEB
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNETRECTOR_DIR=/path/to/netRector -DRIZWEB_BORINGSSL_DIR=/path/to/bssl-prefix -DRIZWEB_BUILD_TESTS=ON
cmake --build build && ctest --test-dir build --output-on-failure && ./build/rizweb
```

`NETRECTOR_DIR` points to netRector (Linux) or netRectorClient (Windows / macOS); when it is not given CMake fetches the
right one from GitHub (`RIZWEB_BACKEND_TAG`, default `master`). `-DRIZWEB_ENABLE_TLS=OFF` builds without TLS,
`-DRIZWEB_STATIC=ON` links the executable statically, `-DRIZWEB_BUILD_SHARED=ON` builds a shared library.

> netRector builds with a stock BoringSSL since its `stock-boringssl-support` change (the patch only enabled an optional
> bypass-mode feature). Until that branch is merged into netRector's `master`, use that ref (the release workflow does).

### Releases (GitHub Actions)

**Actions -> Release (manual) -> Run workflow** builds for `linux`, `windows` and `macos` on `x86_64` and `arm64`
and publishes, for each of the six targets, two archives plus `SHA256SUMS.txt`:

| archive | content |
|---|---|
| `rizweb-<version>-<target>` | the server as one static executable + default `resource/` |
| `rizweb-lib-<version>-<target>` | static library package: `lib/` (RizWEB, backend, BoringSSL, ...), `include/`, `lib/cmake/RizWEB/RizWEBConfig.cmake` |

Native targets run the unit / integration tests and a smoke test in CI; macOS x86_64 is cross-compiled on an arm64 runner
and is only built. An optional input builds a multi-arch Docker image for ghcr.io. **Status: the workflow ran successfully on GitHub for all six targets (build, tests on the native ones, smoke test, both
archives, checksums, draft release). The Docker image has not been built yet.** Windows and macOS were verified by CI only
(build + tests + a plain `GET /` smoke test); the author did not run them by hand. `netRector` / `netRectorClient` must be at
a ref that builds with a stock BoringSSL (`stock-boringssl-support` until merged).

Using the library package:

```cmake
find_package(RizWEB REQUIRED PATHS /path/to/rizweb-lib-<version>-linux-x86_64)
target_link_libraries(myapp PRIVATE RizWEB::RizWEB)     # pulls in the backend, BoringSSL and zlib automatically
```

## Server (`rizweb` executable)

`settings.ini` is created next to the executable on first start.

| Key | Default | Meaning |
|---|---|---|
| `Port`, `ListenIP`, `DomainName` | 8080, 0.0.0.0 | HTTP listener (`Port=0`: none) |
| `SSLPort`, `SSLCertPath`, `SSLKeyPath`, `SelfSigned` | | HTTPS listener (self-signed certificate can be generated) |
| `LocatedRoot`, `ResourceDir`, `MimeTypesPath` | | web root and resources |
| `IsDirectoryBrowsing` | | escaped directory listing |
| `IsUseAuthentication`, `SecurePath`, `AuthRoot`, `SessionExpirationTime` | | login protected area |
| `WorkerThreads`, `WorkerQueue` | 4, 1024 | worker pool for `offload()` handlers and the number of jobs that may wait before requests get `503` |
| `AsyncTimeout` | 30 | seconds a deferred (async) response may stay unanswered; then the client gets `504` and the connection is closed |
| `KeepAliveTime`, `MaxBodyBytes`, `MaxUploadBytes`, `MaxWebSocketMessageBytes` | | limits (0 = unlimited where it makes sense) |
| `MaxConnectionsPerIp`, `MaxRequestsPerSecondPerIp` | | per-IP limits (extra connections closed, `429` + `Retry-After`) |
| `FileCacheSeconds` | 1 | static file metadata / small-content cache (0 = off) |
| `SecurityHeaders`, `ContentSecurityPolicy`, `HstsSeconds` | | response hardening |
| `WebSocketPath`, `WebSocketSameOriginOnly` | | default WebSocket endpoint |

`rizweb -fetch https://example.com [-k]` is the built-in HTTP client on the command line.

The demo server in `main.cpp` also shows both waiting styles: `GET /api/slow-worker/:id` (`offload`, a 3 s sleep standing in for a
blocking database query) and `GET /api/slow-async/:id` (`defer`).

## Use as a library

### Routes, middleware, CORS, WebSocket

```cpp
#include "clsWEBMaster.h"
using namespace rizweb;

WEBMaster web;                                   // owns its network host
web.settings().httpPort = 8080;

web.use([](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {   // runs before routing
    res.setHeaderField("X-Request-From", "rizweb");
    return false;                                                  // true = "I answered, stop here"
});
web.cors().allowOrigin("*").allowMethods("GET, POST").allowHeaders("Content-Type");

web.get("/api/users/:id", [](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {
    res.setContentType("application/json");
    res.write("{\"id\":\"" + Escape::json(req.param("id")) + "\"}");
    res.flush(200);
});
web.post("/api/echo", echoHandler).bodyLimit = 8 * 1024 * 1024;  // per-route body limit

WebSocketEndpoint chat;                          // any number of endpoints
chat.onOpen    = [](HTTPClient &c) { c.sendText("welcome"); };
chat.onMessage = [](HTTPClient &c, bool binary, const char *data, size_t len) { c.sendText(std::string_view(data, len)); };
web.ws("/chat", chat);

web.start();
```

### Async responses (do not block the reactor)

A handler normally answers before it returns. For anything that has to wait (another HTTP call, a database, a worker
thread) call `res.defer()`, return, and answer later: the reactor thread keeps serving other connections, and requests
pipelined on the same connection wait so that responses always come in request order.

```cpp
HTTPFetcher http(server);                                       // shares the reactor, see below
web.get("/weather", [&](HTTPClient &, HTTPRequest &, HTTPResponse &res) {
    std::shared_ptr<AsyncResponse> async = res.defer();         // handler may return now
    HTTPFetchRequest q; q.url = "https://api.example.com/weather"; q.timeoutSeconds = 5;
    http.fetchAsync(q, [async](HTTPFetchResponse &r) {          // runs on a reactor thread, never blocks one
        if (!r.ok()) { async->sendError(502); return; }
        async->respond([body = r.body](HTTPResponse &out) {     // runs on the connection's own thread
            out.setContentType("application/json");
            out.write(body);
            out.flush(200);
        });
    });
});
```

`respond()` works from any thread and returns `false` if the client has gone; `send(status, type, body)` and
`sendError()` are shortcuts. If nobody answers within `AsyncTimeout` the client gets `504 Gateway Timeout` and the connection is closed. `defer()` is not available
on streaming (upload) routes.

### Blocking work: worker pool (sqlite, synchronous libraries)

Code that blocks cannot be made async from the outside. Run it on worker threads with `web.offload()`: the handler gets a
copy of the request and a `WorkerReply` (status, headers, body are collected and sent by the connection's own reactor
thread). Reactor threads stay free, pipelined requests keep their order, and when every worker is busy and `WorkerQueue`
jobs are waiting the client immediately gets `503` instead of the server piling up work.

```cpp
web.settings().workerThreads = 4;                                // pool size
web.get("/api/user/:id", web.offload([](const HTTPRequest &req, WorkerReply &reply) {
    thread_local Database db("app.sqlite");                      // one connection per worker thread (sqlite rule)
    std::string name = db.queryName(req.param("id"));            // blocks - fine here
    reply.setContentType("application/json; charset=utf-8");
    reply.write("{\"name\":\"" + Escape::json(name) + "\"}");
    reply.flush(200);
}));
```

Which one to use: no waiting -> plain handler (fastest); blocking library -> `offload()`; a non-blocking client that calls you
back (`HTTPFetcher::fetchAsync`, an async database driver) -> `res.defer()`.

### Validate input, escape output

The parser only guarantees well-formed requests; it does not know what your values mean. Validate what you accept and
escape for the place you write to. SQL is not covered by any helper: **use parameterized queries**.

```cpp
web.get("/search", [](HTTPClient &, HTTPRequest &req, HTTPResponse &res) {
    Validator v;
    v.query(req, "q").required().maxLength(100).utf8();
    v.query(req, "page").integer(1, 1000);
    if (!v.ok()) {
        res.setContentType("application/json");
        res.write(v.errorsJson());                    // {"errors":{"q":"is required"}}
        res.flush(400);
        return;
    }
    res.setContentType("text/html; charset=utf-8");
    res.writeEscaped("<h1>Results for ");            // writeEscaped() = write(Escape::html(...))
    res.writeEscaped(v.text("q"));
    res.write("</h1><a href=\"/search?q=" + Escape::url(v.text("q")) + "\">again</a>");
    res.flush(200);
});
```

`Escape::shellArg` quotes one argument for a POSIX shell, but prefer `execve` with an argument vector over any shell.

### Uploads

```cpp
UploadOptions options;
options.directory    = "/var/lib/myapp/uploads";
options.maxFileBytes = 512ull << 20;
options.allowedExtensions = {"png", "jpg"};                  // optional; empty = any

web.upload("/upload", options, [](HTTPClient &, HTTPRequest &, HTTPResponse &res, UploadResult &result) {
    for (const UploadedFile &f : result.files)               // f.fieldName, f.fileName (client's), f.contentType,
        printf("%s -> %s (%llu bytes)\n", f.fileName.c_str(), f.path.c_str(), (unsigned long long)f.size);   // f.path (on disk)
    res.write("ok"); res.flush(200);
});
web.uploadProgress();                                          // GET /upload/progress/:id

// anything else that is large: stream it yourself instead of buffering it
web.stream(HTTPMethod::Post, "/raw", [](HTTPClient &, HTTPRequest &, HTTPResponse &) -> std::unique_ptr<UploadSink> {
    return std::make_unique<MySink>();                         // UploadSink: onBody(data, len) / onEnd(...) / onAbort(...)
});
```

### HTTP client (blocking and async)

```cpp
HTTPFetcher http;                                              // private reactor thread
HTTPFetchResponse r = http.get("https://api.example.com/items");   // blocks
if (r.ok()) use(r.body);                                       // r.status, r.headers, r.finalUrl, r.error

HTTPFetchRequest q;                                            // POST + headers + limits
q.method = "POST"; q.url = "https://api.example.com/items"; q.body = "{\"a\":1}";
q.header("Content-Type", "application/json");
q.timeoutSeconds = 10; q.maxResponseBytes = 1 << 20;
q.blockPrivateNetworks = true;                                 // SSRF protection for user supplied URLs (netRector)
http.fetchAsync(q, [](HTTPFetchResponse &resp) { /* runs on a reactor thread */ });

HTTPFetchRequest d;                                            // download to a file with progress
d.url = "https://example.com/big.iso"; d.downloadPath = "/tmp/big.iso";
d.onProgress = [](uint64_t got, uint64_t total) {};
http.fetch(d);
```

Redirects are followed (default 5; `https -> http` never, credentials are dropped when the host changes). Pass your reactor
(`HTTPFetcher fetcher(server)`, the `netrector::Server` / `EventLoop` of the previous section) to run async calls on it.

### Sharing the reactor of an existing netRector / netRectorClient application

```cpp
netrector::Server server(65536);     // netrector::EventLoop on Windows / macOS
rizweb::WEBMaster web(server);       // RizWEB only adds its listeners
web.start();
server.start();
```

In a CMake project that already builds netRector (or netRectorClient), `add_subdirectory(RizWEB)` reuses that target
instead of building a second copy.

## Limitations (read before production use)

* **Threads**: handlers run on reactor threads (several with netRector). Protect shared state. A handler that waits (blocking
  `HTTPFetcher` call, `sleep`, a slow query) blocks its reactor thread and every connection on it: use `res.defer()`
  (see "Async responses"). `web.clients().post(id, fn)` runs code on a client's own thread. `offload()` handlers run on pool threads: protect shared state there too.
* **Descriptors**: every connection and every file being sent needs a descriptor (large files are not shared between
  streams). RizWEB raises its soft limit to the hard limit at start-up; 10,000 simultaneous large downloads need ~20,000.
* **File cache**: changes are picked up after `FileCacheSeconds` (default 1 s); content is cached only for files up to 64 KB.
* **Not implemented**: HTTP/2 and HTTP/3, reverse proxy / FastCGI, virtual hosts, client certificates, `multipart/byteranges` (a single Range is supported).
* **HTTP client**: one connection per request (`Connection: close`, no pooling or HTTP/2); no cookie jar; the SSRF filter
  works on the netRector backend only and fails closed (the request is refused) on netRectorClient; on Windows / macOS give a CA
  bundle with `setCaFile` if the default roots are not found (not tested there).
* **Validation / escaping are tools, not magic**: nothing is sanitised automatically; SQL needs parameterized queries.
* **Platforms**: Linux x86_64 is the tested platform. Windows, macOS and arm64 are built and tested by the release workflow
  (the async tests and a few POSIX-socket tests are skipped on Windows) but not exercised under load. TLS under load, many client IPs and machines with more than 2 cores were not benchmarked.
* **Auth** is a simple session login for a protected path, not an identity system.

## Tests and benchmarks

```sh
cmake --build build && ctest --test-dir build --output-on-failure     # 43 tests: parser, chunked, router, limiter, WebSocket,
                                                                       # multipart / upload (split reads, chunked, progress),
                                                                       # validator, escaping, HTTP client (blocking, async, redirects, TLS)
python3 tools/bench.py 127.0.0.1 8080 /ip 32 5                        # dependency-free load generator
tools/benchVsNginx.sh build/rizweb                                    # needs wrk + nginx
```

### Short dynamic responses vs nginx

A handler that builds a ~20 byte reply (`/ip`) against nginx `return 200 "..."` (no disk, no proxy); `tools/benchDynamic.sh`.
Response head and body are built in pooled buffers and sent with one `writev` (the first version of this path concatenated
`std::string`s and was 24 % behind nginx at 512 connections). Two complete runs, version 2.3.0, 2-core cloud VM with wrk on the
same machine, 8 s per cell:

| conns | RizWEB req/s | nginx req/s | RizWEB p99 | nginx p99 |
|---|---|---|---|---|
| 64 | 170k - 192k | 149k - 159k | 3.7 - 5.1 ms | 5.3 - 5.5 ms |
| 512 | 219k - 249k | 212k - 233k | 7.7 - 8.0 ms | 7.9 - 8.2 ms |

On par with nginx within the noise of this VM (differences below ~10 % mean nothing).

### RizWEB vs nginx 1.24 (static files)

`tools/benchVsNginx.sh`, version 2.3.0, same machine and method as above (nginx with `open_file_cache`, 2 workers, localhost,
plain HTTP, every request checked for 2xx). Two complete runs; each cell shows the lower and the higher value.

| file | conns | RizWEB req/s | nginx req/s | RizWEB p99 | nginx p99 |
|---|---|---|---|---|---|
| 554 B | 64 | 144k - 203k | 132k - 148k | 6.4 - 7.9 ms | 3.3 - 4.5 ms |
| 554 B | 512 | 168k - 227k | 144k - 170k | 7.6 - 8.4 ms | 7.7 - 8.4 ms |
| 100 KB | 64 | 61k - 66k | 72k - 75k | 4.0 - 6.1 ms | 34 - 37 ms |
| 100 KB | 512 | 73k - 80k | 54k - 60k | 9.7 - 10.9 ms | 19 - 30 ms |
| 1 MB | 64 | 5.2k - 5.9k | 4.9k - 5.8k | 13 - 14 ms | 20 - 33 ms |
| 1 MB | 512 | 4.9k - 5.4k | 7.4k - 7.8k | 101 - 123 ms | 122 - 145 ms |

Small files: RizWEB is at or above nginx. 100 KB: below nginx at 64 connections, above at 512. **1 MB at 512 connections:
RizWEB is about 30 % slower than nginx** (the one clear weakness; not investigated). Tail latency is as good as nginx's here;
earlier measurements showed a much higher p99 for small files; the cause was not established (the benchmark script leaked a
server process between runs, which is the likely reason) and it did not reproduce in the clean runs above.

### 10k+ concurrent connections

`tools/benchHighConnections.sh` (wrk, keep-alive, 554 B file, 12 s per run, same VM, nginx `worker_connections 16384`,
two runs each; sandbox limit of 20000 open files per process), version 2.3.0:

| connections | server | req/s | p99 | RSS |
|---|---|---|---|---|
| 1,000 | RizWEB | 167k - 250k (A/B test: 199k - 250k) | 8 - 11 ms | 6 MB |
| 1,000 | nginx | 189k - 212k | 10.6 - 12.5 ms | 21 MB |
| 10,000 | RizWEB | 121k - 122k | 94 - 105 ms | 16 MB |
| 10,000 | nginx | 129k - 135k | 80 - 98 ms | 26 MB |
| 15,000 | RizWEB | 107k - 142k | 133 - 175 ms | 21 MB |
| 15,000 | nginx | 109k - 134k | 144 - 156 ms | 28 MB |

All connections were established (`ss` count equals the target) and no connect/read/timeout errors occurred. Summary:
throughput is on par with nginx (within the spread between runs, nginx slightly ahead at 10,000), RizWEB uses 20-40 % less
memory. The earlier table with 19,000 connections and the `Connection: close` / 100 KB runs was not repeated and has been
removed.

Limits found while testing: every connection and every file being sent needs a descriptor, so 10,000 simultaneous
downloads of a file above 64 KB need ~20,000 descriptors. With the sandbox hard limit of 20000, RizWEB logged
`accept4: too many open files` and ~420 connections were reset at 10,000 large-file connections (nginx shares open
descriptors between requests and did not; measured on an earlier version, not repeated). RizWEB raises its soft open-file
limit to the hard limit at start-up; sharing descriptors between streams of one file is not implemented. Not measured: TLS
under load, connections from many client IPs, machines with more than 2 cores.

## Docker

`docker build -t rizweb .` builds a `FROM scratch` image with the static executable (stock BoringSSL, netRector ref
`stock-boringssl-support`). It has not been built in CI.

## Layout

```
src/clsNetBackend.*     the only place that knows netRector vs netRectorClient
src/clsWEBMaster.*      server: settings, listeners, TLS, routes, upload / stream registration
src/clsHTTPClient.*     one connection (request loop, uploads, WebSocket, file streaming)
src/clsHTTPClientList.* live connection registry
src/HTTP/               request, router, CORS, limiter, response, static files, file cache, multipart, upload, cookies, sessions, security, MIME, compression, WebSocket
src/Client/             HTTPFetcher: URL, connection, redirects (the HTTP client)
src/Utils/              strings, escape, validator, crypto helpers, ini, files, log
src/LinuxService/       systemd service helper
cmake/                  RizWEBConfig.cmake.in (find_package support)
tests/  tools/          unit / integration tests, benchmark scripts
```
