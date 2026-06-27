@echo off
setlocal
rem
rem Configure and build RetroPlug on Windows (the build.sh counterpart).
rem
rem Usage:
rem   build.bat              # incremental build
rem   build.bat --clean      # remove build\ first, then full configure + build
rem
rem Unlike build.sh, Windows needs the MSVC x64 dev environment plus the
rem Ninja + cl + vcpkg-static configure this project uses (SameBoy is isolated
rem to clang-cl from there). This script enters that environment for you.
rem Override the tool locations with these env vars if your layout differs:
rem   VCPKG_ROOT   (default C:\code\vcpkg)
rem   RGBDS_DIR    (default C:\code\tools\rgbds)   - rgbasm/rgblink for boot ROMs
rem   NODE_DIR     (default C:\Program Files\nodejs) - UI bundle + RPC codegen

cd /d "%~dp0"

rem ---- args ----------------------------------------------------------------
set "CLEAN=0"
:argloop
if "%~1"=="" goto argdone
if /i "%~1"=="--clean" goto arg_clean
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
echo error: unknown argument: %~1 1>&2
exit /b 1
:arg_clean
set "CLEAN=1"
shift
goto argloop
:argdone

rem ---- tool locations (overridable) ----------------------------------------
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\code\vcpkg"
if not defined RGBDS_DIR set "RGBDS_DIR=C:\code\tools\rgbds"
if not defined NODE_DIR set "NODE_DIR=C:\Program Files\nodejs"

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo error: vcpkg toolchain not found under "%VCPKG_ROOT%" ^(set VCPKG_ROOT^) 1>&2
    exit /b 1
)
rem Stash our vcpkg: vcvars64 below resets VCPKG_ROOT to its VS-bundled copy
rem (which lacks this project's installed packages), so we must not read
rem %VCPKG_ROOT% after it. Use %RP_VCPKG% for the toolchain instead.
set "RP_VCPKG=%VCPKG_ROOT%"

rem ---- enter the VS x64 dev environment ------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: vswhere.exe not found at "%VSWHERE%" 1>&2
    exit /b 1
)
set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo error: no Visual Studio install with the C++ x64 toolset found 1>&2
    exit /b 1
)
echo ==^> Using Visual Studio at "%VSINSTALL%"
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo error: vcvars64.bat failed 1>&2
    exit /b 1
)
rem vcvars64 just clobbered VCPKG_ROOT with the VS-bundled vcpkg; restore ours
rem so any tooling that reads it (and the toolchain below) uses the right tree.
set "VCPKG_ROOT=%RP_VCPKG%"

rem RGBDS, Node, and the VS-bundled CMake + Ninja must be resolvable.
set "PATH=%RGBDS_DIR%;%NODE_DIR%;%APPDATA%\npm;%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

rem ---- clean ---------------------------------------------------------------
if "%CLEAN%"=="1" (
    echo ==^> Cleaning build\
    if exist build rmdir /s /q build
)

rem ---- configure (only when build\ is absent, like build.sh) ---------------
if not exist build (
    echo ==^> Configuring
    cmake -S . -B build -G Ninja ^
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE="%RP_VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows-static
    if errorlevel 1 (
        echo error: configure failed; removing build\ so the next run retries clean 1>&2
        rmdir /s /q build 2>nul
        exit /b 1
    )
)

rem ---- build ---------------------------------------------------------------
echo ==^> Building (-j%NUMBER_OF_PROCESSORS%)
cmake --build build -j%NUMBER_OF_PROCESSORS%
exit /b %errorlevel%

:help
echo Configure and build RetroPlug.
echo.
echo Usage:
echo   build.bat              # incremental build
echo   build.bat --clean      # remove build\ first, then full configure + build
exit /b 0
