// Resolve a risa version to its internal-RAM offset layout. cc65 BSS/ZP addresses are deterministic per
// (source, toolchain, config) but shuffle across builds, so the layout is authored per risa version from
// that build's labels (symbols.generated.ts). A version with no snapshot resolves to null → the reader
// reports `supported: false` rather than reading wrong addresses.
import { RISA_SYMBOLS } from "./symbols.generated";
import type { RisaLayout } from "./types";

// Some risa releases share an identical internal-RAM layout — cc65 didn't move the BSS/ZP variables the
// reader tracks between those builds — so rather than duplicate a symbol table (and need that version's
// label file), alias the version to the one whose snapshot we have. 2.2.0 → 2.2.1 is verified empirically:
// decoding a live 2.2.0 core with the 2.2.1 addresses yields a coherent state — seq_mode, tempo, screen,
// and per-track playback positions all decode and advance correctly (test-native/risa-220-layout).
const VERSION_ALIASES: Record<string, string> = { "2.2.0": "2.2.1" };

// `version` is the ROM's real version (what a RisaState reports); `symbolsKey` is the snapshot the
// addresses come from — the same as `version` unless it was resolved through VERSION_ALIASES.
function layoutFrom(symbolsKey: string, version: string = symbolsKey): RisaLayout {
  const s = RISA_SYMBOLS[symbolsKey];
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

/** The layout for `version` (e.g. "2.2.1"), or null if that version has neither a committed symbol
 *  snapshot nor an alias to one. An aliased version keeps its own label but borrows the addresses. */
export function resolveRisaLayout(version: string | null): RisaLayout | null {
  if (!version) return null;
  if (version in RISA_SYMBOLS) return layoutFrom(version);
  const alias = VERSION_ALIASES[version];
  if (alias && alias in RISA_SYMBOLS) return layoutFrom(alias, version);
  return null;
}

/** Every risa version the reader supports — a committed symbol snapshot or an alias to one. */
export function supportedRisaVersions(): string[] {
  return [...Object.keys(RISA_SYMBOLS), ...Object.keys(VERSION_ALIASES)];
}
