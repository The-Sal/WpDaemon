#!/bin/bash

set -e  # exit on any error

uname -srmvpo
make --version

# check clang or gcc
if command -v clang &> /dev/null; then
    echo "COMPILER: clang $(clang --version | head -n 1)"
elif command -v gcc &> /dev/null; then
    echo "COMPILER: gcc $(gcc --version | head -n 1)"
else
    echo "ERROR: No supported compiler found (clang or gcc)." >&2
    exit 1
fi

# Check meson
if command -v meson &> /dev/null; then
    echo "BUILD SYSTEM: meson $(meson --version)"
else
    echo "ERROR: meson build system not found." >&2
    exit 1
fi


echo "BUILDING WpDaemon"

# --- Detect OS and install prerequisites on Linux ---
if [[ "$(uname)" == "Linux" ]]; then
    echo "Detected Linux; ensuring cmake and meson are installed..."

    if ! command -v cmake &> /dev/null; then
        echo "ERROR: No supported package manager found (apt-get/dnf/yum). Please install cmake manually." >&2
        exit 1
    fi

    if ! command -v meson &> /dev/null; then
        echo "ERROR: No supported package manager found. Please install meson and ninja-build manually." >&2
        exit 1
    fi
fi

# --- Build WpDaemon ---
mkdir -p build
mkdir -p exports

cd build

time cmake .. -DCMAKE_BUILD_TYPE=Release
# shellcheck disable=SC2046
time make -j$(nproc)

ls -lha .
mv WpDaemon ../exports/WpDaemon

echo "BUILDING WpDaemon DONE"
