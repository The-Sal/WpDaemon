cmake_minimum_required(VERSION 4.1)
project(WpDaemon)

set(CMAKE_CXX_STANDARD 23)

# ── Vendor: sockpp ────────────────────────────────────────────────
set(SOCKPP_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/sockpp)
# ─────────────────────────────────────────────────────────────────

# ── Vendor: libcpr ────────────────────────────────────────────────
option(BUILD_SHARED_LIBS "" OFF)
add_subdirectory(vendor/libcpr)
# ─────────────────────────────────────────────────────────────────


add_executable(WpDaemon src/main.cpp)
target_link_libraries(WpDaemon PRIVATE sockpp-static)
target_link_libraries(WpDaemon PRIVATE cpr::cpr)