@echo off
setlocal
rem
rem Configure and build RetroPlug on Windows (the build.sh counterpart).
rem
rem Usage:
rem   build.bat              # incremental build
rem   build.bat --clean      # remove build\ first, then full configure + build
rem   build.bat --tests      # (re)configure with BUILD_TESTING=ON so the
rem                          # Catch2 unit tests build too (off by default)
rem   build.bat -D<var>=<v>  # extra cache entries for the configure, e.g.
rem                          # -DRETROPLUG_MESEN_LTO=ON (what release.yml passes)
rem
rem Flags combine, e.g. build.bat --clean --tests
rem
rem Any -D forces a configure even when build\ already exists, so the entry
rem lands on an already-configured tree.
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
set "TESTS=0"
set "EXTRA_ARGS="
:argloop
if "%~1"=="" goto argdone
if /i "%~1"=="--clean" goto arg_clean
if /i "%~1"=="--tests" goto arg_tests
if /i "%~1"=="--with-tests" goto arg_tests
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
rem Anything starting with -D is passed straight through to the configure.
set "ARG=%~1"
if "%ARG:~0,2%"=="-D" goto arg_define
echo error: unknown argument: %~1 1>&2
exit /b 1
:arg_clean
set "CLEAN=1"
shift
goto argloop
:arg_tests
set "TESTS=1"
shift
goto argloop
:arg_define
rem cmd tokenises an unquoted FOO=BAR on the '=', so `-Dvar=value` passed without
rem quotes arrives as two args: (-Dvar) (value). Rejoin them when this -D token
rem carries no '=' of its own, so `build.bat -DFOO=ON` behaves like the quoted form
rem and matches build.sh (which needs no such handling). Uses a label, not an
rem if(...) block, because %~1 inside a block expands before the shift takes effect.
set "ARG=%~1"
echo(%ARG% | findstr /C:"=" >nul
if not errorlevel 1 goto arg_define_add
shift
set "ARG=%ARG%=%~1"
:arg_define_add
set "EXTRA_ARGS=%EXTRA_ARGS% %ARG%"
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

rem ---- configure -----------------------------------------------------------
rem Mirror build.sh: configure when there's no build dir yet, or when --tests
rem is requested (so BUILD_TESTING is (re)applied to an already-configured tree).
set "TESTING_ARG="
if "%TESTS%"=="1" set "TESTING_ARG=-DBUILD_TESTING=ON"

set "BUILD_EXISTED=1"
if not exist build set "BUILD_EXISTED=0"

set "DO_CONFIGURE=0"
if "%BUILD_EXISTED%"=="0" set "DO_CONFIGURE=1"
if "%TESTS%"=="1" set "DO_CONFIGURE=1"
if not "%EXTRA_ARGS%"=="" set "DO_CONFIGURE=1"

if "%DO_CONFIGURE%"=="1" (
    echo ==^> Configuring ^(-DCMAKE_BUILD_TYPE=Release %TESTING_ARG%%EXTRA_ARGS%^)
    cmake -S . -B build -G Ninja ^
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
        -DCMAKE_BUILD_TYPE=Release ^
        %TESTING_ARG%%EXTRA_ARGS% ^
        -DCMAKE_TOOLCHAIN_FILE="%RP_VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows-static
    if errorlevel 1 (
        echo error: configure failed 1>&2
        rem Only wipe a build dir we just created; never nuke a pre-existing
        rem tree just because a --tests reconfigure failed.
        if "%BUILD_EXISTED%"=="0" (
            echo removing build\ so the next run retries clean 1>&2
            rmdir /s /q build 2>nul
        )
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
echo   build.bat --tests      # (re)configure with BUILD_TESTING=ON so the
echo                          # Catch2 unit tests build too (off by default)
echo   build.bat -D^<var^>=^<v^>  # extra cache entries for the configure, e.g.
echo                          # -DRETROPLUG_MESEN_LTO=ON (what release.yml passes)
echo.
echo Flags combine, e.g. build.bat --clean --tests
exit /b 0
