#include "clsMimeTypes.h"

#include "../util/stringUtil.h"

#include <fstream>
#include <sstream>

namespace rizweb {

MimeTypes::MimeTypes()
{
    static const char* const table[][2] = {
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"},
        {"mjs", "text/javascript; charset=utf-8"}, {"json", "application/json; charset=utf-8"},
        {"txt", "text/plain; charset=utf-8"}, {"csv", "text/csv; charset=utf-8"},
        {"xml", "application/xml; charset=utf-8"}, {"svg", "image/svg+xml"},
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"}, {"webp", "image/webp"}, {"ico", "image/x-icon"},
        {"avif", "image/avif"}, {"bmp", "image/bmp"},
        {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"ttf", "font/ttf"}, {"otf", "font/otf"},
        {"pdf", "application/pdf"}, {"zip", "application/zip"}, {"gz", "application/gzip"},
        {"wasm", "application/wasm"}, {"mp3", "audio/mpeg"}, {"wav", "audio/wav"},
        {"mp4", "video/mp4"}, {"webm", "video/webm"}, {"ogg", "audio/ogg"},
    };
    for (const auto& row : table)
        m_types.emplace(row[0], row[1]);
}

void MimeTypes::add(std::string extension, std::string contentType)
{
    if (!extension.empty() && extension[0] == '.')
        extension.erase(0, 1);
    if (extension.empty() || contentType.empty())
        return;
    m_types[stringUtil::toLower(extension)] = std::move(contentType);
}

bool MimeTypes::loadFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        return false;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string first;
        if (!(words >> first) || first[0] == '#')
            continue;

        if (first[0] == '.') {
            std::string type;
            if (words >> type)
                add(first, type);
        } else if (first.find('/') != std::string::npos) {
            std::string extension;
            while (words >> extension)
                add(extension, first);
        }
    }
    return true;
}

std::string_view MimeTypes::forPath(std::string_view path) const
{
    size_t slash = path.rfind('/');
    std::string_view name = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size())
        return "application/octet-stream";

    auto it = m_types.find(stringUtil::toLower(name.substr(dot + 1)));
    return it == m_types.end() ? std::string_view("application/octet-stream") : std::string_view(it->second);
}

bool MimeTypes::isCompressible(std::string_view contentType)
{
    return stringUtil::startsWith(contentType, "text/") || stringUtil::startsWith(contentType, "application/json")
           || stringUtil::startsWith(contentType, "application/xml") || stringUtil::startsWith(contentType, "image/svg")
           || stringUtil::startsWith(contentType, "application/wasm");
}

} // namespace rizweb
