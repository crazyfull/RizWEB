#ifndef CLSVERSION_H
#define CLSVERSION_H
// The ONLY place that holds the version. CMake (project version, package version, install names), the release
// workflow (tag and archive names) and the server banner all read it from here.
// Semantic versioning: MAJOR = incompatible API change, MINOR = new feature, PATCH = fix only.
#define RIZWEB_VERSION_MAJOR 2
#define RIZWEB_VERSION_MINOR 3
#define RIZWEB_VERSION_PATCH 2

#define RIZWEB_VERSION_STR2(x) #x
#define RIZWEB_VERSION_STR(x) RIZWEB_VERSION_STR2(x)
#define RIZWEB_VERSION RIZWEB_VERSION_STR(RIZWEB_VERSION_MAJOR) "." RIZWEB_VERSION_STR(RIZWEB_VERSION_MINOR) "." RIZWEB_VERSION_STR(RIZWEB_VERSION_PATCH)
#endif // CLSVERSION_H
