// Plain interfaces for the risa ROM asset views. Like ../../lsdj/rom/types.ts there is no zod SSOT — the
// RisaRom view reads these out and patches bytes in place. Theme roles are risa's own "0xNN" strings (the
// form its .rit files + the inline .rplg override use), so a .rit round-trips verbatim.

import type { ThemeRole } from "./constants";

/** A risa theme: a 4-char name + one NES-palette-index (0x00..0x3F) per role, as "0xNN" strings. */
export type RisaTheme = { name: string } & Record<ThemeRole, string>;

/** One font slot's identity in the ROM (CHR is position-deterministic — no name table, just a slot index). */
export interface RisaFontSlot {
  slot: number;
}
