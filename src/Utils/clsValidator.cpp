#include "clsValidator.h"
#include "../HTTP/clsHTTPRequest.h"
#include "clsEscape.h"
#include "clsStringUtils.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace rizweb {

// ---- standalone checks ------------------------------------------------------------------------

bool Validator::isValidUtf8(const std::string &text, bool allowControlCharacters)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text.data());
    size_t left = text.size();
    while (left > 0) {
        size_t length;
        if (p[0] < 0x80) {
            if (!allowControlCharacters && (p[0] < 0x20 || p[0] == 0x7f) && p[0] != '\t' && p[0] != '\n' && p[0] != '\r')
                return false;
            length = 1;
        } else if (p[0] >= 0xC2 && p[0] <= 0xDF && left >= 2 && (p[1] & 0xC0) == 0x80) {
            length = 2;
        } else if (p[0] >= 0xE0 && p[0] <= 0xEF && left >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
                   !(p[0] == 0xE0 && p[1] < 0xA0) && !(p[0] == 0xED && p[1] >= 0xA0)) {
            length = 3;
        } else if (p[0] >= 0xF0 && p[0] <= 0xF4 && left >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80 &&
                   !(p[0] == 0xF0 && p[1] < 0x90) && !(p[0] == 0xF4 && p[1] >= 0x90)) {
            length = 4;
        } else {
            return false;
        }
        p += length;
        left -= length;
    }
    return true;
}

bool Validator::isEmail(const std::string &text)
{
    if (text.size() < 3 || text.size() > 254)
        return false;
    size_t at = text.find('@');
    if (at == std::string::npos || at == 0 || at != text.rfind('@') || at > 64)
        return false;
    std::string local = text.substr(0, at), domain = text.substr(at + 1);
    for (char c : local)
        if (!(isalnum(static_cast<unsigned char>(c)) || std::string(".!#$%&'*+/=?^_`{|}~-").find(c) != std::string::npos))
            return false;
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string::npos)
        return false;
    size_t dot = domain.find('.');
    if (domain.empty() || dot == std::string::npos || domain.front() == '.' || domain.back() == '.' || domain.find("..") != std::string::npos)
        return false;
    for (char c : domain)
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-'))
            return false;
    return domain.front() != '-' && domain.back() != '-';
}

bool Validator::isIpAddress(const std::string &text)
{
    unsigned char buffer[sizeof(struct in6_addr)];
    return text.size() <= 45 && (inet_pton(AF_INET, text.c_str(), buffer) == 1 || inet_pton(AF_INET6, text.c_str(), buffer) == 1);
}

bool Validator::isUrl(const std::string &text)
{
    if (text.size() > 2048)
        return false;
    size_t start = StringUtils::startsWith(StringUtils::toLower(text.substr(0, 8)), "https://") ? 8
                 : StringUtils::startsWith(StringUtils::toLower(text.substr(0, 7)), "http://") ? 7 : 0;
    if (start == 0)
        return false;
    for (char c : text)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f || c == '<' || c == '>' || c == '"' || c == '\\')
            return false;
    size_t hostEnd = text.find_first_of("/?#", start);
    std::string authority = text.substr(start, hostEnd == std::string::npos ? std::string::npos : hostEnd - start);
    size_t userInfo = authority.rfind('@');
    if (userInfo != std::string::npos)
        authority = authority.substr(userInfo + 1);
    return !authority.empty() && authority[0] != ':' && authority[0] != '.';
}

bool Validator::isSafeFileName(const std::string &text)
{
    if (text.empty() || text.size() > 255 || text == "." || text == "..")
        return false;
    for (char c : text)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    return text.front() != '.' && text.back() != '.' && text.back() != ' ';
}

// ---- rules ------------------------------------------------------------------------------------

Validator::Rule &Validator::Rule::required()
{
    m_required = true;
    return *this;
}

Validator::Rule &Validator::Rule::minLength(size_t bytes)
{
    m_checks.push_back([bytes](const std::string &v) { return v.size() < bytes ? "must be at least " + std::to_string(bytes) + " characters" : std::string(); });
    return *this;
}

Validator::Rule &Validator::Rule::maxLength(size_t bytes)
{
    m_checks.push_back([bytes](const std::string &v) { return v.size() > bytes ? "must be at most " + std::to_string(bytes) + " characters" : std::string(); });
    return *this;
}

Validator::Rule &Validator::Rule::integer(long long minimum, long long maximum)
{
    m_checks.push_back([minimum, maximum](const std::string &v) -> std::string {
        if (v.empty() || v.size() > 20)
            return "must be an integer";
        size_t i = (v[0] == '-' || v[0] == '+') ? 1 : 0;
        if (i == v.size())
            return "must be an integer";
        for (size_t k = i; k < v.size(); k++)
            if (v[k] < '0' || v[k] > '9')
                return "must be an integer";
        errno = 0;
        long long value = strtoll(v.c_str(), nullptr, 10);
        if (errno == ERANGE || value < minimum || value > maximum)
            return "must be between " + std::to_string(minimum) + " and " + std::to_string(maximum);
        return {};
    });
    return *this;
}

Validator::Rule &Validator::Rule::number(double minimum, double maximum)
{
    m_checks.push_back([minimum, maximum](const std::string &v) -> std::string {
        if (v.empty() || v.size() > 40 || v.find_first_of("xXnNiI ") != std::string::npos)   // no hex, nan, inf, spaces
            return "must be a number";
        char *end = nullptr;
        errno = 0;
        double value = strtod(v.c_str(), &end);
        if (end != v.c_str() + v.size() || errno == ERANGE)
            return "must be a number";
        if (value < minimum || value > maximum)
            return "is out of range";
        return {};
    });
    return *this;
}

Validator::Rule &Validator::Rule::boolean()
{
    m_checks.push_back([](const std::string &v) -> std::string {
        std::string t = StringUtils::toLower(v);
        for (const char *ok : {"true", "false", "1", "0", "yes", "no", "on", "off"})
            if (t == ok)
                return {};
        return "must be true or false";
    });
    return *this;
}

Validator::Rule &Validator::Rule::oneOf(std::vector<std::string> allowed)
{
    m_checks.push_back([allowed](const std::string &v) {
        return std::find(allowed.begin(), allowed.end(), v) == allowed.end() ? std::string("is not an allowed value") : std::string();
    });
    return *this;
}

static bool allChars(const std::string &v, bool (*accept)(char))
{
    return !v.empty() && std::all_of(v.begin(), v.end(), accept);
}

Validator::Rule &Validator::Rule::slug()
{
    m_checks.push_back([](const std::string &v) {
        return allChars(v, [](char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }) ? std::string() : std::string("may only contain letters, digits, '_' and '-'");
    });
    return *this;
}

Validator::Rule &Validator::Rule::alphanumeric()
{
    m_checks.push_back([](const std::string &v) {
        return allChars(v, [](char c) { return isalnum(static_cast<unsigned char>(c)) != 0; }) ? std::string() : std::string("may only contain letters and digits");
    });
    return *this;
}

Validator::Rule &Validator::Rule::hex()
{
    m_checks.push_back([](const std::string &v) {
        return allChars(v, [](char c) { return isxdigit(static_cast<unsigned char>(c)) != 0; }) ? std::string() : std::string("must be hexadecimal");
    });
    return *this;
}

Validator::Rule &Validator::Rule::uuid()
{
    m_checks.push_back([](const std::string &v) -> std::string {
        if (v.size() != 36)
            return "must be a UUID";
        for (size_t i = 0; i < v.size(); i++) {
            bool dash = i == 8 || i == 13 || i == 18 || i == 23;
            if (dash ? v[i] != '-' : !isxdigit(static_cast<unsigned char>(v[i])))
                return "must be a UUID";
        }
        return {};
    });
    return *this;
}

Validator::Rule &Validator::Rule::email()
{
    m_checks.push_back([](const std::string &v) { return Validator::isEmail(v) ? std::string() : std::string("must be an e-mail address"); });
    return *this;
}

Validator::Rule &Validator::Rule::url()
{
    m_checks.push_back([](const std::string &v) { return Validator::isUrl(v) ? std::string() : std::string("must be an http(s) URL"); });
    return *this;
}

Validator::Rule &Validator::Rule::ipAddress()
{
    m_checks.push_back([](const std::string &v) { return Validator::isIpAddress(v) ? std::string() : std::string("must be an IP address"); });
    return *this;
}

Validator::Rule &Validator::Rule::utf8()
{
    m_checks.push_back([](const std::string &v) { return Validator::isValidUtf8(v) ? std::string() : std::string("must be valid UTF-8 text"); });
    return *this;
}

Validator::Rule &Validator::Rule::noControlChars()
{
    m_checks.push_back([](const std::string &v) {
        for (char c : v)
            if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7f)
                return std::string("contains control characters");
        return std::string();
    });
    return *this;
}

Validator::Rule &Validator::Rule::safeFileName()
{
    m_checks.push_back([](const std::string &v) { return Validator::isSafeFileName(v) ? std::string() : std::string("is not a safe file name"); });
    return *this;
}

Validator::Rule &Validator::Rule::custom(std::function<bool(const std::string &)> predicate, const std::string &message)
{
    m_checks.push_back([predicate, message](const std::string &v) { return predicate(v) ? std::string() : message; });
    return *this;
}

// ---- validator --------------------------------------------------------------------------------

Validator::Rule &Validator::field(const std::string &name, const std::string *value)
{
    m_evaluated = false;
    m_rules.emplace_back(new Rule);
    Rule &rule = *m_rules.back();
    rule.m_name = name;
    rule.m_present = value != nullptr;
    if (value)
        rule.m_value = *value;
    return rule;
}

Validator::Rule &Validator::query(const HTTPRequest &request, const std::string &name) { return field(name, request.queryField(name)); }
Validator::Rule &Validator::form(const HTTPRequest &request, const std::string &name) { return field(name, request.queryField(name)); }
Validator::Rule &Validator::header(const HTTPRequest &request, const std::string &name) { return field(name, request.headerField(name)); }

Validator::Rule &Validator::param(const HTTPRequest &request, const std::string &name)
{
    std::string value = request.param(name);
    return field(name, value.empty() ? nullptr : &value);
}

void Validator::evaluate()
{
    if (m_evaluated)
        return;
    m_errors.clear();
    for (const auto &rule : m_rules) {
        rule->m_error.clear();
        if (!rule->m_present || (rule->m_value.empty() && !rule->m_required)) {
            if (rule->m_required)
                rule->m_error = "is required";
        } else if (rule->m_required && rule->m_value.empty()) {
            rule->m_error = "is required";
        } else {
            for (const Rule::Check &check : rule->m_checks) {
                rule->m_error = check(rule->m_value);
                if (!rule->m_error.empty())
                    break;
            }
        }
        if (!rule->m_error.empty())
            m_errors.emplace_back(rule->m_name, rule->m_error);
    }
    m_evaluated = true;
}

bool Validator::ok()
{
    evaluate();
    return m_errors.empty();
}

const std::vector<std::pair<std::string, std::string>> &Validator::errors()
{
    evaluate();
    return m_errors;
}

std::string Validator::errorsJson()
{
    evaluate();
    std::string json = "{\"errors\":{";
    for (size_t i = 0; i < m_errors.size(); i++)
        json += std::string(i ? "," : "") + "\"" + Escape::json(m_errors[i].first) + "\":\"" + Escape::json(m_errors[i].second) + "\"";
    return json + "}}";
}

const Validator::Rule *Validator::find(const std::string &name) const
{
    for (const auto &rule : m_rules)
        if (rule->m_name == name && rule->m_present)
            return rule.get();
    return nullptr;
}

std::string Validator::text(const std::string &name, const std::string &fallback) const
{
    const Rule *rule = find(name);
    return rule ? rule->m_value : fallback;
}

long long Validator::integer(const std::string &name, long long fallback) const
{
    const Rule *rule = find(name);
    if (!rule || rule->m_value.empty())
        return fallback;
    char *end = nullptr;
    long long value = strtoll(rule->m_value.c_str(), &end, 10);
    return (end && *end == '\0') ? value : fallback;
}

double Validator::number(const std::string &name, double fallback) const
{
    const Rule *rule = find(name);
    if (!rule || rule->m_value.empty())
        return fallback;
    char *end = nullptr;
    double value = strtod(rule->m_value.c_str(), &end);
    return (end && *end == '\0') ? value : fallback;
}

bool Validator::boolean(const std::string &name, bool fallback) const
{
    const Rule *rule = find(name);
    if (!rule)
        return fallback;
    std::string t = StringUtils::toLower(rule->m_value);
    if (t == "true" || t == "1" || t == "yes" || t == "on")
        return true;
    if (t == "false" || t == "0" || t == "no" || t == "off")
        return false;
    return fallback;
}

} // namespace rizweb
