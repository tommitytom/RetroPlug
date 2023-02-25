#!/bin/bash

set -e

PROJECT_NAME="$1"
BUILD_CONFIG="$2"
DEPLOY=false

if [ -z "$BUILD_CONFIG" ]; then
	BUILD_CONFIG="dist"
fi

if [ "$BUILD_CONFIG" = "dist" ]; then
	DEPLOY=true
	BUILD_CONFIG="release"
fi

WORKING_DIR="$PWD"

BUILD_DIR="${WORKING_DIR}/build/gmake2"
NATIVE_BUILD_CONFIG_DIR="${BUILD_DIR}/${BUILD_CONFIG}"
WEB_BUILD_CONFIG_DIR="${BUILD_DIR}/${BUILD_CONFIG}"

echo "Building native code to to ${NATIVE_BUILD_CONFIG_DIR}"
echo "Building web code to to ${WEB_BUILD_CONFIG_DIR}"

NATIVE_BIN_DIR="${BUILD_DIR}/bin/${BUILD_CONFIG}_x64"
WEB_BIN_DIR="${BUILD_DIR}/bin/${BUILD_CONFIG}_Emscripten"
DIST_DIR="${BUILD_DIR}/dist"

echo "Compiling lua scripts"
cd build/gmake2
CC=clang CXX=clang++ make config=release_x86 -j$(nproc) ScriptCompiler
bin/x86/Release/ScriptCompiler ../../src/compiler.config.lua x86
cd ../..

cd ${BUILD_DIR}
mkdir -p ${BUILD_CONFIG}
make config=${BUILD_CONFIG}_emscripten -j$(nproc) ${PROJECT_NAME}

if [ "$DEPLOY" = true ]; then
	echo "Preparing distribution..."

	mkdir -p ${DIST_DIR}
	cd ${DIST_DIR}

	rm -f *.html
	rm -f *.js
	rm -f *.wasm

	[[ -d "${WEB_BUILD_CONFIG_DIR}" ]] && cp -r ${WEB_BUILD_CONFIG_DIR}/* "${DIST_DIR}"

	echo "Generating wasm and js hashes..."

	cd ${WEB_BUILD_CONFIG_DIR}

	WASM_HASH=$(md5sum ${PROJECT_NAME}.wasm | cut -c 1-8)
	WORKER_HASH=$(md5sum ${PROJECT_NAME}.worker.js | cut -c 1-8)
	sed -i "s/${PROJECT_NAME}.worker.js/${PROJECT_NAME}.worker.${WORKER_HASH}.js/g" ${PROJECT_NAME}.js
	sed -i "s/${PROJECT_NAME}.wasm/${PROJECT_NAME}.${WASM_HASH}.wasm/g" ${PROJECT_NAME}.js

	JS_HASH=$(md5sum ${PROJECT_NAME}.js | cut -c 1-8)
	sed -i "s/${PROJECT_NAME}.js/${PROJECT_NAME}.${JS_HASH}.js/g" ${PROJECT_NAME}.html
	sed -i "s/index.js/${PROJECT_NAME}.${JS_HASH}.js/g" ${PROJECT_NAME}.html

	mv ${PROJECT_NAME}.js ${PROJECT_NAME}.${JS_HASH}.js
	mv ${PROJECT_NAME}.worker.js ${PROJECT_NAME}.worker.${WORKER_HASH}.js
	mv ${PROJECT_NAME}.wasm ${PROJECT_NAME}.${WASM_HASH}.wasm
fi
