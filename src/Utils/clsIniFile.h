#ifndef CLSINIFILE_H
#define CLSINIFILE_H
#include <map>
#include <string>

namespace rizweb {

// Tiny INI reader/writer: [section] / key = value, '#' and ';' comments.
class IniFile
{
public:
    bool load(const std::string &path);
    bool save(const std::string &path) const;
    bool isEmpty() const { return m_values.empty(); }

    std::string getString(const std::string &section, const std::string &key, const std::string &fallback = "") const;
    long getLong(const std::string &section, const std::string &key, long fallback) const;
    bool getBool(const std::string &section, const std::string &key, bool fallback) const;

    void setString(const std::string &section, const std::string &key, const std::string &value);
    void setLong(const std::string &section, const std::string &key, long value);
    void setBool(const std::string &section, const std::string &key, bool value);

private:
    std::map<std::string, std::map<std::string, std::string>> m_values;
    const std::string *find(const std::string &section, const std::string &key) const;
};

} // namespace rizweb

#endif // CLSINIFILE_H
