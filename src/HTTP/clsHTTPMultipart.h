#ifndef CLSHTTPMULTIPART_H
#define CLSHTTPMULTIPART_H
#include <cstddef>
#include <string>

namespace rizweb {

// Header information of one multipart/form-data part.
struct MultipartPart
{
    std::string name;            // form field name
    std::string fileName;        // as sent by the client (NOT sanitized; see sanitizeFileName)
    std::string contentType;     // part Content-Type ("" when not sent)
    bool isFile = false;         // the part has a filename parameter
};

// Receives the parts while HTTPMultipartParser::feed() runs. Return false to abort parsing.
class MultipartHandler
{
public:
    virtual ~MultipartHandler() = default;
    virtual bool onPartBegin(const MultipartPart &part) = 0;
    virtual bool onPartData(const char *data, size_t length) = 0;     // points into the buffer given to feed()
    virtual bool onPartEnd() = 0;
};

// Incremental multipart/form-data parser (RFC 7578 / 2046). Bytes can arrive in any split; part data is passed on
// straight out of the input buffer (no accumulation), only part headers (<= 8 KB) and a boundary-sized tail are kept.
class HTTPMultipartParser
{
public:
    enum class Status { NeedMore, Finished, Error };
    static constexpr size_t kMaxPartHeaderBytes = 8 * 1024;

    explicit HTTPMultipartParser(const std::string &boundary);

    // Extracts boundary=... from a Content-Type value; false when it is not multipart/form-data with a boundary.
    static bool boundaryFromContentType(const std::string &contentType, std::string &boundary);
    // Last path component of a client supplied name with control characters removed (never empty, <= 255 bytes).
    static std::string sanitizeFileName(const std::string &name);

    Status feed(const char *data, size_t length, MultipartHandler &handler);
    bool isFinished() const { return m_state == State::Epilogue; }
    const std::string &error() const { return m_error; }

private:
    enum class State { Preamble, AfterDelimiter, Headers, Body, Epilogue };

    Status fail(const char *message);
    bool parsePartHeaders(const std::string &block, MultipartPart &part);

    std::string m_delimiter;        // "\r\n--" + boundary
    State m_state = State::Preamble;
    std::string m_carry;            // Preamble / Body: tail that may be the start of a delimiter. AfterDelimiter / Headers: bytes collected so far
    std::string m_error;
};

} // namespace rizweb

#endif // CLSHTTPMULTIPART_H
