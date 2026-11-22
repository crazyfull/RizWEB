#ifndef RIZWEB_CLSINIFILE_H
#define RIZWEB_CLSINIFILE_H

#include <map>
#include <string>
#include <string_view>

namespace rizweb {

// Tiny INI reader: "[section]", "key = value", comments start with ';' or '#'.
// Section and key names are case sensitive (same as the old SimpleIni based settings file).
class IniFile
{
public:
    // Returns false when the file cannot be opened.
    bool load(const std::string& path);
    bool loadFromText(std::string_view text);

    bool has(const std::string& section, const std::string& key) const;
    std::string getString(const std::string& section, const std::string& key, const std::string& defaultValue = std::string()) const;
    long getInt(const std::string& section, const std::string& key, long defaultValue) const;
    bool getBool(const std::string& section, const std::string& key, bool defaultValue) const;

    bool isEmpty() const { return m_values.empty(); }

private:
    std::map<std::string, std::string> m_values; // "section\n key" -> value
};

} // namespace rizweb

#endif // RIZWEB_CLSINIFILE_H
