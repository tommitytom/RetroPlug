#!/bin/bash

BUILD_CONFIG="$1"

if [ -z "$BUILD_CONFIG" ]; then
	BUILD_CONFIG="debug"
fi

set -e

./configure-linux.sh
cd build/gmake2
mkdir -p ${BUILD_CONFIG}
make config=${BUILD_CONFIG}_x64 -j$(nproc) RetroPlugApp
