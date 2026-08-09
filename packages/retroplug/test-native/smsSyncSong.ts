// A minimal smsggdj SMDJ4 battery image carrying a one-hit-per-beat metronome, for the host-sync
// guards and the real-Reaper drift render. The SMS twin of risaSyncSong.ts, and test-only for the same
// reason: SMS has no Songs menu yet, so there is nothing in `src/` that would justify a shipping codec.
//
// Format reference: smsggdj's SAVEFORMAT.md, with tools/smdj4.js as the executable oracle (its own
// self-test prints ALL PASS). Every default written here is the ROM's, read off src/engine.asm's
// `song_new` (:4271) and src/editor.asm's `instr_default` (:3638) rather than copied out of a real
// song, so the fixture stays readable and has no vendored blob behind it.
//
// Why a metronome: the drift analyzer pairs transients to clicks, so every beat has to be one distinct
// onset. One note every 4 phrase rows gives exactly that, because IN24 divides the 24-PPQN stream by 6
// and lands 4 rows per quarter note.

// --- SMDJ4 song-block geometry (SAVEFORMAT.md "The 6,912-byte song block") ---
const BLOCK_LEN = 6912;
const P_WAVE = 0; // 256   8 stamp presets, untouched here: a TONE instrument never reads them
const P_PHRASES = 256; // 3328  52 phrases x 64 B (16 steps x note,instr,cmd,param)
const P_CHAINS = 3584; // 1280  40 chains x 32 B (16 x phrase#,transpose)
const P_SONG = 4864; // 512   128 rows x 4 chain numbers (one per channel: T1 T2 T3 N)
const P_INSTR = 5376; // 256   16 instruments x 16 B
const P_TABLES = 5632; // 1024  256 rows x 4
const P_GROOVES = 6656; // 256

const NUM_PHRASES = 52;
const NUM_CHAINS = 40;
const SONG_ROWS = 128;
const STEPS_PER_PHRASE = 16;
const CHAIN_ENTRIES = 16;

// The ROM's own new-song defaults. An EMPTY phrase step is `00 FF 00 00` - note 0 is a real note
// (A-2), so it is the $FF INSTRUMENT that marks a step as silent, not the note byte.
const EMPTY_STEP = [0x00, 0xff, 0x00, 0x00];
// instr_default (editor.asm:3638): TONE, vol $0F, ATK 0 / HLD 1 / DCY 3. The two envelope bytes are
// overridden below - see INSTR_DCY.
const INSTR_DEFAULT = [0, 0x0f, 0x03, 0x01, 0, 0, 0, 0, 0, 0xff, 1, 0, 0, 0, 0, 0];
// A table row is `$FF 00 00 00` - $FF meaning "no volume change".
const EMPTY_TABLE_ROW = [0xff, 0x00, 0x00, 0x00];

/** IN24 divides 24 PPQN by 6, so four phrase rows make a quarter note. */
export const SMS_ROWS_PER_BEAT = 4;

// The metronome note. Mid-range so it is unambiguous against silence in an onset detector.
const HIT_NOTE = 0x0d;
const HIT_INSTR = 0x00; // instrument 0, left at instr_default

// The metronome instrument. instr_default sustains (HLD 1), so consecutive hits merge into a
// continuous tone rather than discrete beats - measured as a steady 0.072 RMS yielding 2 detected
// onsets in 3 s where 6 were expected. HLD 0 is what separates them.
//
// The envelope byte layout was worked out from the manual rather than guessed: an instrument record is
// [type, VOL, (ATK << 4) | DCY, HLD, ...], which the `E` command corroborates (MANUAL.md:366 - "Exy
// sets ATK = x, DCY = y"). instr_default's `0, $0F, $03, $01` reads as TONE / VOL F / ATK 0 DCY 3 /
// HLD 1, matching its own comment exactly.
//
// DCY 2 was chosen by measurement, not by the manual's labelling. The manual calls ATK 0 / HLD 0 /
// DCY 3 "a pluck", but at this beat spacing DCY 3 still decays too slowly to separate - it measured a
// single onset, same as the sustaining default. Sweeping DCY with HLD 0 gave 10 clean, evenly-spaced
// onsets at 0, 1 and 2, and DCY 2 has the highest level of those (0.059 RMS against 0.029 at DCY 0),
// so it is both separable and the easiest for an onset detector to find above the noise floor.
const INSTR_ATK = 0;
const INSTR_DCY = 2;
const INSTR_HLD = 0;

// --- SMDJ4 .sav geometry (SAVEFORMAT.md "SMDJ4: compressed directory + heap") ---
const MAGIC4 = [0x53, 0x4d, 0x44, 0x4a, 0x34]; // "SMDJ4"
const SUPER = 32;
const DIR_ENTRIES = 32;
const DIR_ENTRY = 32;
const HEAP_OFF = SUPER + DIR_ENTRIES * DIR_ENTRY; // $0420 = 1056
const CART_BYTES = 32 * 1024; // smsggdj's cart is 32 KB of SRAM across two 16 KB banks

/** OPTIONS config block offset for a 16/32 KB image (CPU $BF60 with SRAM bank 0 mapped). */
const CFG_OFF = 0x3f60;

/** smsggdj sync modes (engine.asm:88-94). Only the two this fixture uses. */
export const SMS_SYNC_OFF = 0;
export const SMS_SYNC_IN24 = 5;

/** 16-bit little-endian sum, matching the ROM's `sram_sum`. */
function checksum16(block: Uint8Array): number {
  let s = 0;
  for (let i = 0; i < block.length; i++) s = (s + block[i]) & 0xffff;
  return s;
}

function fillPattern(dst: Uint8Array, start: number, count: number, pattern: number[]): void {
  for (let i = 0; i < count; i++) dst.set(pattern, start + i * pattern.length);
}

/** A blank song block, byte-for-byte what the ROM's `song_new` produces (minus the wave presets,
 *  which only a wavetable instrument reads). */
function blankSongBlock(): Uint8Array {
  const b = new Uint8Array(BLOCK_LEN); // $00-filled, which is already right for wave + grooves
  fillPattern(b, P_PHRASES, NUM_PHRASES * STEPS_PER_PHRASE, EMPTY_STEP);
  b.fill(0xff, P_CHAINS, P_CHAINS + NUM_CHAINS * CHAIN_ENTRIES * 2); // chains + song are $FF
  b.fill(0xff, P_SONG, P_SONG + SONG_ROWS * 4);
  for (let i = 0; i < 16; i++) {
    b.set(INSTR_DEFAULT, P_INSTR + i * 16);
    b[P_INSTR + i * 16 + 2] = (INSTR_ATK << 4) | INSTR_DCY; // percussive: see INSTR_DCY
    b[P_INSTR + i * 16 + 3] = INSTR_HLD;
  }
  fillPattern(b, P_TABLES, 256, EMPTY_TABLE_ROW);
  void P_WAVE;
  void P_GROOVES;
  return b;
}

/** The metronome song block: one hit every `SMS_ROWS_PER_BEAT` rows on channel 0, running long enough
 *  that no render outlasts it. */
export function buildMetronomeBlock(): Uint8Array {
  const b = blankSongBlock();

  // Phrase 0: a note on every 4th step, the rest left empty.
  for (let step = 0; step < STEPS_PER_PHRASE; step += SMS_ROWS_PER_BEAT) {
    b.set([HIT_NOTE, HIT_INSTR, 0x00, 0x00], P_PHRASES + step * 4);
  }

  // Chain 0: phrase 0, sixteen times over (phrase#, transpose).
  for (let i = 0; i < CHAIN_ENTRIES; i++) b.set([0x00, 0x00], P_CHAINS + i * 2);

  // Song rows 0..7 run chain 0 on channel 0 and nothing on the other three. Eight rows x 16 phrases x
  // 16 steps is ~512 beats, so the pattern outlives any render here whether or not the song loops.
  for (let row = 0; row < 8; row++) b.set([0x00, 0xff, 0xff, 0xff], P_SONG + row * 4);

  return b;
}

/** The 7-byte v1 OPTIONS block: `'C' 'F' pal_sel sync_mode vid_sel fm_on checksum`, checksum being the
 *  sum of the four value bytes. SAVEFORMAT.md documents a 10-byte v3 form too, but the ROM's loader
 *  tries each length in turn and v1 is what tools/smdj4.js writes, so it is the smallest thing the ROM
 *  definitely accepts.
 *
 *  `fmOn` defaults OFF deliberately: smsggdj writes $F2 = $01 when FM is on, and Mesen models $F2 as a
 *  mux whose PSG branch memsets the buffer, so an FM-on fixture would render silence no matter how
 *  well the sync worked. */
export function buildConfigBlock(syncMode: number, fmOn = 0): Uint8Array {
  const palSel = 0;
  const vidSel = 0; // AUTO
  const sum = (palSel + syncMode + vidSel + fmOn) & 0xff;
  return Uint8Array.from([0x43, 0x46, palSel, syncMode, vidSel, fmOn, sum]);
}

/** Wrap one song block in an SMDJ4 battery image: superblock, a single store-raw directory entry, and
 *  the blob at the heap base.
 *
 *  Store-raw (`raw = 1`, the blob being the verbatim 6,912 B) is what lets this skip an RLE codec
 *  entirely. It costs heap space no fixture cares about, and SAVEFORMAT.md lists it as a first-class
 *  directory state rather than a fallback. The bank-0 cap that `tools/smdj4.js buildSav` enforces
 *  ($3F60, so a blob may not grow into the config block) cannot bite here: 1056 + 6912 is well under
 *  it, and there is only ever one song. */
export function buildSmdj4Sav(block: Uint8Array, config: Uint8Array): Uint8Array {
  if (block.length !== BLOCK_LEN) throw new Error(`song block must be ${BLOCK_LEN} bytes, got ${block.length}`);
  const sav = new Uint8Array(CART_BYTES);
  sav.set(MAGIC4, 0);
  sav[5] = 1; // format version
  sav[6] = DIR_ENTRIES;
  sav.set(config.subarray(0, 7), CFG_OFF);

  const cs = checksum16(block);
  const e = SUPER; // directory entry 0
  sav[e] = 0xa5; // valid
  sav[e + 1] = 1; // store-raw
  sav[e + 2] = 0; // heap offset (relative to HEAP_OFF), LE
  sav[e + 3] = 0;
  sav[e + 4] = BLOCK_LEN & 0xff; // blob length, LE
  sav[e + 5] = (BLOCK_LEN >> 8) & 0xff;
  sav[e + 6] = cs & 0xff; // checksum of the DECOMPRESSED block, LE
  sav[e + 7] = (cs >> 8) & 0xff;
  sav.set(block, HEAP_OFF);
  return sav;
}

/** The fixture: a metronome song in slot 0, with the ROM configured for `syncMode`.
 *
 *  Guard A passes SMS_SYNC_OFF so the ROM plays on its own, which proves the image is valid
 *  independently of any sync path. Guard B and the Reaper render pass SMS_SYNC_IN24. */
export function buildSmsMetronomeSav(syncMode: number = SMS_SYNC_IN24): Uint8Array {
  return buildSmdj4Sav(buildMetronomeBlock(), buildConfigBlock(syncMode));
}

// --- putting the song into the RUNNING core ---------------------------------

/** SMS work RAM is mapped at CPU $C000. */
const WRAM_BASE = 0xc000;

/** Minimal write seam: `writeCpu` on a real backend. Kept structural so a test can pass a spy. */
export interface PokeCaps {
  writeCpu(id: number, addr: number, value: number): boolean;
}

/** Write the metronome into the RUNNING core's working song.
 *
 *  Necessary because smsggdj deliberately does NOT autoload a save: `song_new` (engine.asm:4271) boots
 *  a blank song, and main.asm:238 says why ("no slot-1 autoload for now - a first power-on should make
 *  sound"). So the SRAM image above supplies the OPTIONS block, which `config_load` DOES read at boot,
 *  but its song sits in slot 0 until something loads it. Driving the ROM's file browser blind to do
 *  that would be slow and brittle; poking the working song is neither.
 *
 *  This is exact rather than approximate: smsggdj lays its working song out in WRAM byte-for-byte as
 *  the SMDJ4 save block (verified by locating all three pools by signature - phrases at +$100, chains
 *  and song at +$E00, sixteen instr_default records at +$1500), so a save-block offset IS a WRAM
 *  offset. Only the bytes that differ from what `song_new` already wrote are sent, which is about 50
 *  of them, and diffing the two blocks means this cannot drift from buildMetronomeBlock. */
export function pokeMetronomeIntoWram(caps: PokeCaps, id: number): number {
  const blank = blankSongBlock();
  const want = buildMetronomeBlock();
  let writes = 0;
  for (let i = 0; i < BLOCK_LEN; i++) {
    if (blank[i] === want[i]) continue;
    if (!caps.writeCpu(id, WRAM_BASE + i, want[i])) throw new Error(`writeCpu failed at WRAM +0x${i.toString(16)}`);
    writes++;
  }
  return writes;
}
