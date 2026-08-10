// The LSDj HD player renderer - LSDj's song / chain / phrase screens drawn side by side on one large
// tile grid, in the cartridge's own font and palette. Ported from the old C++ LsdjCanvas / LsdjUi.
//
// `canvas` is the tile blitter, `render` the layout, `tiles` the glyph vocabulary. All pure: give it a
// decoded Song plus an LsdjState from ../runtime and it fills an XRGB8888 buffer.

export { LsdjHdCanvas, TILE_WIDTH, TILE_HEIGHT } from "./canvas";
export { ColorSets, FontTiles, findTile, findNumberTile, formatNote, getCommandTile } from "./tiles";
export { renderMode2, renderSongData, renderChainData, renderPhraseData, HD_COLS, HD_ROWS } from "./render";
export type { KitSampleNameLookup } from "./render";
