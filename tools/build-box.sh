#!/usr/bin/env bash
#
# Builds the Pico box's firmware (pico/n8-host, docs/modular-graph-phase-e2-design.md section 8 and the E3 design's
# section 5), the MIDI-to-N8 bridge, a graph host on the N8 pack's Edio, in both of its USB variants, each under the
# link trap on the heap, on exceptions and on RTTI and the stack limit (cmake/device-image.cmake), and prints what each
# costs in flash and RAM. CI's linux-x64 job runs it beside tools/build-device-check.sh; nothing is flashed or run (the
# hardware gate runs pico/n8-host/loop.sh, stress.sh and faults.sh on the rig).
#
#   1. Builds the plan exporter (retroplug-export-plan) in the desktop build, which must be configured (./build.sh):
#      the firmware's build exports its built-in plan (pico/n8-host/plan.json) with it and embeds the blob, so a plan
#      the box could not run or hold fails here. That build is shared with a concurrent ./build.sh, and two builds of
#      one tree at once race (loop.sh does not build it, for that reason), so where another session builds, point
#      RP_DESKTOP_BUILD_DIR at a tree of your own.
#   2. For each variant, PIO-USB (the rig's: the N8 on GP2/GP3) and RP_NATIVE_USB=ON (the chip's own controller):
#      configures the firmware's CMake project on the Pico SDK's toolchain, in a build dir of its own, and builds it.
#      A link error there naming __wrap_<symbol> is the trap: the object it names reached the heap or a throw.
#   3. Builds the variant's trap probe (n8_host_trap_probe: the firmware's libraries and one source that allocates with
#      new and malloc), and stops unless its link FAILS naming its own object for __wrap__Znwj and __wrap_malloc.
#   4. Prints the image's allocated sections (arm-none-eabi-size -A) and its flash, data and bss.
#
# The ELF is the artifact: no UF2 (PICO_NO_PICOTOOL, as the SDK would otherwise fetch and build picotool). loop.sh's
# own build dirs (pico/n8-host/build, build-native, ...) are left alone.
#
# Usage:  tools/build-box.sh                 (pnpm device:box)
#   PICO_SDK_PATH         the Pico SDK (2.1.1) with its lib/tinyusb submodule; when unset, or an SDK without TinyUSB,
#                         /workspaces/pico-sdk (loop.sh's rule). A path that is set and is no SDK stops the run.
#   PICO_PIO_USB_PATH     a Pico-PIO-USB checkout (5a37a66); when unset, /workspaces/Pico-PIO-USB.
#   RP_DESKTOP_BUILD_DIR  the configured desktop build (default build/), which builds the plan exporter; its reflect-cpp
#                         checkout saves a clone
#   RP_BOX_BUILD_DIR      the build dirs' parent (default build-box/: pio/ and native/ under it)
#   RP_BOX_BUILD_TYPE     default Release (the SDK's default: -O3, NDEBUG)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DESKTOP="${RP_DESKTOP_BUILD_DIR:-$REPO_DIR/build}"
OUT="${RP_BOX_BUILD_DIR:-$REPO_DIR/build-box}"
TYPE="${RP_BOX_BUILD_TYPE:-Release}"
JOBS="$(nproc 2>/dev/null || echo 4)"

die() { echo "build-box: $*" >&2; exit 1; }

# The SDK is loop.sh's: PICO_SDK_PATH when it carries TinyUSB, else /workspaces/pico-sdk (the devcontainer's image
# bakes an SDK without submodules at /opt/pico-sdk and points PICO_SDK_PATH at it), said aloud when it falls back. A
# path that is set and is no SDK at all stops the run, as does a Pico-PIO-USB named that is not one: a wrong path never
# quietly builds against another checkout.
sdk=/workspaces/pico-sdk
if [ -n "${PICO_SDK_PATH:-}" ]; then
    [ -e "$PICO_SDK_PATH/pico_sdk_init.cmake" ] || die "PICO_SDK_PATH ($PICO_SDK_PATH) is not a Pico SDK"
    if [ -e "$PICO_SDK_PATH/lib/tinyusb/src/tusb.h" ]; then
        sdk=$PICO_SDK_PATH
    else
        echo "build-box: PICO_SDK_PATH ($PICO_SDK_PATH) has no TinyUSB submodule; using $sdk, as loop.sh does"
    fi
fi
[ -e "$sdk/pico_sdk_init.cmake" ] || die "no Pico SDK: set PICO_SDK_PATH (the default, $sdk, is not one)"
[ -e "$sdk/lib/tinyusb/src/tusb.h" ] ||
    die "$sdk has no TinyUSB (git -C $sdk submodule update --init lib/tinyusb)"
pio_usb=${PICO_PIO_USB_PATH:-/workspaces/Pico-PIO-USB}
[ -e "$pio_usb/src/pio_usb.h" ] || die "PICO_PIO_USB_PATH ($pio_usb) is not a Pico-PIO-USB checkout"
command -v arm-none-eabi-g++ >/dev/null ||
    die "no arm-none-eabi-g++ (apt: gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib)"

[ -f "$DESKTOP/CMakeCache.txt" ] || die "$DESKTOP is not a configured desktop build (run ./build.sh first)"

echo "== the plan exporter (desktop: retroplug-export-plan)"
cmake --build "$DESKTOP" --target retroplug-export-plan -j"$JOBS"
exporter="$DESKTOP/export-plan/retroplug-export-plan"
[ -x "$exporter" ] || die "no $exporter after building it"

# reflect-cpp at the desktop's pin: the desktop build's own checkout (the firmware's CMakeLists checks its version
# against the pin either way, and clones the pinned tag without it).
reflect="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' "$DESKTOP/CMakeCache.txt")"

for variant in pio native; do
    dir="$OUT/$variant"
    native=OFF
    [ "$variant" = native ] && native=ON
    generator=()
    command -v ninja >/dev/null && [ ! -f "$dir/CMakeCache.txt" ] && generator=(-G Ninja)

    echo "== the firmware, $variant (RP_NATIVE_USB=$native; Pico SDK $sdk, $TYPE)"
    cmake -S "$REPO_DIR/pico/n8-host" -B "$dir" ${generator[@]+"${generator[@]}"} \
        -DPICO_SDK_PATH="$sdk" -DPICO_PIO_USB_PATH="$pio_usb" -DCMAKE_BUILD_TYPE="$TYPE" -DRP_NATIVE_USB="$native" \
        -DPICO_NO_PICOTOOL=1 -DRP_EXPORT_PLAN_TOOL="$exporter" ${reflect:+-DRP_REFLECTCPP_SOURCE_DIR="$reflect"}
    cmake --build "$dir" -j"$JOBS"

    echo "== the trap's positive control, $variant (n8_host_trap_probe must not link)"
    probe_log="$dir/trap-probe.log"
    if cmake --build "$dir" --target n8_host_trap_probe -j"$JOBS" >"$probe_log" 2>&1; then
        die "n8_host_trap_probe LINKED: the link trap is not reaching the firmware's link ($probe_log)"
    fi
    for symbol in __wrap__Znwj __wrap_malloc; do
        grep -q "trap-probe\.cpp\.o: in function .main.:" "$probe_log" &&
            grep -q "undefined reference to .$symbol'" "$probe_log" ||
            die "n8_host_trap_probe failed, but not with its own object's reference to $symbol ($probe_log)"
    done
    grep -E "trap-probe\.cpp\.o: in function|undefined reference" "$probe_log" | sed 's/^.*ld: //; s/^/  /'

    elf="$dir/n8_host.elf"
    [ -f "$elf" ] || die "no $elf"
    echo
    echo "== $elf: allocated sections"
    arm-none-eabi-size -A "$elf" | awk 'NR <= 2 || ($2 > 0 && $3 > 0)'
    # The sizes: flash is what the image keeps there to run (its code, the vector table, the boot blocks and the reset
    # code), data is initialised RAM (its image in flash too) and bss is zeroed RAM; the SDK's reserved heap and stack
    # sections are counted in none of them. The box is a copy_to_ram image (pico/n8-host/CMakeLists.txt): its code is in
    # flash only to be copied out at boot and runs from RAM, and its constants are in data, so the line also says how
    # much of the flash figure RAM holds, where the load image ends in flash (the staged ROMs start 2 MB in, box.cpp's
    # ROM table), and the RAM the image takes in all: from SRAM's base to the end of .heap, which is bss's end while the
    # image reserves no heap (PICO_HEAP_SIZE=0); the stacks are in the scratch banks.
    arm-none-eabi-size -A "$elf" | awk -v variant="$variant" '
        $1 == ".text" || $1 == ".rodata" || $1 == ".ARM.exidx" || $1 == ".ARM.extab" || $1 == ".binary_info" ||
        $1 == ".flash_end" || $1 == ".flashtext" || $1 == ".boot2" {
            f += $2
            if ($3 >= 536870912) r += $2
        }
        $1 == ".data" || $1 == ".tdata" { d += $2 }
        $1 == ".bss" || $1 == ".tbss" || $1 == ".ram_vector_table" || $1 == ".uninitialized_data" { b += $2 }
        $1 ~ /^\.scratch_/ { b += $2 }
        $1 == ".flash_end" { image = $3 + $2 - 268435456 }
        $1 == ".heap" { end = $3 + $2 - 536870912 }
        END {
            printf "== %s: flash %d, data %d, bss %d bytes", variant, f, d, b
            if (r) printf "; %d of the flash runs from RAM", r
            if (image) printf "; the load image ends %d bytes into flash", image
            if (end) printf "; RAM %d bytes in all", end
            printf "\n"
        }'
    echo
done
