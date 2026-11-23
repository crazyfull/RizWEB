#ifndef CLSHTTPBODYDECODER_H
#define CLSHTTPBODYDECODER_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace rizweb {

// Incremental request-body decoder for streaming routes. Feed it the bytes as they arrive; decoded body data
// (chunk framing removed) is delivered through |emit(const char *, size_t)| straight out of the buffer that was
// passed in, so nothing is copied or accumulated here. Handles Content-Length and Transfer-Encoding: chunked.
class HTTPBodyDecoder
{
public:
    enum class Status { NeedMore, Done, Error, TooLarge };
    struct Result
    {
        Status status = Status::NeedMore;
        size_t consumed = 0;        // input bytes used; when Done, the rest belongs to the next request
    };

    static HTTPBodyDecoder contentLength(uint64_t length) { return HTTPBodyDecoder(false, length); }
    static HTTPBodyDecoder chunked() { return HTTPBodyDecoder(true, 0); }

    // |limit|: maximum decoded size (0 = unlimited). |emit| returns false to abort (Result.status = Error).
    template <class Emit>
    Result feed(const char *data, size_t length, uint64_t limit, Emit &&emit);

    uint64_t decodedBytes() const { return m_decoded; }
    // Content-Length: the declared size. Chunked: 0 (unknown).
    uint64_t totalBytes() const { return m_chunked ? 0 : m_declared; }

private:
    HTTPBodyDecoder(bool isChunked, uint64_t declared) : m_chunked(isChunked), m_declared(declared), m_remaining(declared) {}

    enum class State { SizeLine, Data, DataCr, DataLf, Trailer, Done };

    bool m_chunked;
    uint64_t m_declared;
    uint64_t m_remaining;          // bytes left in the current chunk / body
    uint64_t m_decoded = 0;
    State m_state = State::SizeLine;
    uint64_t m_chunkSize = 0;
    int m_lineLength = 0;          // length of the current size / trailer line (bounded)
    bool m_inExtension = false;
    bool m_sawDigit = false;
    bool m_lineCr = false;
};

template <class Emit>
HTTPBodyDecoder::Result HTTPBodyDecoder::feed(const char *data, size_t length, uint64_t limit, Emit &&emit)
{
    Result result;
    size_t position = 0;

    if (!m_chunked) {
        if (limit != 0 && m_declared > limit) {
            result.status = Status::TooLarge;
            return result;
        }
        size_t take = static_cast<size_t>(m_remaining < length ? m_remaining : length);
        if (take > 0 && !emit(data, take)) {
            result.status = Status::Error;
            return result;
        }
        m_remaining -= take;
        m_decoded += take;
        result.consumed = take;
        result.status = m_remaining == 0 ? Status::Done : Status::NeedMore;
        return result;
    }

    while (position < length) {
        switch (m_state) {
        case State::Data: {
            size_t available = length - position;
            size_t take = static_cast<size_t>(m_remaining < available ? m_remaining : available);
            if (limit != 0 && m_decoded + take > limit) {
                result.status = Status::TooLarge;
                result.consumed = position;
                return result;
            }
            if (!emit(data + position, take)) {
                result.status = Status::Error;
                result.consumed = position;
                return result;
            }
            position += take;
            m_remaining -= take;
            m_decoded += take;
            if (m_remaining == 0)
                m_state = State::DataCr;
            break;
        }
        case State::DataCr:
            if (data[position] != '\r') { result.status = Status::Error; result.consumed = position; return result; }
            position++;
            m_state = State::DataLf;
            break;
        case State::DataLf:
            if (data[position] != '\n') { result.status = Status::Error; result.consumed = position; return result; }
            position++;
            m_state = State::SizeLine;
            m_chunkSize = 0; m_lineLength = 0; m_inExtension = false; m_sawDigit = false; m_lineCr = false;
            break;
        case State::SizeLine: {
            char c = data[position++];
            if (++m_lineLength > 256) { result.status = Status::Error; result.consumed = position; return result; }
            if (m_lineCr) {
                if (c != '\n') { result.status = Status::Error; result.consumed = position; return result; }
                if (!m_sawDigit) { result.status = Status::Error; result.consumed = position; return result; }
                if (m_chunkSize == 0) {
                    m_state = State::Trailer;
                    m_lineLength = 0; m_lineCr = false;
                } else {
                    m_remaining = m_chunkSize;
                    m_state = State::Data;
                }
                break;
            }
            if (c == '\r') { m_lineCr = true; break; }
            if (m_inExtension) break;
            if (c == ';' || c == ' ' || c == '\t') { m_inExtension = true; break; }
            int digit = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (digit < 0 || m_chunkSize > (UINT64_MAX >> 5)) { result.status = Status::Error; result.consumed = position; return result; }
            m_chunkSize = m_chunkSize * 16 + static_cast<uint64_t>(digit);
            m_sawDigit = true;
            if (limit != 0 && m_decoded + m_chunkSize > limit) { result.status = Status::TooLarge; result.consumed = position; return result; }
            break;
        }
        case State::Trailer: {
            // trailer lines are skipped; the section ends with an empty line
            char c = data[position++];
            if (++m_lineLength > 8192) { result.status = Status::Error; result.consumed = position; return result; }
            if (m_lineCr) {
                if (c != '\n') { result.status = Status::Error; result.consumed = position; return result; }
                bool emptyLine = m_lineLength == 2;
                m_lineLength = 0; m_lineCr = false;
                if (emptyLine) {
                    m_state = State::Done;
                    result.status = Status::Done;
                    result.consumed = position;
                    return result;
                }
                break;
            }
            if (c == '\r') m_lineCr = true;
            break;
        }
        case State::Done:
            result.status = Status::Done;
            result.consumed = position;
            return result;
        }
    }
    result.consumed = position;
    result.status = m_state == State::Done ? Status::Done : Status::NeedMore;
    return result;
}

} // namespace rizweb

#endif // CLSHTTPBODYDECODER_H
