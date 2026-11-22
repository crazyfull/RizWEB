#ifndef RIZWEB_CLSHTTPHEADERS_H
#define RIZWEB_CLSHTTPHEADERS_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rizweb {

// Ordered header list with case-insensitive lookup. Header counts are small, so a
// vector is faster than a map and keeps the original order for the response.
class HttpHeaders
{
public:
    using Entry = std::pair<std::string, std::string>;

    void add(std::string name, std::string value);

    // Removes every header with this name and adds the new one.
    void set(std::string_view name, std::string value);

    const std::string* get(std::string_view name) const;
    std::string getOr(std::string_view name, const std::string& fallback = std::string()) const;
    bool has(std::string_view name) const;

    // true when any header with this name holds the token in its comma separated value
    bool hasToken(std::string_view name, std::string_view token) const;

    bool remove(std::string_view name);
    void clear() { m_entries.clear(); }
    size_t size() const { return m_entries.size(); }
    const std::vector<Entry>& entries() const { return m_entries; }

private:
    std::vector<Entry> m_entries;
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPHEADERS_H
