// Resolve a risa version to its internal-RAM offset layout. cc65 BSS/ZP addresses are deterministic per
// (source, toolchain, config) but shuffle across builds, so the layout is authored per risa version from
// that build's labels (symbols.generated.ts). A version with no snapshot resolves to null → the reader
// reports `supported: false` rather than reading wrong addresses.
import { RISA_SYMBOLS } from "./symbols.generated";
import type { RisaLayout } from "./types";

function layoutFrom(version: string): RisaLayout {
  const s = RISA_SYMBOLS[version];
  return {
    version,
    seqMode: s.seq_mode,
    seqActive: s.seq_active,
    bpm: s.seq_current_bpm,
    songRow: s.bss_song_row,
    songLastRow: s.bss_song_last_row,
    chainId: s.bss_chain_idx,
    chainRow: s.bss_chain_row,
    phraseId: s.bss_phrase_idx,
    phraseLastRow: s.bss_phrase_last_row,
    note: s.apu_current_note,
    lastInst: s.bss_last_inst,
    currentScreen: s.ui_current_screen,
    cursorRow: s.ui_cursor_row,
    cursorCol: s.ui_cursor_col,
    uiTrack: s.ui_track,
    kitActive: s.kit_active_idx,
  };
}

/** The layout for `version` (e.g. "2.2.1"), or null if that version has no committed symbol snapshot. */
export function resolveRisaLayout(version: string | null): RisaLayout | null {
  if (version && version in RISA_SYMBOLS) return layoutFrom(version);
  return null;
}

/** Every risa version we have a symbol snapshot for. */
export function supportedRisaVersions(): string[] {
  return Object.keys(RISA_SYMBOLS);
}
