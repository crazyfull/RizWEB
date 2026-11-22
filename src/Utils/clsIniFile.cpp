#include "clsIniFile.h"
#include "clsStringUtils.h"
#include <cstdlib>
#include <fstream>

namespace rizweb {

bool IniFile::load(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
        return false;
    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        std::string_view text = StringUtils::trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';')
            continue;
        if (text[0] == '[') {
            size_t end = text.find(']');
            if (end != std::string_view::npos)
                section = std::string(StringUtils::trim(text.substr(1, end - 1)));
            continue;
        }
        size_t equal = text.find('=');
        if (equal == std::string_view::npos)
            continue;
        m_values[section][std::string(StringUtils::trim(text.substr(0, equal)))] = std::string(StringUtils::trim(text.substr(equal + 1)));
    }
    return true;
}

bool IniFile::save(const std::string &path) const
{
    std::ofstream file(path, std::ios::trunc);
    if (!file)
        return false;
    for (const auto &section : m_values) {
        file << "[" << section.first << "]\n";
        for (const auto &item : section.second)
            file << item.first << " = " << item.second << "\n";
        file << "\n";
    }
    return static_cast<bool>(file);
}

const std::string *IniFile::find(const std::string &section, const std::string &key) const
{
    auto s = m_values.find(section);
    if (s == m_values.end())
        return nullptr;
    auto k = s->second.find(key);
    return k == s->second.end() ? nullptr : &k->second;
}

std::string IniFile::getString(const std::string &section, const std::string &key, const std::string &fallback) const
{
    const std::string *value = find(section, key);
    return value ? *value : fallback;
}

long IniFile::getLong(const std::string &section, const std::string &key, long fallback) const
{
    const std::string *value = find(section, key);
    if (!value || value->empty())
        return fallback;
    char *end = nullptr;
    long number = strtol(value->c_str(), &end, 10);
    return (end && *end == '\0') ? number : fallback;
}

bool IniFile::getBool(const std::string &section, const std::string &key, bool fallback) const
{
    const std::string *value = find(section, key);
    if (!value)
        return fallback;
    if (StringUtils::equalsIgnoreCase(*value, "true") || *value == "1" || StringUtils::equalsIgnoreCase(*value, "yes"))
        return true;
    if (StringUtils::equalsIgnoreCase(*value, "false") || *value == "0" || StringUtils::equalsIgnoreCase(*value, "no"))
        return false;
    return fallback;
}

void IniFile::setString(const std::string &section, const std::string &key, const std::string &value) { m_values[section][key] = value; }
void IniFile::setLong(const std::string &section, const std::string &key, long value) { m_values[section][key] = std::to_string(value); }
void IniFile::setBool(const std::string &section, const std::string &key, bool value) { m_values[section][key] = value ? "true" : "false"; }

} // namespace rizweb
