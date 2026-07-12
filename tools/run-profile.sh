#!/usr/bin/env bash
# Build + run the DSP-thread allocation benchmark (spec/08-profiling.md) in a profiling
# build, and optionally under valgrind for allocation-site / CPU attribution.
#
#   tools/run-profile.sh [mode] [slug]
#     mode: stats (default) | trace | dhat | callgrind | massif
#     slug: the test-native file to run (default: dsp-bench)
#
# Uses a SEPARATE build-prof/ dir (RelWithDebInfo + -DRETROPLUG_PROFILE=ON): the bare-QuickJS counting
# allocator + the dsp* profiling RPCs are compiled in, and -g is global so valgrind attributes into
# quickjs.c. The load-bearing build/ and the shipped plugin are untouched (they never define the macro).
#
#   stats     — in-process allocation counters; prints one JSON metrics line (deterministic, fast).
#   trace     — per-role runtime spans (spec/08 Tier B) → build-prof/trace.json (Chrome trace-event JSON;
#               load in ui.perfetto.dev or speedscope). Knob: RP_BENCH_TRACE_BLOCKS (default 64).
#   dhat      — valgrind DHAT allocation census → build-prof/valgrind/dhat.out.json (load in the viewer).
#   callgrind — deterministic instruction counts → callgrind_annotate top-N.
#   massif    — heap size over time → ms_print.
#
# Workload knobs (env, honoured by the benchmark): RP_BENCH_PROFILE (A|B|C), RP_BENCH_CORES,
# RP_BENCH_BLOCKS, RP_BENCH_WARMUP, RP_BENCH_SEED. The valgrind modes default RP_BENCH_BLOCKS low
# (valgrind is ~20-50x) unless you override it.
set -euo pipefail

mode="${1:-stats}"
slug="${2:-dsp-bench}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"
builddir=build-prof
host="$repo/$builddir/bin/retroplug-host"

echo "==> configuring $builddir (-DRETROPLUG_PROFILE=ON)"
cmake -B "$builddir" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DRETROPLUG_PROFILE=ON \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ >/dev/null

echo "==> building retroplug-host (profiling)"
cmake --build "$builddir" -j"$(nproc)" --target retroplug-host

run_native() {  # $1 = host binary the runner launches (RETROPLUG_HOST)
    RETROPLUG_HOST="$1" node packages/retroplug/scripts/run-native-tests.mjs "$slug"
}

case "$mode" in
    stats)
        echo "==> running '$slug' (in-process allocation counters)"
        run_native "$host"
        ;;
    trace)
        : "${RP_BENCH_TRACE_BLOCKS:=64}"
        export RP_BENCH_TRACE=1 RP_BENCH_TRACE_BLOCKS
        out="$repo/$builddir/trace.json"
        log="$(mktemp)"
        echo "==> running '$slug' with per-role tracing (RP_BENCH_TRACE_BLOCKS=$RP_BENCH_TRACE_BLOCKS)"
        run_native "$host" | tee "$log" || true
        grep '^DSP-TRACE ' "$log" | tail -1 | sed 's/^DSP-TRACE //' > "$out" || true
        rm -f "$log"
        if [ -s "$out" ]; then
            echo "==> wrote $out ($(wc -c < "$out") bytes) — load in https://ui.perfetto.dev or speedscope"
        else
            echo "!! no DSP-TRACE line captured (need a RETROPLUG_PROFILE host + the dsp-bench slug)" >&2
            exit 1
        fi
        ;;
    dhat|callgrind|massif)
        command -v valgrind >/dev/null || { echo "!! valgrind not installed" >&2; exit 2; }
        out="$repo/$builddir/valgrind"
        mkdir -p "$out"
        : "${RP_BENCH_BLOCKS:=200}"   # keep valgrind runs tractable unless overridden
        export RP_BENCH_BLOCKS
        case "$mode" in
            dhat)      vg=(--tool=dhat "--dhat-out-file=$out/dhat.out.json") ;;
            callgrind) vg=(--tool=callgrind "--callgrind-out-file=$out/callgrind.out" --dump-instr=yes --collect-jumps=yes) ;;
            massif)    vg=(--tool=massif --time-unit=B "--massif-out-file=$out/massif.out") ;;
        esac
        # run-native-tests.mjs exec's RETROPLUG_HOST directly, so point it at a shim that
        # launches the real host under valgrind.
        shim="$out/host-$mode.sh"
        { echo '#!/usr/bin/env bash'; echo "exec valgrind ${vg[*]} \"$host\" \"\$@\""; } > "$shim"
        chmod +x "$shim"
        echo "==> running '$slug' under valgrind --tool=$mode (RP_BENCH_BLOCKS=$RP_BENCH_BLOCKS)"
        run_native "$shim" || true
        echo "==> $mode results:"
        case "$mode" in
            dhat)      echo "   $out/dhat.out.json — load in the DHAT viewer / Firefox Profiler" ;;
            callgrind) callgrind_annotate --threshold=99 "$out/callgrind.out" | head -60 ;;
            massif)    ms_print "$out/massif.out" | head -60 ;;
        esac
        ;;
    *) echo "usage: $0 <stats|trace|dhat|callgrind|massif> [slug]" >&2; exit 2 ;;
esac
