#!/usr/bin/env bash
#
# Builds rp-device-check, the RP2350 image that runs every portable node type in device mode (rp.core, rp.dsp, rp.n8,
# rp.device and rp.devtest) and a multi-node plan through the plan loader, with a link trap on the heap, on exceptions
# and on RTTI (packages/graph-runtime/device-check, docs/modular-graph-phase-e1-design.md section 7 and the E3 design's
# section 7), and prints what it costs in flash and RAM. CI's linux-x64 job runs it after the desktop build; nothing is
# flashed or run here (the rig runs the image by hand, and reads its result lines off the UART).
#
#   1. Builds the case generator (rp-device-check-cases) and the plan exporter (retroplug-export-plan) in the desktop
#      build, which must be configured (./build.sh), and writes the cases header: each type's default settings as
#      msgpack and the layout they give. The image's build exports its plan (device-check/plan.json) with the
#      exporter and embeds the blob, so a plan the image could not run or hold fails here.
#   2. Configures the image's own CMake project, on the Pico SDK's toolchain, in a build dir of its own, and builds it.
#      A link error there naming __wrap_<symbol> is the trap: the object it names reached the heap or a throw.
#   3. Builds the trap's positive control, rp-device-trap-probe, which allocates with new and malloc, and stops unless
#      its link FAILS naming its own object for __wrap__Znwj and __wrap_malloc: the proof, on every run, that the trap
#      reaches the link.
#   4. Prints the image's allocated sections (arm-none-eabi-size -A) and its flash and RAM by owner, from the map.
#
# Usage:  tools/build-device-check.sh            (pnpm device:check)
#   PICO_SDK_PATH         the Pico SDK (2.1.1); when unset, /workspaces/pico-sdk, the devcontainer's. A path that is
#                         set and is not an SDK stops the run. The check needs none of the SDK's submodules.
#   RP_DESKTOP_BUILD_DIR  the configured desktop build (default build/), which builds the case generator and the plan
#                         exporter
#   RP_DEVICE_BUILD_DIR   the image's build dir (default build-device/)
#   RP_DEVICE_BUILD_TYPE  default Release (the SDK's default: -O3, NDEBUG); MinSizeRel for -Os

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DESKTOP="${RP_DESKTOP_BUILD_DIR:-$REPO_DIR/build}"
OUT="${RP_DEVICE_BUILD_DIR:-$REPO_DIR/build-device}"
TYPE="${RP_DEVICE_BUILD_TYPE:-Release}"
JOBS="$(nproc 2>/dev/null || echo 4)"

die() { echo "build-device-check: $*" >&2; exit 1; }

# An SDK named explicitly is the one used, or the run stops: a wrong path never quietly builds against another SDK.
if [ -n "${PICO_SDK_PATH:-}" ]; then
    sdk=$PICO_SDK_PATH
    [ -e "$sdk/pico_sdk_init.cmake" ] || die "PICO_SDK_PATH ($sdk) is not a Pico SDK"
else
    sdk=/workspaces/pico-sdk
    [ -e "$sdk/pico_sdk_init.cmake" ] || die "no Pico SDK: set PICO_SDK_PATH (the default, $sdk, is not one)"
fi
command -v arm-none-eabi-g++ >/dev/null ||
    die "no arm-none-eabi-g++ (apt: gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib)"
[ -f "$DESKTOP/CMakeCache.txt" ] || die "$DESKTOP is not a configured desktop build (run ./build.sh first)"

echo "== the cases and the plan exporter (desktop: rp-device-check-cases, retroplug-export-plan)"
cmake --build "$DESKTOP" --target rp-device-check-cases retroplug-export-plan-current -j"$JOBS"
mkdir -p "$OUT/cases"
"$DESKTOP/device-check/cases/rp-device-check-cases" "$OUT/cases/DeviceCheckCases.hpp"
exporter="$DESKTOP/export-plan/retroplug-export-plan"
[ -x "$exporter" ] || die "no $exporter after building it"

# reflect-cpp at the desktop's pin: the desktop build's own checkout when it has one, which saves a clone (the image's
# CMakeLists checks its version against the pin either way, and clones the pinned tag without it).
reflect="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' "$DESKTOP/CMakeCache.txt")"
generator=()
command -v ninja >/dev/null && [ ! -f "$OUT/CMakeCache.txt" ] && generator=(-G Ninja)

echo "== the image (Pico SDK $sdk, $TYPE)"
cmake -S "$REPO_DIR/packages/graph-runtime/device-check" -B "$OUT" ${generator[@]+"${generator[@]}"} \
    -DPICO_SDK_PATH="$sdk" -DCMAKE_BUILD_TYPE="$TYPE" \
    -DRP_DEVICE_CHECK_CASES="$OUT/cases/DeviceCheckCases.hpp" -DRP_EXPORT_PLAN_TOOL="$exporter" \
    ${reflect:+-DRP_REFLECTCPP_SOURCE_DIR="$reflect"}
cmake --build "$OUT" -j"$JOBS"

echo "== the trap's positive control (rp-device-trap-probe must not link)"
probe_log="$OUT/trap-probe.log"
if cmake --build "$OUT" --target rp-device-trap-probe -j"$JOBS" >"$probe_log" 2>&1; then
    die "rp-device-trap-probe LINKED: the link trap is not reaching the image's link ($probe_log)"
fi
for symbol in __wrap__Znwj __wrap_malloc; do
    grep -q "trap-probe\.cpp\.o: in function .main.:" "$probe_log" &&
        grep -q "undefined reference to .$symbol'" "$probe_log" ||
        die "rp-device-trap-probe failed, but not with its own object's reference to $symbol ($probe_log)"
done
grep -E "trap-probe\.cpp\.o: in function|undefined reference" "$probe_log" | sed 's/^.*ld: //; s/^/  /'

ELF="$OUT/rp-device-check.elf"
MAP="$OUT/rp-device-check.map"
[ -f "$ELF" ] || die "no $ELF"

echo
echo "== $ELF: allocated sections"
arm-none-eabi-size -A "$ELF" | awk 'NR <= 2 || ($2 > 0 && $3 > 0)'

echo "== by owner (bytes; from the link map: flash is code and constants, data is initialised RAM, also in flash)"
awk '
function hex(s,    i, n) {
    n = 0
    s = tolower(substr(s, 3))
    for (i = 1; i <= length(s); i++) n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
    return n
}
/^Linker script and memory map/ { on = 1; next }
!on { next }
/^\.[A-Za-z_]/ { out = $1; pending = ""; next }
/^ \*fill\*/ { next }
/^ \.[^ ]+$/ { pending = $1; next }
/^ / {
    if ($1 ~ /^0x/ && pending != "") { size = $2; file = $3 }
    else if ($2 ~ /^0x/ && $3 ~ /^0x/) { size = $3; file = $4 }
    else { pending = ""; next }
    pending = ""
    if (file == "" || size == "0x0") next
    if (out == ".text" || out == ".rodata" || out == ".binary_info" || out == ".ARM.exidx" || out == ".flash_end")
        mem = 1
    else if (out == ".data") mem = 2
    else if (out == ".bss" || out == ".ram_vector_table" || out == ".uninitialized_data" || out ~ /^\.scratch_/) mem = 3
    else next
    owner = "toolchain (libc, libm, libgcc)"
    if (file ~ /libretroplug-core-nodes/) owner = "core: the rp.core nodes"
    else if (file ~ /libretroplug-dsp-nodes/) owner = "dsp: the rp.dsp nodes"
    else if (file ~ /libretroplug-dsp-kernels/) owner = "dsp: the kernels"
    else if (file ~ /libretroplug-n8\.a/) owner = "n8: the rp.n8 nodes, the protocol"
    else if (file ~ /libretroplug-device-io-nodes/) owner = "device-io: the rp.device nodes"
    else if (file ~ /libretroplug-devtest-nodes/) owner = "devtest: the rp.devtest nodes"
    else if (file ~ /libretroplug-graph-exec/) owner = "graph-runtime: executor, loader"
    else if (file ~ /rp-device-check\.dir\/(main|DeviceCheck|N8Protocol)\.cpp/ || file ~ /\/packages\/products\//)
        owner = "the check (its cases and arena)"
    # The SDK sources compile into the image (CMakeFiles/rp-device-check.dir/<their path>); its own libraries build
    # under pico-sdk/.
    else if (file ~ /rp-device-check\.dir\// || file ~ /^pico-sdk\//) owner = "the Pico SDK runtime"
    total[owner, mem] += hex(size); owners[owner] = 1
}
END {
    n = split("core: the rp.core nodes|dsp: the rp.dsp nodes|dsp: the kernels|n8: the rp.n8 nodes, the protocol|" \
              "device-io: the rp.device nodes|devtest: the rp.devtest nodes|graph-runtime: executor, loader|" \
              "the check (its cases and arena)|the Pico SDK runtime|toolchain (libc, libm, libgcc)", order, "|")
    printf "  %-34s %9s %9s %9s\n", "", "flash", "data", "bss"
    for (i = 1; i <= n; i++) {
        o = order[i]
        printf "  %-34s %9d %9d %9d\n", o, total[o, 1], total[o, 2], total[o, 3]
        f += total[o, 1]; d += total[o, 2]; b += total[o, 3]
    }
    printf "  %-34s %9d %9d %9d\n", "total", f, d, b
}' "$MAP"
