#ifndef CLSHTTPHEADERFIELDS_H
#define CLSHTTPHEADERFIELDS_H
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rizweb {

// Ordered name/value list. Name lookup is case-insensitive (HTTP header names).
class HTTPHeaderfields
{
public:
    static constexpr size_t kMaxFields = 64;

    size_t count() const { return m_fields.size(); }
    bool isFull() const { return m_fields.size() >= kMaxFields; }
    bool addField(std::string_view name, std::string_view value);
    const std::string *fieldByName(std::string_view name) const;
    const std::string *fieldByIndex(size_t index) const;
    const std::string *fieldNameByIndex(size_t index) const;
    void clear() { m_fields.clear(); }

private:
    std::vector<std::pair<std::string, std::string>> m_fields;
};

} // namespace rizweb

#endif // CLSHTTPHEADERFIELDS_H
