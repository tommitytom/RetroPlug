// The LSDj HD player renderer (src/lsdj/hd): the glyph vocabulary, the tile canvas, and the renderMode2
// layout ported from the old C++ LsdjUi. Pure - synthetic songs and runtime states, a synthetic font and
// palette, no emulator and no ROM on disk (the same tier as rom.test.ts).
//
// Assertions read the tile grid back as text rather than comparing an opaque golden blob, so a layout
// regression names the cell and the string it found.
import { test, expect } from "../../testing/harness";
import { LsdjHdCanvas, HD_COLS, HD_ROWS, ColorSets, FontTiles, findTile, formatNote, getCommandTile, renderMode2 } from "../../src/lsdj/hd";
import { FONT_GLYPH_COUNT } from "../../src/lsdj/hd/tiles";
import { SongSchema, type Song } from "../../src/lsdj/model";
import { CHANNELS, type LsdjState, type LsdjChannelState } from "../../src/lsdj/runtime";
import type { RomColorSet, RomFontTile } from "../../src/lsdj/rom/types";

// ---- synthetic assets ------------------------------------------------------------

// 71 tiles, using the shades a REAL LSDj font uses: 0 for background, 3 for the glyph body, 1 for the
// occasional anti-aliased pixel. Shade 2 never appears on a real cartridge (dumping lsdj9_4_2.gb gives a
// per-font histogram of 0/1/2/3 = 3075/182/0/1287), so a synthetic font that leans on 2 would hide the
// fold that turns 3 into the foreground colour.
function testFont(): RomFontTile[] {
  const tiles: RomFontTile[] = [];
  for (let t = 0; t < FONT_GLYPH_COUNT; t++) {
    // A 4x4 block of body pixels in the tile's top-left, one shade-1 pixel, rest background.
    const px = new Array(64).fill(0);
    for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) px[y * 8 + x] = 3;
    px[63] = 1;
    tiles.push(px);
  }
  return tiles;
}

// 5 colour-sets with distinguishable colours 0 and 3 (the pair LSDj actually uses).
function testPalette(): RomColorSet[] {
  const sets: RomColorSet[] = [];
  for (let s = 0; s < 5; s++) {
    sets.push({
      colors: [
        { r: s * 10, g: 0, b: 0 },
        { r: 0, g: 0, b: 0 },
        { r: 0, g: 0, b: 0 },
        { r: 0, g: s * 10, b: 0 },
      ],
    });
  }
  return sets;
}

function makeCanvas(cols = HD_COLS, rows = HD_ROWS): LsdjHdCanvas {
  const c = new LsdjHdCanvas(cols, rows);
  c.setFont(testFont());
  c.setPalette(testPalette());
  return c;
}

// ---- reading the grid back -------------------------------------------------------

// Reverse of FontTiles: the character each glyph index spells, for the glyphs findTile can produce.
const GLYPH_CHARS = (() => {
  const m = new Map<number, string>();
  m.set(FontTiles.Space, " ");
  m.set(FontTiles.ArrowRight, ">");
  m.set(FontTiles.Note, "♪");
  for (let i = 0; i < 10; i++) m.set(FontTiles.Num0 + i, String(i));
  for (let i = 0; i < 26; i++) m.set(FontTiles.A + i, String.fromCharCode(0x41 + i));
  m.set(FontTiles.Dash, "-");
  m.set(FontTiles.Hash, "#");
  m.set(FontTiles.Period, ".");
  m.set(FontTiles.Slash, "/");
  return m;
})();

/** Read `len` cells at (x, y) back as a string. Cells holding a solid fill or a blank read as "_". */
function readText(c: LsdjHdCanvas, x: number, y: number, len: number): string {
  const tiles = c.snapshotTiles();
  let out = "";
  for (let i = 0; i < len; i++) {
    const id = tiles[y * c.cols + x + i];
    out += id < FONT_GLYPH_COUNT * 10 ? GLYPH_CHARS.get(id % FONT_GLYPH_COUNT) ?? "?" : "_";
  }
  return out;
}

/** The colour-set a glyph cell was drawn in (0..4), or -1 for a fill/blank cell. */
function colorSetAt(c: LsdjHdCanvas, x: number, y: number): number {
  const id = c.snapshotTiles()[y * c.cols + x];
  return id < FONT_GLYPH_COUNT * 10 ? Math.floor(id / FONT_GLYPH_COUNT) % 5 : -1;
}

// ---- synthetic song / state ------------------------------------------------------

function emptySong(): Song {
  const song = SongSchema.parse({});
  // A real cart parks unused bookmark slots at 0xFF (every golden decode is all-255); the schema default
  // of 0 would read as "row 0 is bookmarked on every channel" and shade the top row.
  song.bookmarks = new Array(song.bookmarks.length).fill(0xff);
  return song;
}

/** A state with `channel` playing at the given position - enough for the renderer to resolve its chain. */
function playingOn(ch: number, over: Partial<LsdjChannelState> = {}): LsdjState {
  const channels = { pu1: channel(), pu2: channel(), wav: channel(), noi: channel() };
  channels[CHANNELS[ch]] = channel({ playing: true, songRow: 0, chainRow: 0, phraseRow: 0, ...over });
  return makeState({ playing: true, channels });
}

function channel(over: Partial<LsdjChannelState> = {}): LsdjChannelState {
  return { playing: false, phrase: null, phraseRow: null, chain: null, chainRow: null, songRow: null, ...over };
}

function makeState(over: Partial<LsdjState> = {}): LsdjState {
  return {
    supported: true,
    version: null,
    playing: false,
    channels: { pu1: channel(), pu2: channel(), wav: channel(), noi: channel() },
    songRow: null,
    screen: "song",
    cursor: null,
    tempo: 128,
    ...over,
  };
}

const noKits: (kit: number, sample: number) => string = () => "SMP";

// ---- glyph vocabulary ------------------------------------------------------------

test("findTile maps digits, letters and the three punctuation glyphs; others blank", () => {
  expect(findTile(0x30)).toBe(FontTiles.Num0);
  expect(findTile(0x39)).toBe(FontTiles.Num9);
  expect(findTile(0x41)).toBe(FontTiles.A);
  expect(findTile(0x5a)).toBe(FontTiles.Z);
  expect(findTile(0x2d)).toBe(FontTiles.Dash);
  expect(findTile(0x2e)).toBe(FontTiles.Period);
  expect(findTile(0x2f)).toBe(FontTiles.Slash);
  expect(findTile(0x40)).toBe(FontTiles.Space); // '@' - not in the font
});

test("formatNote spells notes as letter + sharp + octave, and 0 as ---", () => {
  const out: FontTiles[] = [FontTiles.Space, FontTiles.Space, FontTiles.Space];

  formatNote(0, out);
  expect(out).toEqual([FontTiles.Dash, FontTiles.Dash, FontTiles.Dash]);

  formatNote(1, out); // first note = C, octave 3
  expect(out).toEqual([FontTiles.C, FontTiles.Space, FontTiles.Num3]);

  formatNote(2, out); // C#3
  expect(out).toEqual([FontTiles.C, FontTiles.Hash, FontTiles.Num3]);

  formatNote(12, out); // B3 - top of the first octave
  expect(out).toEqual([FontTiles.B, FontTiles.Space, FontTiles.Num3]);

  formatNote(13, out); // C4 - octave rolls over
  expect(out).toEqual([FontTiles.C, FontTiles.Space, FontTiles.Num4]);
});

test("getCommandTile maps each command letter, and None to a dash", () => {
  expect(getCommandTile("None")).toBe(FontTiles.Dash);
  expect(getCommandTile("A")).toBe(FontTiles.A);
  expect(getCommandTile("O")).toBe(FontTiles.O);
  expect(getCommandTile("Z")).toBe(FontTiles.Z);
});

// ---- the canvas ------------------------------------------------------------------

test("canvas: text, hexNumber and number land on the right cells in the right colour set", () => {
  const c = makeCanvas(20, 6);

  c.text(2, 1, "SONG", ColorSets.Normal);
  expect(readText(c, 2, 1, 4)).toBe("SONG");
  expect(colorSetAt(c, 2, 1)).toBe(ColorSets.Normal);

  c.text(2, 2, "song", ColorSets.Alternate); // lower case folds up
  expect(readText(c, 2, 2, 4)).toBe("SONG");
  expect(colorSetAt(c, 2, 2)).toBe(ColorSets.Alternate);

  c.hexNumber(0, 3, 0x3a, ColorSets.Shaded);
  expect(readText(c, 0, 3, 2)).toBe("3A");
  expect(colorSetAt(c, 0, 3)).toBe(ColorSets.Shaded);

  // Unpadded: one digit for anything below 0x10, INCLUDING 0x0F (the original padded that one, an
  // off-by-one that numbered the last chain step "0F" in a column of single digits).
  c.hexNumber(0, 4, 0x0c, ColorSets.Normal, false);
  expect(readText(c, 0, 4, 2)).toBe("C_");
  c.hexNumber(4, 4, 0x0f, ColorSets.Normal, false);
  expect(readText(c, 4, 4, 2)).toBe("F_");
  c.hexNumber(8, 4, 0x10, ColorSets.Normal, false); // needs both digits
  expect(readText(c, 8, 4, 2)).toBe("10");

  c.number(0, 5, 7, ColorSets.Normal);
  expect(readText(c, 0, 5, 3)).toBe("007");
});

test("canvas: translation offsets draws, and untranslate restores the previous origin", () => {
  const c = makeCanvas(20, 6);

  c.translate(3, 1);
  c.text(0, 0, "AB", ColorSets.Normal);
  expect(readText(c, 3, 1, 2)).toBe("AB");

  c.translate(2, 1); // nests on top of the first
  c.text(0, 0, "CD", ColorSets.Normal);
  expect(readText(c, 5, 2, 2)).toBe("CD");

  c.untranslate();
  c.text(0, 2, "EF", ColorSets.Normal);
  expect(readText(c, 3, 3, 2)).toBe("EF");

  c.untranslate();
  c.text(0, 4, "GH", ColorSets.Normal);
  expect(readText(c, 0, 4, 2)).toBe("GH");
});

test("canvas: draws outside the grid are dropped, not wrapped onto the next row", () => {
  const c = makeCanvas(8, 4);
  c.text(6, 0, "ABCD", ColorSets.Normal); // runs two tiles past the right edge
  expect(readText(c, 6, 0, 2)).toBe("AB");
  expect(readText(c, 0, 1, 4)).toBe("____"); // row 1 untouched - no wrap
});

test("canvas: flush repaints everything once, then only what changed", () => {
  const c = makeCanvas(10, 4);
  c.text(0, 0, "AB", ColorSets.Normal);

  // First flush has to paint every cell - the atlas was just built.
  expect(c.flush()).toBe(40);
  expect(c.flush()).toBe(0); // nothing moved

  c.text(0, 0, "AC", ColorSets.Normal); // one glyph differs
  expect(c.flush()).toBe(1);

  // Rebuilding the atlas invalidates the painted surface, so the next flush is full again.
  c.setPalette(testPalette());
  expect(c.flush()).toBe(40);
});

test("canvas: a glyph's body (shade 3) paints in the colour-set's FOREGROUND, not the background", () => {
  // The regression that made the whole view render as solid bars: LSDj spells a glyph body as shade 3,
  // and without folding that to 2 it lands on colorForPixel's `default:` branch = the background colour.
  // Normal-coloured text then vanishes into the background and every other colour-set becomes a block.
  const c = makeCanvas(1, 1);
  c.drawTile(0, 0, FontTiles.A, ColorSets.Selection);
  c.flush();

  const px = c.getPixels();
  expect(px[0] >>> 0).toBe(0xff001e00); // body pixel = colour 3 of set 3, the foreground
  expect(px[4] >>> 0).toBe(0xff1e0000); // background pixel = colour 0 of set 3
  expect(px[63] >>> 0).toBe(0xff0f0f00); // the shade-1 pixel = their blend

  // And the background colour a fill() paints must actually differ from the glyph body, or text drawn in
  // that same colour-set would be invisible.
  expect(px[0] !== px[4]).toBeTruthy();
});

test("canvas: pixels come out as opaque XRGB8888 words, blending the middle shade", () => {
  // Palette set 3 has colour 0 = rgb(30,0,0) and colour 3 = rgb(0,30,0). LSDj only stores that pair and
  // derives the shade between them, which is what the pixel values map to: 0 → colour 0, 1 → their
  // average, 2 → colour 3. Value 3 is unused by the font and falls back to colour 0.
  const c = makeCanvas(4, 1);
  c.fill(0, 0, 1, 1, ColorSets.Selection, 0);
  c.fill(1, 0, 1, 1, ColorSets.Selection, 1);
  c.fill(2, 0, 1, 1, ColorSets.Selection, 2);
  c.fill(3, 0, 1, 1, ColorSets.Selection, 3);
  c.flush();

  const px = c.getPixels();
  expect(px[0] >>> 0).toBe(0xff1e0000);
  expect(px[8] >>> 0).toBe(0xff0f0f00); // (30+0)/2, (0+30)/2
  expect(px[16] >>> 0).toBe(0xff001e00);
  expect(px[24] >>> 0).toBe(0xff1e0000);
});

// ---- the layout ------------------------------------------------------------------

test("renderMode2: draws the section headers and per-channel labels", () => {
  const c = makeCanvas();
  renderMode2(c, emptySong(), makeState(), noKits);

  expect(readText(c, 0, 0, 4)).toBe("SONG");

  // Chain blocks: one per channel, stacked 18 rows apart at x=17, each labelled with its channel.
  const names = ["PU1", "PU2", "WAV", "NOI"];
  for (let i = 0; i < 4; i++) {
    expect(readText(c, 17, i * 18, 5)).toBe("CHAIN");
    expect(readText(c, 17 + 9, i * 18, 3)).toBe(names[i]);
  }

  // Phrase columns: one per channel, 17 tiles apart starting at x=32.
  for (let i = 0; i < 4; i++) {
    const x = 32 + 17 * i;
    expect(readText(c, x, 0, 6)).toBe("PHRASE");
    expect(readText(c, x + 11, 0, 3)).toBe(names[i]);
  }
});

test("renderMode2: song grid shows chain indices, dashes for empty rows, and the row numbers", () => {
  const song = emptySong();
  song.rows[0].chains[0] = 0x05;
  song.rows[1].chains[2] = 0x1f;

  const c = makeCanvas();
  renderMode2(c, song, makeState(), noKits);

  // Row numbers run down the left in the Alternate colour set, starting at y=2.
  expect(readText(c, 0, 2, 2)).toBe("00");
  expect(readText(c, 0, 12, 2)).toBe("0A");
  expect(colorSetAt(c, 0, 2)).toBe(ColorSets.Alternate);

  // The grid is translated by (2,2); each channel column is 3 tiles wide with the index at +1.
  expect(readText(c, 2 + 1, 2, 2)).toBe("05"); // channel 0, song row 0
  expect(readText(c, 2 + 7, 3, 2)).toBe("1F"); // channel 2, song row 1
  expect(readText(c, 2 + 4, 2, 2)).toBe("--"); // channel 1 is empty
});

test("renderMode2: the playing channel gets an arrow and its cursor cell is highlighted", () => {
  const song = emptySong();
  song.rows[3].chains[1] = 0x11;

  const c = makeCanvas();
  renderMode2(
    c,
    song,
    makeState({
      playing: true,
      channels: {
        pu1: channel(),
        pu2: channel({ playing: true, songRow: 3, chainRow: 0, phraseRow: 0 }),
        wav: channel(),
        noi: channel(),
      },
      screen: "song",
      cursor: { col: 1, row: 3 },
    }),
    noKits,
  );

  // Arrow in channel 1's column at song row 3 (grid origin (2,2), column stride 3).
  expect(readText(c, 2 + 3, 2 + 3, 1)).toBe(">");
  expect(readText(c, 2 + 4, 2 + 3, 2)).toBe("11");
  // The cursor is on that cell, so it draws in the Selection set rather than Normal.
  expect(colorSetAt(c, 2 + 4, 2 + 3)).toBe(ColorSets.Selection);
});

test("renderMode2: the cursor only highlights while the cart is on the song screen", () => {
  const song = emptySong();
  const c = makeCanvas();
  renderMode2(c, song, makeState({ screen: "phrase", cursor: { col: 0, row: 0 } }), noKits);
  expect(colorSetAt(c, 2 + 1, 2)).toBe(ColorSets.Normal);
});

test("renderMode2: chain step numbers are single digits all the way to F", () => {
  const c = makeCanvas();
  renderMode2(c, emptySong(), makeState(), noKits);
  // The step column for channel 0's chain block sits at x = CHAIN_OFFSET_X, y = 2.
  expect(readText(c, 17, 2 + 14, 2)).toBe("E_");
  expect(readText(c, 17, 2 + 15, 2)).toBe("F_"); // was "0F" - the original's pad off-by-one
});

test("renderMode2: an all-zero bookmark block is uninitialised, not 16 bookmarks on row 0", () => {
  const song = emptySong();
  for (let ch = 0; ch < 4; ch++) song.rows[0].chains[ch] = 0x20 + ch;

  // The shape 58 songs in the corpus have: channel 0 padded with 0xFF, the rest left zeroed. Reading the
  // zeros literally shaded row 0 of channels 1-3 on songs where LSDj itself shows nothing.
  for (let i = 0; i < 16; i++) song.bookmarks[i] = 0xff;
  for (let i = 16; i < 64; i++) song.bookmarks[i] = 0x00;

  const c = makeCanvas();
  renderMode2(c, song, makeState(), noKits);
  for (let ch = 0; ch < 4; ch++) expect(colorSetAt(c, 2 + ch * 3 + 1, 2)).toBe(ColorSets.Normal);
});

test("renderMode2: a real bookmark still shades its row, including one on row 0", () => {
  const song = emptySong();
  for (let ch = 0; ch < 4; ch++) {
    song.rows[0].chains[ch] = 0x20 + ch;
    song.rows[5].chains[ch] = 0x30 + ch;
  }
  // liblsdj writes one slot per bookmarked row and pads the rest with 0xFF, so a genuine row-0 bookmark
  // reads "00 ff ff ..." - which the all-zero guard above must not swallow.
  song.bookmarks[0] = 0x00;
  song.bookmarks[16] = 0x05;

  const c = makeCanvas();
  renderMode2(c, song, makeState(), noKits);
  expect(colorSetAt(c, 2 + 1, 2)).toBe(ColorSets.Shaded); // ch0 row 0 bookmarked
  expect(colorSetAt(c, 2 + 1, 2 + 5)).toBe(ColorSets.Normal); // ch0 row 5 not
  expect(colorSetAt(c, 2 + 3 + 1, 2 + 5)).toBe(ColorSets.Shaded); // ch1 row 5 bookmarked
  expect(colorSetAt(c, 2 + 3 + 1, 2)).toBe(ColorSets.Normal); // ch1 row 0 not
});

test("renderMode2: chain block lists phrase indices and transpositions", () => {
  const song = emptySong();
  song.rows[0].chains[0] = 0x02;
  song.chains[0x02] = { phrases: new Array(16).fill(null), transpositions: new Array(16).fill(0) };
  song.chains[0x02]!.phrases[0] = 0x40;
  song.chains[0x02]!.transpositions[0] = 0x0c;

  const c = makeCanvas();
  renderMode2(c, song, makeState({ channels: { pu1: channel({ playing: true, songRow: 0, chainRow: 0 }), pu2: channel(), wav: channel(), noi: channel() } }), noKits);

  expect(readText(c, 17 + 6, 0, 2)).toBe("02"); // the chain index in the header
  // Body sits at x = 17+1, y = 2; step numbers are in the column at x = 17.
  expect(readText(c, 17, 2, 1)).toBe("0"); // step 0, unpadded - a single digit
  expect(readText(c, 17 + 1, 2, 1)).toBe(">"); // playing this step
  expect(readText(c, 17 + 2, 2, 2)).toBe("40"); // phrase index
  expect(readText(c, 17 + 5, 2, 2)).toBe("0C"); // transposition
  expect(readText(c, 17 + 2, 3, 2)).toBe("--"); // step 1 is empty
});

test("renderMode2: phrase column spells notes, instrument and command value", () => {
  const song = emptySong();
  song.rows[0].chains[0] = 0x00;
  song.chains[0] = { phrases: new Array(16).fill(null), transpositions: new Array(16).fill(0) };
  song.chains[0]!.phrases[0] = 0x07;
  song.phrases[0x07] = {
    notes: new Array(16).fill(0),
    instruments: new Array(16).fill(null),
    commands: new Array(16).fill("None"),
    commandValues: new Array(16).fill(0),
  };
  song.phrases[0x07]!.notes[0] = 2; // C#3
  song.phrases[0x07]!.instruments[0] = 0x03;
  song.phrases[0x07]!.commands[0] = "H";
  song.phrases[0x07]!.commandValues[0] = 0x2b;

  const c = makeCanvas();
  renderMode2(c, song, makeState({ channels: { pu1: channel({ playing: true, songRow: 0, chainRow: 0, phraseRow: 0 }), pu2: channel(), wav: channel(), noi: channel() } }), noKits);

  // Phrase body for channel 0, group 0: origin (32 + 2, 2).
  const x = 34;
  expect(readText(c, x, 2, 1)).toBe(">"); // the playing row
  expect(readText(c, x + 1, 2, 3)).toBe("C#3");
  expect(readText(c, x + 5, 2, 1)).toBe("I");
  expect(readText(c, x + 6, 2, 2)).toBe("03");
  expect(readText(c, x + 9, 2, 1)).toBe("H");
  expect(readText(c, x + 10, 2, 2)).toBe("2B");
});

test("renderMode2: a kit instrument shows both sample names and shifts the later columns right", () => {
  const song = emptySong();
  song.rows[0].chains[0] = 0x00;
  song.chains[0] = { phrases: new Array(16).fill(null), transpositions: new Array(16).fill(0) };
  song.chains[0]!.phrases[0] = 0x00;
  song.phrases[0] = {
    notes: new Array(16).fill(0),
    instruments: new Array(16).fill(null),
    commands: new Array(16).fill("None"),
    commandValues: new Array(16).fill(0),
  };
  song.phrases[0]!.notes[0] = 0x12; // sample 1 of kit1, sample 2 of kit2
  song.phrases[0]!.instruments[0] = 0x00;
  song.instruments[0] = { type: "kit", name: "", panning: "LeftRight", kit1: 1, kit2: 2 } as never;

  const kitName = (kit: number, sample: number) => `K${kit}${sample}`;

  const c = makeCanvas();
  renderMode2(c, song, playingOn(0), kitName);

  const x = 34;
  expect(readText(c, x + 1, 2, 3)).toBe("K10"); // kit1, sample index 0
  expect(readText(c, x + 4, 2, 3)).toBe("K21"); // kit2, sample index 1
  expect(readText(c, x + 8, 2, 1)).toBe("I"); // shifted right by 3
  expect(readText(c, x + 9, 2, 2)).toBe("00");
});

test("renderMode2: an O command draws L/R panning rather than a hex value", () => {
  const song = emptySong();
  song.rows[0].chains[0] = 0x00;
  song.chains[0] = { phrases: new Array(16).fill(null), transpositions: new Array(16).fill(0) };
  song.chains[0]!.phrases[0] = 0x00;
  song.phrases[0] = {
    notes: new Array(16).fill(0),
    instruments: new Array(16).fill(null),
    commands: new Array(16).fill("None"),
    commandValues: new Array(16).fill(0),
  };
  song.phrases[0]!.commands[0] = "O";
  song.phrases[0]!.commandValues[0] = 0;
  song.phrases[0]!.commands[1] = "O";
  song.phrases[0]!.commandValues[1] = 1;

  const c = makeCanvas();
  renderMode2(c, song, playingOn(0), noKits);

  const x = 34;
  expect(readText(c, x + 9, 2, 1)).toBe("O");
  expect(readText(c, x + 10, 2, 2)).toBe("LR");
  expect(readText(c, x + 10, 3, 2)).toBe("L_"); // left only
});

test("renderMode2: the grid is wide enough that nothing is clipped, incl. kit rows in the last column", () => {
  // The original was hard-coded to 97 columns and lost the command value off the right edge of the 4th
  // channel's phrase column whenever that row held a kit instrument. Drive exactly that case: a kit
  // instrument on every channel, so all four phrase columns render at their widest.
  const song = emptySong();
  song.instruments[0] = { type: "kit", name: "", panning: "LeftRight", kit1: 1, kit2: 2 } as never;
  song.phrases[0] = {
    notes: new Array(16).fill(0x12),
    instruments: new Array(16).fill(0),
    commands: new Array(16).fill("H"),
    commandValues: new Array(16).fill(0xab),
  };
  song.chains[0] = { phrases: new Array(16).fill(0), transpositions: new Array(16).fill(0) };
  for (let ch = 0; ch < 4; ch++) song.rows[0].chains[ch] = 0;

  const channels = { pu1: channel(), pu2: channel(), wav: channel(), noi: channel() };
  for (const name of CHANNELS) channels[name] = channel({ playing: true, songRow: 0, chainRow: 0, phraseRow: 0 });

  const c = makeCanvas();
  c.resetDropped();
  renderMode2(c, song, makeState({ playing: true, channels }), () => "SMP");
  expect(c.droppedTiles).toBe(0);

  // And the last column's command value really is on screen, at the far right.
  const lastColumnX = 32 + 17 * 3 + 2;
  expect(readText(c, lastColumnX + 12, 2, 2)).toBe("AB");
  expect(lastColumnX + 13 < HD_COLS).toBeTruthy();
});

test("renderMode2: a too-narrow grid reports the clipping rather than silently losing tiles", () => {
  // The guard on the guard: droppedTiles must actually count, or the test above proves nothing.
  const c = new LsdjHdCanvas(HD_COLS - 4, HD_ROWS);
  c.setFont(testFont());
  c.setPalette(testPalette());
  c.resetDropped();
  renderMode2(c, emptySong(), makeState(), noKits);
  expect(c.droppedTiles > 0).toBeTruthy();
});

test("renderMode2: advancing one playback row repaints only a handful of tiles", () => {
  const song = emptySong();
  song.rows[0].chains[0] = 0x00;
  song.chains[0] = { phrases: new Array(16).fill(null), transpositions: new Array(16).fill(0) };
  song.chains[0]!.phrases[0] = 0x00;
  song.phrases[0] = {
    notes: new Array(16).fill(0),
    instruments: new Array(16).fill(null),
    commands: new Array(16).fill("None"),
    commandValues: new Array(16).fill(0),
  };

  const playing = (phraseRow: number): LsdjState =>
    makeState({
      playing: true,
      channels: { pu1: channel({ playing: true, songRow: 0, chainRow: 0, phraseRow }), pu2: channel(), wav: channel(), noi: channel() },
    });

  const c = makeCanvas();
  renderMode2(c, song, playing(0), noKits);
  expect(c.flush()).toBe(HD_COLS * HD_ROWS); // first frame paints the whole surface

  renderMode2(c, song, playing(1), noKits);
  // Only the arrow moves: one tile cleared, one tile drawn.
  expect(c.flush()).toBe(2);
});
