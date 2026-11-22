#include "clsHTTPHeaderfields.h"
#include "../Utils/clsStringUtils.h"

namespace rizweb {

bool HTTPHeaderfields::addField(std::string_view name, std::string_view value)
{
    if (isFull() || name.empty())
        return false;
    m_fields.emplace_back(std::string(name), std::string(value));
    return true;
}

const std::string *HTTPHeaderfields::fieldByName(std::string_view name) const
{
    for (const auto &field : m_fields)
        if (StringUtils::equalsIgnoreCase(field.first, name))
            return &field.second;
    return nullptr;
}

const std::string *HTTPHeaderfields::fieldByIndex(size_t index) const
{
    return index < m_fields.size() ? &m_fields[index].second : nullptr;
}

const std::string *HTTPHeaderfields::fieldNameByIndex(size_t index) const
{
    return index < m_fields.size() ? &m_fields[index].first : nullptr;
}

} // namespace rizweb
