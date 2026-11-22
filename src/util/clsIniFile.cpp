#include "clsIniFile.h"

#include "stringUtil.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rizweb {

namespace {
std::string makeKey(const std::string& section, const std::string& key)
{
    return section + "\n" + key;
}
} // namespace

bool IniFile::load(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    return loadFromText(buffer.str());
}

bool IniFile::loadFromText(std::string_view text)
{
    m_values.clear();
    std::string section;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos)
            eol = text.size();

        std::string_view line = stringUtil::trim(text.substr(pos, eol - pos));
        if (!line.empty() && line.back() == '\r')
            line = stringUtil::trim(line.substr(0, line.size() - 1));
        pos = eol + 1;

        // skip an optional UTF-8 BOM
        if (line.size() >= 3 && line.substr(0, 3) == "\xEF\xBB\xBF")
            line = line.substr(3);

        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string_view::npos)
                section = std::string(stringUtil::trim(line.substr(1, close - 1)));
            continue;
        }

        size_t equals = line.find('=');
        if (equals == std::string_view::npos)
            continue;

        std::string key(stringUtil::trim(line.substr(0, equals)));
        std::string value(stringUtil::trim(line.substr(equals + 1)));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        if (!key.empty())
            m_values[makeKey(section, key)] = value;
    }
    return true;
}

bool IniFile::has(const std::string& section, const std::string& key) const
{
    return m_values.find(makeKey(section, key)) != m_values.end();
}

std::string IniFile::getString(const std::string& section, const std::string& key, const std::string& defaultValue) const
{
    auto it = m_values.find(makeKey(section, key));
    return it == m_values.end() ? defaultValue : it->second;
}

long IniFile::getInt(const std::string& section, const std::string& key, long defaultValue) const
{
    auto it = m_values.find(makeKey(section, key));
    if (it == m_values.end() || it->second.empty())
        return defaultValue;

    char* end = nullptr;
    long value = std::strtol(it->second.c_str(), &end, 10);
    return (end && *end == '\0') ? value : defaultValue;
}

bool IniFile::getBool(const std::string& section, const std::string& key, bool defaultValue) const
{
    auto it = m_values.find(makeKey(section, key));
    if (it == m_values.end())
        return defaultValue;

    std::string v = stringUtil::toLower(it->second);
    if (v == "1" || v == "true" || v == "yes" || v == "on")
        return true;
    if (v == "0" || v == "false" || v == "no" || v == "off")
        return false;
    return defaultValue;
}

} // namespace rizweb
