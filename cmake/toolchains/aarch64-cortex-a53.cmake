# Cross-compile toolchain: x86_64 host -> aarch64 (Cortex-A53), matching the CI handheld-arm64 /
# profile-host-arm64 jobs (Ubuntu 22.04 = glibc 2.35, gcc-12, -mcpu=cortex-a53, MI_NO_OPT_ARCH). Lets the
# dev container build device binaries at NATIVE x86 speed instead of round-tripping through CI. The
# glibc-2.35 sysroot (<= the device's 2.38 ceiling) is assembled by tools/arm-sysroot.sh into .arm64/sysroot.
#
# Build-time tools run on the HOST (x86): tjsc (pass -DTJSC_EXECUTABLE=<host tjsc>), rgbasm/rgblink, and
# node/esbuild for the UI/CP bundles. Only the emitted code is aarch64. Configure with:
#   tools/arm-build.sh [target]         # wraps the cmake invocation below
# or directly:
#   cmake -S . -B build-arm -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-cortex-a53.cmake \
#         -DMI_NO_OPT_ARCH=ON -DTJSC_EXECUTABLE=$PWD/build/dpfjs/deps/lv_binding_js/deps/txiki/tjsc
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# gcc-12 matches CI: Ubuntu's default GCC 11 can't compile txiki's `ada` (constexpr std::string needs
# GCC 12+); gcc-12 still targets the sysroot's glibc 2.35, preserving the device-glibc ceiling.
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc-12)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++-12)

get_filename_component(_rp_sysroot "${CMAKE_CURRENT_LIST_DIR}/../../.arm64/sysroot" ABSOLUTE)
if(NOT EXISTS "${_rp_sysroot}/usr/include/SDL2/SDL.h")
    message(FATAL_ERROR "arm64 sysroot missing at ${_rp_sysroot} — run tools/arm-sysroot.sh first")
endif()
set(CMAKE_SYSROOT "${_rp_sysroot}")
set(CMAKE_FIND_ROOT_PATH "${_rp_sysroot}")
# Host-built tools (tjsc, rgbasm, pkg-config) come from the host; libs/headers/packages ONLY from the sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Device tuning, identical to CI: the A53 in-order pipeline (esp. the SameBoy/Mesen cores), and it fixes the
# ISA at ARMv8.0-A — no LSE atomics the Cortex-A53 lacks. Pair with -DMI_NO_OPT_ARCH=ON (mimalloc would
# otherwise emit LSE `casal` at its startup ctor -> SIGILL on the device).
string(APPEND CMAKE_C_FLAGS_INIT   " -mcpu=cortex-a53")
string(APPEND CMAKE_CXX_FLAGS_INIT " -mcpu=cortex-a53")

# Pin HEADER-driven glibc symbols to the sysroot's 2.35. Ubuntu's cross-gcc is `--with-sysroot=/`, so
# --sysroot does NOT relocate its built-in glibc header dir (/usr/aarch64-linux-gnu/include — the HOST
# distro's glibc 2.39). Left in the search path it leaks newer symbol versions (`__isoc23_*` etc.) —
# directly AND through libstdc++'s #include_next chain (<cmath> -> <math.h>). A plain -isystem can't fix the
# include_next leak (the C++ dir must stay ahead of glibc). So drop ALL auto include dirs (-nostdinc
# -nostdinc++) and rebuild the search list explicitly, in order, WITHOUT the toolchain glibc dir: c++
# headers -> gcc builtins -> the sysroot glibc. include_next then falls through to the sysroot's 2.35.
#
# This does NOT reach a pure 2.35 floor: the toolchain's own static libstdc++ (built for this 24.04 host)
# needs arc4random@2.36, and fmod/fmodf get 2.38 version nodes at link — so the binary's floor is glibc 2.38.
# That's fine: the device IS glibc 2.38, and pinning the *headers* keeps the floor from drifting ABOVE 2.38
# if the host toolchain's glibc bumps. CI (native-arm gcc on 22.04) stays the true <=2.35 release path.
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE _gcc_inc OUTPUT_STRIP_TRAILING_WHITESPACE)
file(GLOB _cxx_dirs "/usr/aarch64-linux-gnu/include/c++/*")
list(GET _cxx_dirs 0 _cxx)   # e.g. /usr/aarch64-linux-gnu/include/c++/12
if(NOT IS_DIRECTORY "${_cxx}" OR NOT IS_DIRECTORY "${_gcc_inc}")
    message(FATAL_ERROR "cross toolchain include dirs not found (c++='${_cxx}', gcc='${_gcc_inc}') — is g++-12-aarch64-linux-gnu installed?")
endif()
set(_rp_sys_inc "-isystem ${_rp_sysroot}/usr/include/aarch64-linux-gnu -isystem ${_rp_sysroot}/usr/include")
string(APPEND CMAKE_C_FLAGS_INIT   " -nostdinc -isystem ${_gcc_inc} ${_rp_sys_inc}")
string(APPEND CMAKE_CXX_FLAGS_INIT " -nostdinc -nostdinc++ -isystem ${_cxx} -isystem ${_cxx}/aarch64-linux-gnu -isystem ${_cxx}/backward -isystem ${_gcc_inc} ${_rp_sys_inc}")

# pkg-config resolves .pc files against the sysroot (SDL2/ALSA/dbus/curl/…) and prefixes -I/-L with it.
set(ENV{PKG_CONFIG_LIBDIR} "${_rp_sysroot}/usr/lib/aarch64-linux-gnu/pkgconfig:${_rp_sysroot}/usr/lib/pkgconfig:${_rp_sysroot}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${_rp_sysroot}")
