#!/usr/bin/env bash
#
# Per-ROM DAW parameter names, checked against a REAL host (spec/12-dynamic-parameters.md).
#
# The plugin exposes a fixed pool of MIDI-CC parameter slots and re-labels them from the loaded ROM's
# CC map. Whether that re-labelling actually reaches the user depends on the host acting on a flag the
# plugin raises (VST3 restart_component/PARAM_TITLES_CHANGED, CLAP params.rescan/RESCAN_INFO) - which no
# unit test can prove. This runs the whole chain in the isolated headless harness (tools/reaper-env.sh):
#
#   1. insert RetroPlug with no project, float the editor, read every parameter name via ReaScript
#   2. click-load mGB through the UI (the path that never passes through DPF's setState, so it also
#      covers the editor's idle poll)
#   3. read the names again and compare
#
#   PASS (exit 0): the names changed to mGB's CC map, and the parameter COUNT did not move
#   FAIL (exit 1): the host still reports the pre-load names, or the count changed
#   SKIP (exit 2): couldn't click-load mGB (setup issue), result inconclusive
#
# Format is selectable: RP_PARAMS_FORMAT=vst3 (default) or clap. Both are worth running - they take
# entirely different code paths in DPF, and CLAP is the only one that can defer to the main thread.
#
# Keyboard input doesn't reach the plugin editor under headless Reaper, so mGB is loaded with a
# synthesized MOUSE click on the "Load mGB" menu row, exactly as run-reaper-editor-reopen.sh does.
#
# NOT part of CI - it needs a full DAW + X stack. Run via `pnpm reaper:params` / `pnpm reaper:params-clap`.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

FORMAT="${RP_PARAMS_FORMAT:-vst3}"
LOAD_X_OFF="${RP_LOAD_X_OFF:-135}"   # "Load mGB" row offset from the FX window origin
LOAD_Y_OFF="${RP_LOAD_Y_OFF:-110}"
GRID_MAX_BYTES="${RP_GRID_MAX_BYTES:-2500}"
W=1280; H=720

# A name only the mGB map produces, and one only the unclaimed pool produces. Both come from
# packages/retroplug/src/parameterMap.ts + PluginDSP::initParameter, which prefixes every CC slot with
# its 1-based system number - so these are the exact strings a host reports, prefix included.
MGB_NAME="${RP_PARAMS_EXPECT:-1: PU1 Pulse Width}"
POOL_NAME="1: CC 1"
# Autoload mode only: `;`-separated lane names (without the "1: " system prefix) the loaded project's
# ROM must expose. Defaults to a VRC7 spread - a core 2A03 lane, a per-voice FM lane, and one of the
# chip-global custom-patch lanes, so a build that silently fell back to the 2A03 table fails here.
RP_PARAMS_EXPECT_LANES="${RP_PARAMS_EXPECT_LANES:-Pulse 1 Duty;FM 1 Volume;FM 6 Volume;FM Attack;FM Key-Scale Rate;FM 3 LFO Shape;FM 3 Tremolo Depth}"

: "${RP_JOB_TAG:=params-$FORMAT}"
RP_SCREEN_W=$W RP_SCREEN_H=$H
export RP_SCREEN_W RP_SCREEN_H
[ "$FORMAT" = "clap" ] && export RP_WANT_CLAP=1
source "$SCRIPT_DIR/reaper-env.sh"
reaper_env_up

SIGDIR="$RP_LOG_DIR"
SNAP="$REPO_DIR/build/reaper-params-$FORMAT.png"
OUT="$SIGDIR/rp-params.txt"
export RP_PARAMS_SIGDIR="$SIGDIR"
# Ask Reaper for the format we want BY ITS PREFIXED NAME. Reaper picks whichever format it finds first
# for a bare "RetroPlug", which silently ran the VST3 build twice; the token is re-checked against the
# dump below so a fallback can never pass as the other format.
case "$FORMAT" in
  clap) FX_TOKEN="CLAPi" ;;
  vst3) FX_TOKEN="VST3i" ;;
  *) echo "run-reaper-params: unknown RP_PARAMS_FORMAT '$FORMAT' (want vst3 or clap)" >&2; exit 2 ;;
esac
export RP_PARAMS_FX="$FX_TOKEN: RetroPlug"
rm -f "$OUT" "$SNAP" "$SIGDIR/rp-params-lua.log" \
      "$SIGDIR/rp-params-before" "$SIGDIR/rp-params-loaded" "$SIGDIR/rp-params-done"

# Autoload mode (RP_PARAMS_ROM=<rom>): the plugin comes up with the ROM already loaded, so there is
# nothing to click and no before/after transition - the check is simply "does the host report the lanes
# this ROM should have". Deterministic (no mouse), and the only coverage of the per-chip BlipToaster
# parameter tables, which the click-driven mGB path never reaches.
#
# The project is a hand-written THIN .rplg: JSON referencing the ROM by path, with no `roles` key, so
# loading it re-runs the ROM providers - which is exactly the detection chain under test. No fixture
# authoring and no retroplug-host build needed.
if [ -n "${RP_PARAMS_ROM:-}" ]; then
  if [ ! -e "$RP_PARAMS_ROM" ]; then
    echo "SKIP: $RP_PARAMS_ROM not present, nothing to check." >&2
    reaper_env_down; trap - EXIT INT TERM; exit 2
  fi
  rom_abs="$(cd "$(dirname "$RP_PARAMS_ROM")" && pwd)/$(basename "$RP_PARAMS_ROM")"
  RETROPLUG_AUTOLOAD_PROJECT="$SIGDIR/params.rplg"
  printf '{"schemaVersion":"4","settings":{},"systems":[{"platform":"nes","core":"mesen","romPath":"%s"}]}\n' \
    "$rom_abs" >"$RETROPLUG_AUTOLOAD_PROJECT"
  export RETROPLUG_AUTOLOAD_PROJECT RP_PARAMS_AUTOLOAD=1
fi

RETROPLUG_SCREENSHOT_PATH="$SNAP" \
RETROPLUG_SCREENSHOT_INTERVAL_MS=400 \
  reaper -cfgfile "$REAPER_CFG/reaper.ini" -nosplash "$SCRIPT_DIR/reaper-params.lua" >"$SIGDIR/reaper.log" 2>&1 & REAPER_PID=$!

# Wait for the pre-load dump, which the script writes once the editor has floated and settled.
for _ in $(seq 1 120); do [ -f "$SIGDIR/rp-params-before" ] && break; sleep 0.5; done
reaper_wait_snapshot "$SNAP" "${RP_PARAMS_TIMEOUT:-45}" || true

if [ -n "${RP_PARAMS_ROM:-}" ]; then
  # Autoload mode: nothing to click, just wait for the second dump.
  for _ in $(seq 1 120); do [ -f "$SIGDIR/rp-params-done" ] && break; sleep 0.5; done
  reaper_env_down
  trap - EXIT INT TERM
  cp -f "$OUT" "$REPO_DIR/build/reaper-params-$FORMAT.txt" 2>/dev/null || true
  [ -s "$OUT" ] || { echo "SKIP: no parameter dump written — see $SIGDIR/reaper.log" >&2; exit 2; }

  FX_LOADED=$(grep -m1 '^#fx	' "$OUT" | cut -f2-)
  case "$FX_LOADED" in "$FX_TOKEN"*) ;; *)
    echo "SKIP: Reaper loaded '$FX_LOADED', not a $FX_TOKEN plugin." >&2; exit 2 ;;
  esac
  claimed=$(grep -cP "^after\t[0-9]+\t1: (?!CC [0-9]+$)" "$OUT" || true)
  echo "run-reaper-params[$FORMAT]: fx='$FX_LOADED' rom=$(basename "$RP_PARAMS_ROM") claimed=$claimed lanes"
  fail=0
  IFS=';' read -r -a EXPECT <<<"$RP_PARAMS_EXPECT_LANES"
  for want in "${EXPECT[@]}"; do
    [ -n "$want" ] || continue
    grep -qF "	1: $want" "$OUT" || { echo "FAIL: the host does not report '1: $want'." >&2; fail=1; }
  done
  [ "$fail" = "0" ] || exit 1
  echo "PASS: the host reports this build's lanes ($claimed claimed)."
  exit 0
fi

# Click-load mGB through the UI menu (mouse routes to the plugin; keyboard does not).
FXW=$(xdotool search --name "CLAPi: RetroPlug" 2>/dev/null | head -1 || true)
[ -z "$FXW" ] && FXW=$(xdotool search --name "VST3i: RetroPlug" 2>/dev/null | head -1 || true)
eval "$(xdotool getwindowgeometry --shell "$FXW" 2>/dev/null || true)" # sets X, Y, WIDTH, HEIGHT
CX=$(( ${X:-114} + LOAD_X_OFF )); CY=$(( ${Y:-100} + LOAD_Y_OFF ))
echo "run-reaper-params[$FORMAT]: DISPLAY=$DISPLAY  FXW=${FXW:-none}  click 'Load mGB' at ($CX,$CY)"
# Under `reaper:all` (jobs=4) everything is slower and LVGL's indev polling misses more presses, so be
# more patient here than the standalone case needs: RP_PARAMS_CLICK_TRIES raises the attempt count.
loaded_sz=0
for attempt in $(seq 1 "${RP_PARAMS_CLICK_TRIES:-12}"); do
  xdotool mousemove "$CX" "$CY" 2>/dev/null || true
  sleep 0.4
  xdotool mousedown 1 2>/dev/null || true; sleep 0.4; xdotool mouseup 1 2>/dev/null || true
  for _ in $(seq 1 16); do
    sleep 0.5
    loaded_sz=$(wc -c < "$SNAP" 2>/dev/null || echo 0)
    { [ "$loaded_sz" -gt 0 ] && [ "$loaded_sz" -le "$GRID_MAX_BYTES" ]; } && break
  done
  { [ "$loaded_sz" -gt 0 ] && [ "$loaded_sz" -le "$GRID_MAX_BYTES" ]; } && break
  echo "run-reaper-params[$FORMAT]: load-click attempt $attempt didn't register (snapshot ${loaded_sz}B), retrying…"
done

touch "$SIGDIR/rp-params-loaded"
for _ in $(seq 1 120); do [ -f "$SIGDIR/rp-params-done" ] && break; sleep 0.5; done

reaper_env_down
trap - EXIT INT TERM

cp -f "$OUT" "$REPO_DIR/build/reaper-params-$FORMAT.txt" 2>/dev/null || true

if [ ! -s "$OUT" ]; then
  echo "SKIP: no parameter dump written (plugin never loaded?) — see $SIGDIR/reaper.log" >&2
  exit 2
fi

count_of() { grep -m1 "^#$1	count=" "$OUT" | sed 's/.*count=//'; }
has()      { grep -qP "^$1\t[0-9]+\t\Q$2\E$" "$OUT"; }

FX_LOADED=$(grep -m1 '^#fx	' "$OUT" | cut -f2-)
CB=$(count_of before); CA=$(count_of after)
echo "run-reaper-params[$FORMAT]: fx='$FX_LOADED' parameter count before=$CB after=$CA (dump: build/reaper-params-$FORMAT.txt)"

case "$FX_LOADED" in
  "$FX_TOKEN"*) ;;
  *) echo "SKIP: Reaper loaded '$FX_LOADED', not a $FX_TOKEN plugin — this run proves nothing about $FORMAT." >&2
     exit 2 ;;
esac

if [ -z "${CA:-}" ]; then
  echo "SKIP: the post-load dump is missing — mGB was probably never click-loaded (snapshot ${loaded_sz}B)." >&2
  echo "      Adjust RP_LOAD_X_OFF / RP_LOAD_Y_OFF; result is inconclusive." >&2
  exit 2
fi
if ! has before "$POOL_NAME"; then
  echo "SKIP: the pre-load dump has no unclaimed pool slot named '$POOL_NAME' — the harness is not" >&2
  echo "      looking at the parameter pool it thinks it is; result is inconclusive." >&2
  exit 2
fi
# The click-retry loop above gives up after 6 attempts and signals the ReaScript regardless, so the
# post-load dump exists even when mGB never loaded. Without this the run reports FAIL ("the names did
# not change") for what is really a setup failure — the names were never asked to change.
if ! { [ "$loaded_sz" -gt 0 ] && [ "$loaded_sz" -le "$GRID_MAX_BYTES" ]; }; then
  echo "SKIP: could not click-load mGB (the editor snapshot is still the menu at ${loaded_sz}B, grid is" >&2
  echo "      <= ${GRID_MAX_BYTES}B). Adjust RP_LOAD_X_OFF / RP_LOAD_Y_OFF; result is inconclusive." >&2
  exit 2
fi

fail=0
if [ "$CB" != "$CA" ]; then
  echo "FAIL: the parameter COUNT changed ($CB -> $CA). The pool must be fixed for the instance's life;" >&2
  echo "      a moving count orphans host automation and is exactly what the design forbids." >&2
  fail=1
fi
if has before "$MGB_NAME"; then
  echo "FAIL: '$MGB_NAME' was already present BEFORE mGB was loaded — the check proves nothing." >&2
  fail=1
fi
if ! has after "$MGB_NAME"; then
  echo "FAIL: the host still does not report '$MGB_NAME' after mGB was loaded." >&2
  echo "      Either the plugin never asked for a re-read, or this host ignores the flag it raised." >&2
  fail=1
fi
if has after "$POOL_NAME"; then
  echo "FAIL: '$POOL_NAME' is still there after the load — the slot was not re-labelled in place." >&2
  fail=1
fi

if [ "$fail" = "0" ]; then
  echo "PASS: the host re-read the parameter info — the CC slots are now mGB's map, count unchanged at $CA."
  exit 0
fi
exit 1
