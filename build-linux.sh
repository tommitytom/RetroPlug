#!/bin/bash

BUILD_CONFIG="$1"

if [ -z "$BUILD_CONFIG" ]; then
	BUILD_CONFIG="debug"
fi

set -e

./configure-linux.sh

cd build/gmake2
mkdir -p ${BUILD_CONFIG}
CC=/usr/bin/clang CXX=/usr/bin/clang++ make config=${BUILD_CONFIG}_x64 -j$(nproc) ScriptCompiler

cd ../../thirdparty/Framework
../../build/gmake2/bin/x64/Debug/ScriptCompiler ./src/compiler.config.lua x64

cd ../..
./build/gmake2/bin/x64/Debug/ScriptCompiler ./src/compiler.config.lua x64

cd build/gmake2
CC=/usr/bin/clang CXX=/usr/bin/clang++ make config=${BUILD_CONFIG}_x64 -j$(nproc) RetroPlugApp
