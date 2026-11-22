#ifndef CLSMIMETYPES_H
#define CLSMIMETYPES_H
#include <string>
#include <string_view>
#include <unordered_map>

namespace rizweb {

// Extension -> Content-Type. A built-in table covers the common web types; an optional
// mime.types.db file (".ext <whitespace> type/subtype" per line) adds or overrides entries.
// Load before the server starts; lookups afterwards are read-only and thread-safe.
class MIMETypes
{
public:
    MIMETypes();
    bool loadDatabase(const std::string &path);
    void add(const std::string &extension, const std::string &type);
    // extension with or without the leading dot, any case
    const std::string &typeForExtension(std::string_view extension) const;
    const std::string &typeForPath(std::string_view path) const;
    static std::string_view extensionOf(std::string_view path);   // ".html" or ""

private:
    std::unordered_map<std::string, std::string> m_types;
    std::string m_default;
};

} // namespace rizweb

#endif // CLSMIMETYPES_H
