#include "clsServerConfig.h"

#include "../util/clsIniFile.h"

namespace rizweb {

void ServerConfig::loadFromIni(const IniFile& ini)
{
    const std::string http = "httpServer";
    const std::string ssl = "sslSetting";

    documentRoot = ini.getString(http, "LocatedRoot", documentRoot);
    resourceRoot = ini.getString(http, "ResourceDir", resourceRoot);
    // the old code read this key from the ssl section by mistake; both places are accepted
    mimeTypesFile = ini.getString(http, "mimeTypesPath", ini.getString(ssl, "mimeTypesPath", mimeTypesFile));
    httpPort = static_cast<uint16_t>(ini.getInt(http, "Port", httpPort));
    listenIp = ini.getString(http, "ListenIP", listenIp);
    hostName = ini.getString(http, "DomainName", hostName);
    directoryListing = ini.getBool(http, "IsDirectoryBrowsing", directoryListing);
    keepAliveSeconds = static_cast<int>(ini.getInt(http, "KeepAliveTime", keepAliveSeconds));
    sessionMinutes = static_cast<int>(ini.getInt(http, "SeassionExpirationTime", sessionMinutes));
    threads = static_cast<int>(ini.getInt(http, "Threads", threads));

    tlsCertFile = ini.getString(ssl, "SSLCertPath", tlsCertFile);
    tlsKeyFile = ini.getString(ssl, "SSLKeyPath", tlsKeyFile);
    if (!tlsCertFile.empty() && !tlsKeyFile.empty())
        httpsPort = static_cast<uint16_t>(ini.getInt(ssl, "SSLPort", 443));
}

} // namespace rizweb
