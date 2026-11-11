#!/usr/bin/env bash
#
# Source this from a harness that could reach real hardware: it builds the device guard from the tracked source beside
# it into the caller's scratch directory, puts it in FRONT of LD_PRELOAD, and PROVES it is live (nosnd.c says what it
# refuses and why). A caller that already has the guard in LD_PRELOAD keeps its own: the verification kit prepends one
# for the whole run, and loading a second copy would only count every audio-library dlopen twice.
#
# The self-test is not ceremony. ld.so treats an unloadable preload as non-fatal ("cannot be preloaded ... ignored") and
# runs the program anyway, so a guard built for the wrong ELF class, sitting in a noexec directory or truncated would
# leave the whole run unguarded with nothing said. The probe opens a path the guard refuses; it does not exist, so an
# unguarded probe fails harmlessly at the syscall, and it runs with a scratch NOSND_LOG so the probe's own line is never
# counted in the run's.
#
# Linux only: there is no /dev/snd, /dev/video or /dev/ttyACM to guard on macOS or Windows, so there the guard is
# skipped with a notice and the harness runs as it did before.
#
# Usage, after `TMP="$(mktemp -d)"`:
#     . "$REPO_DIR/tools/sdl-audio-guard/guard-env.sh" "$TMP/guard"
#     export NOSND_LOG="${NOSND_LOG:-$TMP/nosnd.log}"      (the log of what the run refused and loaded)
#
# REPO_DIR must be set by the caller.
GUARD_DIR="${1:?guard-env.sh needs a scratch directory for the built guard}"
case "$(uname -s)" in
    Linux) : ;;
    *)
        echo "notice: the device guard is a Linux facility; this $(uname -s) run is unguarded (there is no" \
             "/dev/snd, /dev/video or /dev/ttyACM to reach)" >&2
        return 0 2>/dev/null || exit 0
        ;;
esac
GUARD="$("$REPO_DIR/tools/sdl-audio-guard/build-guard.sh" "$GUARD_DIR")" || exit 1
case "${LD_PRELOAD:-}" in
    *nosnd.so*) : ;;  # already guarded (the kit's copy, or an outer harness's)
    *) export LD_PRELOAD="$GUARD${LD_PRELOAD:+:$LD_PRELOAD}" ;;
esac
mkdir -p "$GUARD_DIR"
SELFTEST_LOG="$GUARD_DIR/selftest.log"
: >"$SELFTEST_LOG"
NOSND_LOG="$SELFTEST_LOG" sh -c ': </dev/snd/nosnd-selftest' 2>/dev/null || true
grep -q ' refused /dev/snd/nosnd-selftest$' "$SELFTEST_LOG" || {
    echo "error: the device guard is not active (LD_PRELOAD='${LD_PRELOAD:-}'): a /dev/snd open reached the syscall" \
         "instead of being refused. Nothing this harness runs may be trusted to keep real hardware closed." >&2
    exit 1
}
