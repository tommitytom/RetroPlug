#!/usr/bin/env bash
#
# Configure and build RetroPlug.
#
# Usage:
#   ./build.sh              # incremental build
#   ./build.sh --clean      # remove build/ first, then full configure + build
#   ./build.sh --tests      # (re)configure with BUILD_TESTING=ON so the
#                           # Catch2 unit tests build too (off by default)
#   ./build.sh -D<var>=<v>  # extra cache entries for the configure, e.g.
#                           # -DRETROPLUG_MESEN_LTO=ON (what release.yml passes)
#
# Flags combine, e.g. ./build.sh --clean --tests
#
# Any -D forces a configure even when build/ already exists, so the entry lands
# on an already-configured tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CLEAN=0
TESTS=0
EXTRA_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --tests|--with-tests) TESTS=1 ;;
        -D*) EXTRA_ARGS+=("$arg") ;;
        -h|--help)
            sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
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
CONFIGURE_ARGS+=("${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}")

if [[ $CLEAN -eq 1 ]]; then
    echo "==> Cleaning build/"
    rm -rf build
fi

# Configure when there's no build dir yet, or when --tests / a -D is requested
# (so those cache entries are (re)applied to an already-configured tree).
if [[ ! -d build || $TESTS -eq 1 || ${#EXTRA_ARGS[@]} -gt 0 ]]; then
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
