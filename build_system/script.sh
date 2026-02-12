#!/bin/bash

set -e

uname -srmvpo
make --version

if command -v clang &> /dev/null; then
    echo "COMPILER: clang $(clang --version | head -n 1)"
elif command -v gcc &> /dev/null; then
    echo "COMPILER: gcc $(gcc --version | head -n 1)"
else
    echo "ERROR: No supported compiler found (clang or gcc)." >&2
    exit 1
fi

if command -v meson &> /dev/null; then
    echo "BUILD SYSTEM: meson $(meson --version)"
else
    echo "ERROR: meson build system not found." >&2
    exit 1
fi

if command -v python3 &> /dev/null; then
    echo "PYTHON: python3 $(python3 --version)"
else
    echo "ERROR: python3 not found." >&2
    exit 1
fi

echo "BUILDING WpDaemon"

if [[ "$(uname)" == "Linux" ]]; then
    echo "Detected Linux; ensuring cmake and meson are installed..."

    if ! command -v cmake &> /dev/null; then
        echo "ERROR: cmake not found. Please install cmake manually." >&2
        exit 1
    fi

    if ! command -v meson &> /dev/null; then
        echo "ERROR: meson not found. Please install meson and ninja-build manually." >&2
        exit 1
    fi
fi

# --- Capture build machine identity for path scrubbing ---
BUILD_USER=$(whoami)
BUILD_HOME=$(eval echo ~"$BUILD_USER")
PATH_REMAP_FLAGS="-ffile-prefix-map=${BUILD_HOME}=/build -ffile-prefix-map=$(pwd)=/src"
echo "PATH REMAPPING: ${BUILD_HOME} -> /build  |  $(pwd) -> /src"

# --- Build ---
mkdir -p build
mkdir -p exports

cd build

time cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${PATH_REMAP_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${PATH_REMAP_FLAGS}"

time make -j$(nproc)

ls -lha .
mv WpDaemon ../exports/WpDaemon

# --- Binary patch: scrub any remaining hardcoded paths ---
# libpsl and similar deps embed absolute file paths as string literals,
# which compiler flags can't reach. We overwrite them in-place with
# null-padded generic replacements to preserve binary layout.
BINARY="../exports/WpDaemon"
echo "Scrubbing hardcoded paths from binary..."

python3 - <<EOF
import sys

binary_path = "${BINARY}"
search_bytes = "${BUILD_HOME}".encode()
replace_bytes = b"/build"

with open(binary_path, "rb") as f:
    data = f.read()

count = data.count(search_bytes)
if count == 0:
    print("OK: No hardcoded paths found — binary is clean.")
    sys.exit(0)

# Replace with null-padded string to maintain exact binary length
padded_replace = replace_bytes + b"\x00" * (len(search_bytes) - len(replace_bytes))
patched = data.replace(search_bytes, padded_replace)

with open(binary_path, "wb") as f:
    f.write(patched)

print(f"PATCHED: Replaced {count} occurrence(s) of '{search_bytes.decode()}' -> '/build'")
EOF

# --- Final verification ---
echo "Verifying binary is clean..."
if strings "${BINARY}" | grep -q "${BUILD_HOME}"; then
    echo "WARNING: Path still detected — may be encoded/split across chunks." >&2
else
    echo "OK: No user-specific paths found in binary."
fi

echo "BUILDING WpDaemon DONE"