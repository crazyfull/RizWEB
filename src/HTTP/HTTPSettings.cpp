#include "HTTPSettings.h"
#include "../Utils/clsFileUtils.h"
#include "../Utils/clsIniFile.h"
#include "../Utils/clsRizLog.h"

namespace rizweb {

static const char *kServer = "httpServer";
static const char *kSsl = "sslSetting";

HTTPSettings::HTTPSettings()
{
    std::string base = FileUtils::executableDirectory();
    locatedRoot = base + "/www";
    authRoot = base + "/auth";
    resourceRoot = base + "/resource";
    mimeTypesPath = resourceRoot + "/rizweb/mime.types.db";
}

bool HTTPSettings::load(const std::string &path)
{
    std::string file = path.empty() ? FileUtils::executableDirectory() + "/" + kFileName : path;
    IniFile ini;
    bool loaded = ini.load(file);

    locatedRoot = FileUtils::removeTrailingSlash(ini.getString(kServer, "LocatedRoot", locatedRoot));
    authRoot = FileUtils::removeTrailingSlash(ini.getString(kServer, "AuthRoot", authRoot));
    resourceRoot = FileUtils::removeTrailingSlash(ini.getString(kServer, "ResourceDir", resourceRoot));
    mimeTypesPath = ini.getString(kServer, "MimeTypesPath", resourceRoot + "/rizweb/mime.types.db");
    serverHostName = ini.getString(kServer, "DomainName", serverHostName);
    listenIp = ini.getString(kServer, "ListenIP", listenIp);
    httpPort = static_cast<uint16_t>(ini.getLong(kServer, "Port", httpPort));
    directoryBrowsing = ini.getBool(kServer, "IsDirectoryBrowsing", directoryBrowsing);
    authenticationEnabled = ini.getBool(kServer, "IsUseAuthentication", authenticationEnabled);
    securePath = ini.getString(kServer, "SecurePath", securePath);
    sessionExpirationMinutes = static_cast<uint32_t>(ini.getLong(kServer, "SessionExpirationTime", ini.getLong(kServer, "SeassionExpirationTime", sessionExpirationMinutes)));
    keepAliveSeconds = static_cast<uint32_t>(ini.getLong(kServer, "KeepAliveTime", keepAliveSeconds));
    workerThreads = static_cast<uint32_t>(ini.getLong(kServer, "WorkerThreads", workerThreads));
    workerQueueLimit = static_cast<uint32_t>(ini.getLong(kServer, "WorkerQueue", workerQueueLimit));
    asyncTimeoutSeconds = static_cast<uint32_t>(ini.getLong(kServer, "AsyncTimeout", asyncTimeoutSeconds));
    maxBodyBytes = static_cast<size_t>(ini.getLong(kServer, "MaxBodyBytes", static_cast<long>(maxBodyBytes)));
    securityHeaders = ini.getBool(kServer, "SecurityHeaders", securityHeaders);
    contentSecurityPolicy = ini.getString(kServer, "ContentSecurityPolicy", contentSecurityPolicy);
    hstsSeconds = static_cast<uint32_t>(ini.getLong(kServer, "HstsSeconds", hstsSeconds));
    maxUploadBytes = static_cast<uint64_t>(ini.getLong(kServer, "MaxUploadBytes", static_cast<long>(maxUploadBytes)));
    maxWebSocketMessageBytes = static_cast<size_t>(ini.getLong(kServer, "MaxWebSocketMessageBytes", static_cast<long>(maxWebSocketMessageBytes)));
    maxConnectionsPerIp = static_cast<uint32_t>(ini.getLong(kServer, "MaxConnectionsPerIp", maxConnectionsPerIp));
    maxRequestsPerSecondPerIp = static_cast<uint32_t>(ini.getLong(kServer, "MaxRequestsPerSecondPerIp", maxRequestsPerSecondPerIp));
    fileCacheSeconds = static_cast<uint32_t>(ini.getLong(kServer, "FileCacheSeconds", fileCacheSeconds));
    webSocketSameOriginOnly = ini.getBool(kServer, "WebSocketSameOriginOnly", webSocketSameOriginOnly);
    webSocketPath = ini.getString(kServer, "WebSocketPath", webSocketPath);

    sslCertPath = ini.getString(kSsl, "SSLCertPath", sslCertPath);
    sslKeyPath = ini.getString(kSsl, "SSLKeyPath", sslKeyPath);
    sslPort = static_cast<uint16_t>(ini.getLong(kSsl, "SSLPort", sslPort));
    selfSignedTls = ini.getBool(kSsl, "SelfSigned", selfSignedTls);

    if (!loaded) {
        if (save(file))
            RIZLOG_INFO("settings file created: %s", file.c_str());
    } else {
        RIZLOG_INFO("settings loaded from %s", file.c_str());
    }
    return loaded;
}

bool HTTPSettings::save(const std::string &path) const
{
    std::string file = path.empty() ? FileUtils::executableDirectory() + "/" + kFileName : path;
    IniFile ini;
    ini.setString(kServer, "LocatedRoot", locatedRoot);
    ini.setString(kServer, "AuthRoot", authRoot);
    ini.setString(kServer, "ResourceDir", resourceRoot);
    ini.setString(kServer, "MimeTypesPath", mimeTypesPath);
    ini.setString(kServer, "DomainName", serverHostName);
    ini.setString(kServer, "ListenIP", listenIp);
    ini.setLong(kServer, "Port", httpPort);
    ini.setBool(kServer, "IsDirectoryBrowsing", directoryBrowsing);
    ini.setBool(kServer, "IsUseAuthentication", authenticationEnabled);
    ini.setString(kServer, "SecurePath", securePath);
    ini.setLong(kServer, "SessionExpirationTime", sessionExpirationMinutes);
    ini.setLong(kServer, "KeepAliveTime", keepAliveSeconds);
    ini.setLong(kServer, "WorkerThreads", workerThreads);
    ini.setLong(kServer, "WorkerQueue", workerQueueLimit);
    ini.setLong(kServer, "AsyncTimeout", asyncTimeoutSeconds);
    ini.setLong(kServer, "MaxBodyBytes", static_cast<long>(maxBodyBytes));
    ini.setBool(kServer, "SecurityHeaders", securityHeaders);
    ini.setString(kServer, "ContentSecurityPolicy", contentSecurityPolicy);
    ini.setLong(kServer, "HstsSeconds", hstsSeconds);
    ini.setLong(kServer, "MaxUploadBytes", static_cast<long>(maxUploadBytes));
    ini.setLong(kServer, "MaxWebSocketMessageBytes", static_cast<long>(maxWebSocketMessageBytes));
    ini.setLong(kServer, "MaxConnectionsPerIp", maxConnectionsPerIp);
    ini.setLong(kServer, "MaxRequestsPerSecondPerIp", maxRequestsPerSecondPerIp);
    ini.setLong(kServer, "FileCacheSeconds", fileCacheSeconds);
    ini.setBool(kServer, "WebSocketSameOriginOnly", webSocketSameOriginOnly);
    ini.setString(kServer, "WebSocketPath", webSocketPath);
    ini.setString(kSsl, "SSLCertPath", sslCertPath);
    ini.setString(kSsl, "SSLKeyPath", sslKeyPath);
    ini.setLong(kSsl, "SSLPort", sslPort);
    ini.setBool(kSsl, "SelfSigned", selfSignedTls);
    return ini.save(file);
}

} // namespace rizweb
