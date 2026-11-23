cmake_minimum_required(VERSION 3.16)
project(RizWEB VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Network backend (the two libraries have the same API):
#   Linux            -> netRector        (epoll, sharded server)
#   Windows / macOS  -> netRectorClient  (portable event loop)
#
# Where the backend comes from, in order:
#   1. -DNETRECTOR_DIR=<checkout of netRector / netRectorClient>
#   2. an already existing target (RizWEB used through add_subdirectory by a project that
#      builds netRector / netRectorClient itself - then both share the same library)
#   3. FetchContent from GitHub
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RIZWEB_BACKEND_NAME netRector)
    set(RIZWEB_BACKEND_DEFINE RIZWEB_USE_NETRECTOR)
else()
    set(RIZWEB_BACKEND_NAME netRectorClient)
    set(RIZWEB_BACKEND_DEFINE RIZWEB_USE_NETRECTOR_CLIENT)
endif()
set(RIZWEB_FORCE_CLIENT_BACKEND OFF CACHE BOOL "Use netRectorClient even on Linux")
if(RIZWEB_FORCE_CLIENT_BACKEND)
    set(RIZWEB_BACKEND_NAME netRectorClient)
    set(RIZWEB_BACKEND_DEFINE RIZWEB_USE_NETRECTOR_CLIENT)
endif()

option(RIZWEB_ENABLE_TLS "TLS (HTTPS / WSS) with BoringSSL" ON)
set(RIZWEB_BORINGSSL_DIR "" CACHE PATH "BoringSSL prefix with include/ and lib/ (patched, see netRector scripts/build_boringssl.sh)")
option(RIZWEB_BUILD_APP "Build the standalone rizweb server" ON)
option(RIZWEB_BUILD_SHARED "Build RizWEB as a shared library instead of a static one" OFF)
option(RIZWEB_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(RIZWEB_STATIC "Link the server fully static where the platform allows it" ON)
set(NETRECTOR_DIR "" CACHE PATH "Source tree of ${RIZWEB_BACKEND_NAME}")

if(NOT TARGET ${RIZWEB_BACKEND_NAME})
    if(NOT NETRECTOR_DIR)
        include(FetchContent)
        FetchContent_Declare(${RIZWEB_BACKEND_NAME}
            GIT_REPOSITORY https://github.com/crazyfull/${RIZWEB_BACKEND_NAME}.git
            GIT_TAG main)
        FetchContent_GetProperties(${RIZWEB_BACKEND_NAME})
        if(NOT ${RIZWEB_BACKEND_NAME}_POPULATED)
            FetchContent_Populate(${RIZWEB_BACKEND_NAME})
        endif()
        string(TOLOWER ${RIZWEB_BACKEND_NAME} _lower)
        set(NETRECTOR_DIR "${${_lower}_SOURCE_DIR}")
    endif()
    set(NETRECTOR_ENABLE_TLS ${RIZWEB_ENABLE_TLS} CACHE BOOL "" FORCE)
    set(NETRECTOR_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(NETRECTOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NETRECTOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(RIZWEB_BORINGSSL_DIR)
        set(BORINGSSL_DIR "${RIZWEB_BORINGSSL_DIR}" CACHE PATH "" FORCE)    # netRector
        set(BORINGSSL_ROOT "${RIZWEB_BORINGSSL_DIR}" CACHE PATH "" FORCE)   # netRectorClient
    endif()
    add_subdirectory("${NETRECTOR_DIR}" netrector-build EXCLUDE_FROM_ALL)
endif()

# TLS must really be there when it was asked for (the backends silently fall back to "no TLS")
if(RIZWEB_ENABLE_TLS)
    get_target_property(_defs ${RIZWEB_BACKEND_NAME} INTERFACE_COMPILE_DEFINITIONS)
    if(NOT _defs MATCHES "NETRECTOR_HAS_TLS")
        message(FATAL_ERROR "RIZWEB_ENABLE_TLS=ON but ${RIZWEB_BACKEND_NAME} was built without BoringSSL. "
                            "Pass -DRIZWEB_BORINGSSL_DIR=<prefix> or -DRIZWEB_ENABLE_TLS=OFF.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Library: the portable C++ API for other applications
# ---------------------------------------------------------------------------
file(GLOB_RECURSE RIZWEB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE RIZWEB_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
if(RIZWEB_BUILD_SHARED)
    add_library(RizWEB SHARED ${RIZWEB_SOURCES} ${RIZWEB_HEADERS})
    set_target_properties(RizWEB PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    add_library(RizWEB STATIC ${RIZWEB_SOURCES} ${RIZWEB_HEADERS})
endif()
add_library(RizWEB::RizWEB ALIAS RizWEB)

target_include_directories(RizWEB PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/RizWEB>)
target_compile_definitions(RizWEB PUBLIC ${RIZWEB_BACKEND_DEFINE} SERVER_NAME="RizWEB/${PROJECT_VERSION}")
target_link_libraries(RizWEB PUBLIC ${RIZWEB_BACKEND_NAME})

if(RIZWEB_ENABLE_TLS)
    target_compile_definitions(RizWEB PUBLIC RIZWEB_HAS_TLS=1)
endif()

if(RIZWEB_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)          # a static executable needs libz.a
endif()
find_package(ZLIB QUIET)
if(_saved_suffixes)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_saved_suffixes})
endif()
if(ZLIB_FOUND)
    target_link_libraries(RizWEB PUBLIC ZLIB::ZLIB)
    target_compile_definitions(RizWEB PUBLIC RIZWEB_HAS_ZLIB=1)
else()
    message(STATUS "RizWEB: zlib not found, gzip/deflate responses are disabled")
endif()

find_package(Threads REQUIRED)
target_link_libraries(RizWEB PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(RizWEB PRIVATE /W3 /utf-8)
    target_compile_definitions(RizWEB PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(RizWEB PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(WIN32)
    target_link_libraries(RizWEB PUBLIC ws2_32 mswsock)
endif()

# ---------------------------------------------------------------------------
# Standalone server
# ---------------------------------------------------------------------------
if(RIZWEB_BUILD_APP)
    add_executable(rizweb main.cpp)
    target_link_libraries(rizweb PRIVATE RizWEB)
    if(RIZWEB_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(rizweb PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    install(TARGETS rizweb RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Tests (ctest)
# ---------------------------------------------------------------------------
if(RIZWEB_BUILD_TESTS)
    enable_testing()
    add_executable(rizweb_tests tests/testMain.cpp tests/requestTest.cpp tests/utilTest.cpp)
    target_link_libraries(rizweb_tests PRIVATE RizWEB)
    add_test(NAME rizweb_tests COMMAND rizweb_tests)
endif()

# ---------------------------------------------------------------------------
# Install: library, headers (same layout as src/), default resources
# ---------------------------------------------------------------------------
install(TARGETS RizWEB ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY src/ DESTINATION include/RizWEB FILES_MATCHING PATTERN "*.h")
install(DIRECTORY resource/ DESTINATION share/rizweb/resource)
