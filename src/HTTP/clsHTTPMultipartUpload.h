#ifndef CLSHTTPMULTIPARTUPLOAD_H
#define CLSHTTPMULTIPARTUPLOAD_H
#include "clsHTTPUpload.h"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rizweb {

struct UploadedFile
{
    std::string fieldName;       // form field name
    std::string fileName;        // name sent by the client, sanitized (display only; never used as a path unless keepOriginalName)
    std::string contentType;     // Content-Type of the part, as sent by the client (do not trust it)
    std::string path;            // where the file was stored
    uint64_t size = 0;
};

struct UploadResult
{
    std::vector<std::pair<std::string, std::string>> fields;   // non-file form fields (name, value)
    std::vector<UploadedFile> files;
    uint64_t totalBytes = 0;                                   // request body bytes received

    const std::string *field(const std::string &name) const;
    const UploadedFile *file(const std::string &fieldName) const;
};

struct UploadProgressInfo
{
    uint64_t received = 0;       // body bytes received so far
    uint64_t total = 0;          // Content-Length, 0 when unknown (chunked)
    std::string currentFile;     // sanitized name of the file being written ("" between files)
};

// Options of WEBMaster::upload().
struct UploadOptions
{
    std::string directory = "upload";               // files are stored here (created when missing)
    uint64_t maxFileBytes = 0;                      // per file; 0 = only the request limit applies
    size_t maxFiles = 32;
    size_t maxFields = 128;
    size_t maxFieldBytes = 64 * 1024;               // size of one text field
    std::vector<std::string> allowedExtensions;     // lower case, no dot ("png", "jpg"); empty = any
    bool keepOriginalName = false;                  // false: <directory>/<random hex>.<extension>; true: sanitized client name ("name (1).ext" when taken)
    uint64_t progressStepBytes = 256 * 1024;        // onProgress is called at most once per step (and at the end)
    std::function<void(HTTPClient &, const UploadProgressInfo &)> onProgress;
};

using UploadHandler = std::function<void(HTTPClient &, HTTPRequest &, HTTPResponse &, UploadResult &)>;

// Sink that parses multipart/form-data and writes every file part directly to disk (no buffering of file
// contents in memory). Answers 4xx itself for malformed / too large / forbidden uploads and removes partial files.
// Returns nullptr after sending 415 when the request is not multipart/form-data.
std::unique_ptr<UploadSink> makeMultipartUploadSink(const UploadOptions &options, const UploadHandler &handler,
                                                    HTTPClient &client, HTTPRequest &request, HTTPResponse &response);

} // namespace rizweb

#endif // CLSHTTPMULTIPARTUPLOAD_H
