// The predicted playback model, against songs whose timing is known by construction.
//
// These are the deterministic half of the model's verification: no ROM, no emulator, no clock - just
// "given this song and this many ticks, where should each channel be?". The other half, whether those
// answers match a REAL cart, is test-native/lsdj-playback-differential.test.ts.
//
// The expected numbers all come from the measured rules in docs/launchpad-plan.md 2.5: 6 ticks/step,
// 96 ticks per 16-step phrase, one row per chain, wrap at the end of the song, chain ends at its first
// empty slot.
import { test, expect } from "../../testing/harness";
import { SongSchema, type Song } from "../../src/lsdj/model";
import { PredictedLsdjModel, phraseTicks, chainPhraseCount } from "../../src/lsdj/playback";

const PHRASE = 96; // 16 steps x 6 ticks at the factory groove

// A song built from a per-row chain table, so each test states its shape directly. `rows` is a list of
// per-channel chain indices; `chains` a list of phrase-slot arrays.
function song(rows: (number | null)[][], chains: (number | null)[][]): Song {
  return SongSchema.parse({
    formatVersion: 22,
    rows: rows.map((chainsForRow) => ({ chains: chainsForRow })),
    chains: chains.map((phrases) => ({ phrases })),
  });
}

// One channel, three rows, one phrase each - the simplest thing that can advance and wrap.
const SIMPLE = () => song([[0], [1], [2]], [[0], [1], [2]]);

test("phraseTicks: the factory 6/6 groove is 96 ticks, and a zero groove falls back rather than freezing", () => {
  expect(phraseTicks([6, 6])).toBe(96);
  expect(phraseTicks([6, 6, 0, 0])).toBe(96); // the non-zero prefix is what cycles
  expect(phraseTicks([3, 3])).toBe(48); // a faster groove halves the phrase
  expect(phraseTicks([4, 8])).toBe(96); // asymmetric grooves sum across the 16 steps
  expect(phraseTicks([])).toBe(96); // all-zero groove would never advance — fall back to 6/step
});

test("chainPhraseCount: a chain ends at its first empty slot", () => {
  expect(chainPhraseCount([0, 1, 2])).toBe(3);
  expect(chainPhraseCount([0, null, 2])).toBe(1); // MEASURED (B6): phrase 2 never plays
  expect(chainPhraseCount([null, 1])).toBe(0); // an empty first slot means nothing playable
  expect(chainPhraseCount([])).toBe(0);
});

test("a launch puts every channel on the row, and counts as the cart's first tick", () => {
  const m = new PredictedLsdjModel(song([[0, 0, 0, 0]], [[0]]));
  expect(m.position().playing).toBe(false); // idle until launched

  m.launch(0);
  const p = m.position();
  expect(p.playing).toBe(true);
  expect(p.channels.length).toBe(4);
  expect(p.channels.every((c) => c.playing && c.songRow === 0)).toBeTruthy();

  // MEASURED: the launch byte is itself the first tick, so a PHRASE-long row has PHRASE-1 ticks left
  // to run. Two short of the boundary the row must not have turned over yet.
  m.advance(PHRASE - 2);
  expect(m.position().channels[0].songRow).toBe(0);
});

test("one row per single-phrase chain, then a wrap at the end of the song", () => {
  const m = new PredictedLsdjModel(SIMPLE());
  m.launch(0);

  m.advance(PHRASE - 1); // the launch already spent one tick of this row
  expect(m.position().channels[0].songRow).toBe(1);
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(2);
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(0); // MEASURED (B3): wraps rather than stopping
});

test("chain length drives row duration: a two-phrase chain holds its row twice as long", () => {
  const m = new PredictedLsdjModel(song([[0], [1]], [[0, 0], [1]]));
  expect(m.rowTicks(0, 0)).toBe(2 * PHRASE);
  expect(m.rowTicks(0, 1)).toBe(PHRASE);

  m.launch(0);
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(0); // still on the long chain
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(1);
});

test("channels advance independently once their chains differ in length", () => {
  // pu1 has a one-phrase chain at row 0, pu2 a two-phrase one — exactly the shape measured in B4.
  const m = new PredictedLsdjModel(song([[0, 1], [2, 3]], [[0], [0, 0], [1], [1]]));
  m.launch(0);

  m.advance(PHRASE);
  const p = m.position();
  expect(p.channels[0].songRow).toBe(1); // pu1's short chain ended
  expect(p.channels[1].songRow).toBe(0); // pu2's long chain has not
});

test("a chain that ends at an empty slot is timed by the phrases that actually play", () => {
  // MEASURED (B6): chain 0 is [phrase 0, EMPTY, phrase 2] but only phrase 0 sounds, so the row is one
  // phrase long, not three.
  const m = new PredictedLsdjModel(song([[0], [1]], [[0, null, 2], [1]]));
  expect(m.rowTicks(0, 0)).toBe(PHRASE);

  m.launch(0);
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(1);
});

test("an unplayable row ends the song rather than being stepped over", () => {
  // MEASURED (B9): an empty row is the END of the song, not a hole to skip. Row 1's chain starts with an
  // empty slot, so it is unplayable - and the cart loops rows 0..0 forever rather than reaching row 2.
  // B6 saw exactly this on a real cart: pu1's songRow never left 0 across 400 ticks.
  const m = new PredictedLsdjModel(song([[0], [1], [2]], [[0], [null, 1], [2]]));
  expect(m.rowTicks(0, 1)).toBe(null);

  m.launch(0);
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(0); // wrapped to the start, NOT on to row 2
  m.advance(PHRASE * 4);
  expect(m.position().channels[0].songRow).toBe(0); // and stays in that one-row loop
});

test("a gap in the middle of a song loops the first section, and never reaches the second", () => {
  // The shape B9 measured: rows 0-1 populated, 2 empty, 3 populated. The cart wraps at the gap.
  const m = new PredictedLsdjModel(song([[0], [1], [null], [2]], [[0], [1], [2]]));
  m.launch(0);
  m.advance(PHRASE - 1);
  expect(m.position().channels[0].songRow).toBe(1);
  m.advance(PHRASE);
  expect(m.position().channels[0].songRow).toBe(0); // row 2 is the end of the song
  m.advance(PHRASE * 10);
  expect(m.position().channels[0].songRow).toBe(0); // row 3 is unreachable, however long we run
});

test("launching an empty row scans BACK to the nearest playable one", () => {
  // MEASURED (B9): launching an empty row is neither a stop (B8) nor a no-op - the cart lands on the
  // last playable row at or before it. Note this is the OPPOSITE direction to the advance rule above;
  // they really are two different rules on the cart.
  const m = new PredictedLsdjModel(song([[0], [1], [null], [null], [null], [2]], [[0], [1], [2]]));

  m.launch(3);
  expect(m.position().channels[0].songRow).toBe(1); // back to 1, not forward to 5 and not row 0
  expect(m.position().playing).toBe(true);

  m.launch(5);
  expect(m.position().channels[0].songRow).toBe(5); // a populated row still lands where asked
});

test("a single large tick step crosses several rows at once", () => {
  // A slow UI frame or a long audio block hands over many ticks in one go; the model must walk them
  // all rather than dropping the surplus.
  const m = new PredictedLsdjModel(SIMPLE());
  m.launch(0);
  m.advance(PHRASE * 4); // four chains' worth: 0 -> 1 -> 2 -> 0 -> 1
  expect(m.position().channels[0].songRow).toBe(1);
});

test("advancing by the exact chain length lands on the next row, not one tick before it", () => {
  const m = new PredictedLsdjModel(SIMPLE());
  m.launch(0);
  for (let i = 0; i < PHRASE; i++) m.advance(1); // tick-at-a-time must agree with one big step
  expect(m.position().channels[0].songRow).toBe(1);
});

test("stop and reset: stop freezes, reset forgets", () => {
  const m = new PredictedLsdjModel(SIMPLE());
  m.launch(0);
  m.advance(PHRASE);
  m.stop();
  expect(m.position().playing).toBe(false);

  m.advance(PHRASE * 2); // stopped: the clock no longer moves anything
  m.launch(0);
  expect(m.position().channels[0].songRow).toBe(0);

  m.reset();
  expect(m.position().playing).toBe(false);
  expect(m.position().channels[0].songRow).toBe(null);
});

test("the grid reports launchable cells, and is safe at the edges", () => {
  const g = new PredictedLsdjModel(song([[0, null], [null, 1]], [[0], [1]])).grid();
  expect(g.rowCount).toBe(256);
  expect(g.channelCount).toBe(4);
  expect(g.hasContent(0, 0)).toBe(true);
  expect(g.hasContent(1, 0)).toBe(false);
  expect(g.hasContent(1, 1)).toBe(true);
  expect(g.hasContent(0, 5)).toBe(false); // beyond the authored rows
  expect(g.hasContent(-1, 0)).toBe(false); // out of range reads false rather than throwing
  expect(g.hasContent(0, 9999)).toBe(false);
});

test("a channel with nothing to land on stays silent instead of guessing", () => {
  // pu2 has no chain at row 0 and none before it, so there is nowhere for the backward scan to land.
  const m = new PredictedLsdjModel(song([[0, null]], [[0]]));
  m.launch(0);
  const p = m.position();
  expect(p.channels[0].playing).toBe(true);
  expect(p.channels[1].playing).toBe(false);
  expect(p.channels[1].songRow).toBe(null);
  expect(p.playing).toBe(true); // the cart as a whole is playing
});

test("an out-of-range launch is ignored rather than corrupting the cursors", () => {
  const m = new PredictedLsdjModel(SIMPLE());
  m.launch(0);
  m.launch(999);
  expect(m.position().channels[0].songRow).toBe(0);
});
