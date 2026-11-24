cmake_minimum_required(VERSION 3.16)
project(RizWEB VERSION 2.2.0 LANGUAGES C CXX)

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
set(RIZWEB_BORINGSSL_DIR "" CACHE PATH "BoringSSL prefix with include/ and lib/ (stock BoringSSL works; a patched one only adds netRector's bypass-mode record version)")
option(RIZWEB_BUILD_APP "Build the standalone rizweb server" ON)
option(RIZWEB_BUILD_SHARED "Build RizWEB as a shared library instead of a static one" OFF)
option(RIZWEB_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(RIZWEB_STATIC "Link the server fully static where the platform allows it" ON)
set(NETRECTOR_DIR "" CACHE PATH "Source tree of ${RIZWEB_BACKEND_NAME}")
set(RIZWEB_BACKEND_TAG "master" CACHE STRING "Branch / tag of ${RIZWEB_BACKEND_NAME} fetched from GitHub when NETRECTOR_DIR is not given")

if(NOT TARGET ${RIZWEB_BACKEND_NAME})
    if(NOT NETRECTOR_DIR)
        include(FetchContent)
        FetchContent_Declare(${RIZWEB_BACKEND_NAME}
            GIT_REPOSITORY https://github.com/crazyfull/${RIZWEB_BACKEND_NAME}.git
            GIT_TAG ${RIZWEB_BACKEND_TAG})

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
    add_executable(RizWEBApp main.cpp)
    set_target_properties(RizWEBApp PROPERTIES OUTPUT_NAME "rizweb")
    target_link_libraries(RizWEBApp PRIVATE RizWEB)
    if(RIZWEB_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(RizWEBApp PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    install(TARGETS RizWEBApp RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Tests (ctest)
# ---------------------------------------------------------------------------
if(RIZWEB_BUILD_TESTS)
    enable_testing()
    add_executable(rizweb_tests tests/testMain.cpp tests/requestTest.cpp tests/utilTest.cpp tests/uploadTest.cpp tests/fetchTest.cpp tests/asyncTest.cpp)
    target_link_libraries(rizweb_tests PRIVATE RizWEB)
    add_test(NAME rizweb_tests COMMAND rizweb_tests)
endif()

# ---------------------------------------------------------------------------
# Install: everything an application needs to link RizWEB statically
#   lib/      libRizWEB + the network backend, lib/boringssl/ BoringSSL
#   include/  RizWEB headers (same layout as src/), backend headers, BoringSSL headers
#   lib/cmake/RizWEB/RizWEBConfig.cmake  -> find_package(RizWEB) + target RizWEB::RizWEB
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS RizWEB ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY src/ DESTINATION include/RizWEB FILES_MATCHING PATTERN "*.h")
install(DIRECTORY resource/ DESTINATION share/rizweb/resource)

option(RIZWEB_INSTALL_DEPENDENCIES "Also install the network backend and BoringSSL (a self-contained package)" ${PROJECT_IS_TOP_LEVEL})
if(RIZWEB_INSTALL_DEPENDENCIES AND NOT RIZWEB_BUILD_SHARED)
    install(TARGETS ${RIZWEB_BACKEND_NAME} ARCHIVE DESTINATION lib)
    get_target_property(_backend_includes ${RIZWEB_BACKEND_NAME} INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_dir ${_backend_includes})
        string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1" _dir "${_dir}")
        if(IS_DIRECTORY "${_dir}" AND NOT _dir MATCHES "boringssl|bssl")
            install(DIRECTORY "${_dir}/" DESTINATION include/${RIZWEB_BACKEND_NAME} FILES_MATCHING PATTERN "*.h")
        endif()
    endforeach()

    set(RIZWEB_CONFIG_BSSL_INCLUDE "")
    set(RIZWEB_CONFIG_BSSL_LIBS "")
    if(RIZWEB_ENABLE_TLS AND RIZWEB_BORINGSSL_DIR)
        install(DIRECTORY "${RIZWEB_BORINGSSL_DIR}/include/" DESTINATION include/boringssl)
        foreach(_name ssl crypto)
            find_library(_bssl_${_name} NAMES ${_name} lib${_name} PATHS "${RIZWEB_BORINGSSL_DIR}/lib" NO_DEFAULT_PATH)
            if(_bssl_${_name})
                get_filename_component(_file "${_bssl_${_name}}" NAME)
                install(FILES "${_bssl_${_name}}" DESTINATION lib/boringssl)
                string(APPEND RIZWEB_CONFIG_BSSL_LIBS ";\${_rizweb_prefix}/lib/boringssl/${_file}")
            endif()
        endforeach()
        set(RIZWEB_CONFIG_BSSL_INCLUDE ";\${_rizweb_prefix}/include/boringssl")
    endif()

    set(RIZWEB_CONFIG_DEFINITIONS "${RIZWEB_BACKEND_DEFINE}")
    if(RIZWEB_ENABLE_TLS)
        string(APPEND RIZWEB_CONFIG_DEFINITIONS ";RIZWEB_HAS_TLS=1")
    endif()
    get_target_property(_backend_defs ${RIZWEB_BACKEND_NAME} INTERFACE_COMPILE_DEFINITIONS)
    if(_backend_defs)
        string(APPEND RIZWEB_CONFIG_DEFINITIONS ";${_backend_defs}")
    endif()
    set(RIZWEB_CONFIG_FIND_ZLIB "")
    set(RIZWEB_CONFIG_LINK_EXTRA "Threads::Threads")
    if(ZLIB_FOUND)
        set(RIZWEB_CONFIG_FIND_ZLIB "find_dependency(ZLIB)")
        string(APPEND RIZWEB_CONFIG_DEFINITIONS ";RIZWEB_HAS_ZLIB=1")
        string(APPEND RIZWEB_CONFIG_LINK_EXTRA ";ZLIB::ZLIB")
    endif()
    if(WIN32)
        string(APPEND RIZWEB_CONFIG_LINK_EXTRA ";ws2_32;mswsock")
        string(APPEND RIZWEB_CONFIG_DEFINITIONS ";NOMINMAX;_CRT_SECURE_NO_WARNINGS")
    endif()
    configure_file(cmake/RizWEBConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/RizWEBConfig.cmake.in" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/RizWEBConfig.cmake" INPUT "${CMAKE_CURRENT_BINARY_DIR}/RizWEBConfig.cmake.in")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/RizWEBConfig.cmake" DESTINATION lib/cmake/RizWEB)
endif()
