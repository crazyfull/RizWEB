#include "clsMIMETypes.h"
#include "../Utils/clsStringUtils.h"
#include <fstream>

namespace rizweb {

MIMETypes::MIMETypes() : m_default("application/octet-stream")
{
    static const char *builtin[][2] = {
        {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"}, {".json", "application/json; charset=utf-8"},
        {".txt", "text/plain; charset=utf-8"}, {".xml", "application/xml; charset=utf-8"},
        {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".webp", "image/webp"},
        {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"},
        {".ttf", "font/ttf"}, {".otf", "font/otf"}, {".pdf", "application/pdf"},
        {".zip", "application/zip"}, {".gz", "application/gzip"}, {".wasm", "application/wasm"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mp3", "audio/mpeg"},
        {".map", "application/json"},
    };
    for (const auto &item : builtin)
        m_types[item[0]] = item[1];
}

void MIMETypes::add(const std::string &extension, const std::string &type)
{
    std::string key = StringUtils::toLower(extension);
    if (key.empty())
        return;
    if (key[0] != '.')
        key.insert(key.begin(), '.');
    std::string value = type;
    if (StringUtils::startsWith(value, "text/") && value.find("charset") == std::string::npos)
        value += "; charset=utf-8";
    m_types[key] = value;
}

bool MIMETypes::loadDatabase(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
        return false;
    std::string line;
    while (std::getline(file, line)) {
        std::string_view text = StringUtils::trim(line);
        if (text.empty() || text[0] == '#')
            continue;
        size_t split = text.find_first_of(" \t");
        if (split == std::string_view::npos)
            continue;
        std::string_view type = StringUtils::trim(text.substr(split));
        if (!type.empty())
            add(std::string(text.substr(0, split)), std::string(type));
    }
    return true;
}

std::string_view MIMETypes::extensionOf(std::string_view path)
{
    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return {};
    return path.substr(dot);
}

const std::string &MIMETypes::typeForExtension(std::string_view extension) const
{
    std::string key = StringUtils::toLower(extension);
    if (!key.empty() && key[0] != '.')
        key.insert(key.begin(), '.');
    auto it = m_types.find(key);
    return it == m_types.end() ? m_default : it->second;
}

const std::string &MIMETypes::typeForPath(std::string_view path) const
{
    return typeForExtension(extensionOf(path));
}

} // namespace rizweb
