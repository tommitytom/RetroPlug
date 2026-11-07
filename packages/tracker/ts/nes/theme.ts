// risa theme codec — a faithful port of risa's tools/rom_patcher/src/theme/{codec.js,palette.js}. A theme
// is NOT RGB: it's 7 named roles, each a 6-bit index (0x00..0x3F) into the fixed 64-entry NES master
// palette, plus a 4-char ASCII name. The on-ROM form is a 7-byte record + a 4-byte name; the interchange
// / inline-override form is readable JSON with "0xNN" role strings (the .rit shape — no base64).

import { ROLES, THEME_NAME_SIZE, THEME_RECORD_SIZE, THEME_VERSION, NES_PALETTE } from "./constants";
import type { RisaTheme } from "./types";

/** Parse a palette value (number, "0xNN", or decimal string) to a 6-bit index. */
export function parsePaletteByte(value: unknown): number {
  if (typeof value === "number" && Number.isFinite(value)) return value & 0x3f;
  const raw = String(value ?? "").trim();
  if (/^0x[0-9a-f]+$/i.test(raw)) return parseInt(raw, 16) & 0x3f;
  if (/^[0-9]+$/.test(raw)) return parseInt(raw, 10) & 0x3f;
  return 0;
}

/** Format a palette value as "0xNN" (uppercase, 2-digit). */
export function formatPaletteByte(value: unknown): string {
  return `0x${parsePaletteByte(value).toString(16).toUpperCase().padStart(2, "0")}`;
}

/** The #rrggbb color for a role value (for a UI preview). */
export function colorFor(value: unknown): string {
  return NES_PALETTE[parsePaletteByte(value) & 0x3f];
}

/** Coerce a loose theme object into a canonical RisaTheme: 4-char name, every role a "0xNN" string, with
 *  risa's fallbacks (cursor defaults to alternate; selection defaults to cursor ?? alternate). */
export function normalizeTheme(theme: Partial<Record<string, unknown>> | null | undefined): RisaTheme {
  const src = theme ?? {};
  const out = {
    name: String(src.name ?? "").slice(0, THEME_NAME_SIZE).padEnd(THEME_NAME_SIZE, " "),
  } as RisaTheme;
  for (const role of ROLES) {
    const value =
      role === "cursor" && src[role] == null
        ? src.alternate
        : role === "selection" && src[role] == null
          ? (src.cursor ?? src.alternate)
          : src[role];
    out[role] = formatPaletteByte(value);
  }
  return out;
}

/** Encode a theme's 7 role indices to the on-ROM record bytes. */
export function encodeThemeRecord(theme: RisaTheme): Uint8Array {
  const bytes = new Uint8Array(THEME_RECORD_SIZE);
  ROLES.forEach((role, i) => {
    bytes[i] = parsePaletteByte(theme[role]);
  });
  return bytes;
}

/** Encode a theme's 4-char name to the on-ROM name bytes (ASCII, & 0x7F, zero-padded). */
export function encodeThemeName(theme: RisaTheme): Uint8Array {
  const out = new Uint8Array(THEME_NAME_SIZE);
  const raw = String(theme.name ?? "");
  for (let i = 0; i < THEME_NAME_SIZE; i++) out[i] = i < raw.length ? raw.charCodeAt(i) & 0x7f : 0;
  return out;
}

/** Decode an on-ROM theme (7-byte record + 4-byte name) into a canonical RisaTheme. */
export function decodeThemeFromRom(recordBytes: Uint8Array, nameBytes: Uint8Array): RisaTheme {
  let name = "";
  for (let i = 0; i < THEME_NAME_SIZE; i++) name += String.fromCharCode(nameBytes[i] || 0x20);
  const theme: Record<string, unknown> = { name };
  ROLES.forEach((role, i) => {
    theme[role] = formatPaletteByte(recordBytes[i] || 0);
  });
  return normalizeTheme(theme);
}

/** Validation errors for a theme (empty = valid): 4 ASCII name chars, each role a 0x00..0x3F index. */
export function validateTheme(theme: Partial<Record<string, unknown>> | null | undefined, label = "theme"): string[] {
  const errors: string[] = [];
  const name = String(theme?.name ?? "");
  if (name.length !== THEME_NAME_SIZE) errors.push(`${label}: name must be ${THEME_NAME_SIZE} chars`);
  if (!/^[\x20-\x7e]*$/.test(name)) errors.push(`${label}: name must be ASCII`);
  for (const role of ROLES) {
    const raw = String(theme?.[role] ?? "").trim();
    if (!/^(0x[0-9a-f]{1,2}|[0-9]{1,2})$/i.test(raw)) errors.push(`${label}: invalid ${role}`);
    else if (parsePaletteByte(raw) > 0x3f) errors.push(`${label}: ${role} out of range`);
  }
  return errors;
}

/** Parse a `.rit` file JSON ({version, theme:{...}}) into a normalized theme. Throws on a malformed shape. */
export function parseRit(json: unknown): { version: number; theme: RisaTheme } {
  if (!json || typeof json !== "object" || Array.isArray(json)) throw new Error("invalid .rit: expected object");
  const obj = json as Record<string, unknown>;
  if (!obj.theme || typeof obj.theme !== "object" || Array.isArray(obj.theme)) {
    throw new Error("invalid .rit: missing theme");
  }
  return {
    version: typeof obj.version === "number" ? obj.version : THEME_VERSION,
    theme: normalizeTheme(obj.theme as Record<string, unknown>),
  };
}

/** Serialize a theme to the `.rit` JSON shape ({version, theme:{name, bg, normal, ... as "0xNN"}}). */
export function serializeRit(theme: RisaTheme, version = THEME_VERSION): { version: number; theme: RisaTheme } {
  const out = { name: String(theme.name ?? "").slice(0, THEME_NAME_SIZE).padEnd(THEME_NAME_SIZE, " ") } as RisaTheme;
  for (const role of ROLES) out[role] = formatPaletteByte(theme[role]);
  return { version, theme: out };
}
