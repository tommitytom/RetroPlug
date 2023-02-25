#!/bin/bash

./configure-linux.sh
emconfigure ./thirdparty/bin/premake5 --emscripten gmake2
