#include "clsCacheFileType.h"
#include "clsMIMETypes.h"
#include "../Utils/clsStringUtils.h"

namespace rizweb {

void CacheFileType::add(const std::string &extension, unsigned seconds)
{
    std::string key = StringUtils::toLower(extension);
    if (key.empty())
        return;
    if (key[0] != '.')
        key.insert(key.begin(), '.');
    m_rules[key] = "public, max-age=" + std::to_string(seconds);
}

const std::string *CacheFileType::cacheControlFor(std::string_view path) const
{
    if (m_rules.empty())
        return nullptr;
    auto it = m_rules.find(StringUtils::toLower(MIMETypes::extensionOf(path)));
    return it == m_rules.end() ? nullptr : &it->second;
}

} // namespace rizweb
