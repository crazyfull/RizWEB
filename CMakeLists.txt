cmake_minimum_required(VERSION 3.16)
project(RizWEB VERSION 1.3.45 LANGUAGES C CXX)

# netRector needs C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
#   RIZWEB_NET_BACKEND : auto | netRector | netRectorClient
#       auto -> Linux: netRector (epoll, multi-shard server)
#               others (Windows / macOS): netRectorClient (portable poll/select loop)
#   NETRECTOR_DIR      : checkout of the chosen backend (contains its CMakeLists.txt)
# ---------------------------------------------------------------------------
set(RIZWEB_NET_BACKEND "auto" CACHE STRING "Network backend: auto|netRector|netRectorClient")
set(NETRECTOR_DIR "" CACHE PATH "Path to the netRector / netRectorClient source tree")
option(RIZWEB_BUILD_APP "Build the standalone rizweb executable" ON)
option(RIZWEB_ENABLE_TLS "Enable TLS (needs BoringSSL, see netRector docs)" OFF)
option(RIZWEB_STATIC "Link fully static where the platform allows it" ON)

if(RIZWEB_NET_BACKEND STREQUAL "auto")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(RIZWEB_NET_BACKEND "netRector")
    else()
        set(RIZWEB_NET_BACKEND "netRectorClient")
    endif()
endif()
message(STATUS "RizWEB: network backend = ${RIZWEB_NET_BACKEND}")

if(NOT NETRECTOR_DIR OR NOT EXISTS "${NETRECTOR_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set -DNETRECTOR_DIR=<path to ${RIZWEB_NET_BACKEND} checkout>")
endif()

# Forward options to the backend and keep it lean
set(NETRECTOR_ENABLE_TLS ${RIZWEB_ENABLE_TLS} CACHE BOOL "" FORCE)
set(NETRECTOR_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)    # netRector
set(NETRECTOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)   # netRectorClient
set(NETRECTOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${NETRECTOR_DIR}" netrector-build EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Library (the portable C++ API for other applications)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE RIZWEB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE RIZWEB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER RIZWEB_SOURCES EXCLUDE REGEX "/LinuxService/")
    list(FILTER RIZWEB_HEADERS EXCLUDE REGEX "/LinuxService/")
endif()

add_library(RizWEB STATIC ${RIZWEB_SOURCES})
target_include_directories(RizWEB PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/HTTP>
    $<INSTALL_INTERFACE:include/RizWEB>)
target_compile_definitions(RizWEB PUBLIC
    SERVER_NAME="RizWEB/${PROJECT_VERSION}"
    RIZWEB_BACKEND_${RIZWEB_NET_BACKEND}=1)

if(RIZWEB_NET_BACKEND STREQUAL "netRector")
    target_link_libraries(RizWEB PUBLIC netRector)
else()
    target_link_libraries(RizWEB PUBLIC netRectorClient)
endif()

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(RizWEB PUBLIC ZLIB::ZLIB)
    target_compile_definitions(RizWEB PUBLIC ZLIB_DLL)   # the sources test this macro
endif()

find_package(Threads REQUIRED)
target_link_libraries(RizWEB PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Standalone server
# ---------------------------------------------------------------------------
if(RIZWEB_BUILD_APP)
    add_executable(rizweb main.cpp)
    target_compile_definitions(rizweb PRIVATE RizWEBApp)
    target_link_libraries(rizweb PRIVATE RizWEB)
    if(RIZWEB_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(rizweb PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    install(TARGETS rizweb RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Install: lib + headers + default resources
# ---------------------------------------------------------------------------
install(TARGETS RizWEB ARCHIVE DESTINATION lib)
install(FILES ${RIZWEB_HEADERS} DESTINATION include/RizWEB)
install(DIRECTORY resource/ DESTINATION share/rizweb/resource)
