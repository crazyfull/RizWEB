#ifndef CLSLINUXSERVICE_H
#define CLSLINUXSERVICE_H
#include <string>

namespace rizweb {

// Installs / removes a systemd unit for the running executable (Linux only, needs root).
class LinuxService
{
public:
    static bool isRoot();
    static bool createService(const std::string &serviceName, const std::string &description);
    static bool removeService(const std::string &serviceName);
};

} // namespace rizweb

#endif // CLSLINUXSERVICE_H
