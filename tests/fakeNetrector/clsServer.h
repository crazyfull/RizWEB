#ifndef FAKE_CLSSERVER_H
#define FAKE_CLSSERVER_H
#include "clsTCPSocket.h"
#include <algorithm>
#include <thread>
namespace netrector {
class Server
{
public:
    typedef TCPSocket* (*acceptCallback)(void*);
    explicit Server(int, int = 1, size_t = 0, int = 0) {}
    bool AddNewListener(int, const char* = nullptr, acceptCallback = nullptr, void* = nullptr) { return true; }
    bool start() { return true; }
    void stop() {}
    void setUseGarbageCollector(bool) {}
};
}
#endif
