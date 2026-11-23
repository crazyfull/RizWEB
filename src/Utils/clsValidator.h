#ifndef CLSVALIDATOR_H
#define CLSVALIDATOR_H
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rizweb {

class HTTPRequest;

// Checks untrusted input (query / form fields, route parameters, headers) before a handler uses it.
//
//   Validator v;
//   v.query(request, "page").integer(1, 1000);                       // optional, but must be 1..1000 when present
//   v.form(request, "email").required().email().maxLength(254);
//   v.param(request, "id").required().slug();
//   if (!v.ok()) { response.setContentType("application/json"); response.write(v.errorsJson()); response.flush(400); return; }
//   long long page = v.integer("page", 1);                           // typed access (default when missing)
//
// A missing optional field passes every rule. The parser only guarantees well-formed requests; what the values
// mean (and where they are used: SQL, files, commands, HTML) is decided by the rules you pick and by escaping on output.
class Validator
{
public:
    class Rule
    {
    public:
        Rule &required();
        Rule &minLength(size_t bytes);
        Rule &maxLength(size_t bytes);
        Rule &integer(long long minimum = std::numeric_limits<long long>::min(), long long maximum = std::numeric_limits<long long>::max());
        Rule &number(double minimum = -1e308, double maximum = 1e308);
        Rule &boolean();                                   // true/false/1/0/yes/no/on/off
        Rule &oneOf(std::vector<std::string> allowed);
        Rule &slug();                                      // [A-Za-z0-9_-]+
        Rule &alphanumeric();
        Rule &hex();
        Rule &uuid();
        Rule &email();                                     // pragmatic check: local@domain.tld, no spaces
        Rule &url();                                       // http:// or https:// followed by a host
        Rule &ipAddress();                                 // IPv4 or IPv6 literal
        Rule &utf8();                                      // valid UTF-8 without control characters (TAB / LF / CR allowed)
        Rule &noControlChars();
        Rule &safeFileName();                              // no path separators, "..", control characters or reserved characters
        Rule &custom(std::function<bool(const std::string &)> predicate, const std::string &message);

    private:
        friend class Validator;
        using Check = std::function<std::string(const std::string &)>;     // returns an error message or ""
        std::string m_name;
        bool m_present = false;
        std::string m_value;
        bool m_required = false;
        std::vector<Check> m_checks;
        std::string m_error;
    };

    // |value| == nullptr means "missing".
    Rule &field(const std::string &name, const std::string *value);
    Rule &query(const HTTPRequest &request, const std::string &name);   // ?name=...
    Rule &form(const HTTPRequest &request, const std::string &name);    // application/x-www-form-urlencoded body (same table as the query)
    Rule &param(const HTTPRequest &request, const std::string &name);   // ":name" route parameter
    Rule &header(const HTTPRequest &request, const std::string &name);

    bool ok();
    const std::vector<std::pair<std::string, std::string>> &errors();   // (field, message)
    std::string errorsJson();                                           // {"errors":{"email":"must be an e-mail address"}}

    // Typed access after ok(); |fallback| when the field is missing or invalid.
    std::string text(const std::string &name, const std::string &fallback = std::string()) const;
    long long integer(const std::string &name, long long fallback = 0) const;
    double number(const std::string &name, double fallback = 0) const;
    bool boolean(const std::string &name, bool fallback = false) const;

    // Standalone checks, usable without a Validator.
    static bool isEmail(const std::string &text);
    static bool isUrl(const std::string &text);
    static bool isIpAddress(const std::string &text);
    static bool isValidUtf8(const std::string &text, bool allowControlCharacters = false);
    static bool isSafeFileName(const std::string &text);

private:
    const Rule *find(const std::string &name) const;
    void evaluate();

    std::vector<std::unique_ptr<Rule>> m_rules;
    std::vector<std::pair<std::string, std::string>> m_errors;
    bool m_evaluated = false;
};

} // namespace rizweb

#endif // CLSVALIDATOR_H
