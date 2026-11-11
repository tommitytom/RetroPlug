#!/usr/bin/env bash
# Build + run the native host under a sanitizer to prove the audio-thread /
# control-thread seam is race-free (thread) and its cross-thread ownership handoff is
# leak / use-after-free free (address).
#
#   tools/run-sanitizer.sh thread  [slug]   # ThreadSanitizer  -> build-tsan/
#   tools/run-sanitizer.sh address [slug]   # AddressSanitizer -> build-asan/
#
# Default slugs are the two audio-thread tests (dsp-threaded + dsp-lifecycle); pass a slug to run
# just one. The Catch2 binaries are a separate job - build them by name into build-<san>/ if you want
# them instrumented too. Thread mode also builds and runs the Launchpad pack's transport binary by
# name: its swap case drives the UI, audio and device threads at once, so its rings are only CHECKED
# under this script (host slugs cannot reach it). The host lands in build-<san>/bin/ and needs a JS bundle argument, so this drives it via
# run-native-tests.mjs with the instrumented host. Sanitizer flags are global (CMakeLists.txt
# RETROPLUG_SANITIZE), so the host is instrumented automatically. Reuses the build-<san>/ dir
# (configure is idempotent); the load-bearing build/ is untouched.
#
# Exits nonzero on a build failure, a test failure, OR any sanitizer finding.
set -euo pipefail

mode="${1:-thread}"
case "$mode" in
    thread)  san=thread;  builddir=build-tsan ;;
    address) san=address; builddir=build-asan ;;
    *) echo "usage: $0 <thread|address> [slug]" >&2; exit 2 ;;
esac

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

slugs=("dsp-threaded" "dsp-lifecycle")
[ -n "${2:-}" ] && slugs=("$2")

echo "==> configuring $builddir (-DRETROPLUG_SANITIZE=$san)"
cmake -B "$builddir" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON \
    -DRETROPLUG_SANITIZE="$san" \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

echo "==> building retroplug-host (instrumented)"
cmake --build "$builddir" -j"$(nproc)" --target retroplug-host

# Any finding aborts the host -> nonzero exit -> the runner reports the file failed. Thread mode
# reuses the Catch2 suppression file (efsw's teardown only; the seqlock is annotated in
# transport/SeqlockCopy.hpp rather than suppressed). Address mode
# detects leaks at exit — the cross-thread new/delete handoff is exactly what it guards.
if [ "$san" = thread ]; then
    export TSAN_OPTIONS="suppressions=$repo/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1"
else
    export ASAN_OPTIONS="abort_on_error=1 detect_leaks=1"
    lsan="$repo/packages/native/test/sanitizer/lsan.supp"
    [ -f "$lsan" ] && export LSAN_OPTIONS="suppressions=$lsan"
fi
export RETROPLUG_HOST="$repo/$builddir/bin/retroplug-host"

rc=0
for slug in "${slugs[@]}"; do
    echo "==> running native test '$slug' under $san sanitizer"
    if ! node packages/retroplug/scripts/run-native-tests.mjs "$slug"; then
        echo "!! $slug failed under $san sanitizer" >&2
        rc=1
    fi
done

# The pack's own transport binary (EXCLUDE_FROM_ALL, so built here by name): the claimed pair's rings, the writer
# thread, the facet and one case that runs a producer thread, the audio thread and the UI thread at once.
if [ "$san" = thread ]; then
    echo "==> building retroplug-launchpad-test (instrumented)"
    cmake --build "$builddir" -j"$(nproc)" --target retroplug-launchpad-test
    echo "==> running the Launchpad pack's transport under thread sanitizer"
    if ! "$repo/$builddir/bin/retroplug-launchpad-test"; then
        echo "!! retroplug-launchpad-test failed under thread sanitizer" >&2
        rc=1
    fi
    # The midi package's stream binary: the output set replaced under a send in flight, a close under one, the input ring
    # with several producers and a delivery thread. TSan checks happens-before, not the weak-memory reordering the
    # handshake's seq_cst is for (that one is argued in MidiStream::retire), but it is what catches a port freed under a
    # send on this machine.
    echo "==> building retroplug-midi-test (instrumented)"
    cmake --build "$builddir" -j"$(nproc)" --target retroplug-midi-test
    echo "==> running the midi package's stream under thread sanitizer"
    if ! "$repo/$builddir/bin/retroplug-midi-test"; then
        echo "!! retroplug-midi-test failed under thread sanitizer" >&2
        rc=1
    fi
fi

[ "$rc" -eq 0 ] && echo "==> host passed under $san sanitizer"
exit "$rc"
