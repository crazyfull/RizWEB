#ifndef CLSHTTPFETCHER_H
#define CLSHTTPFETCHER_H
#include "../clsNetBackend.h"
#include "clsHTTPFetchTypes.h"
#include <functional>
#include <memory>
#include <string>

namespace rizweb {

struct HTTPFetchShared;

// HTTP/HTTPS client built on the same network library as the server (netRector on Linux, netRectorClient
// elsewhere) with BoringSSL for https. Two ways to call it:
//
//   blocking  HTTPFetchResponse r = fetcher.fetch(request);      // waits for the answer
//   async     fetcher.fetchAsync(request, [](HTTPFetchResponse &r) { ... });   // returns at once
//
// Blocking calls run on a private reactor thread of the fetcher, so they are safe from any thread except from
// inside an async callback (that would wait for itself: fetch() answers with an error there).
//
// Async callbacks run on a reactor thread. A fetcher built with a host (HTTPFetcher(host)) uses that host's
// reactor threads, i.e. the same ones as a WEBMaster that shares the host: the answer to an outgoing request
// can then be handled without any thread switch. Keep callbacks short, never block in them. The host must
// outlive the requests that were started on it.
//
// Every request uses its own connection ("Connection: close"); redirects are followed (see HTTPFetchRequest).
// https verifies the certificate and host name against the system roots (setCaFile() for a private CA).
class HTTPFetcher
{
public:
    using Callback = std::function<void(HTTPFetchResponse &)>;

    HTTPFetcher();                          // own reactor thread, started on first use
    explicit HTTPFetcher(NetHost &host);    // async calls use |host|; blocking calls still use a private thread
    ~HTTPFetcher();
    HTTPFetcher(const HTTPFetcher &) = delete;
    HTTPFetcher &operator=(const HTTPFetcher &) = delete;

    HTTPFetchResponse fetch(HTTPFetchRequest request);
    void fetchAsync(HTTPFetchRequest request, Callback done);

    // Shortcuts (blocking).
    HTTPFetchResponse get(const std::string &url);
    HTTPFetchResponse post(const std::string &url, const std::string &body, const std::string &contentType = "application/x-www-form-urlencoded");

    void setCaFile(const std::string &pemFile);          // before the first https request
    void setUserAgent(const std::string &userAgent);

    struct Job;                             // implementation detail

private:
    struct PrivateHost;

    void runOn(NetHost &host, std::shared_ptr<Job> job);
    NetHost &privateHost();

    NetHost *m_host;                        // may be null
    std::shared_ptr<HTTPFetchShared> m_shared;
    std::unique_ptr<PrivateHost> m_private;
};

} // namespace rizweb

#endif // CLSHTTPFETCHER_H
