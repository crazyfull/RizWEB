#include "HTTP/clsHTTPBodyDecoder.h"
#include "HTTP/clsHTTPMultipart.h"
#include "Utils/clsEscape.h"
#include "Utils/clsValidator.h"
#include "testFramework.h"

using namespace rizweb;

namespace {

struct Collector : MultipartHandler
{
    struct Part
    {
        MultipartPart info;
        std::string data;
    };
    std::vector<Part> parts;
    bool onPartBegin(const MultipartPart &part) override { parts.push_back({part, {}}); return true; }
    bool onPartData(const char *data, size_t length) override { parts.back().data.append(data, length); return true; }
    bool onPartEnd() override { return true; }
};

const char *kBoundary = "----WebKitFormBoundaryABC123";

std::string sampleBody(const std::string &fileData)
{
    std::string b = std::string("--") + kBoundary;
    return b + "\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\nhello world\r\n" +
           b + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"pic.png\"\r\nContent-Type: image/png\r\n\r\n" + fileData + "\r\n" +
           b + "\r\nContent-Disposition: form-data; name=\"empty\"; filename=\"\"\r\nContent-Type: application/octet-stream\r\n\r\n\r\n" +
           b + "--\r\n";
}

} // namespace

TEST(multipartParsesAllSplits)
{
    // file data that contains look-alikes of the delimiter
    std::string fileData = "abc\r\n--" + std::string(kBoundary).substr(0, 10) + "XYZ\r\n--\r\n\r\nend";
    fileData.append(1000, 'z');
    std::string body = sampleBody(fileData);
    for (size_t step : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(31), size_t(64), size_t(1000), body.size()}) {
        HTTPMultipartParser parser(kBoundary);
        Collector collector;
        HTTPMultipartParser::Status status = HTTPMultipartParser::Status::NeedMore;
        for (size_t i = 0; i < body.size(); i += step)
            status = parser.feed(body.data() + i, std::min(step, body.size() - i), collector);
        if (status != HTTPMultipartParser::Status::Finished) printf("    step %zu status %d parts %zu\n", step, (int)status, collector.parts.size());
        CHECK(status == HTTPMultipartParser::Status::Finished);
        CHECK_EQ(collector.parts.size(), size_t(3));
        if (collector.parts.size() == 3) {
            CHECK_EQ(collector.parts[0].info.name, std::string("title"));
            CHECK(!collector.parts[0].info.isFile);
            CHECK_EQ(collector.parts[0].data, std::string("hello world"));
            CHECK_EQ(collector.parts[1].info.fileName, std::string("pic.png"));
            CHECK_EQ(collector.parts[1].info.contentType, std::string("image/png"));
            CHECK(collector.parts[1].data == fileData);
            CHECK(collector.parts[2].info.isFile && collector.parts[2].info.fileName.empty());
            CHECK(collector.parts[2].data.empty());
        }
    }
}

TEST(multipartRejectsMalformed)
{
    Collector collector;
    HTTPMultipartParser noDisposition(kBoundary);
    std::string text = std::string("--") + kBoundary + "\r\nX-Other: 1\r\n\r\ndata\r\n--" + kBoundary + "--\r\n";
    CHECK(noDisposition.feed(text.data(), text.size(), collector) == HTTPMultipartParser::Status::Error);

    HTTPMultipartParser badAfter(kBoundary);
    text = std::string("--") + kBoundary + "XX";
    CHECK(badAfter.feed(text.data(), text.size(), collector) == HTTPMultipartParser::Status::Error);

    HTTPMultipartParser hugeHeaders(kBoundary);
    text = std::string("--") + kBoundary + "\r\nContent-Disposition: form-data; name=\"a\"; x=\"" + std::string(9000, 'a');
    CHECK(hugeHeaders.feed(text.data(), text.size(), collector) == HTTPMultipartParser::Status::Error);

    HTTPMultipartParser truncated(kBoundary);
    text = sampleBody("data").substr(0, 120);
    CHECK(truncated.feed(text.data(), text.size(), collector) == HTTPMultipartParser::Status::NeedMore);
    CHECK(!truncated.isFinished());
}

TEST(multipartHeaderHelpers)
{
    std::string boundary;
    CHECK(HTTPMultipartParser::boundaryFromContentType("multipart/form-data; boundary=abc123", boundary));
    CHECK_EQ(boundary, std::string("abc123"));
    CHECK(HTTPMultipartParser::boundaryFromContentType("Multipart/Form-Data; charset=x; boundary=\"a b\"; foo=1", boundary));
    CHECK_EQ(boundary, std::string("a b"));
    CHECK(!HTTPMultipartParser::boundaryFromContentType("multipart/form-data", boundary));
    CHECK(!HTTPMultipartParser::boundaryFromContentType("text/plain; boundary=x", boundary));
    CHECK(!HTTPMultipartParser::boundaryFromContentType("multipart/form-data; boundary=" + std::string(80, 'a'), boundary));

    CHECK_EQ(HTTPMultipartParser::sanitizeFileName("../../etc/passwd"), std::string("passwd"));
    CHECK_EQ(HTTPMultipartParser::sanitizeFileName("C:\\Users\\a\\b.txt"), std::string("b.txt"));
    CHECK_EQ(HTTPMultipartParser::sanitizeFileName("a\r\nb.txt"), std::string("ab.txt"));
    CHECK_EQ(HTTPMultipartParser::sanitizeFileName(".."), std::string("file"));
    CHECK_EQ(HTTPMultipartParser::sanitizeFileName("...hidden"), std::string("hidden"));
    CHECK_EQ(HTTPMultipartParser::sanitizeFileName(""), std::string("file"));
    CHECK(HTTPMultipartParser::sanitizeFileName(std::string(400, 'a') + ".png").size() <= 255);
    CHECK_EQ(HTTPMultipartParser::sanitizeFileName(std::string(400, 'a') + ".png").substr(251), std::string(".png"));

    Collector collector;
    HTTPMultipartParser parser("b");
    std::string text = "--b\r\nContent-Disposition: form-data; name=\"f\"; filename*=UTF-8''%D8%B3%D9%84%D8%A7%D9%85.txt\r\n\r\nx\r\n--b--\r\n";
    CHECK(parser.feed(text.data(), text.size(), collector) == HTTPMultipartParser::Status::Finished);
    CHECK_EQ(collector.parts[0].info.fileName, std::string("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85.txt"));
}

TEST(bodyDecoderContentLength)
{
    HTTPBodyDecoder decoder = HTTPBodyDecoder::contentLength(10);
    std::string seen;
    auto emit = [&](const char *d, size_t n) { seen.append(d, n); return true; };
    auto r = decoder.feed("hello", 5, 0, emit);
    CHECK(r.status == HTTPBodyDecoder::Status::NeedMore);
    r = decoder.feed("worldNEXT", 9, 0, emit);
    CHECK(r.status == HTTPBodyDecoder::Status::Done);
    CHECK_EQ(r.consumed, size_t(5));
    CHECK_EQ(seen, std::string("helloworld"));

    HTTPBodyDecoder big = HTTPBodyDecoder::contentLength(100);
    CHECK(big.feed("x", 1, 50, emit).status == HTTPBodyDecoder::Status::TooLarge);
    HTTPBodyDecoder aborted = HTTPBodyDecoder::contentLength(5);
    CHECK(aborted.feed("abc", 3, 0, [](const char *, size_t) { return false; }).status == HTTPBodyDecoder::Status::Error);
}

TEST(bodyDecoderChunkedAllSplits)
{
    std::string wire = "4\r\nWiki\r\n5;name=value\r\npedia\r\nD\r\n in\r\n\r\nchunks\r\n0\r\nTrailer: x\r\n\r\nGET /next";
    for (size_t step : {size_t(1), size_t(2), size_t(5), size_t(100)}) {
        HTTPBodyDecoder decoder = HTTPBodyDecoder::chunked();
        std::string seen;
        auto emit = [&](const char *d, size_t n) { seen.append(d, n); return true; };
        size_t used = 0;
        HTTPBodyDecoder::Status status = HTTPBodyDecoder::Status::NeedMore;
        for (size_t i = 0; i < wire.size() && status == HTTPBodyDecoder::Status::NeedMore; i += step) {
            size_t n = std::min(step, wire.size() - i);
            auto r = decoder.feed(wire.data() + i, n, 0, emit);
            status = r.status;
            used = i + r.consumed;
        }
        CHECK(status == HTTPBodyDecoder::Status::Done);
        CHECK_EQ(seen, std::string("Wikipedia in\r\n\r\nchunks"));
        CHECK_EQ(wire.substr(used), std::string("GET /next"));
    }
    auto sink = [](const char *, size_t) { return true; };
    HTTPBodyDecoder badHex = HTTPBodyDecoder::chunked();
    CHECK(badHex.feed("zz\r\n", 4, 0, sink).status == HTTPBodyDecoder::Status::Error);
    HTTPBodyDecoder missingCrlf = HTTPBodyDecoder::chunked();
    CHECK(missingCrlf.feed("1\r\nabX", 6, 0, sink).status == HTTPBodyDecoder::Status::Error);
    HTTPBodyDecoder limited = HTTPBodyDecoder::chunked();
    CHECK(limited.feed("10\r\n0123456789abcdef\r\n0\r\n\r\n", 28, 8, sink).status == HTTPBodyDecoder::Status::TooLarge);
    HTTPBodyDecoder overflow = HTTPBodyDecoder::chunked();
    CHECK(overflow.feed("FFFFFFFFFFFFFFFFFF\r\n", 20, 0, sink).status == HTTPBodyDecoder::Status::Error);
}

TEST(escapeFunctions)
{
    CHECK_EQ(Escape::html("<script>alert('x')</script> & \"q\""), std::string("&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt; &amp; &quot;q&quot;"));
    CHECK_EQ(Escape::htmlAttribute("a\"b\r\nc"), std::string("a&quot;bc"));
    CHECK_EQ(Escape::json("a\"b\\c\n</script>"), std::string("a\\\"b\\\\c\\n\\u003c/script\\u003e"));
    CHECK_EQ(Escape::json(std::string("\xff", 1)), std::string("\\ufffd"));
    CHECK_EQ(Escape::json("\xD8\xB3"), std::string("\xD8\xB3"));
    CHECK_EQ(Escape::jsString("it's `${x}`"), std::string("it\\u0027s \\u0060\\u0024{x}\\u0060"));
    CHECK_EQ(Escape::url("a b&c=d/é"), std::string("a%20b%26c%3Dd%2F%C3%A9"));
    CHECK_EQ(Escape::shellArg("it's; rm -rf /"), std::string("'it'\\''s; rm -rf /'"));
    CHECK_EQ(Escape::headerValue("ok\r\nSet-Cookie: x=1"), std::string("okSet-Cookie: x=1"));
}

TEST(validatorRules)
{
    std::string age = "42", bad = "4x2", email = "user@example.com", badMail = "user@@example", slug = "my-id_1", empty;
    Validator v;
    v.field("age", &age).required().integer(0, 120);
    v.field("email", &email).required().email();
    v.field("slug", &slug).slug();
    v.field("optional", nullptr).integer();
    CHECK(v.ok());
    CHECK_EQ(v.integer("age"), 42LL);
    CHECK_EQ(v.integer("optional", 7), 7LL);

    Validator w;
    w.field("age", &bad).required().integer();
    w.field("mail", &badMail).email();
    w.field("missing", nullptr).required();
    w.field("blank", &empty).required();
    CHECK(!w.ok());
    CHECK_EQ(w.errors().size(), size_t(4));
    CHECK(w.errorsJson().find("\"age\":\"must be an integer\"") != std::string::npos);

    Validator x;
    std::string big = "130", url = "https://example.com/a?b=1", js = "javascript:alert(1)", name = "../x", ok = "photo.png", ip = "::1", tooLong(10, 'a'), flag = "Yes";
    x.field("big", &big).integer(0, 120);
    x.field("url", &url).url();
    x.field("js", &js).url();
    x.field("name", &name).safeFileName();
    x.field("ok", &ok).safeFileName();
    x.field("ip", &ip).ipAddress();
    x.field("len", &tooLong).maxLength(5);
    x.field("flag", &flag).boolean();
    CHECK(!x.ok());
    std::vector<std::string> failed;
    for (const auto &e : x.errors()) failed.push_back(e.first);
    CHECK_EQ(failed, (std::vector<std::string>{"big", "js", "name", "len"}));
    CHECK(x.boolean("flag"));

    CHECK(Validator::isValidUtf8("\xD8\xB3\xD9\x84"));
    CHECK(!Validator::isValidUtf8("\xC0\xAF"));
    CHECK(!Validator::isValidUtf8(std::string("a\0b", 3)));
    CHECK(!Validator::isEmail("a@b"));
    CHECK(Validator::isEmail("first.last+tag@sub.example.org"));
}
