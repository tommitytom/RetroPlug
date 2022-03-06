#!/bin/bash

BUILD_CONFIG="$1"

if [ -z "$BUILD_CONFIG" ]; then
	BUILD_CONFIG="debug"
fi

set -e

cd build/gmake2
CC=clang CXX=clang++ make config=release_x64 -j$(nproc) ScriptCompiler
#bin/x64/Release/ScriptCompiler ../../src/compiler.config.lua x86
cd ../..

cd build/emscripten
mkdir -p ${BUILD_CONFIG}
emmake make config=${BUILD_CONFIG}_x86 -j$(nproc) RetroPlugApp
