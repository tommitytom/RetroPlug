#!/usr/bin/env bash
# Builds Mesen with every console it emulates switched ON (MESEN_ENABLE_SNES / -PCE / -WS), which is upstream Mesen's
# own default and the one configuration RetroPlug's root CMakeLists turns off, and builds the OFF side a second way that
# catches a missed guard: at -O0 with the options OFF. The per-console compile-out is two-way, and the guards around a
# compiled-out console rot unnoticed if only the OFF Release build is ever compiled.
#
#   tools/build-mesen-all-consoles.sh [build dir]    # every console ON, Release (default build-mesen-all)
#   tools/build-mesen-all-consoles.sh --off [dir]    # every console OFF, Debug (-O0), default build-mesen-all-off
#
# The ON run configures a build directory of its own (the root's cache defaults are OFF, and this passes the three -D
# options through), builds the `mesen` library and `retroplug-cli`, and asserts each of the four consoles' symbols is in
# the CLI. Release, because that is what ships: the proof is that the ON side's guards hold where the drop is real.
#
# The OFF run configures its own directory as Debug and builds the `mesen` library alone. At -O0 a switch over a
# template parameter emits every arm instead of folding the unreachable ones away, so a call into a compiled-out console
# that Release left invisible is a reference in the archive. The archive is then held self-contained: no C++ symbol a
# member references may be one another member does not define (the C library's and libstdc++'s symbols resolve at the
# link, which this run does not do). A missed guard fails here even though it links in Release.
#
# Both modes exit non-zero on any failure, and print what they checked.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc)"
else
    jobs="$(sysctl -n hw.ncpu)"   # macOS / BSD
fi

mode="on"
if [[ "${1:-}" == "--off" ]]; then
    mode="off"
    shift
fi
case "$mode" in
    on) dir="${1:-build-mesen-all}" ;;
    off) dir="${1:-build-mesen-all-off}" ;;
esac

fail() {
    echo "tools/build-mesen-all-consoles.sh: $*" >&2
    exit 1
}

# The four consoles' symbols, counted in <target> (a binary or an archive), printed one line: the OFF mode wants every
# count 0, the ON mode wants none of them 0, and a count 0 is the difference between the drop and its proof.
check_console_symbols() {
    local target="$1" want="$2"
    local snes gb pce ws
    snes="$(nm -C "$target" 2>/dev/null | grep -c 'SnesPpu::' || true)"
    gb="$(nm -C "$target" 2>/dev/null | grep -c 'GbPpu::' || true)"
    pce="$(nm -C "$target" 2>/dev/null | grep -c 'PceVdc::' || true)"
    ws="$(nm -C "$target" 2>/dev/null | grep -c 'WsPpu::' || true)"
    echo "SnesPpu $snes GbPpu $gb PceVdc $pce WsPpu $ws"
    local pair name count
    for pair in "SnesPpu:$snes" "GbPpu:$gb" "PceVdc:$pce" "WsPpu:$ws"; do
        name="${pair%%:*}"
        count="${pair#*:}"
        if [[ "$want" == "on" && "$count" -eq 0 ]]; then
            fail "$name is absent from $target, so the all-consoles options did not take effect"
        fi
        if [[ "$want" == "off" && "$count" -ne 0 ]]; then
            fail "$name is in $target with the consoles OFF, so a console was not compiled out"
        fi
    done
}

if [[ "$mode" == "on" ]]; then
    cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Release \
        -DMESEN_ENABLE_SNES=ON -DMESEN_ENABLE_PCE=ON -DMESEN_ENABLE_WS=ON
    cmake --build "$dir" --target mesen -j"$jobs"
    cmake --build "$dir" --target retroplug-cli -j"$jobs"
    check_console_symbols "$dir/bin/retroplug-cli" on
    echo "tools/build-mesen-all-consoles.sh: $dir built Mesen and the CLI with every console ON"
    exit 0
fi

cmake -S . -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
    -DMESEN_ENABLE_SNES=OFF -DMESEN_ENABLE_PCE=OFF -DMESEN_ENABLE_WS=OFF
cmake --build "$dir" --target mesen -j"$jobs"

archive="$dir/deps/mesen/libmesen.a"
[[ -f "$archive" ]] || fail "$archive was not built"
check_console_symbols "$archive" off

scan="$(mktemp -d)"
trap 'rm -rf "${scan:?}"' EXIT
# An archive's references are per member: an undefined symbol of one member may be defined by any other (which is what
# pulls that member in at a real link), so the two sets are read over the whole archive and the difference is what
# nothing there defines. Weak undefined symbols are left out: they resolve to 0, which is not a dropped console.
nm "$archive" | awk 'NF == 2 && $1 == "U" { print $2 }' | sort -u > "$scan/undefined"
nm "$archive" | awk 'NF == 3 && $2 != "U" { print $3 }' | sort -u > "$scan/defined"
missing="$(comm -23 "$scan/undefined" "$scan/defined")"
# A mesen symbol is a C++ name in a class or namespace. What the archive legitimately leaves undefined is libstdc++'s
# and the C++ ABI's, their typeinfo and vtables included; anything else with a `::` is a member this archive lacks.
unresolved=""
if [[ -n "$missing" ]]; then
    unresolved="$(printf '%s\n' "$missing" | c++filt \
        | sed -E 's/^(typeinfo for|vtable for|VTT for|non-virtual thunk to|virtual thunk to) //' \
        | grep -E '::' | grep -Ev '^(std|__cxxabiv1|__gnu_cxx)::' || true)"
fi
if [[ -n "$unresolved" ]]; then
    printf '%s\n' "$unresolved" >&2
    fail "$archive references $(printf '%s\n' "$unresolved" | wc -l) C++ symbol(s) no member defines, so a call into a compiled-out console was left unguarded"
fi
echo "tools/build-mesen-all-consoles.sh: $dir built Mesen at -O0 with every console OFF, and the archive references nothing it does not define"
