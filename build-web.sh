#!/bin/bash

set -e

BUILD_CONFIG="$1"
DEPLOY=false

if [ -z "$BUILD_CONFIG" ]; then
	BUILD_CONFIG="dist"
fi

if [ "$BUILD_CONFIG" = "dist" ]; then
	DEPLOY=true
	BUILD_CONFIG="release"
fi

WORKING_DIR="$PWD"
BUILD_DIR="${WORKING_DIR}/build/emscripten"
BUILD_CONFIG_DIR="${BUILD_DIR}/${BUILD_CONFIG}"
DIST_DIR="${BUILD_DIR}/dist"

cd build/gmake2
CC=clang CXX=clang++ make config=release_x86 -j$(nproc) ScriptCompiler
bin/x86/Release/ScriptCompiler ../../src/compiler.config.lua x86
cd ../..

cd ${BUILD_DIR}
mkdir -p ${BUILD_CONFIG}
emmake make config=${BUILD_CONFIG}_x86 -j$(nproc) RetroPlugApp

if [ "$DEPLOY" = true ]; then
	echo "Preparing distribution..."

	mkdir -p ${DIST_DIR}
	cd ${DIST_DIR}

	rm -f *.html
	rm -f *.js
	rm -f *.wasm

	[[ -d "${BUILD_CONFIG_DIR}" ]] && cp -r ${BUILD_CONFIG_DIR}/* "${DIST_DIR}"

	echo "Generating wasm and js hashes..."

	WASM_HASH=$(md5sum index.wasm | cut -c 1-8)
	WORKER_HASH=$(md5sum index.worker.js | cut -c 1-8)
	sed -i "s/index.worker.js/index.worker.${WORKER_HASH}.js/g" index.js
	sed -i "s/index.wasm/index.${WASM_HASH}.wasm/g" index.js

	JS_HASH=$(md5sum index.js | cut -c 1-8)
	sed -i "s/index.js/index.${JS_HASH}.js/g" index.html

	mv index.js index.${JS_HASH}.js
	mv index.worker.js index.worker.${WORKER_HASH}.js
	mv index.wasm index.${WASM_HASH}.wasm
fi
