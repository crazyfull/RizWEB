// Test double for netrector::TCPSocket: same public signatures, but it records what would be
// sent instead of touching the network. Only used by the unit tests; the real library is used
// for every real build.
#ifndef FAKE_CLSTCPSOCKET_H
#define FAKE_CLSTCPSOCKET_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

namespace netrector {

class TLSContext;

struct SendQueue
{
    using ReleaseFn = void (*)(void* data, void* arg);
};

class TCPSocket
{
public:
    enum socketStatus { Ready = 1, Closed = 2, Closing = 3, Connecting = 4, Connected = 5, Handshaking = 6 };

    virtual ~TCPSocket() {}
    virtual void onAccepted() {}
    virtual void onClose() {}
    virtual void onReceiveData(const uint8_t*, size_t) {}
    virtual void onPause() {}
    virtual void onResume() {}

    void setMaxSendQueue(size_t) {}
    void setIdleTimeout(uint32_t seconds) { idleTimeout = seconds; }
    bool enableTLS(TLSContext*, bool = false, const char* = nullptr) { return true; }
    bool isTLS() const { return false; }
    bool getPeerAddress(std::string& ip, uint16_t& port) const { ip = "203.0.113.9"; port = 40000; return true; }
    bool isWriteBlocked() const { return blocked; }
    socketStatus getStatus() const { return status; }

    bool send(const void* data, size_t length)
    {
        if (status != Connected) return false;
        sent.append(static_cast<const char*>(data), length);
        return true;
    }

    bool sendv(const struct iovec* iov, int count)
    {
        for (int i = 0; i < count; i++)
            if (!send(iov[i].iov_base, iov[i].iov_len)) return false;
        return true;
    }

    // everything is "sent" at once, so the release callback runs right away (like netRector with an empty queue)
    bool sendNoCopy(const void* data, size_t length, SendQueue::ReleaseFn release = nullptr, void* arg = nullptr)
    {
        bool ok = send(data, length);
        sentNoCopyCalls++;
        if (release) release(const_cast<void*>(data), arg);
        return ok;
    }

    bool sendFile(int fd, off_t offset, size_t length)
    {
        if (status != Connected) return false;
        sendFileCalls++;
        std::string buffer(length, '\0');
        size_t done = 0;
        while (done < length) {
            ssize_t n = ::pread(fd, &buffer[done], length - done, offset + static_cast<off_t>(done));
            if (n <= 0) return false;
            done += static_cast<size_t>(n);
        }
        sent += buffer;
        return true;
    }

    void close(bool force = false)
    {
        if (status == Closed) return;
        closeCalls++;
        forced = force;
        status = Closed;
        onClose();
    }

    // ---- test controls ----
    std::string sent;
    bool blocked = false;
    bool forced = false;
    int closeCalls = 0;
    int sendFileCalls = 0;
    int sentNoCopyCalls = 0;
    uint32_t idleTimeout = 0;
    socketStatus status = Connected;
};

} // namespace netrector

#endif // FAKE_CLSTCPSOCKET_H
