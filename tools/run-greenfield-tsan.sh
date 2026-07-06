#!/usr/bin/env bash
# Build + run the greenfield native host under ThreadSanitizer to prove the audio-thread /
# control-thread seam is race-free.
#
#   tools/run-greenfield-tsan.sh [slugFilter]     # default: dsp-threaded
#
# tools/run-sanitizers.sh only builds/runs the Catch2 targets (from build-tsan/test/). The
# native-greenfield-host lands in build-tsan/bin/ and needs a JS bundle argument, so it doesn't fit
# that loop — this drives it via run-native-tests.mjs with the instrumented host. Sanitizer flags
# are global (CMakeLists.txt RETROPLUG_SANITIZE), so the host is instrumented automatically in
# build-tsan/. Reuses build-tsan/ (configure is idempotent); the load-bearing build/ is untouched.
#
# Exits nonzero on a build failure, a test failure, OR any TSan finding (halt_on_error=1).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"

builddir=build-tsan
filter="${1:-dsp-threaded}"

echo "==> configuring $builddir (-DRETROPLUG_SANITIZE=thread)"
cmake -B "$builddir" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON \
    -DRETROPLUG_SANITIZE=thread \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

echo "==> building native-greenfield-host (instrumented)"
cmake --build "$builddir" -j"$(nproc)" --target native-greenfield-host

echo "==> running greenfield native test '$filter' under thread sanitizer"
# halt_on_error=1 turns any race into a nonzero exit; the same suppressions as the Catch2 gate (the
# seqlock triple-buffer entries) — the audio-thread / DSP-context seam is expected to need NONE.
export TSAN_OPTIONS="suppressions=$repo/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1"
export RETROPLUG_GREENFIELD_HOST="$repo/$builddir/bin/native-greenfield-host"

node packages/retroplug-greenfield/scripts/run-native-tests.mjs "$filter"
