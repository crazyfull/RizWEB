#ifndef RIZWEB_CLSMIMETYPES_H
#define RIZWEB_CLSMIMETYPES_H

#include <string>
#include <string_view>
#include <unordered_map>

namespace rizweb {

// Extension -> content type. Fill it before the server starts; lookups are read-only
// afterwards, so no locking is needed.
class MimeTypes
{
public:
    MimeTypes();   // built-in table of common types

    // Loads extra entries. Two line formats are accepted:
    //   ".ext   type/subtype"        (the format of resource/rizweb/mime.types.db)
    //   "type/subtype   ext1 ext2"   (Apache mime.types)
    // Returns false when the file cannot be read.
    bool loadFile(const std::string& path);

    void add(std::string extension, std::string contentType);

    // Content type for a file path or extension; "application/octet-stream" if unknown.
    std::string_view forPath(std::string_view path) const;

    // true for types worth compressing (text, json, javascript, svg, xml)
    static bool isCompressible(std::string_view contentType);

private:
    std::unordered_map<std::string, std::string> m_types;   // key: lowercase extension without dot
};

} // namespace rizweb

#endif // RIZWEB_CLSMIMETYPES_H
