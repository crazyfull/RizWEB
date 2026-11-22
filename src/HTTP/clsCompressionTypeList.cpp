#include "clsCompressionTypeList.h"
#include "clsMIMETypes.h"
#include "../Utils/clsStringUtils.h"

namespace rizweb {

CompressionTypeList::CompressionTypeList() : m_method(CompressMethod::CompressJustSelected) {}

void CompressionTypeList::add(const std::string &extension)
{
    std::string key = StringUtils::toLower(extension);
    if (key.empty())
        return;
    if (key[0] != '.')
        key.insert(key.begin(), '.');
    m_extensions.insert(key);
}

bool CompressionTypeList::needsCompression(std::string_view path) const
{
    bool listed = m_extensions.count(StringUtils::toLower(MIMETypes::extensionOf(path))) > 0;
    switch (m_method) {
    case CompressMethod::CompressAllFiles: return true;
    case CompressMethod::CompressJustSelected: return listed;
    case CompressMethod::CompressAllExceptSelected: return !listed;
    }
    return false;
}

} // namespace rizweb
