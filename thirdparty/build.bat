@echo off

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

cd gainput
mkdir build
cd build
cmake ..
cd lib
call msbuild /verbosity:minimal /nologo /m /t:Clean,Build /p:Platform=x64 /p:Configuration=Debug gainputstatic.vcxproj
call msbuild /verbosity:minimal /nologo /m /t:Clean,Build /p:Platform=x64 /p:Configuration=Release gainputstatic.vcxproj