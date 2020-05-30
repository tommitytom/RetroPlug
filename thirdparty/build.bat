@echo off

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

premake5 vs2019
msbuild build\vs2019\retroplug-dep.sln
