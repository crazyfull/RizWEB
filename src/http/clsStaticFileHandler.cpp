#include "clsStaticFileHandler.h"

#include "../util/clsHttpDate.h"
#include "../util/clsLogger.h"
#include "../util/clsUrlCodec.h"
#include "../util/stringUtil.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

namespace rizweb {

namespace {

std::string makeEtag(const struct stat& info, bool gzip)
{
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "W/\"%llx-%llx%s\"", static_cast<unsigned long long>(info.st_size),
                  static_cast<unsigned long long>(info.st_mtime), gzip ? "-gz" : "");
    return buffer;
}

// weak comparison of an If-None-Match list against our ETag
bool etagMatches(std::string_view list, std::string_view etag)
{
    list = stringUtil::trim(list);
    if (list == "*")
        return true;

    std::string_view ours = etag;
    if (stringUtil::startsWith(ours, "W/"))
        ours.remove_prefix(2);

    size_t pos = 0;
    while (pos <= list.size()) {
        size_t comma = list.find(',', pos);
        if (comma == std::string_view::npos)
            comma = list.size();
        std::string_view candidate = stringUtil::trim(list.substr(pos, comma - pos));
        if (stringUtil::startsWith(candidate, "W/"))
            candidate.remove_prefix(2);
        if (!candidate.empty() && candidate == ours)
            return true;
        pos = comma + 1;
    }
    return false;
}

// "gzip" listed and not refused with q=0
bool acceptsGzip(std::string_view header)
{
    size_t pos = 0;
    while (pos <= header.size()) {
        size_t comma = header.find(',', pos);
        if (comma == std::string_view::npos)
            comma = header.size();
        std::string_view item = stringUtil::trim(header.substr(pos, comma - pos));
        pos = comma + 1;

        size_t semicolon = item.find(';');
        std::string_view name = stringUtil::trim(item.substr(0, semicolon));
        if (!stringUtil::equalsIgnoreCase(name, "gzip"))
            continue;

        if (semicolon != std::string_view::npos) {
            std::string_view params = stringUtil::trim(item.substr(semicolon + 1));
            if (stringUtil::startsWith(params, "q=0") && params.find_first_of("123456789") == std::string_view::npos)
                return false;
        }
        return true;
    }
    return false;
}

struct ByteRange
{
    off_t start = 0;
    off_t end = 0;   // inclusive
};

enum class RangeResult { none, ok, unsatisfiable };

RangeResult parseRange(std::string_view header, off_t size, ByteRange& range)
{
    header = stringUtil::trim(header);
    if (!stringUtil::startsWith(header, "bytes=") || size <= 0)
        return RangeResult::none;

    std::string_view spec = stringUtil::trim(header.substr(6));
    if (spec.find(',') != std::string_view::npos)
        return RangeResult::none;   // multiple ranges: serve the whole file instead

    size_t dash = spec.find('-');
    if (dash == std::string_view::npos)
        return RangeResult::none;

    std::string_view first = spec.substr(0, dash);
    std::string_view last = spec.substr(dash + 1);

    auto toNumber = [](std::string_view text, long long& value) {
        if (text.empty() || text.size() > 18)
            return false;
        long long result = 0;
        for (char c : text) {
            if (c < '0' || c > '9')
                return false;
            result = result * 10 + (c - '0');
        }
        value = result;
        return true;
    };

    long long a = 0, b = 0;
    if (first.empty()) {
        if (!toNumber(last, b) || b == 0)
            return b == 0 && !last.empty() ? RangeResult::unsatisfiable : RangeResult::none;
        range.start = (b >= size) ? 0 : static_cast<off_t>(size - b);
        range.end = size - 1;
        return RangeResult::ok;
    }

    if (!toNumber(first, a))
        return RangeResult::none;
    if (a >= size)
        return RangeResult::unsatisfiable;

    if (last.empty()) {
        b = size - 1;
    } else {
        if (!toNumber(last, b) || b < a)
            return RangeResult::none;
        if (b >= size)
            b = size - 1;
    }

    range.start = static_cast<off_t>(a);
    range.end = static_cast<off_t>(b);
    return RangeResult::ok;
}

bool hasDotSegment(std::string_view path)
{
    size_t pos = 0;
    while (pos < path.size()) {
        if (path[pos] == '/') {
            pos++;
            continue;
        }
        if (path[pos] == '.')
            return true;
        size_t slash = path.find('/', pos);
        if (slash == std::string_view::npos)
            break;
        pos = slash + 1;
    }
    return false;
}

} // namespace

StaticFileHandler::StaticFileHandler(StaticFileOptions options, std::shared_ptr<const MimeTypes> mimeTypes)
    : m_options(std::move(options)), m_mimeTypes(std::move(mimeTypes))
{
    if (!m_mimeTypes)
        m_mimeTypes = std::make_shared<MimeTypes>();

    char* real = ::realpath(m_options.rootDirectory.c_str(), nullptr);
    if (real) {
        m_rootReal = real;
        std::free(real);
        while (m_rootReal.size() > 1 && m_rootReal.back() == '/')
            m_rootReal.pop_back();
    } else {
        RIZWEB_LOG_ERROR("static root \"%s\" cannot be resolved: %s", m_options.rootDirectory.c_str(), std::strerror(errno));
    }
}

bool StaticFileHandler::resolveInsideRoot(const std::string& candidate, std::string& resolved) const
{
    char* real = ::realpath(candidate.c_str(), nullptr);
    if (!real)
        return false;

    resolved = real;
    std::free(real);

    if (resolved == m_rootReal)
        return true;

    std::string prefix = (m_rootReal == "/") ? m_rootReal : m_rootReal + "/";
    return stringUtil::startsWith(resolved, prefix);
}

bool StaticFileHandler::openRegularFile(const std::string& path, UniqueFd& fd, struct stat& info) const
{
    UniqueFd opened(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!opened.valid())
        return false;
    if (::fstat(opened.get(), &info) != 0 || !S_ISREG(info.st_mode))
        return false;
    fd = std::move(opened);
    return true;
}

void StaticFileHandler::serve(const HttpRequest& request, HttpResponse& response, std::string_view subPath) const
{
    if (request.method != HttpMethod::get && request.method != HttpMethod::head) {
        response.sendError(405);
        response.setHeader("Allow", "GET, HEAD");
        return;
    }

    if (m_rootReal.empty()) {
        response.sendError(500, "static root is not available");
        return;
    }

    if (subPath.empty() || subPath[0] != '/') {
        response.sendError(400);
        return;
    }

    if (!m_options.serveDotFiles && hasDotSegment(subPath)) {
        response.sendError(403);
        return;
    }

    std::string resolved;
    if (!resolveInsideRoot(m_rootReal + std::string(subPath), resolved)) {
        // either missing, or a symlink that points outside the root: do not tell which
        response.sendError(errno == EACCES ? 403 : 404);
        return;
    }

    struct stat info;
    if (::stat(resolved.c_str(), &info) != 0) {
        response.sendError(404);
        return;
    }

    response.setHeader("X-Content-Type-Options", "nosniff");

    if (S_ISDIR(info.st_mode)) {
        serveDirectory(request, response, resolved);
        return;
    }

    UniqueFd fd;
    if (!openRegularFile(resolved, fd, info)) {
        response.sendError(errno == EACCES ? 403 : 404);
        return;
    }

    serveFile(request, response, std::move(fd), info, resolved);
}

void StaticFileHandler::serveDirectory(const HttpRequest& request, HttpResponse& response, const std::string& directory) const
{
    // "/docs" -> "/docs/" so that relative links inside the page work
    if (request.path.empty() || request.path.back() != '/') {
        std::string location = UrlCodec::encodePath(request.path) + "/";
        if (!request.rawQuery.empty())
            location += "?" + request.rawQuery;
        response.redirect(location, 301);
        return;
    }

    for (const std::string& name : m_options.indexFiles) {
        std::string resolved;
        UniqueFd fd;
        struct stat info;
        if (resolveInsideRoot(directory + "/" + name, resolved) && openRegularFile(resolved, fd, info)) {
            serveFile(request, response, std::move(fd), info, resolved);
            return;
        }
    }

    if (!m_options.directoryListing) {
        response.sendError(403, "Directory listing denied.");
        return;
    }

    listDirectory(request, response, directory);
}

void StaticFileHandler::listDirectory(const HttpRequest& request, HttpResponse& response, const std::string& directory) const
{
    DIR* dir = ::opendir(directory.c_str());
    if (!dir) {
        response.sendError(errno == EACCES ? 403 : 500);
        return;
    }

    struct Item
    {
        std::string name;
        bool isDirectory;
        uint64_t size;
    };
    std::vector<Item> items;
    const size_t kMaxItems = 5000;

    while (struct dirent* entry = ::readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        if (!m_options.serveDotFiles && name[0] == '.')
            continue;

        struct stat info;
        if (::fstatat(::dirfd(dir), name.c_str(), &info, 0) != 0)
            continue;   // broken symlink or vanished file

        items.push_back({name, S_ISDIR(info.st_mode), static_cast<uint64_t>(info.st_size)});
        if (items.size() >= kMaxItems)
            break;
    }
    ::closedir(dir);

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name < b.name;
    });

    const std::string title = stringUtil::htmlEscape(request.path);
    std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of " + title
                       + "</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                       + "<link href=\"" + stringUtil::htmlEscape(m_options.listingCssUrl) + "\" rel=\"stylesheet\"></head><body>"
                       + "<h1>Index of " + title + "</h1><a class=\"icon back\" href=\"../\">Go to back</a><br><br>"
                       + "<table id=\"Table1\"><tbody><tr><td class=\"celltop\">Name</td><td class=\"celltop\">Size</td></tr>";

    for (const Item& item : items) {
        // "./" keeps a name such as "a:b" from being read as a URL scheme
        std::string href = "./" + UrlCodec::encodeComponent(item.name) + (item.isDirectory ? "/" : "");
        html += "<tr><td class=\"cell1\"><a class=\"icon ";
        html += item.isDirectory ? "dir" : "file";
        html += "\" href=\"" + href + "\">" + stringUtil::htmlEscape(item.name) + "</a></td><td class=\"cell1\">";
        html += item.isDirectory ? "--" : stringUtil::formatSize(item.size);
        html += "</td></tr>";
    }
    html += "</tbody></table></body></html>";

    response.sendHtml(200, html);
    response.setHeader("Cache-Control", "no-cache");
}

void StaticFileHandler::serveFile(const HttpRequest& request, HttpResponse& response, UniqueFd fd, const struct stat& info,
                                  const std::string& realPath) const
{
    const std::string_view contentType = m_mimeTypes->forPath(realPath);
    const bool compressible = MimeTypes::isCompressible(contentType);
    const std::string lastModified = HttpDate::format(info.st_mtime);

    // ---- gzip decision (the ETag differs per representation) ----
    const std::string* acceptEncoding = request.header("Accept-Encoding");
    const bool wantGzip = m_options.enableGzip && GzipCache::isAvailable() && compressible && acceptEncoding
                          && acceptsGzip(*acceptEncoding) && !request.headers.has("Range")
                          && static_cast<size_t>(info.st_size) >= m_options.gzipMinBytes
                          && static_cast<size_t>(info.st_size) <= m_options.gzipMaxBytes;

    std::string etag = makeEtag(info, wantGzip);

    // ---- headers shared by 200, 206 and 304 ----
    response.setHeader("Content-Type", contentType);
    response.setHeader("ETag", etag);
    response.setHeader("Last-Modified", lastModified);
    response.setHeader("Accept-Ranges", "bytes");
    if (compressible)
        response.setHeader("Vary", "Accept-Encoding");

    std::string extension;
    size_t dot = realPath.rfind('.');
    if (dot != std::string::npos)
        extension = stringUtil::toLower(realPath.substr(dot + 1));
    auto cacheRule = m_options.cacheSecondsByExtension.find(extension);
    if (cacheRule != m_options.cacheSecondsByExtension.end())
        response.setHeader("Cache-Control", "public, max-age=" + std::to_string(cacheRule->second));

    // ---- conditional requests ----
    const std::string* ifNoneMatch = request.header("If-None-Match");
    bool notModified = false;
    if (ifNoneMatch) {
        notModified = etagMatches(*ifNoneMatch, etag);
    } else if (const std::string* ifModifiedSince = request.header("If-Modified-Since")) {
        time_t since = 0;
        notModified = HttpDate::parse(*ifModifiedSince, since) && info.st_mtime <= since;
    }
    if (notModified) {
        response.setStatus(304);
        return;
    }

    // ---- range ----
    if (!wantGzip) {
        const std::string* rangeHeader = request.header("Range");
        bool rangeApplies = rangeHeader != nullptr;
        if (rangeApplies) {
            // If-Range only matches the exact Last-Modified date (our ETags are weak)
            if (const std::string* ifRange = request.header("If-Range"))
                rangeApplies = (stringUtil::trim(*ifRange) == lastModified);
        }

        ByteRange range;
        RangeResult parsed = rangeApplies ? parseRange(*rangeHeader, info.st_size, range) : RangeResult::none;

        if (parsed == RangeResult::unsatisfiable) {
            response.sendError(416);
            response.setHeader("Content-Range", "bytes */" + std::to_string(info.st_size));
            return;
        }

        if (parsed == RangeResult::ok) {
            response.setStatus(206);
            response.setHeader("Content-Range", "bytes " + std::to_string(range.start) + "-" + std::to_string(range.end) + "/"
                                                    + std::to_string(info.st_size));
            response.setFileBody(std::move(fd), range.start, static_cast<size_t>(range.end - range.start + 1));
            return;
        }
    }

    // ---- compressed copy ----
    if (wantGzip) {
        const std::string key = realPath + "|" + std::to_string(info.st_size) + "|" + std::to_string(info.st_mtime);
        std::shared_ptr<const std::string> packed = m_gzipCache.find(key);

        if (!packed) {
            std::string raw(static_cast<size_t>(info.st_size), '\0');
            size_t done = 0;
            while (done < raw.size()) {
                ssize_t n = ::pread(fd.get(), &raw[done], raw.size() - done, static_cast<off_t>(done));
                if (n <= 0)
                    break;
                done += static_cast<size_t>(n);
            }

            std::string compressed;
            if (done == raw.size() && GzipCache::compress(raw, compressed) && compressed.size() < raw.size()) {
                packed = std::make_shared<const std::string>(std::move(compressed));
                m_gzipCache.store(key, packed);
            }
        }

        if (packed) {
            response.setHeader("Content-Encoding", "gzip");
            response.setStatus(200);
            response.setSharedBody(std::move(packed));
            return;
        }

        // compression did not help: fall back to the plain file with the plain ETag
        response.setHeader("ETag", makeEtag(info, false));
    }

    response.setStatus(200);
    response.setFileBody(std::move(fd), 0, static_cast<size_t>(info.st_size));
}

} // namespace rizweb
