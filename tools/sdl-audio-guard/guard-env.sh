#!/usr/bin/env bash
#
# Source this from a harness that could reach a real sound card: it builds the /dev/snd guard from the tracked source
# beside it into the caller's scratch directory and puts it in FRONT of LD_PRELOAD, so every /dev/snd node but the
# sequencer and the timer is refused before the syscall (nosnd.c says why). A caller that already has the guard in
# LD_PRELOAD keeps its own: the verification kit prepends one for the whole run, and loading a second copy would only
# count every audio-library dlopen twice.
#
# Usage, after `TMP="$(mktemp -d)"`:
#     . "$REPO_DIR/tools/sdl-audio-guard/guard-env.sh" "$TMP/guard"
#     export NOSND_LOG="${NOSND_LOG:-$TMP/nosnd.log}"      (optional: the log of what the run refused and loaded)
#
# REPO_DIR must be set by the caller.
GUARD_DIR="${1:?guard-env.sh needs a scratch directory for the built guard}"
GUARD="$("$REPO_DIR/tools/sdl-audio-guard/build-guard.sh" "$GUARD_DIR")" || exit 1
case "${LD_PRELOAD:-}" in
    *nosnd.so*) : ;;  # already guarded (the kit's copy, or an outer harness's)
    *) export LD_PRELOAD="$GUARD${LD_PRELOAD:+:$LD_PRELOAD}" ;;
esac
