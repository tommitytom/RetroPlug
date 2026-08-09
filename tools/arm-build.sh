#!/usr/bin/env bash
# Cross-build a target for the aarch64 handheld (Cortex-A53) at native x86 speed, matching the CI
# handheld-arm64 / profile-host-arm64 jobs. Configures build-arm/ with the cross toolchain + the
# glibc-2.35 sysroot (tools/arm-sysroot.sh) and builds. No CI round-trip.
#
#   tools/arm-build.sh [target ...]        # default target: retroplug-sdl
#     RP_ARM_PROFILE=1 tools/arm-build.sh retroplug-host   # RETROPLUG_PROFILE build (build-arm-prof/)
#
# Requires a host build/ first (build.sh) — the cross build reuses its host-native tjsc + sameboy_pb12
# (build-time codegen tools that can't run as aarch64). Deploy the result with e.g.:
#   scp build-arm/bin/retroplug-sdl root@<device>:/mnt/mmc/MUOS/application/RetroPlug/retroplug-sdl
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

targets=("$@"); [[ ${#targets[@]} -eq 0 ]] && targets=(retroplug-sdl)

# The sysroot the toolchain links against (assemble once).
[[ -f .arm64/sysroot/usr/include/SDL2/SDL.h ]] || tools/arm-sysroot.sh

# Host-native build-time tools (must exist — run build.sh for a host build first).
tjsc="$repo/build/dpfjs/deps/lv_binding_js/deps/txiki/tjsc"
pb12="$repo/build/sameboy-bootroms/sameboy_pb12"
for t in "$tjsc" "$pb12"; do
    [[ -x "$t" ]] || { echo "!! missing host tool: $t — run ./build.sh (a host build) first" >&2; exit 1; }
done

builddir=build-arm
btype=Release
extra=()
if [[ "${RP_ARM_PROFILE:-0}" == "1" ]]; then
    builddir=build-arm-prof; btype=RelWithDebInfo; extra=(-DRETROPLUG_PROFILE=ON)
fi

cmake -S . -B "$builddir" -G Ninja -DCMAKE_BUILD_TYPE="$btype" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-cortex-a53.cmake \
    -DMI_NO_OPT_ARCH=ON \
    -DTJSC_EXECUTABLE="$tjsc" -DSAMEBOY_PB12_EXECUTABLE="$pb12" \
    "${extra[@]}"
cmake --build "$builddir" --target "${targets[@]}" -j"$(nproc)"

echo "==> built: ${targets[*]}"
for t in "${targets[@]}"; do
    bin="$builddir/bin/$t"
    [[ -f "$bin" ]] || continue
    maxglibc="$(readelf -V "$bin" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1)"
    printf "    %-22s %s  (max %s)\n" "$t" "$(du -h "$bin" | cut -f1)" "${maxglibc:-none}"
done
