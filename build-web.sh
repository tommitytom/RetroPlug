#!/bin/bash

set -e

BUILD_CONFIG="${1:-release}"
CONFIGURE=false

if [ "$2" = "--configure" ]; then
	CONFIGURE=true
fi

WORKING_DIR="$PWD"
BUILD_DIR="${WORKING_DIR}/build/gmake2"
BUILD_CONFIG_DIR="${BUILD_DIR}/${BUILD_CONFIG}"

if [ "$CONFIGURE" = true ]; then
	emconfigure ./thirdparty/bin/premake5 --emscripten gmake2
fi

mkdir -p "${BUILD_CONFIG_DIR}"
cd "${BUILD_DIR}"
emmake make config=${BUILD_CONFIG}_emscripten -j$(nproc) RetroPlug-app

cd "${WORKING_DIR}"
mkdir -p web/src/native
mkdir -p web/public

cp "${BUILD_CONFIG_DIR}"/*.mjs "${WORKING_DIR}/web/src/native/" 2>/dev/null || true
cp "${BUILD_CONFIG_DIR}"/*.d.ts "${WORKING_DIR}/web/src/native/" 2>/dev/null || true
cp "${BUILD_CONFIG_DIR}"/*.mjs "${WORKING_DIR}/web/public/" 2>/dev/null || true
cp "${BUILD_CONFIG_DIR}"/*.wasm "${WORKING_DIR}/web/public/" 2>/dev/null || true

echo "Web build finished successfully."
