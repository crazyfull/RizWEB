#ifndef CLSCACHEFILETYPE_H
#define CLSCACHEFILETYPE_H
#include <string>
#include <string_view>
#include <unordered_map>

namespace rizweb {

// Which file extensions get a Cache-Control: max-age header, and for how long.
// Configure before the server starts.
class CacheFileType
{
public:
    void add(const std::string &extension, unsigned seconds);
    // "public, max-age=600", or an empty string when the extension has no cache rule
    const std::string *cacheControlFor(std::string_view path) const;

private:
    std::unordered_map<std::string, std::string> m_rules;
};

} // namespace rizweb

#endif // CLSCACHEFILETYPE_H
