// The plain types the NES asset formats are read into. Like the LSDj ROM views
// (packages/retroplug/src/lsdj/rom/types.ts) there is no zod SSOT: a ROM view reads these out and patches bytes in
// place. A theme's roles are "0xNN" strings (the form a .rit file and the inline asset override use), so a .rit
// round-trips verbatim.

import type { ThemeRole } from "./constants";

/** A risa theme: a 4-char name + one NES-palette-index (0x00..0x3F) per role, as "0xNN" strings. */
export type RisaTheme = { name: string } & Record<ThemeRole, string>;

/** One font slot's identity in the ROM (CHR is position-deterministic — no name table, just a slot index). */
export interface RisaFontSlot {
  slot: number;
}
