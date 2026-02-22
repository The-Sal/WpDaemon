cmake_minimum_required(VERSION 3.15)
project(WpDaemon VERSION 1.0.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define version macros
add_compile_definitions(WPDAEMON_VERSION="${PROJECT_VERSION}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Vendor: sockpp ──────────────────────────────────────────────────────
set(SOCKPP_BUILD_STATIC        ON  CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_SHARED        OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/sockpp)
# ────────────────────────────────────────────────────────────────────────

# ── Vendor: libcpr ──────────────────────────────────────────────────────
option(BUILD_SHARED_LIBS "" OFF)
set(CPR_USE_SYSTEM_CURL       OFF CACHE BOOL "" FORCE)
set(CPR_FORCE_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL           OFF CACHE BOOL "" FORCE)
set(USE_LIBPSL                OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_PSL          ON  CACHE BOOL "" FORCE)
add_subdirectory(vendor/libcpr)
# ────────────────────────────────────────────────────────────────────────

# ── Vendor: nlohmann json ───────────────────────────────────────────────
add_subdirectory(vendor/json)
# ────────────────────────────────────────────────────────────────────────

set(SOURCES
        src/main.cpp
        src/binary_manager.cpp
        src/state_machine.cpp
        src/config_manager.cpp
        src/log_manager.cpp
        src/wireproxy_process.cpp
        src/command_handler.cpp
        src/tcp_server.cpp
        src/audit_logger.cpp
        src/arg_parser.cpp
        src/interactive_cli.cpp
        src/daemonizer.cpp
)

add_executable(WpDaemon ${SOURCES})

target_link_libraries(WpDaemon PRIVATE sockpp-static)
target_link_libraries(WpDaemon PRIVATE cpr::cpr)
target_link_libraries(WpDaemon PRIVATE nlohmann_json::nlohmann_json)

# ── Platform-specific ───────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    # --copy-dt-needed-entries: if any static lib (.a) we link against
    # itself depends on a shared lib (.so), pull that .so in automatically.
    # This is the correct fix for "DSO missing from command line" caused
    # by vendored static libs with transitive shared-lib deps (libpsl→libunistring).
    # I GENUINELY HATE C++ LINKER
    target_link_options(WpDaemon PRIVATE -Wl,--copy-dt-needed-entries)
endif()

# Build universal binary for macOS
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build for both x86_64 and arm64")
endif()