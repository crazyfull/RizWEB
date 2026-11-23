#ifndef CLSHTTPSTATICFILES_H
#define CLSHTTPSTATICFILES_H
#include "clsCompressionCache.h"
#include "clsFileCache.h"
#include <string>

namespace rizweb {

class HTTPClient;
class HTTPRequest;
class HTTPResponse;
class WEBMaster;

// Serves files and directory listings from a root directory.
//  * paths are resolved with realpath and must stay inside the root (no traversal, no symlink escape)
//  * dot-files are never served (except /.well-known/)
//  * GET + HEAD, Range (single range), ETag / Last-Modified / 304
//  * gzip / deflate for the configured file types, compressed once and cached in memory
//  * resolved files (path, validators, small content) are cached for FileCacheSeconds: a hot URL costs no syscalls except send
//  * small files go out in one send, big files are streamed with back-pressure (sendfile on Linux)
class HTTPStaticFiles
{
public:
    explicit HTTPStaticFiles(WEBMaster &master);

    void serve(HTTPClient &client, HTTPRequest &request, HTTPResponse &response, const std::string &root, const std::string &relativeUrlPath);

private:
    // Returns the real path of root + relative, or an empty string when it escapes the root / is hidden.
    static std::string resolve(const std::string &root, const std::string &relativeUrlPath);
    FileCache::EntryPtr buildEntry(const std::string &path, unsigned long long size, time_t modified);
    void serveFile(HTTPClient &client, HTTPRequest &request, HTTPResponse &response, const FileCache::Entry &entry);
    void browse(HTTPRequest &request, HTTPResponse &response, const std::string &directory);

    WEBMaster &m_master;
    CompressionCache m_compressionCache;
    FileCache m_fileCache;
};

} // namespace rizweb

#endif // CLSHTTPSTATICFILES_H
