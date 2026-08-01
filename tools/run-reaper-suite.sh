#!/usr/bin/env bash
#
# Run the whole headless-Reaper leg CONCURRENTLY: build + author once, then fan out all nine
# checks (7 audio renders + 3 editor snapshots) in parallel and print a PASS/FAIL summary.
#
# Each check runs through the isolated harness (tools/reaper-env.sh) with a distinct RP_JOB_TAG,
# so they don't share a JACK server, a Reaper config dir, an Xvfb display, or log files — which is
# what makes them safe to run at the same time (the old per-scenario `pnpm reaper:*` commands all
# grabbed the same defaults and could only run one at a time). Offline render is sample-accurate
# and wall-clock-independent, so parallel scheduling can't change the audio.
#
# Usage:
#   tools/run-reaper-suite.sh                 # build + author + run all 9
#   RP_SUITE_JOBS=4 tools/run-reaper-suite.sh # cap concurrency at 4 (default: 8)
#   RP_SUITE_NO_BUILD=1 tools/run-reaper-suite.sh   # skip the vst3 build + fixture regen
#
# Wired as `pnpm reaper:all`.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_DIR"

MAXJOBS="${RP_SUITE_JOBS:-8}"
RESULTS_DIR="$REPO_DIR/build/reaper-suite"
rm -rf "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR"

# ---- scenario table -------------------------------------------------------------------------
# Render scenarios: name -> author-lua, autoload fixture, .rpp, output .wav, jack period, an
# optional scenario env (the lsdj author lua keys off RP_SCENARIO), and the analyzer command.
declare -A AUTHOR_LUA FIXTURE RPP WAV JACKP SCEN_ENV ANALYZE

RENDER_SCENARIOS=(mgb-smoke mgb-midi-timing n8-midi-timing lsdj-midi-metro lsdj-arduinoboy-metro lsdj-midi-drift risa-sync)

AUTHOR_LUA[mgb-smoke]="tools/reaper-mgb-author.lua"
FIXTURE[mgb-smoke]="build/mgb.rplg.zip"
RPP[mgb-smoke]="examples/reaper/mgb_smoke.rpp"
WAV[mgb-smoke]="build/reaper-mgb-smoke.wav"
JACKP[mgb-smoke]="1024"
ANALYZE[mgb-smoke]='_assert_nonsilent build/reaper-mgb-smoke.wav'

AUTHOR_LUA[mgb-midi-timing]="tools/reaper-mgb-timing-author.lua"
FIXTURE[mgb-midi-timing]="build/mgb.rplg.zip"
RPP[mgb-midi-timing]="examples/reaper/mgb_midi_timing.rpp"
WAV[mgb-midi-timing]="build/reaper-mgb-midi-timing.wav"
JACKP[mgb-midi-timing]="8192"
ANALYZE[mgb-midi-timing]='tools/reaper-timing-analyze.py --midi-timing --from-ms 2000 --gap-ms 136.05 --tol-ms 25 build/reaper-mgb-midi-timing.wav'

AUTHOR_LUA[n8-midi-timing]="tools/reaper-nes-timing-author.lua"
FIXTURE[n8-midi-timing]="build/nes.rplg.zip"
RPP[n8-midi-timing]="examples/reaper/nes_midi_timing.rpp"
WAV[n8-midi-timing]="build/reaper-nes-midi-timing.wav"
JACKP[n8-midi-timing]="8192"
ANALYZE[n8-midi-timing]='tools/reaper-timing-analyze.py --midi-timing --from-ms 2000 --gap-ms 136.05 --tol-ms 30 build/reaper-nes-midi-timing.wav'

AUTHOR_LUA[lsdj-midi-metro]="tools/reaper-lsdj-author.lua"
FIXTURE[lsdj-midi-metro]="build/lsdj_midi-metro.rplg.zip"
RPP[lsdj-midi-metro]="examples/reaper/lsdj_midi_metro.rpp"
WAV[lsdj-midi-metro]="build/reaper-lsdj-midi-metro.wav"
JACKP[lsdj-midi-metro]="1024"
SCEN_ENV[lsdj-midi-metro]="RP_SCENARIO=midi-metro"
ANALYZE[lsdj-midi-metro]='tools/reaper-timing-analyze.py build/reaper-lsdj-midi-metro.wav'

AUTHOR_LUA[lsdj-arduinoboy-metro]="tools/reaper-lsdj-author.lua"
FIXTURE[lsdj-arduinoboy-metro]="build/lsdj_arduinoboy-metro.rplg.zip"
RPP[lsdj-arduinoboy-metro]="examples/reaper/lsdj_arduinoboy_metro.rpp"
WAV[lsdj-arduinoboy-metro]="build/reaper-lsdj-arduinoboy-metro.wav"
JACKP[lsdj-arduinoboy-metro]="1024"
SCEN_ENV[lsdj-arduinoboy-metro]="RP_SCENARIO=arduinoboy-metro"
ANALYZE[lsdj-arduinoboy-metro]='tools/reaper-timing-analyze.py build/reaper-lsdj-arduinoboy-metro.wav'

AUTHOR_LUA[lsdj-midi-drift]="tools/reaper-lsdj-author.lua"
FIXTURE[lsdj-midi-drift]="build/lsdj_midi-drift.rplg.zip"
RPP[lsdj-midi-drift]="examples/reaper/lsdj_midi_drift.rpp"
WAV[lsdj-midi-drift]="build/reaper-lsdj-midi-drift.wav"
JACKP[lsdj-midi-drift]="1024"
SCEN_ENV[lsdj-midi-drift]="RP_SCENARIO=midi-drift"
ANALYZE[lsdj-midi-drift]='tools/reaper-timing-analyze.py build/reaper-lsdj-midi-drift.wav --drift'

# risa host sync: no MIDI item at all - the DAW TRANSPORT is the whole input, turned into risa's
# arm / start / 24-PPQN clock / stop byte stream by the risa-sync role. One noise hit per beat, so
# --drift pairs every beat to its click.
AUTHOR_LUA[risa-sync]="tools/reaper-risa-author.lua"
FIXTURE[risa-sync]="build/risa.rplg.zip"
RPP[risa-sync]="examples/reaper/risa_sync.rpp"
WAV[risa-sync]="build/reaper-risa-sync.wav"
JACKP[risa-sync]="1024"
ANALYZE[risa-sync]='tools/reaper-timing-analyze.py build/reaper-risa-sync.wav --drift'

# Editor scenarios: name -> the standalone script (each already self-judges).
declare -A EDITOR_SCRIPT
EDITOR_SCENARIOS=(editor editor-reopen editor-autoload)
EDITOR_SCRIPT[editor]="tools/run-reaper-editor.sh"
EDITOR_SCRIPT[editor-reopen]="tools/run-reaper-editor-reopen.sh"
EDITOR_SCRIPT[editor-autoload]="tools/run-reaper-editor-autoload.sh"

ALL_SCENARIOS=("${RENDER_SCENARIOS[@]}" "${EDITOR_SCENARIOS[@]}")

# ---- helpers --------------------------------------------------------------------------------
# mgb-smoke has no timing analyzer; assert its WAV carries real signal instead of silence.
_assert_nonsilent() {
    python3 - "$1" <<'PY'
import sys, wave
path = sys.argv[1]
w = wave.open(path, 'rb'); sw = w.getsampwidth()
data = w.readframes(min(w.getnframes(), 400000))
if sw == 3:
    peak = max((abs(int.from_bytes(data[i:i+3], 'little', signed=True)) for i in range(0, len(data) - 2, 3)), default=0)
else:
    import audioop; peak = audioop.max(data, sw)
print(f"{path}: frames={w.getnframes()} sampwidth={sw} peak={peak}")
sys.exit(0 if peak > 1000 else 1)
PY
}

# One render pipeline: author the .rpp, render it, analyze — sequential WITHIN the job (author
# writes the .rpp the render reads), but the whole pipeline runs concurrently with the others.
# Always re-authors: a stale embedded plugin identity renders silence (the .rpp is gitignored).
run_render() {
    local n="$1"
    echo "=== [$n] author ==="
    # shellcheck disable=SC2086
    env RP_JOB_TAG="suite-$n-author" REAPER_JACK_PERIOD="${JACKP[$n]}" ${SCEN_ENV[$n]:-} \
        tools/run-reaper-author.sh "${RPP[$n]}" build "${AUTHOR_LUA[$n]}" "${FIXTURE[$n]}" || return 1
    echo "=== [$n] render ==="
    env RP_JOB_TAG="suite-$n" REAPER_JACK_PERIOD="${JACKP[$n]}" \
        tools/run-reaper-render.sh "${RPP[$n]}" "${FIXTURE[$n]}" || return 1
    echo "=== [$n] analyze ==="
    eval "${ANALYZE[$n]}"
}

run_editor() {
    local n="$1"
    RP_JOB_TAG="suite-$n" "${EDITOR_SCRIPT[$n]}"
}

# Dispatch one scenario by name, recording rc + a one-line verdict for the summary.
run_one() {
    local n="$1" rc
    if [ -n "${EDITOR_SCRIPT[$n]:-}" ]; then
        run_editor "$n" >"$RESULTS_DIR/$n.log" 2>&1; rc=$?
    else
        run_render "$n" >"$RESULTS_DIR/$n.log" 2>&1; rc=$?
    fi
    echo "$rc" >"$RESULTS_DIR/$n.rc"
    if [ "$rc" -eq 0 ]; then echo "  ✓ $n"; else echo "  ✗ $n (rc=$rc)"; fi
}

# ---- build + author once --------------------------------------------------------------------
if [ "${RP_SUITE_NO_BUILD:-0}" != "1" ]; then
    echo "[suite] building retroplug-vst3 …"
    node scripts/cmake-build.js retroplug-vst3 || { echo "[suite] build failed" >&2; exit 1; }
    echo "[suite] regenerating autoload fixtures …"
    node tools/author-rplg.js            >"$RESULTS_DIR/fixture-mgb.log" 2>&1 || { echo "[suite] mgb fixture failed" >&2; exit 1; }
    node tools/author-nes-rplg.js        >"$RESULTS_DIR/fixture-nes.log" 2>&1 || { echo "[suite] nes fixture failed" >&2; exit 1; }
    node tools/author-risa-rplg.js       >"$RESULTS_DIR/fixture-risa.log" 2>&1 || { echo "[suite] risa fixture failed" >&2; exit 1; }
    for s in midi-metro arduinoboy-metro midi-drift; do
        node tools/author-lsdj-rplg.js "$s" >"$RESULTS_DIR/fixture-lsdj-$s.log" 2>&1 || { echo "[suite] lsdj $s fixture failed" >&2; exit 1; }
    done
fi

# ---- fan out --------------------------------------------------------------------------------
echo "[suite] running ${#ALL_SCENARIOS[@]} scenarios, up to $MAXJOBS at once …"
START=$SECONDS
running=0
for n in "${ALL_SCENARIOS[@]}"; do
    run_one "$n" &
    running=$((running + 1))
    if [ "$running" -ge "$MAXJOBS" ]; then wait -n; running=$((running - 1)); fi
done
wait
ELAPSED=$((SECONDS - START))

# ---- summary --------------------------------------------------------------------------------
echo
echo "================ reaper suite summary (${ELAPSED}s, jobs=$MAXJOBS) ================"
fail=0
for n in "${ALL_SCENARIOS[@]}"; do
    rc=$(cat "$RESULTS_DIR/$n.rc" 2>/dev/null || echo "?")
    if [ "$rc" = "0" ]; then
        printf "  PASS  %s\n" "$n"
    else
        printf "  FAIL  %s (rc=%s)\n" "$n" "$rc"
        # Surface the tail of the failing job so the cause is visible without hunting logs.
        sed 's/^/          /' <(tail -n 4 "$RESULTS_DIR/$n.log" 2>/dev/null) || true
        fail=$((fail + 1))
    fi
done
echo "  logs: $RESULTS_DIR/<scenario>.log"
echo "======================================================================"
if [ "$fail" -eq 0 ]; then
    echo "ALL ${#ALL_SCENARIOS[@]} REAPER CHECKS PASSED"
    exit 0
fi
echo "$fail of ${#ALL_SCENARIOS[@]} REAPER CHECKS FAILED"
exit 1
