cmake_minimum_required(VERSION 4.1)
project(WpDaemon)

set(CMAKE_CXX_STANDARD 23)

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


add_executable(WpDaemon
        src/main.cpp
        src/binary_management.cpp
        src/tcp_server.cpp)
target_link_libraries(WpDaemon PRIVATE sockpp-static)
target_link_libraries(WpDaemon PRIVATE cpr::cpr)
target_link_libraries(WpDaemon PRIVATE nlohmann_json::nlohmann_json)