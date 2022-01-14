#!/bin/sh

BUILD_CONFIG="$1"

if [ -z "$BUILD_CONFIG" ]; then
	BUILD_CONFIG="debug"
fi

set -e

sh configure.sh
cd build/gmake2
mkdir -p ${BUILD_CONFIG}
emmake make config=${BUILD_CONFIG}_x86 -j$(nproc) RetroPlugApp
