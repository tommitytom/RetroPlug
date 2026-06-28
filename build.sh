#!/usr/bin/env bash
#
# Configure and build RetroPlug.
#
# Usage:
#   ./build.sh              # incremental build
#   ./build.sh --clean      # remove build/ first, then full configure + build
#   ./build.sh --tests      # (re)configure with BUILD_TESTING=ON so the
#                           # Catch2 unit tests build too (off by default)
#
# Flags combine, e.g. ./build.sh --clean --tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CLEAN=0
TESTS=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --tests|--with-tests) TESTS=1 ;;
        -h|--help)
            sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

CONFIGURE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
if [[ $TESTS -eq 1 ]]; then
    CONFIGURE_ARGS+=(-DBUILD_TESTING=ON)
fi

if [[ $CLEAN -eq 1 ]]; then
    echo "==> Cleaning build/"
    rm -rf build
fi

# Configure when there's no build dir yet, or when --tests is requested (so the
# BUILD_TESTING option is (re)applied to an already-configured tree).
if [[ ! -d build || $TESTS -eq 1 ]]; then
    echo "==> Configuring (${CONFIGURE_ARGS[*]})"
    cmake -S . -B build "${CONFIGURE_ARGS[@]}"
fi

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS="$(sysctl -n hw.ncpu)"   # macOS / BSD
fi
echo "==> Building (-j${JOBS})"
cmake --build build -j"${JOBS}"
