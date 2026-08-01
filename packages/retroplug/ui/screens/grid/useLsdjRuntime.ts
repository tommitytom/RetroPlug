// Live LSDj runtime state for one system, sampled per frame. Builds an LsdjReader from the ROM header
// once (version identification), then on every "frame" tick pulls the per-block WRAM snapshot
// (backend.readRam — the emulator-facet seam, safe while the core plays) and decodes it. Re-renders
// only when the DISPLAYED state actually changes, so a 60fps frame tick doesn't thrash React.
import { useEffect, useMemo, useRef, useState } from "react";

import { useStores } from "../../stores/useStores";
import { useSystems } from "../../stores/useStores";
import { useNativeEvent } from "../../lvgl/useNativeEvent";
import { LsdjReader, type LsdjState } from "../../../src/lsdj/runtime";

// A compact signature of EXACTLY the fields LsdjOverlay renders — used to skip no-op re-renders. It shows
// the screen + cursor position, play-stop, song row, tempo, and each channel's playing phrase.row, so the
// signature covers those. It still EXCLUDES chainRow (not displayed). The displayed phraseRow/cursor DO
// tick every playback row — re-rendering then is correct (the readout genuinely changed); the dedupe still
// suppresses frames where nothing shown moved (e.g. paused, or navigating a static screen).
function signature(s: LsdjState): string {
  const ch = (c: LsdjState["channels"]["pu1"]) => `${c.playing ? 1 : 0}:${c.phrase}:${c.phraseRow}`;
  return [
    s.supported ? 1 : 0,
    s.playing ? 1 : 0,
    s.screen,
    s.songRow,
    s.tempo,
    s.cursor ? `${s.cursor.col},${s.cursor.row}` : "-",
    ch(s.channels.pu1),
    ch(s.channels.pu2),
    ch(s.channels.wav),
    ch(s.channels.noi),
  ].join("|");
}

/** The live LsdjState for `systemId`, or null when the system isn't a supported LSDj ROM. `enabled` gates
 *  the per-frame WRAM read + decode: when false (the overlay is hidden) it does no work and yields null. */
export function useLsdjRuntime(systemId: number, enabled = true): LsdjState | null {
  const { backend } = useStores();
  const systems = useSystems();
  const sys = systems.find((s) => s.id === systemId);
  const romPath = sys?.romPath ?? "";
  const isLsdj = !!sys?.roles?.some((r) => r.kind === "lsdj-sync");

  // Build the reader once per (rom, backend, enabled): reads the header prefix, identifies the version,
  // resolves the WRAM layout. Null when disabled, not a file-backed LSDj ROM, or the version isn't supported.
  const reader = useMemo(() => {
    if (!enabled || !isLsdj || !romPath) return null;
    const header = backend.readFilePrefix(romPath, 0x150);
    if (!header) return null;
    const r = LsdjReader.fromHeader(header);
    return r.supported ? r : null;
  }, [enabled, isLsdj, romPath, backend]);

  const [state, setState] = useState<LsdjState | null>(null);
  const sigRef = useRef<string>("");

  // Reset when the reader identity changes (rom swapped / no longer LSDj).
  useEffect(() => {
    setState(null);
    sigRef.current = "";
  }, [reader]);

  useNativeEvent("frame", () => {
    if (!reader) return;
    const wram = backend.readRam(systemId);
    if (!wram) return;
    const next = reader.read(wram);
    const sig = signature(next);
    if (sig === sigRef.current) return; // nothing the overlay shows changed → no re-render
    sigRef.current = sig;
    setState(next);
  });

  return reader ? state : null;
}
