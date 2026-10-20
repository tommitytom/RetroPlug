// Live risa runtime state for one system, sampled per frame. Resolves the internal-RAM offset layout
// once from the ROM's version (identifyRisaVersion needs the whole PRG — the "RISA V" marker is deep in
// it — so it reads the full ROM, not just the header), then on every "frame" tick pulls the per-block
// RAM snapshot (backend.readRam — safe while the core plays) and decodes it. Re-renders only when the
// DISPLAYED state changes, so the 60fps tick doesn't thrash React. Mirrors useLsdjRuntime.
import { useMemo } from "react";

import { useStores, useSystem } from "../../stores/useStores";
import { useFrameSampled } from "../../lvgl/useFramePoll";
import { hasRole } from "../../../src/systemRoles";
import { runtime } from "../../../src/risa";
import type { RisaState } from "../../../src/risa/runtime";

// A compact signature of exactly the fields RisaOverlay renders — skip no-op re-renders. Covers screen +
// cursor, play/mode, tempo, and each track's playing phrase.row (excludes chain/song rows, not shown).
function signature(s: RisaState): string {
  const tr = (t: RisaState["tracks"][number]) => `${t.active ? 1 : 0}:${t.phraseId}:${t.phraseRow}`;
  return [
    s.supported ? 1 : 0,
    s.playing ? 1 : 0,
    s.mode,
    s.screen,
    s.bpm,
    s.fourX ? 1 : 0,
    s.cursor ? `${s.cursor.col},${s.cursor.row}` : "-",
    ...s.tracks.map(tr),
  ].join("|");
}

/** The live RisaState for `systemId`, or null when the system isn't a supported risa ROM. `enabled` gates
 *  the per-frame RAM read + decode: when false (overlay hidden) it does no work and yields null. */
export function useRisaRuntime(systemId: number, enabled = true): RisaState | null {
  const { backend } = useStores();
  const sys = useSystem(systemId);
  const romPath = sys?.romPath ?? "";
  const isRisa = hasRole(sys?.roles, "risa");

  // Resolve the layout once per (rom, backend, enabled): read the ROM, identify the version, resolve its
  // symbol snapshot. Null when disabled, not a file-backed risa ROM, or the version isn't supported.
  const sample = useMemo(() => {
    if (!enabled || !isRisa || !romPath) return null;
    const rom = backend.readFile(romPath);
    if (!rom) return null;
    const layout = runtime.resolveRisaLayout(runtime.identifyRisaVersion(rom));
    if (!layout) return null;
    return (): RisaState | undefined => {
      const ram = backend.readRam(systemId);
      return ram ? runtime.decodeRisaState(ram, layout) : undefined;
    };
  }, [enabled, isRisa, romPath, systemId, backend]);

  return useFrameSampled(sample, signature);
}
