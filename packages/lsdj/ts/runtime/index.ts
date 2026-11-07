// LSDj runtime-WRAM reader: identify an LSDj ROM by title, resolve its per-version WRAM offset layout,
// and decode a live GB WRAM snapshot into typed playback/UI state (is-playing, per-channel song/chain/
// phrase position, active screen, cursor, tempo). Sibling to ../codec (the saved-song codec); this is
// the transient runtime state. See offsets.ts for the address provenance (LSDisJ + old ecs table).
export * from "./types";
export { romTitle, isLsdjTitle, parseLsdjVersion, identifyLsdj } from "./identify";
export { layoutForVersion } from "./offsets";
export { resolveLayout } from "./layout";
export { LsdjReader, readLsdjState, decodeLsdjState, screenFromByte } from "./reader";
