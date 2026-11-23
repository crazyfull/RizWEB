#include "clsHTTPMultipartUpload.h"
#include "../Utils/clsCrypto.h"
#include "../Utils/clsFileUtils.h"
#include "../Utils/clsFileWriter.h"
#include "../Utils/clsStringUtils.h"
#include "../clsHTTPClient.h"
#include "clsHTTPMultipart.h"
#include <algorithm>

namespace rizweb {

const std::string *UploadResult::field(const std::string &name) const
{
    for (const auto &item : fields)
        if (item.first == name)
            return &item.second;
    return nullptr;
}

const UploadedFile *UploadResult::file(const std::string &fieldName) const
{
    for (const UploadedFile &item : files)
        if (item.fieldName == fieldName)
            return &item;
    return nullptr;
}

namespace {

std::string extensionOf(const std::string &fileName)
{
    size_t dot = fileName.rfind('.');
    if (dot == std::string::npos || dot == 0 || fileName.size() - dot > 16)
        return {};
    std::string extension = StringUtils::toLower(fileName.substr(dot + 1));
    for (char c : extension)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return {};
    return extension;
}

class MultipartUploadSink : public UploadSink, private MultipartHandler
{
public:
    MultipartUploadSink(const UploadOptions &options, const UploadHandler &handler, HTTPClient &client, const std::string &boundary, uint64_t total)
        : m_options(options), m_handler(handler), m_client(client), m_parser(boundary)
    {
        m_progress.total = total;
        m_directory = FileUtils::removeTrailingSlash(options.directory);
    }

    ~MultipartUploadSink() override { removeFiles(); }

    bool onBody(const char *data, size_t length) override
    {
        m_progress.received += length;
        m_result.totalBytes += length;
        HTTPMultipartParser::Status status = m_parser.feed(data, length, *this);
        if (status == HTTPMultipartParser::Status::Error) {
            if (m_failure.status == 0)
                fail(400, "Malformed multipart body: " + m_parser.error());
            return false;
        }
        if (m_options.onProgress && m_progress.received - m_lastReported >= m_options.progressStepBytes) {
            m_lastReported = m_progress.received;
            m_options.onProgress(m_client, m_progress);
        }
        return true;
    }

    void onEnd(HTTPClient &client, HTTPRequest &request, HTTPResponse &response) override
    {
        if (!m_parser.isFinished()) {
            fail(400, "The multipart body is incomplete.");
            onAbort(&response);
            return;
        }
        if (m_options.onProgress) {
            m_progress.currentFile.clear();
            m_options.onProgress(m_client, m_progress);
        }
        m_completed = true;
        m_handler(client, request, response, m_result);
    }

    void onAbort(HTTPResponse *response) override
    {
        removeFiles();
        if (response && !response->isCommitted() && m_failure.status != 0)
            response->sendError(m_failure.status, m_failure.message);
    }

private:
    struct Failure
    {
        int status = 0;
        std::string message;
    };

    void fail(int status, const std::string &message)
    {
        if (m_failure.status == 0) {
            m_failure.status = status;
            m_failure.message = message;
        }
    }

    void removeFiles()
    {
        if (m_completed)
            return;                                   // the handler owns the files now
        m_writer.discard();
        for (const UploadedFile &file : m_result.files)
            std::remove(file.path.c_str());
        m_result.files.clear();
    }

    // Picks the stored path for a client file name.
    bool chooseStoredPath(const std::string &safeName, std::string &path)
    {
        if (!FileUtils::createDirectories(m_directory))
            return false;
        std::string extension = extensionOf(safeName);
        if (!m_options.keepOriginalName) {
            for (int attempt = 0; attempt < 4; attempt++) {
                path = m_directory + "/" + Crypto::randomHex(10) + (extension.empty() ? "" : "." + extension);
                if (m_writer.open(path))
                    return true;
            }
            return false;
        }
        path = m_directory + "/" + safeName;
        if (m_writer.open(path))
            return true;
        size_t dot = safeName.rfind('.');
        std::string stem = dot == std::string::npos || dot == 0 ? safeName : safeName.substr(0, dot);
        std::string suffix = dot == std::string::npos || dot == 0 ? std::string() : safeName.substr(dot);
        for (int number = 1; number < 1000; number++) {
            path = m_directory + "/" + stem + " (" + std::to_string(number) + ")" + suffix;
            if (m_writer.open(path))
                return true;
        }
        return false;
    }

    bool onPartBegin(const MultipartPart &part) override
    {
        m_part = part;
        m_fieldValue.clear();
        m_partBytes = 0;
        if (!part.isFile) {
            if (m_result.fields.size() >= m_options.maxFields) {
                fail(413, "Too many form fields.");
                return false;
            }
            return true;
        }
        if (part.fileName.empty()) {
            m_skipping = true;              // browsers send an empty filename when no file was selected
            return true;
        }
        m_skipping = false;
        if (m_result.files.size() >= m_options.maxFiles) {
            fail(413, "Too many files.");
            return false;
        }
        std::string safeName = HTTPMultipartParser::sanitizeFileName(part.fileName);
        if (!m_options.allowedExtensions.empty()) {
            std::string extension = extensionOf(safeName);
            if (std::find(m_options.allowedExtensions.begin(), m_options.allowedExtensions.end(), extension) == m_options.allowedExtensions.end()) {
                fail(415, "This file type is not allowed.");
                return false;
            }
        }
        std::string path;
        if (!chooseStoredPath(safeName, path)) {
            fail(500, "The file cannot be stored.");
            return false;
        }
        m_current.fieldName = part.name;
        m_current.fileName = safeName;
        m_current.contentType = part.contentType;
        m_current.path = path;
        m_progress.currentFile = safeName;
        return true;
    }

    bool onPartData(const char *data, size_t length) override
    {
        m_partBytes += length;
        if (!m_part.isFile) {
            if (m_fieldValue.size() + length > m_options.maxFieldBytes) {
                fail(413, "A form field is too large.");
                return false;
            }
            m_fieldValue.append(data, length);
            return true;
        }
        if (m_skipping)
            return true;
        if (m_options.maxFileBytes && m_partBytes > m_options.maxFileBytes) {
            fail(413, "The file is too large.");
            return false;
        }
        if (!m_writer.write(data, length)) {
            fail(500, "The file cannot be written.");
            return false;
        }
        return true;
    }

    bool onPartEnd() override
    {
        if (!m_part.isFile) {
            m_result.fields.emplace_back(m_part.name, m_fieldValue);
            return true;
        }
        if (m_skipping) {
            m_skipping = false;
            return true;
        }
        if (!m_writer.close()) {
            fail(500, "The file cannot be written.");
            return false;
        }
        m_current.size = m_partBytes;
        m_result.files.push_back(m_current);
        m_writer.forget();
        m_progress.currentFile.clear();
        return true;
    }

    UploadOptions m_options;
    UploadHandler m_handler;
    HTTPClient &m_client;
    HTTPMultipartParser m_parser;
    std::string m_directory;
    UploadResult m_result;
    UploadProgressInfo m_progress;
    uint64_t m_lastReported = 0;
    MultipartPart m_part;
    UploadedFile m_current;
    FileWriter m_writer;
    std::string m_fieldValue;
    uint64_t m_partBytes = 0;
    bool m_skipping = false;
    bool m_completed = false;
    Failure m_failure;
};

} // namespace

std::unique_ptr<UploadSink> makeMultipartUploadSink(const UploadOptions &options, const UploadHandler &handler,
                                                    HTTPClient &client, HTTPRequest &request, HTTPResponse &response)
{
    const std::string *contentType = request.headerField("content-type");
    std::string boundary;
    if (!contentType || !HTTPMultipartParser::boundaryFromContentType(*contentType, boundary)) {
        response.sendError(415, "multipart/form-data is required.");
        return nullptr;
    }
    return std::unique_ptr<UploadSink>(new MultipartUploadSink(options, handler, client, boundary, request.bodyKind() == BodyKind::ContentLength ? request.contentLength() : 0));
}

} // namespace rizweb
