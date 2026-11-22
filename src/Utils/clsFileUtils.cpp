#include "clsFileUtils.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace rizweb {
namespace fs = std::filesystem;

FileInfo FileUtils::stat(const std::string &path)
{
    FileInfo info;
#if defined(_WIN32)
    struct _stat64 st;
    if (_stat64(path.c_str(), &st) != 0)
        return info;
    info.isDirectory = (st.st_mode & _S_IFDIR) != 0;
#else
    struct ::stat st;
    if (::stat(path.c_str(), &st) != 0)
        return info;
    info.isDirectory = S_ISDIR(st.st_mode);
#endif
    info.exists = true;
    info.size = static_cast<unsigned long long>(st.st_size);
    info.modified = static_cast<time_t>(st.st_mtime);
    return info;
}

std::string FileUtils::executablePath()
{
#if defined(_WIN32)
    char buffer[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::string(buffer, length);
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0)
        return "";
    std::error_code ec;
    return fs::weakly_canonical(buffer, ec).string();
#else
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    return length > 0 ? std::string(buffer, static_cast<size_t>(length)) : std::string();
#endif
}

std::string FileUtils::executableDirectory()
{
    std::string path = executablePath();
    if (path.empty())
        return fs::current_path().string();
    return fs::path(path).parent_path().string();
}

bool FileUtils::readFile(const std::string &path, std::string &out, size_t maxBytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    std::streamoff size = file.tellg();
    if (size < 0 || static_cast<size_t>(size) > maxBytes)
        return false;
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(&out[0], size);
    return static_cast<bool>(file) || size == 0;
}

bool FileUtils::writeFile(const std::string &path, const std::string &content)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(file);
}

bool FileUtils::createDirectories(const std::string &path)
{
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

bool FileUtils::isDirectory(const std::string &path)
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::string FileUtils::removeTrailingSlash(std::string path)
{
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    return path;
}

static struct tm toGmt(time_t value)
{
    struct tm parts;
#if defined(_WIN32)
    gmtime_s(&parts, &value);
#else
    gmtime_r(&value, &parts);
#endif
    return parts;
}

std::string FileUtils::formatHttpDate(time_t value)
{
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm p = toGmt(value);
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT", days[p.tm_wday], p.tm_mday, months[p.tm_mon], p.tm_year + 1900, p.tm_hour, p.tm_min, p.tm_sec);
    return buffer;
}

bool FileUtils::parseHttpDate(const std::string &text, time_t &out)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char day[4] = {0}, month[4] = {0};
    int mday, year, hour, minute, second;
    if (sscanf(text.c_str(), "%3s, %d %3s %d %d:%d:%d GMT", day, &mday, month, &year, &hour, &minute, &second) != 7)
        return false;
    int monthIndex = -1;
    for (int i = 0; i < 12; i++)
        if (strcmp(month, months[i]) == 0)
            monthIndex = i;
    if (monthIndex < 0)
        return false;
    // days-from-civil, avoids timegm() which is not portable
    int y = year - (monthIndex < 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned m = static_cast<unsigned>(monthIndex + 1);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + static_cast<unsigned>(mday) - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
    out = static_cast<time_t>(days * 86400 + hour * 3600 + minute * 60 + second);
    return true;
}

} // namespace rizweb
