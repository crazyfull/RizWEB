#ifndef CLSHTTPFETCHCONNECTION_H
#define CLSHTTPFETCHCONNECTION_H
#include "../HTTP/clsHTTPBodyDecoder.h"
#include "../Utils/clsFileWriter.h"
#include "../clsNetBackend.h"
#include "clsHTTPFetchShared.h"
#include "clsHTTPFetchTypes.h"
#include "clsHTTPUrl.h"
#include <chrono>
#include <functional>
#include <memory>

namespace rizweb {

// One request / response exchange over its own connection (Connection: close). Runs on a reactor thread and
// reports the result exactly once through |done|. Used by HTTPFetcher.
class HTTPFetchConnection : public NetSocket
{
public:
    using Done = std::function<void(HTTPFetchResponse &&)>;

    // Creates the connection on |reactor|'s thread and starts it. |done| is always called (also on immediate failure).
    // Must be called from the thread of |reactor|.
    static void launch(NetReactor *reactor, const HTTPUrl &url, const HTTPFetchRequest &request,
                       std::shared_ptr<HTTPFetchShared> shared, Done done);

protected:
    void onConnected() override;
    void onReceiveData(const uint8_t *data, size_t length) override;
    void onClose() override;

private:
    HTTPFetchConnection(const HTTPUrl &url, const HTTPFetchRequest &request, std::shared_ptr<HTTPFetchShared> shared, Done done);
    bool begin(NetReactor *reactor, std::string &error);
    std::string buildRequest() const;
    bool parseHead(const std::string &head);
    void consume(const char *data, size_t length);
    bool emit(const char *data, size_t length);
    void succeed();
    void fail(const std::string &message, bool closeSocket = true);
    void report();

    enum class Stage { Head, Body, Finished };
    enum class Framing { None, Length, Chunked, UntilClose };

    HTTPUrl m_url;
    HTTPFetchRequest m_requestCopy;
    std::shared_ptr<HTTPFetchShared> m_shared;
    Done m_done;
    HTTPFetchResponse m_response;
    Stage m_stage = Stage::Head;
    Framing m_framing = Framing::None;
    std::string m_head;
    std::unique_ptr<HTTPBodyDecoder> m_decoder;
    FileWriter m_file;
    uint64_t m_total = 0;
    bool m_reported = false;
    bool m_isHead = false;
};

} // namespace rizweb

#endif // CLSHTTPFETCHCONNECTION_H
