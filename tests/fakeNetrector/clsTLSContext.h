#ifndef FAKE_CLSTLSCONTEXT_H
#define FAKE_CLSTLSCONTEXT_H
#include <string>
#include <vector>
namespace netrector {
class TLSContext
{
public:
    static bool isSupported() { return true; }
    static TLSContext* createServer(const char*, const char*, std::string* = nullptr) { return new TLSContext; }
    static TLSContext* createServerSelfSigned(const char* = "localhost", std::string* = nullptr) { return new TLSContext; }
    bool setALPN(const std::vector<std::string>&) { return true; }
    bool enableAsyncPrivateKey(int) { return true; }
};
}
#endif
