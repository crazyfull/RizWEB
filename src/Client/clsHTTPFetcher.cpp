#include "clsHTTPFetcher.h"
#include "../Utils/clsStringUtils.h"
#include "clsHTTPFetchConnection.h"
#include <future>
#include <mutex>
#include <thread>

namespace rizweb {

namespace {
thread_local bool t_inCallback = false;       // true while a user callback runs on a reactor thread

NetReactor *reactorOf(NetHost &host)
{
#if defined(RIZWEB_USE_NETRECTOR)
    return host.getRoundRobinShard();
#else
    return &host;
#endif
}
} // namespace

struct HTTPFetcher::Job
{
    HTTPFetchRequest request;
    HTTPUrl url;
    Callback done;
    int redirects = 0;
    std::shared_ptr<HTTPFetchShared> shared;     // keeps TLS contexts alive for as long as the job runs
};

struct HTTPFetcher::PrivateHost
{
    std::mutex mutex;
    std::unique_ptr<NetHost> host;
    std::thread thread;

    ~PrivateHost()
    {
        if (!host)
            return;
        host->stop();                   // closes the open connections: their callbacks report "closed"
        if (thread.joinable())
            thread.join();
    }
};

HTTPFetcher::HTTPFetcher() : m_host(nullptr), m_shared(std::make_shared<HTTPFetchShared>()), m_private(new PrivateHost) {}

HTTPFetcher::HTTPFetcher(NetHost &host) : m_host(&host), m_shared(std::make_shared<HTTPFetchShared>()), m_private(new PrivateHost) {}

HTTPFetcher::~HTTPFetcher() = default;

void HTTPFetcher::setCaFile(const std::string &pemFile)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->caFile = pemFile;
}

void HTTPFetcher::setUserAgent(const std::string &userAgent)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->userAgent = userAgent;
}

NetHost &HTTPFetcher::privateHost()
{
    std::lock_guard<std::mutex> lock(m_private->mutex);
    if (!m_private->host) {
#if defined(RIZWEB_USE_NETRECTOR)
        m_private->host.reset(new NetHost(4096, 1));
        m_private->host->start();
#else
        m_private->host.reset(new NetHost());
        NetHost *loop = m_private->host.get();
        m_private->thread = std::thread([loop]() { loop->run(); });
#endif
    }
    return *m_private->host;
}

// ---------------------------------------------------------------------------------------------

void HTTPFetcher::fetchAsync(HTTPFetchRequest request, Callback done)
{
    NetHost &host = m_host ? *m_host : privateHost();
    auto job = std::make_shared<Job>();
    job->request = std::move(request);
    job->done = std::move(done);
    job->shared = m_shared;
    runOn(host, job);
}

HTTPFetchResponse HTTPFetcher::fetch(HTTPFetchRequest request)
{
    if (t_inCallback) {
        HTTPFetchResponse response;
        response.error = "fetch() cannot be called from inside a fetch callback (it would wait for itself); use fetchAsync()";
        return response;
    }
    auto promise = std::make_shared<std::promise<HTTPFetchResponse>>();
    std::future<HTTPFetchResponse> future = promise->get_future();
    auto job = std::make_shared<Job>();
    job->request = std::move(request);
    job->shared = m_shared;
    job->done = [promise](HTTPFetchResponse &response) { promise->set_value(std::move(response)); };
    runOn(privateHost(), job);
    return future.get();
}

HTTPFetchResponse HTTPFetcher::get(const std::string &url)
{
    HTTPFetchRequest request;
    request.url = url;
    return fetch(std::move(request));
}

HTTPFetchResponse HTTPFetcher::post(const std::string &url, const std::string &body, const std::string &contentType)
{
    HTTPFetchRequest request;
    request.method = "POST";
    request.url = url;
    request.body = body;
    request.header("Content-Type", contentType);
    return fetch(std::move(request));
}

static void attempt(NetHost &host, std::shared_ptr<HTTPFetcher::Job> job);

void HTTPFetcher::runOn(NetHost &host, std::shared_ptr<Job> job)
{
    std::string error;
    if (!HTTPUrl::parse(job->request.url, job->url, error)) {
        HTTPFetchResponse response;
        response.error = error;
        response.finalUrl = job->request.url;
        t_inCallback = true;
        job->done(response);
        t_inCallback = false;
        return;
    }
    reactorOf(host)->post([&host, job]() { attempt(host, job); });
}

static bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static void attemptDone(NetHost &host, std::shared_ptr<HTTPFetcher::Job> job, HTTPFetchResponse &&response)
{
    response.redirects = job->redirects;
    const std::string *location = response.header("location");
    if (response.completed() && isRedirect(response.status) && location && job->request.followRedirects) {
        HTTPUrl next;
        std::string error;
        if (job->redirects >= job->request.maxRedirects) {
            response.error = "too many redirects";
        } else if (!job->url.resolve(*location, next, error)) {
            response.error = "bad redirect: " + error;
        } else if (job->url.https && !next.https) {
            response.error = "redirect from https to http refused";
        } else {
            HTTPFetchRequest &request = job->request;
            bool dropBody = response.status == 303 || ((response.status == 301 || response.status == 302) && request.method == "POST");
            if (dropBody && request.method != "HEAD")
                request.method = "GET";
            if (dropBody)
                request.body.clear();
            HTTPHeaderList kept;
            for (const auto &header : request.headers) {
                std::string name = StringUtils::toLower(header.first);
                if (dropBody && (name == "content-type" || name == "content-length"))
                    continue;
                if (!job->url.sameOrigin(next) && (name == "authorization" || name == "cookie" || name == "proxy-authorization"))
                    continue;                       // credentials never travel to another origin
                kept.push_back(header);
            }
            request.headers = std::move(kept);
            request.url = next.toString();
            job->url = next;
            job->redirects++;
            reactorOf(host)->post([&host, job]() { attempt(host, job); });
            return;
        }
    }
    t_inCallback = true;
    job->done(response);
    t_inCallback = false;
}

// Runs on a reactor thread.
static void attempt(NetHost &host, std::shared_ptr<HTTPFetcher::Job> job)
{
    HTTPFetchConnection::launch(reactorOf(host), job->url, job->request, job->shared, [&host, job](HTTPFetchResponse &&response) {
        attemptDone(host, job, std::move(response));
    });
}

} // namespace rizweb
