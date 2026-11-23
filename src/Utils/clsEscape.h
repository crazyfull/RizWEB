#ifndef CLSESCAPE_H
#define CLSESCAPE_H
#include <string>
#include <string_view>

namespace rizweb {

// Output escaping for untrusted text. Pick the function for the context the text lands in:
//   html()           element text:               <p>html(x)</p>
//   htmlAttribute()  quoted attribute value:     <a title="htmlAttribute(x)">
//   json()           content of a JSON string:   {"name":"json(x)"}            (safe inside <script> too)
//   jsString()       JavaScript string literal:  var s = 'jsString(x)';
//   url()            URL component:              /search?q=url(x)
//   shellArg()       one POSIX shell word:       system("convert " + shellArg(file))   (prefer not to use a shell at all)
//   headerValue()    HTTP header value:          CR/LF/NUL removed
// There is deliberately no "SQL escape": use prepared statements / bound parameters of your database library.
class Escape
{
public:
    static std::string html(std::string_view text);
    static std::string htmlAttribute(std::string_view text);
    static std::string json(std::string_view text);
    static std::string jsString(std::string_view text);
    static std::string url(std::string_view text);
    static std::string shellArg(std::string_view text);
    static std::string headerValue(std::string_view text);
};

} // namespace rizweb

#endif // CLSESCAPE_H
