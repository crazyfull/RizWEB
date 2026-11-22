#ifndef CLSHTTPSTATUSCODE_H
#define CLSHTTPSTATUSCODE_H

namespace rizweb {

class HTTPStatusCode
{
public:
    // Reason phrase for a status code ("Not Found"); "Unknown" for codes RizWEB does not use.
    static const char *reasonPhrase(int code);
};

} // namespace rizweb

#endif // CLSHTTPSTATUSCODE_H
