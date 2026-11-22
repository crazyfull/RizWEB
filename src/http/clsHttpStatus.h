#ifndef RIZWEB_CLSHTTPSTATUS_H
#define RIZWEB_CLSHTTPSTATUS_H

namespace rizweb {

class HttpStatus
{
public:
    // Reason phrase for the status line ("Not Found"); "Unknown" for codes that are not listed.
    static const char* reason(int code);

    // 1xx, 204 and 304 responses never carry a body.
    static bool allowsBody(int code);
};

} // namespace rizweb

#endif // RIZWEB_CLSHTTPSTATUS_H
