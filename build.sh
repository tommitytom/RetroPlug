#!/usr/bin/env bash
#
# Configure and build RetroPlug.
#
# Usage:
#   ./build.sh              # incremental build
#   ./build.sh --clean      # remove build/ first, then full configure + build

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help)
            sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

if [[ $CLEAN -eq 1 ]]; then
    echo "==> Cleaning build/"
    rm -rf build
fi

if [[ ! -d build ]]; then
    echo "==> Configuring"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS="$(sysctl -n hw.ncpu)"   # macOS / BSD
fi
echo "==> Building (-j${JOBS})"
cmake --build build -j"${JOBS}"
