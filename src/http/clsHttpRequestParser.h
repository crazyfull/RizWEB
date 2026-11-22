#ifndef RIZWEB_CLSHTTPREQUESTPARSER_H
#define RIZWEB_CLSHTTPREQUESTPARSER_H

#include "clsHttpRequest.h"

#include <cstddef>
#include <string>

namespace rizweb {

struct HttpParserLimits
{
    size_t maxHeaderBytes = 16 * 1024;       // request line + all headers
    size_t maxHeaderCount = 100;
    size_t maxUriBytes = 8 * 1024;
    size_t maxBodyBytes = 8 * 1024 * 1024;   // Content-Length or the sum of all chunks
};

enum class ParseStatus { needMore, complete, error };

// Incremental HTTP/1.x request parser.
//
// feed() may be called with any slicing of the byte stream. It never reads past the
// end of the data it was given and never modifies it. When one request is complete
// it stops and reports how many bytes it used, so pipelined requests that follow in
// the same buffer stay untouched and can be fed again after reset().
//
// Strictness (request smuggling / traversal defence): bare LF line endings, obsolete
// line folding, spaces before the colon, conflicting or duplicated Content-Length,
// Content-Length together with Transfer-Encoding, paths that climb above the root
// and malformed percent escapes are all rejected.
class HttpRequestParser
{
public:
    explicit HttpRequestParser(HttpParserLimits limits = HttpParserLimits());

    ParseStatus feed(const char* data, size_t length, size_t& consumed);

    // true once the request line and headers are parsed (the body may still be arriving)
    bool isHeadComplete() const { return m_headComplete; }

    // true when the client sent "Expect: 100-continue" and is waiting for the interim response
    bool expectsContinue() const { return m_expectContinue; }

    // true when at least one byte of the current request has been seen
    bool hasStarted() const { return m_started; }

    HttpRequest& request() { return m_request; }
    const HttpRequest& request() const { return m_request; }

    // HTTP status to answer with when feed() returned ParseStatus::error
    int errorStatus() const { return m_errorStatus; }

    // Prepares for the next request on the same connection (limits stay).
    void reset();

private:
    enum class State { head, bodyFixed, chunkSize, chunkData, chunkDataEnd, trailers, done, failed };

    bool parseHead();
    bool parseRequestLine(std::string_view line);
    bool parseHeaderLines(std::string_view lines);
    bool finishHead();
    bool parseChunkSizeLine();
    bool fail(int status);

    HttpParserLimits m_limits;
    HttpRequest m_request;
    State m_state = State::head;
    std::string m_head;
    std::string m_line;           // chunk size / trailer line being collected
    int m_terminatorState = 0;    // progress of "\r\n\r\n" detection
    int m_crlfProgress = 0;       // progress of the CRLF after chunk data
    size_t m_remaining = 0;       // bytes left in the current body part
    size_t m_trailerBytes = 0;
    size_t m_contentLength = 0;
    bool m_chunked = false;
    bool m_started = false;
    bool m_headComplete = false;
    bool m_expectContinue = false;
    int m_errorStatus = 400;
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPREQUESTPARSER_H
