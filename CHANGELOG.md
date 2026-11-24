# Changelog

The version is defined only in `src/clsVersion.h` (CMake, the release workflow and the server banner read it from there).
MAJOR = incompatible API change, MINOR = new feature, PATCH = fix only.

## 1.5.0
- Async responses: `res.defer()` / `AsyncResponse`; an unanswered deferral ends with `504` after `AsyncTimeout`.
- Worker pool for blocking handlers: `web.offload()`, `WorkerReply`, `WorkerThreads` / `WorkerQueue` (`503` when full).
- Responses are built in pooled buffers and sent with one `writev` (no `std::string` on the response path).
- Release workflow runs on all six targets; version is read from `clsVersion.h`; netRectorClient builds with a stock BoringSSL.
- New tests (43), `tools/benchDynamic.sh`, README benchmarks re-measured.

## 1.4.0
- HTTP client (`HTTPFetcher`, blocking and async), uploads (multipart, chunked, progress), `Validator`, `Escape`,
  security headers, file cache, installable static library package, BoringSSL without patch.
