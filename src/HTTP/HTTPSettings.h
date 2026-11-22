#ifndef HTTPSETTINGS_H
#define HTTPSETTINGS_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace rizweb {

// Plain configuration of the server. Defaults are relative to the executable directory.
// Everything is read-only after WEBMaster::start().
class HTTPSettings
{
public:
    static constexpr const char *kFileName = "settings.ini";

    HTTPSettings();

    // Loads settings.ini (next to the executable unless |path| is given). When the file does not
    // exist yet it is created with the current values. Returns true when a file was read.
    bool load(const std::string &path = std::string());
    bool save(const std::string &path = std::string()) const;

    std::string locatedRoot;            // static web root          (default <exe>/www)
    std::string authRoot;               // login page files, /auth/ (default <exe>/auth)
    std::string resourceRoot;           // shared files, /resource/ (default <exe>/resource)
    std::string mimeTypesPath;          // optional mime.types.db   (default <resource>/rizweb/mime.types.db)
    std::string serverHostName;         // empty = accept any Host
    std::string listenIp = "0.0.0.0";
    std::string sslCertPath;
    std::string sslKeyPath;
    bool selfSignedTls = false;         // generate a throw-away certificate when no cert/key is configured
    uint16_t httpPort = 8080;           // 0 = no plain HTTP listener
    uint16_t sslPort = 443;
    bool directoryBrowsing = true;
    bool authenticationEnabled = false;
    std::string securePath = "/";
    uint32_t sessionExpirationMinutes = 20;
    uint32_t keepAliveSeconds = 20;     // idle time before a connection is closed
    size_t maxBodyBytes = 1024 * 1024;
    size_t maxWebSocketMessageBytes = 1024 * 1024;
    bool webSocketSameOriginOnly = true;
    std::string webSocketPath = "/ws";
};

} // namespace rizweb

#endif // HTTPSETTINGS_H
