#ifndef CLSCOMPRESSIONTYPELIST_H
#define CLSCOMPRESSIONTYPELIST_H
#include <string>
#include <string_view>
#include <unordered_set>

namespace rizweb {

enum class CompressMethod
{
    CompressAllFiles,
    CompressJustSelected,
    CompressAllExceptSelected
};

// Decides which files are worth compressing. Configure before the server starts.
class CompressionTypeList
{
public:
    CompressionTypeList();
    void add(const std::string &extension);
    void setMethod(CompressMethod method) { m_method = method; }
    CompressMethod method() const { return m_method; }
    bool needsCompression(std::string_view path) const;

private:
    std::unordered_set<std::string> m_extensions;
    CompressMethod m_method;
};

} // namespace rizweb

#endif // CLSCOMPRESSIONTYPELIST_H
