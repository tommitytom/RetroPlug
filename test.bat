@echo off
setlocal
rem
rem Run the headless test suites on Windows (the CI + local counterpart to the
rem bare `pnpm test*` used on Linux/macOS).
rem
rem Unlike Unix, Windows needs the MSVC x64 dev environment (cl / cmake / ninja
rem plus INCLUDE / LIB) for the test scripts' `cmake-build` step, and GitHub
rem Actions does not persist the env build.bat set up into later steps. So, like
rem build.bat, this script enters that environment itself, then runs the suites.
rem
rem Usage:
rem   test.bat            # run every suite (ts, native, plugin, ui)
rem   test.bat ts         # run only the named suite(s)
rem   test.bat native ui  #   (names: ts native plugin ui)
rem
rem Override the tool locations with these env vars if your layout differs:
rem   VCPKG_ROOT   (default C:\code\vcpkg)
rem   RGBDS_DIR    (default C:\code\tools\rgbds)
rem   NODE_DIR     (default C:\Program Files\nodejs)

cd /d "%~dp0"

rem ---- which suites (default: all) -----------------------------------------
set "SUITES=%*"
if "%SUITES%"=="" set "SUITES=ts native plugin ui"

rem ---- tool locations (overridable) ----------------------------------------
if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\code\vcpkg"
if not defined RGBDS_DIR set "RGBDS_DIR=C:\code\tools\rgbds"
if not defined NODE_DIR set "NODE_DIR=C:\Program Files\nodejs"

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo error: vcpkg toolchain not found under "%VCPKG_ROOT%" ^(set VCPKG_ROOT^) 1>&2
    exit /b 1
)
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
rem vcvars64 clobbers VCPKG_ROOT with its VS-bundled copy; restore ours.
set "VCPKG_ROOT=%RP_VCPKG%"

rem RGBDS, Node, and the VS-bundled CMake + Ninja must be resolvable.
set "PATH=%RGBDS_DIR%;%NODE_DIR%;%APPDATA%\npm;%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

rem pnpm 9 strips PATH for spawned children on Windows (hiding cl / cmake); pin
rem the version the repo uses via corepack so this works regardless of whatever
rem pnpm is installed globally. corepack ships with Node (NODE_DIR on PATH).
set "PNPM=corepack pnpm@10.15.0"

rem ---- run the requested suites --------------------------------------------
setlocal enabledelayedexpansion
set "FAILED="
for %%s in (%SUITES%) do (
    echo.
    echo ==^> Running suite: %%s
    if /i "%%s"=="ts"     ( call %PNPM% test        )
    if /i "%%s"=="native" ( call %PNPM% test:native )
    if /i "%%s"=="plugin" ( call %PNPM% test:plugin )
    if /i "%%s"=="ui"     ( call %PNPM% test:ui      )
    if errorlevel 1 set "FAILED=!FAILED! %%s"
)
if defined FAILED (
    echo.
    echo ==^> FAILED suites:!FAILED! 1>&2
    exit /b 1
)
echo.
echo ==^> all test suites passed
exit /b 0
