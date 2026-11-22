#ifndef RIZWEB_CLSURLCODEC_H
#define RIZWEB_CLSURLCODEC_H

#include <string>
#include <string_view>

namespace rizweb {

class UrlCodec
{
public:
    // Percent decoding. Returns false for a malformed escape ("%zz", a lone "%").
    // plusAsSpace is for query strings and form bodies, not for paths.
    static bool decode(std::string_view input, std::string& output, bool plusAsSpace);

    // Escapes everything except unreserved characters and "/" (used for href values).
    static std::string encodePath(std::string_view input);

    // Escapes everything except unreserved characters (used for single path segments).
    static std::string encodeComponent(std::string_view input);

    // Normalizes an already decoded absolute path: collapses "//", resolves "." and "..".
    // Returns false when the path does not start with "/", contains NUL or a backslash,
    // or tries to climb above the root ("/../etc/passwd").
    // A trailing slash is kept because it decides how directories are served.
    static bool normalizePath(std::string_view decodedPath, std::string& output);
};

} // namespace rizweb

#endif // RIZWEB_CLSURLCODEC_H
