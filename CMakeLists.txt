cmake_minimum_required(VERSION 3.16)

project(RizWEB
    VERSION 2.0.0
    DESCRIPTION "Small, fast HTTP/1.1 + WebSocket server library on top of netRector"
    LANGUAGES CXX
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "RizWEB needs Linux (epoll, sendfile)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(RIZWEB_TOP_LEVEL ON)
else()
    set(RIZWEB_TOP_LEVEL OFF)
endif()

# cmake -S . -B build -DBORINGSSL_DIR=/path/to/boringssl      (TLS; see netRector scripts/build_boringssl.sh)
# cmake -S . -B build -DRIZWEB_NETRECTOR_DIR=/path/to/netRector   (use a local netRector checkout)
# cmake --build build -j"$(nproc)" && ctest --test-dir build --output-on-failure
option(RIZWEB_BUILD_APP "Build the rizweb example executable" ${RIZWEB_TOP_LEVEL})
option(RIZWEB_BUILD_TESTS "Build the unit tests" ${RIZWEB_TOP_LEVEL})
option(RIZWEB_USE_ZLIB "Compress text files with gzip when zlib is available" ON)
option(RIZWEB_STATIC "Link the executable fully static (release binaries)" OFF)
set(RIZWEB_NETRECTOR_DIR "" CACHE PATH "Local netRector checkout (default: fetched from github.com/ArsPand/netRector)")
set(RIZWEB_NETRECTOR_REPOSITORY "https://github.com/ArsPand/netRector.git" CACHE STRING "netRector git URL")
set(RIZWEB_NETRECTOR_TAG "HEAD" CACHE STRING "netRector branch or tag to fetch")

# ---------------------------------------------------------------------------
# netRector (epoll reactors, BoringSSL TLS, sendfile)
# ---------------------------------------------------------------------------
if(NOT TARGET netRector)
    if(RIZWEB_NETRECTOR_DIR)
        add_subdirectory("${RIZWEB_NETRECTOR_DIR}" netRector)
    else()
        include(FetchContent)
        FetchContent_Declare(netRector
            GIT_REPOSITORY "${RIZWEB_NETRECTOR_REPOSITORY}"
            GIT_TAG        "${RIZWEB_NETRECTOR_TAG}"
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(netRector)
    endif()
endif()

# ---------------------------------------------------------------------------
# rizweb_core: everything that does not need the network (parser, response, router,
# static files, WebSocket codec). Unit tested without netRector.
# ---------------------------------------------------------------------------
add_library(rizweb_core STATIC
    src/util/stringUtil.cpp
    src/util/clsUrlCodec.cpp
    src/util/clsSha1.cpp
    src/util/clsBase64.cpp
    src/util/clsRandom.cpp
    src/util/clsLogger.cpp
    src/util/clsHttpDate.cpp
    src/util/clsIniFile.cpp
    src/http/clsHttpStatus.cpp
    src/http/clsHttpHeaders.cpp
    src/http/clsHttpRequest.cpp
    src/http/clsHttpRequestParser.cpp
    src/http/clsHttpResponse.cpp
    src/http/clsMimeTypes.cpp
    src/http/clsRouter.cpp
    src/http/clsSessionStore.cpp
    src/http/clsGzipCache.cpp
    src/http/clsStaticFileHandler.cpp
    src/http/clsAuthGuard.cpp
    src/websocket/clsWebSocketFrame.cpp
    src/websocket/clsWebSocketHandshake.cpp
    src/server/clsServerConfig.cpp
)
target_include_directories(rizweb_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(rizweb_core PRIVATE -Wall -Wextra)

if(RIZWEB_USE_ZLIB)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        target_link_libraries(rizweb_core PUBLIC ZLIB::ZLIB)
        target_compile_definitions(rizweb_core PUBLIC RIZWEB_HAS_ZLIB=1)
        message(STATUS "RizWEB: gzip enabled (zlib ${ZLIB_VERSION_STRING})")
    else()
        message(STATUS "RizWEB: zlib not found, gzip disabled")
    endif()
endif()

# ---------------------------------------------------------------------------
# rizweb: the library applications link against
# ---------------------------------------------------------------------------
add_library(rizweb STATIC
    src/server/clsHttpConnection.cpp
    src/server/clsWebServer.cpp
)
target_link_libraries(rizweb PUBLIC rizweb_core netRector)
target_compile_options(rizweb PRIVATE -Wall -Wextra)
add_library(rizweb::rizweb ALIAS rizweb)

# ---------------------------------------------------------------------------
# executable
# ---------------------------------------------------------------------------
if(RIZWEB_BUILD_APP)
    add_executable(rizweb_app src/main.cpp)
    target_link_libraries(rizweb_app PRIVATE rizweb)
    target_compile_options(rizweb_app PRIVATE -Wall -Wextra)
    set_target_properties(rizweb_app PROPERTIES
        OUTPUT_NAME rizweb
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(RIZWEB_STATIC)
        target_link_options(rizweb_app PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---------------------------------------------------------------------------
# tests (no network; the connection tests use a fake netrector::TCPSocket)
# ---------------------------------------------------------------------------
if(RIZWEB_BUILD_TESTS)
    enable_testing()

    add_executable(rizweb_core_tests tests/testMain.cpp)
    target_link_libraries(rizweb_core_tests PRIVATE rizweb_core)
    add_test(NAME core COMMAND rizweb_core_tests)

    # compiled against tests/fakeNetrector instead of the real netRector headers
    add_executable(rizweb_connection_tests
        tests/connectionTest.cpp
        src/server/clsHttpConnection.cpp
        src/server/clsWebServer.cpp
    )
    target_include_directories(rizweb_connection_tests BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fakeNetrector")
    target_link_libraries(rizweb_connection_tests PRIVATE rizweb_core)
    add_test(NAME connection COMMAND rizweb_connection_tests)
endif()
