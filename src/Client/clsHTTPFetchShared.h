#ifndef CLSHTTPFETCHSHARED_H
#define CLSHTTPFETCHSHARED_H
#include "../clsNetBackend.h"
#include <memory>
#include <mutex>
#include <string>

namespace rizweb {

// State shared by an HTTPFetcher and its connections (kept alive by the connections, so a connection can finish
// after the fetcher was destroyed): TLS client contexts and settings.
struct HTTPFetchShared
{
    std::mutex mutex;
    std::string caFile;                           // PEM bundle; empty = system roots
    std::string userAgent;
    std::unique_ptr<NetTlsContext> verifying;
    std::unique_ptr<NetTlsContext> insecure;

    // nullptr (with |error|) when TLS is not available.
    NetTlsContext *tlsContext(bool verify, std::string &error);
};

} // namespace rizweb

#endif // CLSHTTPFETCHSHARED_H
