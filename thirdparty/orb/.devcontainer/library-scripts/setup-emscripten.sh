#!/bin/bash

rm -rf /emsdk/upstream/emscripten
git clone -b worklet_support https://github.com/tklajnscek/emscripten.git /emsdk/upstream/emscripten
cd /emsdk/upstream/emscripten
npm install
