#include "clsHTTPStaticFiles.h"
#include "../Utils/clsFileUtils.h"
#include "../Utils/clsRizLog.h"
#include "../Utils/clsStringUtils.h"
#include "../clsHTTPClient.h"
#include "../clsWEBMaster.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

namespace rizweb {
namespace fs = std::filesystem;

static constexpr unsigned long long kSmallFile = 64 * 1024;
static constexpr size_t kMaxListEntries = 5000;

HTTPStaticFiles::HTTPStaticFiles(WEBMaster &master) : m_master(master) {}

static bool hasHiddenSegment(const std::string &urlPath)
{
    size_t start = 0;
    while (start < urlPath.size()) {
        size_t end = urlPath.find('/', start);
        if (end == std::string::npos)
            end = urlPath.size();
        if (end > start && urlPath[start] == '.' && urlPath.compare(start, end - start, ".well-known") != 0)
            return true;
        start = end + 1;
    }
    return false;
}

std::string HTTPStaticFiles::resolve(const std::string &root, const std::string &relativeUrlPath)
{
    if (root.empty() || hasHiddenSegment(relativeUrlPath))
        return {};
    std::error_code ec;
    fs::path realRoot = fs::canonical(root, ec);
    if (ec)
        return {};
    fs::path target = fs::canonical(realRoot / fs::path(relativeUrlPath.substr(relativeUrlPath.empty() ? 0 : 1)), ec);
    if (ec)
        return {};
    // component-wise prefix check: "/srv/www2" is not inside "/srv/www"
    auto rootIt = realRoot.begin();
    auto targetIt = target.begin();
    for (; rootIt != realRoot.end(); ++rootIt, ++targetIt)
        if (targetIt == target.end() || *rootIt != *targetIt)
            return {};
    return target.string();
}

void HTTPStaticFiles::serve(HTTPClient &client, HTTPRequest &request, HTTPResponse &response, const std::string &root, const std::string &relativeUrlPath)
{
    if (request.method() != HTTPMethod::Get && request.method() != HTTPMethod::Head) {
        response.methodNotAllowed("GET, HEAD");
        return;
    }
    if (relativeUrlPath.empty() || relativeUrlPath[0] != '/') {
        response.badRequest();
        return;
    }

    std::string path = resolve(root, relativeUrlPath);
    if (path.empty()) {
        response.notFound();
        return;
    }
    request.setFilePath(path);

    FileInfo info = FileUtils::stat(path);
    if (!info.exists) {
        response.notFound();
        return;
    }

    if (info.isDirectory) {
        if (!request.isDirectoryRequest()) {
            std::string location = StringUtils::urlEncode(request.urlPath(), true) + "/";
            if (!request.queryString().empty())
                location += "?" + request.queryString();
            response.redirect(location, 301);
            return;
        }
        for (const char *index : {"index.html", "index.htm"}) {
            std::string candidate = path + "/" + index;
            FileInfo indexInfo = FileUtils::stat(candidate);
            if (indexInfo.exists && !indexInfo.isDirectory) {
                serveFile(client, request, response, candidate, indexInfo.size, indexInfo.modified);
                return;
            }
        }
        if (!m_master.settings().directoryBrowsing) {
            response.accessDenied();
            return;
        }
        browse(request, response, path);
        return;
    }

    serveFile(client, request, response, path, info.size, info.modified);
}

// ---------------------------------------------------------------------------------------------

static bool parseRange(const std::string &header, unsigned long long size, unsigned long long &first, unsigned long long &last, bool &satisfiable)
{
    // returns false when the header must be ignored (unsupported form); satisfiable=false -> 416
    satisfiable = true;
    if (!StringUtils::startsWith(header, "bytes=") || header.find(',') != std::string::npos)
        return false;
    std::string spec = header.substr(6);
    size_t dash = spec.find('-');
    if (dash == std::string::npos)
        return false;
    std::string a = spec.substr(0, dash), b = spec.substr(dash + 1);
    auto digits = [](const std::string &s) { return !s.empty() && s.size() < 19 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    if (a.empty()) {                       // suffix: last N bytes
        if (!digits(b))
            return false;
        unsigned long long n = std::stoull(b);
        if (n == 0 || size == 0) { satisfiable = false; return true; }
        first = n >= size ? 0 : size - n;
        last = size - 1;
        return true;
    }
    if (!digits(a) || (!b.empty() && !digits(b)))
        return false;
    first = std::stoull(a);
    last = b.empty() ? size - 1 : std::min<unsigned long long>(std::stoull(b), size ? size - 1 : 0);
    if (first >= size || (!b.empty() && std::stoull(b) < first)) {
        satisfiable = false;
        return true;
    }
    return true;
}

void HTTPStaticFiles::serveFile(HTTPClient &client, HTTPRequest &request, HTTPResponse &response, const std::string &path, unsigned long long size, time_t modified)
{
    char etagBuffer[64];
    snprintf(etagBuffer, sizeof(etagBuffer), "W/\"%llx-%llx\"", size, static_cast<unsigned long long>(modified));
    const std::string etag = etagBuffer;
    const std::string lastModified = FileUtils::formatHttpDate(modified);

    response.setHeaderField("Content-Type", m_master.mimeTypes().typeForPath(path));
    response.setHeaderField("ETag", etag);
    response.setHeaderField("Last-Modified", lastModified);
    response.setHeaderField("Accept-Ranges", "bytes");
    if (const std::string *cache = m_master.cacheFileTypes().cacheControlFor(path))
        response.setHeaderField("Cache-Control", *cache);

    // conditional requests
    const std::string *ifNoneMatch = request.headerField("if-none-match");
    bool notModified = false;
    if (ifNoneMatch) {
        notModified = *ifNoneMatch == "*" || ifNoneMatch->find(etag) != std::string::npos;
    } else if (const std::string *since = request.headerField("if-modified-since")) {
        time_t sinceTime;
        notModified = FileUtils::parseHttpDate(*since, sinceTime) && modified <= sinceTime;
    }
    if (notModified) {
        response.sendHeaders(304, false, 0);
        client.onResponseSent(response.keepAlive());
        return;
    }

    // Range
    unsigned long long first = 0, last = size ? size - 1 : 0;
    bool isRange = false;
    if (const std::string *range = request.headerField("range")) {
        bool rangeValid = true;
        if (const std::string *ifRange = request.headerField("if-range"))
            rangeValid = (*ifRange == etag || *ifRange == lastModified);
        bool satisfiable = true;
        if (rangeValid && parseRange(*range, size, first, last, satisfiable)) {
            if (!satisfiable) {
                response.setHeaderField("Content-Range", "bytes */" + std::to_string(size));
                response.sendError(416);
                return;
            }
            isRange = true;
        }
    }

    if (isRange) {
        unsigned long long length = last - first + 1;
        response.setHeaderField("Content-Range", "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(size));
        if (response.isHeadRequest()) {
            response.sendHeaders(206, true, length);
            client.onResponseSent(response.keepAlive());
            return;
        }
        response.sendHeaders(206, true, length);
        if (!client.streamFile(path, first, length, response.keepAlive()))
            client.close(true);
        return;
    }

    // whole file, compressed when it pays off
    if (size >= 256 && m_master.compressionTypes().needsCompression(path) && CompressionCache::isAvailable()) {
        const std::string *accept = request.headerField("accept-encoding");
        ContentEncoding encoding = ContentEncoding::None;
        if (accept && StringUtils::containsIgnoreCase(*accept, "gzip"))
            encoding = ContentEncoding::Gzip;
        else if (accept && StringUtils::containsIgnoreCase(*accept, "deflate"))
            encoding = ContentEncoding::Deflate;
        response.setHeaderField("Vary", "Accept-Encoding");
        if (encoding != ContentEncoding::None) {
            if (auto packed = m_compressionCache.get(path, size, modified, encoding)) {
                response.setHeaderField("Content-Encoding", encoding == ContentEncoding::Gzip ? "gzip" : "deflate");
                response.sendSharedBody(200, std::move(packed));
                return;
            }
        }
    }

    if (response.isHeadRequest()) {
        response.sendHeaders(200, true, size);
        client.onResponseSent(response.keepAlive());
        return;
    }

    if (size <= kSmallFile) {
        std::string content;
        if (!FileUtils::readFile(path, content, kSmallFile)) {
            response.internalServerError("The file cannot be read.");
            return;
        }
        response.write(content);
        response.flush(200);
        return;
    }

    response.sendHeaders(200, true, size);
    if (!client.streamFile(path, 0, size, response.keepAlive()))
        client.close(true);
}

// ---------------------------------------------------------------------------------------------

void HTTPStaticFiles::browse(HTTPRequest &request, HTTPResponse &response, const std::string &directory)
{
    struct Entry
    {
        std::string name;
        bool isDirectory;
        unsigned long long size;
    };
    std::vector<Entry> entries;

    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end && entries.size() < kMaxListEntries; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.')
            continue;
        FileInfo info = FileUtils::stat(it->path().string());
        if (!info.exists)
            continue;
        entries.push_back({name, info.isDirectory, info.size});
    }
    if (ec && entries.empty()) {
        response.internalServerError("Cannot open the directory.");
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name < b.name;
    });

    std::string shownPath = StringUtils::htmlEscape(request.urlPath());
    std::string page = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of " + shownPath +
                       "</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                       "<link href=\"/resource/rizweb/brows.css\" rel=\"stylesheet\"></head><body><h1>Index of " + shownPath +
                       "</h1><a class=\"icon back\" href=\"../\">Go to back</a><br><br>"
                       "<table id=\"Table1\"><tbody><tr><td class=\"celltop\">Name</td><td class=\"celltop\">Size</td></tr>";
    for (const Entry &entry : entries) {
        std::string link = StringUtils::urlEncode(entry.name) + (entry.isDirectory ? "/" : "");
        page += "<tr><td class=\"cell1\"><a class=\"icon " + std::string(entry.isDirectory ? "dir" : "file") + "\" href=\"" + link + "\">" +
                StringUtils::htmlEscape(entry.name) + "</a></td><td class=\"cell1\">" +
                (entry.isDirectory ? "--" : StringUtils::formatSize(entry.size)) + "</td></tr>";
    }
    page += "</tbody></table></body></html>";

    response.setHeaderField("Content-Type", "text/html; charset=utf-8");
    response.setHeaderField("Cache-Control", "no-cache");
    response.write(page);
    response.flush(200);
}

} // namespace rizweb
