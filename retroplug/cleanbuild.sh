#!/bin/bash

export PATH=$PATH:/c/apps/bin
make clean
make
cp ../build/bin/sameboy_retroplug.dll ../../../resources/dlls/sameboy_retroplug.dll