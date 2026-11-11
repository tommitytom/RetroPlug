#!/usr/bin/env bash
#
# The audio backends' product wiring, checked on the binaries themselves (docs/modular-graph-phase-f5b-design.md
# section 5.7(4)). A product names its audio backends in AUDIO_BACKENDS, and a product that names none must link neither
# PortAudio nor SDL audio: the backends are static libraries, so the proof is that the binary carries neither backend's
# FACTORY symbol and no audio library dependency.
#
#   retroplug-cli, retroplug-host, build/node/retroplug.node,   no rp_portaudio_backend / rp_sdl_backend symbol, no
#   retroplug-ui-test, retroplug-render-host-test              portaudio/SDL-audio/pulse/jack/pipewire DT_NEEDED (or
#                                                              otool entry)
#   retroplug-sdl                                              names `AUDIO_BACKENDS portaudio sdl`, so both factories and
#                                                              the Pa_* symbols are expected; PortAudio must not be a
#                                                              DYNAMIC dependency (the static library is linked in),
#                                                              and this script prints the counts so a build that named
#                                                              one backend shows zero for the other
#
# The factory symbols are the check, not SDL2: a product that links NO audio backend may still need libSDL2 for its
# window, so a DT_NEEDED on SDL2 says nothing either way - the two factories do.
#
# A binary with an EMPTY symbol table proves nothing (stripped, or a static archive that dropped what the script greps
# for), so that fails rather than passing vacuously.
#
# Run by `pnpm check:audio-linkage` and the verification kit (the `audiolink` leg). Linux reads DT_NEEDED with readelf
# and symbols with nm; macOS uses otool -L and nm -gU (a framework's install name is ".../SDL2.framework/...", not
# "libSDL2"). Anything else reports SKIPPED (CI builds these two platforms).
#
# Usage: tools/check-audio-linkage.sh [binary ...]     (defaults: the four above, under build/)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINS=("$@")
if [ "${#BINS[@]}" -eq 0 ]; then
    BINS=(
        "$ROOT/build/bin/retroplug-cli"
        "$ROOT/build/bin/retroplug-host"
        "$ROOT/build/node/retroplug.node"
        "$ROOT/build/bin/retroplug-ui-test"
        "$ROOT/build/bin/retroplug-render-host-test"
        "$ROOT/build/bin/retroplug-sdl"
    )
fi

case "$(uname -s)" in
    Linux)
        deps() { readelf -d "$1" 2>/dev/null; }
        syms() { nm -g "$1" 2>/dev/null || nm -D "$1" 2>/dev/null; } ;;
    Darwin)
        deps() { otool -L "$1" 2>/dev/null; }
        syms() { nm -gU "$1" 2>/dev/null; } ;;
    *) echo "audio linkage: SKIPPED (no dt-needed/otool reader for $(uname -s))"; exit 0 ;;
esac

FAIL=0
for bin in "${BINS[@]}"; do
    if [ ! -f "$bin" ]; then
        echo "  FAIL: $bin is missing (the kit builds it first)"; FAIL=1; continue
    fi
    name=$(basename "$bin")
    needs=$(deps "$bin")
    table=$(syms "$bin")
    if [ -z "$table" ]; then
        echo "  FAIL: $name has an empty symbol table: nothing here is proved"; FAIL=1; continue
    fi
    pa_symbols=$(printf '%s\n' "$table" | grep -cE ' Pa_[A-Za-z]' || true)
    # Each factory on its own: a build that named one backend must show zero for the OTHER, which a combined count hides.
    pa_factory=$(printf '%s\n' "$table" | grep -c 'rp_portaudio_backend' || true)
    sdl_factory=$(printf '%s\n' "$table" | grep -c 'rp_sdl_backend' || true)
    factories=$((pa_factory + sdl_factory))
    audio_dep=$(printf '%s\n' "$needs" | grep -ciE 'portaudio|libpulse|libjack|libpipewire' || true)
    case "$name" in
        retroplug-sdl)
            sdl_dep=$(printf '%s\n' "$needs" | grep -ciE 'libSDL2|SDL2\.framework' || true)
            if [ "$audio_dep" -eq 0 ] && [ "$sdl_dep" -gt 0 ] && [ "$pa_factory" -gt 0 ] && [ "$sdl_factory" -gt 0 ]; then
                echo "  ok: $name keeps its backends static (SDL2 dependency only, $pa_factory portaudio and"
                echo "      $sdl_factory sdl factory symbol(s), $pa_symbols Pa_* symbol(s) linked in)"
            else
                echo "  FAIL: $name links an audio library dynamically, has no SDL2 dependency, or names no backend"
                printf '%s\n' "$needs" | grep -iE 'portaudio|pulse|jack|pipewire' >&2
                FAIL=1
            fi ;;
        *)
            if [ "$audio_dep" -eq 0 ] && [ "$factories" -eq 0 ] && [ "$pa_symbols" -eq 0 ]; then
                echo "  ok: $name names no audio backend (no factory symbol, no Pa_*, no audio dependency)"
            else
                echo "  FAIL: $name names an audio backend ($factories factory symbol(s), $pa_symbols Pa_*)" >&2
                printf '%s\n' "$needs" | grep -iE 'portaudio|pulse|jack|pipewire' >&2
                FAIL=1
            fi ;;
    esac
done

if [ "$FAIL" -eq 0 ]; then echo "audio linkage clean"; else echo "audio linkage FAILED"; fi
[ "$FAIL" -eq 0 ]
