#!/bin/sh -e

emconfigure premake5 --emscripten gmake2
cd build/gmake2
emmake make config=debug_x86 -j24 RetroPlug-wam
