#!/bin/bash

make
mkdir ../../../resources/dlls
cp ../build/bin/sameboy_retroplug.dll ../../../resources/dlls/sameboy_retroplug-${1}.dll
