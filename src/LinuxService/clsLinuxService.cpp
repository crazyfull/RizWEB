#include "clsLinuxService.h"
#include "../Utils/clsFileUtils.h"
#include "../Utils/clsRizLog.h"
#include <cstdlib>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace rizweb {

static const char *kUnitDirectory = "/etc/systemd/system/";

bool LinuxService::isRoot()
{
#if defined(__linux__)
    return getuid() == 0;
#else
    return false;
#endif
}

static bool validName(const std::string &name)
{
    if (name.empty())
        return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;           // the name ends up in a shell command
    return true;
}

bool LinuxService::createService(const std::string &serviceName, const std::string &description)
{
#if defined(__linux__)
    if (!isRoot()) {
        RIZLOG_ERROR("root access is needed to install a service");
        return false;
    }
    if (!validName(serviceName)) {
        RIZLOG_ERROR("invalid service name");
        return false;
    }
    std::string unit = "[Unit]\nDescription=" + description + "\nAfter=network.target\n\n"
                       "[Service]\nUser=root\nRestart=always\nType=simple\nWorkingDirectory=" + FileUtils::executableDirectory() +
                       "\nExecStart=" + FileUtils::executablePath() + "\nLimitNOFILE=65535\n\n[Install]\nWantedBy=multi-user.target\n";
    std::string unitPath = kUnitDirectory + serviceName + ".service";
    if (!FileUtils::writeFile(unitPath, unit)) {
        RIZLOG_ERROR("cannot write %s", unitPath.c_str());
        return false;
    }
    int result = system("systemctl daemon-reload");
    result |= system(("systemctl enable --now " + serviceName).c_str());
    return result == 0;
#else
    (void)serviceName; (void)description;
    return false;
#endif
}

bool LinuxService::removeService(const std::string &serviceName)
{
#if defined(__linux__)
    if (!isRoot() || !validName(serviceName))
        return false;
    system(("systemctl disable --now " + serviceName).c_str());
    std::string unitPath = kUnitDirectory + serviceName + ".service";
    bool removed = remove(unitPath.c_str()) == 0;
    system("systemctl daemon-reload");
    return removed;
#else
    (void)serviceName;
    return false;
#endif
}

} // namespace rizweb
