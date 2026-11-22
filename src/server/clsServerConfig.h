#ifndef RIZWEB_CLSSERVERCONFIG_H
#define RIZWEB_CLSSERVERCONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace rizweb {

class IniFile;

#define RIZWEB_VERSION "2.0.0"

struct ServerConfig
{
    // ---- listening ----
    std::string listenIp = "0.0.0.0";   // "*" or "::" for IPv4 + IPv6
    uint16_t httpPort = 8080;           // 0 = no plain HTTP listener
    uint16_t httpsPort = 0;             // 0 = no TLS listener (needs a certificate or tlsSelfSigned)
    std::string hostName;               // when set, requests for any other Host get 404

    // ---- TLS (BoringSSL through netRector) ----
    std::string tlsCertFile;
    std::string tlsKeyFile;
    bool tlsSelfSigned = false;         // throw-away certificate, for tests only
    int tlsKeyThreads = 0;              // >0: RSA/ECDSA signing on worker threads, keeps the reactor free

    // ---- content ----
    std::string serverName = "RizWEB/" RIZWEB_VERSION;
    std::string documentRoot = "www";
    std::string resourceRoot = "resource";
    std::string mimeTypesFile;          // optional extra types (resource/rizweb/mime.types.db format)
    bool directoryListing = false;
    bool enableGzip = true;

    // ---- limits and timeouts ----
    int keepAliveSeconds = 20;          // idle time before a keep-alive connection is closed
    int requestTimeoutSeconds = 30;     // time allowed to deliver one complete request
    int webSocketIdleSeconds = 0;       // 0 = no idle limit for WebSocket connections
    size_t maxHeaderBytes = 16 * 1024;
    size_t maxBodyBytes = 8 * 1024 * 1024;
    int maxRequestsPerConnection = 10000;
    size_t maxSendQueueBytes = 0;       // 0 = netRector default

    // ---- engine ----
    int threads = 0;                    // reactor threads, 0 = number of CPU cores
    int maxConnections = 65535;         // upper bound for the descriptor table
    int sessionMinutes = 20;

    // Reads the sections used by the old settings.ini ([httpServer] and [sslSetting]).
    // Keys that are missing keep the values already in this object.
    void loadFromIni(const IniFile& ini);
};

} // namespace rizweb

#endif // RIZWEB_CLSSERVERCONFIG_H
