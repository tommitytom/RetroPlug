#!/usr/bin/env bash
# Build + run the C++ Catch2 test binaries under a sanitizer.
#
#   tools/run-sanitizers.sh thread     # ThreadSanitizer  -> build-tsan/
#   tools/run-sanitizers.sh address    # AddressSanitizer -> build-asan/
#
# Uses a SEPARATE build dir per sanitizer so the load-bearing build/ is never
# touched. Only the three test targets are built (not the plugin / UI bundle /
# standalone), so the heaviest cost is instrumenting the Mesen core.
#
# Exits nonzero if configure, build, or any test binary fails — so it doubles
# as a one-shot gate.
set -euo pipefail

mode="${1:-}"
case "$mode" in
    thread)  san=thread;  builddir=build-tsan ;;
    address) san=address; builddir=build-asan ;;
    *) echo "usage: $0 <thread|address>" >&2; exit 2 ;;
esac

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

targets=(retroplug-tests retroplug-sameboy-tests retroplug-mesen-tests retroplug-rpc-tests)

echo "==> configuring $builddir (-DRETROPLUG_SANITIZE=$san)"
cmake -B "$builddir" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON \
    -DRETROPLUG_SANITIZE="$san" \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

echo "==> building ${targets[*]}"
cmake --build "$builddir" -j"$(nproc)" --target "${targets[@]}"

# Sanitizer runtime options. halt/abort on the first real finding.
if [ "$san" = thread ]; then
    export TSAN_OPTIONS="suppressions=$repo/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1"
else
    export ASAN_OPTIONS="abort_on_error=1 detect_leaks=1"
    lsan="$repo/packages/native/test/sanitizer/lsan.supp"
    [ -f "$lsan" ] && export LSAN_OPTIONS="suppressions=$lsan"
fi

# [MesenSingleton] tests deliberately hammer Mesen's process-global state
# (GameDatabase / FolderUtilities / SimpleLock singletons) from many threads to
# document its known non-thread-safety — several are already [!mayfail]. Those
# races are inside Mesen, not our code, and aren't how the plugin uses it (one
# instance is only ever driven by one thread), so exclude them from the gate.
# Catch2 runs everything when the tag is absent, so this is a no-op elsewhere.
exclude="~[MesenSingleton]"

rc=0
for t in "${targets[@]}"; do
    bin="$builddir/test/$t"
    echo "==> running $t"
    if ! "$bin" "$exclude"; then
        echo "!! $t failed under $san sanitizer" >&2
        rc=1
    fi
done

[ "$rc" -eq 0 ] && echo "==> all test binaries passed under $san sanitizer"
exit "$rc"
