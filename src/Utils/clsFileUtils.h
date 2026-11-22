#ifndef CLSFILEUTILS_H
#define CLSFILEUTILS_H
#include <ctime>
#include <string>

namespace rizweb {

struct FileInfo
{
    bool exists = false;
    bool isDirectory = false;
    unsigned long long size = 0;
    time_t modified = 0;
};

class FileUtils
{
public:
    static FileInfo stat(const std::string &path);
    // Directory of the running executable (with trailing separator removed).
    static std::string executableDirectory();
    static std::string executablePath();
    static bool readFile(const std::string &path, std::string &out, size_t maxBytes);
    static bool writeFile(const std::string &path, const std::string &content);
    static bool createDirectories(const std::string &path);
    static bool isDirectory(const std::string &path);
    static std::string removeTrailingSlash(std::string path);

    // RFC 7231 "IMF-fixdate", e.g. "Sun, 06 Nov 1994 08:49:37 GMT"
    static std::string formatHttpDate(time_t value);
    static bool parseHttpDate(const std::string &text, time_t &out);
};

} // namespace rizweb

#endif // CLSFILEUTILS_H
