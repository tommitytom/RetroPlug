#!/bin/bash

set -e

rm -rf build/emscripten/bin
rm -rf build/emscripten/obj
rm -f build/emscripten/debug/*
rm -f build/emscripten/release/*
rm -f build/emscripten/*.make
rm -f build/emscripten/Makefile
