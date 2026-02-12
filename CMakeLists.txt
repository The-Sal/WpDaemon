cmake_minimum_required(VERSION 3.15)
project(WpDaemon)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directory for our headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Vendor: sockpp ──────────────────────────────────────────────────────
set(SOCKPP_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/sockpp)
# ────────────────────────────────────────────────────────────────────────

# ── Vendor: libcpr ──────────────────────────────────────────────────────
option(BUILD_SHARED_LIBS "" OFF)
add_subdirectory(vendor/libcpr)
# ────────────────────────────────────────────────────────────────────────

# ── Vendor: nlohman json ────────────────────────────────────────────────
add_subdirectory(vendor/json)
# ────────────────────────────────────────────────────────────────────────

# Source files
set(SOURCES
    src/main.cpp
    src/binary_manager.cpp
    src/state_machine.cpp
    src/config_manager.cpp
    src/log_manager.cpp
    src/wireproxy_process.cpp
    src/command_handler.cpp
    src/tcp_server.cpp
)

add_executable(WpDaemon ${SOURCES})

target_link_libraries(WpDaemon PRIVATE sockpp-static)
target_link_libraries(WpDaemon PRIVATE cpr::cpr)
target_link_libraries(WpDaemon PRIVATE nlohmann_json::nlohmann_json)

# Platform-specific libraries
if(APPLE)
    # macOS-specific settings if needed
elseif(UNIX)
    # Linux-specific settings if needed
endif()
