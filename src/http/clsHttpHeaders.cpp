#include "clsHttpHeaders.h"

#include "../util/stringUtil.h"

namespace rizweb {

void HttpHeaders::add(std::string name, std::string value)
{
    m_entries.emplace_back(std::move(name), std::move(value));
}

void HttpHeaders::set(std::string_view name, std::string value)
{
    remove(name);
    m_entries.emplace_back(std::string(name), std::move(value));
}

const std::string* HttpHeaders::get(std::string_view name) const
{
    for (const Entry& entry : m_entries) {
        if (stringUtil::equalsIgnoreCase(entry.first, name))
            return &entry.second;
    }
    return nullptr;
}

std::string HttpHeaders::getOr(std::string_view name, const std::string& fallback) const
{
    const std::string* value = get(name);
    return value ? *value : fallback;
}

bool HttpHeaders::has(std::string_view name) const
{
    return get(name) != nullptr;
}

bool HttpHeaders::hasToken(std::string_view name, std::string_view token) const
{
    for (const Entry& entry : m_entries) {
        if (stringUtil::equalsIgnoreCase(entry.first, name) && stringUtil::containsToken(entry.second, token))
            return true;
    }
    return false;
}

bool HttpHeaders::remove(std::string_view name)
{
    bool removed = false;
    for (size_t i = 0; i < m_entries.size();) {
        if (stringUtil::equalsIgnoreCase(m_entries[i].first, name)) {
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
            removed = true;
        } else {
            i++;
        }
    }
    return removed;
}

} // namespace rizweb
