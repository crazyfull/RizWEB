#ifndef RIZWEB_CLSSTATICFILEHANDLER_H
#define RIZWEB_CLSSTATICFILEHANDLER_H

#include "clsGzipCache.h"
#include "clsHttpRequest.h"
#include "clsHttpResponse.h"
#include "clsMimeTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace rizweb {

struct StaticFileOptions
{
    std::string rootDirectory;
    std::vector<std::string> indexFiles = {"index.html", "index.htm"};
    bool directoryListing = false;
    bool serveDotFiles = false;     // names starting with '.' are refused otherwise
    bool enableGzip = true;
    size_t gzipMinBytes = 256;
    size_t gzipMaxBytes = 1024 * 1024;
    // Cache-Control max-age by extension, for example {"png", 600}
    std::unordered_map<std::string, int> cacheSecondsByExtension;
    std::string listingCssUrl = "/resource/rizweb/brows.css";
};

// Serves files below one directory. Everything the client controls is validated:
//  - the path is already decoded and normalized by the parser (no "..", no NUL),
//  - the real path (symlinks resolved) must stay inside the root,
//  - dot files are refused unless allowed,
//  - directory listings are HTML escaped.
// Supports HEAD, conditional requests (ETag / Last-Modified), single byte ranges and
// cached gzip. The file itself is sent by the connection with sendfile.
class StaticFileHandler
{
public:
    StaticFileHandler(StaticFileOptions options, std::shared_ptr<const MimeTypes> mimeTypes);

    // subPath: path below the mount point, normalized, starting with '/'
    void serve(const HttpRequest& request, HttpResponse& response, std::string_view subPath) const;

private:
    bool resolveInsideRoot(const std::string& candidate, std::string& resolved) const;
    void serveDirectory(const HttpRequest& request, HttpResponse& response, const std::string& directory) const;
    void serveFile(const HttpRequest& request, HttpResponse& response, UniqueFd fd, const struct stat& info,
                   const std::string& realPath) const;
    void listDirectory(const HttpRequest& request, HttpResponse& response, const std::string& directory) const;
    bool openRegularFile(const std::string& path, UniqueFd& fd, struct stat& info) const;

    StaticFileOptions m_options;
    std::shared_ptr<const MimeTypes> m_mimeTypes;
    std::string m_rootReal;
    mutable GzipCache m_gzipCache;
};

} // namespace rizweb

#endif // RIZWEB_CLSSTATICFILEHANDLER_H
