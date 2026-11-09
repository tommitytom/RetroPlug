// The shapes the old platform tables took, which the walk must see: a table keyed by an identifier, a pattern reading a
// ROM extension's tail, a node type, a core id, an enum member and a template that builds `rom.gb` at its end.
export const CORE_BY_PLATFORM = {
  gb: "sameboy",
  nes: "mesen",
};
export enum Systems {
  gb = 1,
  colour = 2,
}
export const ROM_PATTERN = /\.(gb|gbc)$/i;
export const NODE_TYPE = "rp.sameboy.gb";
export const CORE = "sameboy";
export const ROM_NAME = `rom.gb`;
// What it must not see: a comment naming "gb" and ".nes", a property ACCESS of a table it already read, an unrelated key,
// a pattern with no escaped dot, and a pattern too permissive to mean a ROM (it matches the extension's bare letters).
export const core = CORE_BY_PLATFORM.gb;
export const MENU = { colour: "red", speed: 3 };
export const ANY_TEXT = /^[\x20-\x7e]*$/;
export const PACKAGE = /\.rplg$/;
