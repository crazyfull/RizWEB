#ifndef CLSHTTPUPLOAD_H
#define CLSHTTPUPLOAD_H
#include <cstddef>
#include <functional>
#include <memory>

namespace rizweb {

class HTTPClient;
class HTTPRequest;
class HTTPResponse;

// Receiver of a streamed request body (see WEBMaster::stream()). One object per request.
//   onBody()   decoded body bytes, in order, straight from the network buffer (valid only during the call).
//              Return false to abort: the connection is answered (onAbort may send the reason) and closed.
//   onEnd()    the whole body arrived: answer the request here.
//   onAbort()  called when onBody returned false, the body is over the limit, the body is malformed or the
//              connection dropped. |response| is null when the connection is gone.
class UploadSink
{
public:
    virtual ~UploadSink() = default;
    virtual bool onBody(const char *data, size_t length) = 0;
    virtual void onEnd(HTTPClient &client, HTTPRequest &request, HTTPResponse &response) = 0;
    virtual void onAbort(HTTPResponse *response) { (void)response; }
};

// Called when the headers of a request to a streaming route have been parsed (after authentication, CORS,
// rate limiting and middleware). Return the sink, or nullptr after answering the request yourself (reject).
using UploadFactory = std::function<std::unique_ptr<UploadSink>(HTTPClient &, HTTPRequest &, HTTPResponse &)>;

} // namespace rizweb

#endif // CLSHTTPUPLOAD_H
