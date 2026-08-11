// Experiments B1-B7 from the M0 plan: what does a real LSDj cart actually DO in SYNC=MI.MAP?
//
// The row-level playback predictor (docs/launchpad-plan.md) rests on a handful of semantics that are
// documented nowhere — the aboy build is closed-source and stock LSDj's manual doesn't mention MI.MAP.
// This file measures them against a real cart and PRINTS the findings; assertions here guard the
// instrument (did the cart boot, did anything move at all), not the semantics being discovered.
// Locking a semantic in as an assertion happens in lsdj-playback-differential.test.ts, once the model
// exists to be held to it.
//
// Run: pnpm test:native lsdj-playback-probe
import { test, expect } from "../testing/harness";
import { type SavInput } from "../src/lsdjSav";
import { CHANNELS } from "../src/lsdj/runtime";
import { LsdjProbe, MAP_CLOCK, MAP_NOTEOFF, transitions, changeTicks, gaps, fmtSample, type ProbeSample } from "./lsdjPlaybackProbe";

const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;

// Notes on every step so a stepping channel is audibly + visibly active.
const phrase = (note: number, inst: number) => ({
  notes: Array.from({ length: 16 }, () => note),
  instruments: Array.from({ length: 16 }, () => inst),
});

// A song built so that POSITION IS IDENTIFIABLE from the readout alone:
//   row 0 -> chains 0,1,2,3   (chain 1 is TWO phrases long, the rest are one)
//   row 1 -> chains 4,5,6,7   (all one phrase)
//   row 2 -> chains 8,9,10,11 (all one phrase)
// Distinct chain numbers per row+channel mean the decoded `chain` field alone says where we are, and
// the deliberately long chain 1 is how B4 (do channels diverge?) gets a channel that lags the others.
const SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap", tempo: 128 },
    rows: [
      { chains: [0, 1, 2, 3] },
      { chains: [4, 5, 6, 7] },
      { chains: [8, 9, 10, 11] },
    ],
    chains: [
      { phrases: [0] }, { phrases: [1, 1] }, { phrases: [2] }, { phrases: [3] },
      { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
      { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
    ],
    phrases: [phrase(1, 0), phrase(2, 1), phrase(3, 2), phrase(4, 3)],
    instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
  },
};

// B6 needs a chain with a HOLE (slot 0 and slot 2 filled, slot 1 empty) to see whether LSDj stops at
// the gap or skips it, and one whose phrases start at a later slot.
const GAP_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap", tempo: 128 },
    rows: [{ chains: [0] }, { chains: [1] }],
    chains: [
      { phrases: [0, null, 2] }, // hole at slot 1
      { phrases: [null, 1] },    // starts at slot 1
    ],
    phrases: [phrase(1, 0), phrase(2, 0), phrase(3, 0)],
    instruments: [pulse],
  },
};

// B9 needs a song with a HOLE IN THE MIDDLE, so that "where does an empty-row launch land" has four
// distinguishable answers. Rows 0-1 and 5-6 hold chains, rows 2-4 are empty; launching row 3 then lands
// on row 1 (scan back), row 5 (scan forward), row 6 (last populated) or row 0 (wrap to the start), and
// the decoded chain number says which without ambiguity.
const SPARSE_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiMap", tempo: 128 },
    rows: [
      { chains: [0, 1, 2, 3] },
      { chains: [4, 5, 6, 7] },
      { chains: [null, null, null, null] },
      { chains: [null, null, null, null] },
      { chains: [null, null, null, null] },
      { chains: [8, 9, 10, 11] },
      { chains: [12, 13, 14, 15] },
    ],
    chains: [
      { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
      { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
      { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
      { phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] },
    ],
    phrases: [phrase(1, 0), phrase(2, 1), phrase(3, 2), phrase(4, 3)],
    instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
  },
};

/** MEASURED by B2: exactly 6 clock bytes per phrase step at LSDj's default 6/6 groove, which makes a
 *  24-PPQN clock 4 steps/beat and one 16-step phrase 96 ticks (one bar). */
const TICKS_PER_STEP = 6;
const TICKS_PER_PHRASE = TICKS_PER_STEP * 16;

const skip = (why: string): void => console.log(`# SKIP lsdj-playback-probe: ${why}`);

const dump = (label: string, samples: ProbeSample[], every = 1): void => {
  console.log(`  --- ${label} (${samples.length} samples)`);
  for (let i = 0; i < samples.length; i += every) console.log(`    ${fmtSample(samples[i])}`);
};

// B1 — the shipped role, no clock. Our `midiMap` role sends row bytes and 0xFE but never a clock
// byte, while Arduinoboy's map mode forwards MIDI clock as 0xFF. So: does the cart advance anyway
// (free-running on its own tempo), or does it sit still? This is the evidence behind the "should
// midiMap forward clock?" decision.
test("B1: MI.MAP through the shipped midiMap role, with no clock byte", () => {
  const p = LsdjProbe.create({ song: SONG, mode: "midiMap" });
  if (!p) return skip("aboy ROM not found / unsupported version");

  const before = p.sample();
  p.launchNote(0); // ch1 NoteOn note 0 -> row byte 0
  const after = p.runFree(3000, 50);

  console.log(`[B1] before: ${fmtSample(before)}`);
  dump("B1 after launching row 0, free-running 3s", after, 10);

  const rows = transitions(after, (s) => s.songRow);
  const phraseRows = transitions(after, (s) => s.channels.pu1.phraseRow);
  const played = after.some((s) => s.playing);
  console.log(`[B1] playing-at-any-point=${played} songRow transitions=${JSON.stringify(rows)}`);
  console.log(`[B1] pu1 phraseRow transitions (${phraseRows.length} distinct): ${JSON.stringify(phraseRows.slice(0, 20))}`);
  console.log(`[B1] VERDICT: cart ${phraseRows.length > 2 ? "ADVANCES without a host clock" : "does NOT advance without a host clock"}`);

  expect(before.screen === "song" || before.screen === "unknown").toBeTruthy(); // the instrument booted
});

// B0 — instrument check, and it turned out to be load-bearing. Isolates the two halves of the raw
// path: does a bare row byte through `midiPassthrough` trigger the cart at all, and what does a
// single 0xFF then do to it? If 0xFF is a clock the cart keeps playing; if the cart reads it as just
// another row byte, 0xFF means row 255 (empty) and playback stops dead.
test("B0: does a raw row byte trigger, and what does one 0xFF do to a playing cart?", () => {
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  const afterLaunch = p.launchRaw(0);
  console.log(`[B0] after raw row byte 0x00: ${fmtSample(afterLaunch)}`);

  p.raw(MAP_CLOCK);
  const afterOneClock = p.render(60);
  console.log(`[B0] after one 0xFF:          ${fmtSample(afterOneClock)}`);

  p.raw(MAP_CLOCK);
  const afterTwoClocks = p.render(60);
  console.log(`[B0] after two 0xFF:          ${fmtSample(afterTwoClocks)}`);

  console.log(`[B0] raw row byte triggers playback: ${afterLaunch.playing}`);
  console.log(`[B0] still playing after 0xFF:       ${afterOneClock.playing}`);
  if (afterLaunch.playing && !afterOneClock.playing)
    console.log("[B0] VERDICT: 0xFF is NOT a transparent clock here - it stopped the cart");

  // Does the passthrough path deliver ANYTHING? Send the exact 3-byte NoteOn that triggers the cart
  // through the midiMap role. Passthrough forwards all three bytes verbatim, so LSDj should see
  // row 0x90, row 0x00, row 0x64 - and row 0 has content, so it should sound at least in passing.
  p.raw(0x90);
  p.raw(0x00);
  p.raw(0x64);
  const afterThree = p.render(200);
  console.log(`[B0] after raw 90 00 64:      ${fmtSample(afterThree)}`);

  // And a non-zero row, in case byte 0x00 is special.
  const afterRow1 = p.launchRaw(1, 200);
  console.log(`[B0] after raw row byte 0x01: ${fmtSample(afterRow1)}`);
  console.log(`[B0] passthrough delivers bytes at all: ${afterThree.playing || afterRow1.playing}`);
});

// B2 — one clock byte per tick. Does the cart step at the documented 6 ticks/step of the default
// groove (which would make 24 PPQN = 4 steps/beat)? The gaps between phraseRow changes answer it.
test("B2: ticks per step under an explicit 0xFF clock stream", () => {
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(0); // row 0
  const samples = p.runTicks(240); // 240 ticks = 10 beats at 24 PPQN

  dump("B2 240 clock bytes after launching row 0", samples, 12);

  const stepPoints = changeTicks(samples, (s) => s.channels.pu1.phraseRow);
  const stepGaps = gaps(stepPoints);
  console.log(`[B2] pu1 phraseRow changed at ticks: ${JSON.stringify(stepPoints.slice(0, 24))}`);
  console.log(`[B2] gaps between steps: ${JSON.stringify(stepGaps.slice(0, 24))}`);
  console.log(`[B2] VERDICT: ${stepGaps.length ? `~${stepGaps[Math.floor(stepGaps.length / 2)]} ticks/step` : "never stepped"}`);

  // MEASURED: every gap is exactly 6. This is the predictor's core arithmetic, so hold it.
  expect(stepGaps.length > 10).toBeTruthy();
  expect(stepGaps.every((g) => g === TICKS_PER_STEP)).toBeTruthy();
});

// B3 — THE question for a row-level predictor. Launch row 0 and keep clocking: when the chain ends,
// does the cart move to row 1 on its own, or loop row 0 until told otherwise?
test("B3: does the cart auto-advance past a launched row?", () => {
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(0);
  // One phrase at the default groove is 16 steps x 6 ticks = 96 ticks. Clock well past several
  // chains so an auto-advance has room to happen more than once.
  const samples = p.runTicks(600);

  // Read PER-CHANNEL, not the aggregate `songRow`: that field is the max across channels (the
  // GBPresenter convention in the runtime reader), so once channels diverge it zig-zags and says
  // nothing clean about advance. pu1's own cursor is the honest signal.
  const pu1Rows = transitions(samples, (s) => s.channels.pu1.songRow);
  const pu1RowTicks = changeTicks(samples, (s) => s.channels.pu1.songRow);
  console.log(`[B3] pu1 songRow sequence: ${JSON.stringify(pu1Rows)}`);
  console.log(`[B3] pu1 chain sequence:   ${JSON.stringify(transitions(samples, (s) => s.channels.pu1.chain))}`);
  console.log(`[B3] pu1 row change ticks: ${JSON.stringify(pu1RowTicks)} gaps=${JSON.stringify(gaps(pu1RowTicks))}`);
  console.log(`[B3] aggregate songRow (confounded by divergence): ${JSON.stringify(transitions(samples, (s) => s.songRow))}`);
  console.log(`[B3] VERDICT: ${pu1Rows.length > 1 ? "AUTO-ADVANCES past the launched row, and WRAPS at the end of the song" : "STAYS on the launched row"}`);

  // MEASURED: launching row 0 walks 0 -> 1 -> 2 -> 0 ..., one row per single-phrase chain (96 ticks).
  // This is THE rule the row-level predictor implements, so it is worth holding.
  expect(pu1Rows.length > 3).toBeTruthy();
  expect(pu1Rows[0]).toBe(0);
  expect(pu1Rows[1]).toBe(1);
  expect(gaps(pu1RowTicks).every((g) => g === TICKS_PER_PHRASE)).toBeTruthy();
});

// B4 — is a launch song-wide, and do the four channels then drift apart? Row 0's chain 1 (pu2) is
// twice as long as its neighbours, so if channels advance independently pu2 should fall a row behind.
test("B4: is a launch song-wide, and do channels diverge afterwards?", () => {
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(0);
  const samples = p.runTicks(600);

  for (const ch of CHANNELS) {
    console.log(`[B4] ${ch}: songRow ${JSON.stringify(transitions(samples, (s) => s.channels[ch].songRow))}`);
    console.log(`[B4] ${ch}: chain   ${JSON.stringify(transitions(samples, (s) => s.channels[ch].chain))}`);
  }
  const last = samples[samples.length - 1];
  const rows = CHANNELS.map((c) => last.channels[c].songRow);
  const first = samples[0];
  console.log(`[B4] first sample per-channel songRow: ${JSON.stringify(CHANNELS.map((c) => first.channels[c].songRow))}`);
  console.log(`[B4] final per-channel songRow: ${JSON.stringify(rows)}`);
  console.log(`[B4] VERDICT: channels ${new Set(rows).size > 1 ? "DIVERGE (independent cursors)" : "stay together (one cursor)"}`);

  // MEASURED: a launch sets every channel to the launched row, but pu2's row-0 chain is two phrases
  // long against everyone else's one, so it falls behind and never catches up. Four cursors, not one.
  expect(CHANNELS.every((c) => first.channels[c].songRow === 0)).toBeTruthy();
  expect(new Set(rows).size > 1).toBeTruthy();
});

// B5 — what the Launchpad app's "pad released" actually does to the cart.
test("B5: what does the 0xFE NoteOff handshake do?", () => {
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(0);
  const running = p.runTicks(150);
  p.raw(MAP_NOTEOFF);
  const afterOff = p.runTicks(150);

  const beforeState = running[running.length - 1];
  console.log(`[B5] before 0xFE: ${fmtSample(beforeState)}`);
  dump("B5 after 0xFE", afterOff, 30);
  const stillPlaying = afterOff.filter((s) => s.playing).length;
  const stepped = changeTicks(afterOff, (s) => s.channels.pu1.phraseRow).length;
  console.log(`[B5] samples still 'playing' after 0xFE: ${stillPlaying}/${afterOff.length}; step changes: ${stepped}`);
  console.log(`[B5] pu1 rows after 0xFE: ${JSON.stringify(transitions(afterOff, (s) => s.channels.pu1.songRow))}`);
  console.log(`[B5] VERDICT: 0xFE ${stillPlaying === 0 ? "STOPS playback" : stepped === 0 ? "freezes stepping" : "leaves the cart playing and stepping normally"}`);

  // MEASURED: playback continues untouched through the handshake. So "pad released" is NOT a stop -
  // the Launchpad app cannot rely on it to end a launched row, and needs its own stop affordance.
  expect(stillPlaying).toBe(afterOff.length);
  expect(stepped > 0).toBeTruthy();
});

// B6 — chain duration is the predictor's only real input, so how a chain treats empty slots decides
// the arithmetic: stop at the first hole, or skip it and keep going?
test("B6: how does a chain treat empty phrase slots?", () => {
  const p = LsdjProbe.create({ song: GAP_SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(0); // chain 0 = [phrase 0, EMPTY, phrase 2]
  const samples = p.runTicks(400);

  console.log(`[B6] pu1 phrase sequence:   ${JSON.stringify(transitions(samples, (s) => s.channels.pu1.phrase))}`);
  console.log(`[B6] pu1 chainRow sequence: ${JSON.stringify(transitions(samples, (s) => s.channels.pu1.chainRow))}`);
  console.log(`[B6] pu1 songRow sequence:  ${JSON.stringify(transitions(samples, (s) => s.channels.pu1.songRow))}`);
  console.log(`[B6] playing throughout:    ${samples.every((s) => s.playing)}`);
  const phrases = transitions(samples, (s) => s.channels.pu1.phrase).filter((v) => v !== null);
  console.log(`[B6] VERDICT: chain [0, EMPTY, 2] played phrases ${JSON.stringify(phrases)} - ${phrases.includes(2) ? "SKIPS the hole" : "ENDS at the hole (phrase 2 never plays)"}`);

  // MEASURED: phrase 2 never plays, so a chain ENDS at its first empty slot. That single rule is the
  // predictor's whole notion of chain length: count phrase slots up to the first null.
  expect(phrases.includes(2)).toBe(false);
  expect(phrases[0]).toBe(0);
});

// B7 — rows 254/255 share their byte values with the 0xFE/0xFF sentinels, so they may not be
// launchable at all. That would cap the app's addressable range, which the grid layout needs to know.
test("B7: do rows 254/255 collide with the 0xFE/0xFF sentinels?", () => {
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  console.log(`[B7] MAP_NOTEOFF=0x${MAP_NOTEOFF.toString(16)} MAP_CLOCK=0x${MAP_CLOCK.toString(16)}`);
  console.log("[B7] rows 254/255 are byte-identical to those sentinels, so the addressable launch");
  console.log("[B7] range is at most 0..253 unless the cart distinguishes them by context.");

  // Sanity: a mid-range row still launches, so the range question is specifically about the top two.
  p.launchRaw(1);
  const samples = p.runTicks(120);
  console.log(`[B7] launching row 1 gave chain sequence ${JSON.stringify(transitions(samples, (s) => s.channels.pu1.chain))}`);
  expect(samples.length).toBe(120);
});

// B8 — is there a STOP? MI.MAP has no stop message and 0xFE turned out to be a no-op (B5), so the only
// stop a host has is halting the transport, which is out of reach of a pad. But B4 showed that a channel
// with nothing at the launched row parks silently, which suggests launching an EMPTY row might park all
// four - an effective stop built out of the one gesture the protocol does have. The predictor already
// models it that way (predict.ts launch()), on an assumption nothing had tested. This settles whether the
// Launchpad app can offer a stop pad at all.
test("B8: what does launching an EMPTY song row do?", () => {
  // Part 1 — from a PLAYING cart. Rows 0-2 hold chains; everything from row 3 up is empty. Watching the
  // CHAIN sequence (not just songRow) is what separates the three possible outcomes: a stop, a jump to
  // row 5 followed by a wrap, or the byte being ignored outright.
  const p = LsdjProbe.create({ song: SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(0);
  const playing = p.runTicks(150);
  const before = playing[playing.length - 1];

  p.launchRaw(5);
  const after = p.runTicks(200);

  console.log(`[B8] before launching the empty row: ${fmtSample(before)}`);
  dump("B8 after launching empty row 5", after, 40);

  const stillPlaying = after.filter((s) => s.playing).length;
  const stepped = changeTicks(after, (s) => s.channels.pu1.phraseRow).length;
  console.log(`[B8] samples still 'playing': ${stillPlaying}/${after.length}; pu1 step changes: ${stepped}`);
  console.log(`[B8] pu1 songRow after: ${JSON.stringify(transitions(after, (s) => s.channels.pu1.songRow))}`);
  console.log(`[B8] pu1 chain after:   ${JSON.stringify(transitions(after, (s) => s.channels.pu1.chain))}`);
  console.log(`[B8] per-channel playing: ${JSON.stringify(CHANNELS.map((c) => after[after.length - 1].channels[c].playing))}`);
  console.log(`[B8] VERDICT(playing): an empty row ${stillPlaying === 0 ? "PARKS the cart - usable as a stop" : stepped === 0 ? "freezes stepping without clearing the playing flag" : "does NOT stop it"}`);

  // Part 2 — from a STOPPED cart, where nothing is already in motion to confuse the reading. If an empty
  // row is simply ignored, this cart never starts at all.
  const idle = LsdjProbe.create({ song: SONG });
  if (!idle) return skip("aboy ROM not found / unsupported version");

  idle.launchRaw(5);
  const fromIdle = idle.runTicks(200);
  console.log(`[B8] from stopped, pu1 songRow: ${JSON.stringify(transitions(fromIdle, (s) => s.channels.pu1.songRow))}`);
  console.log(`[B8] from stopped, pu1 chain:   ${JSON.stringify(transitions(fromIdle, (s) => s.channels.pu1.chain))}`);
  console.log(`[B8] from stopped, playing samples: ${fromIdle.filter((s) => s.playing).length}/${fromIdle.length}`);
  console.log(`[B8] VERDICT(stopped): launching an empty row ${fromIdle.some((s) => s.playing) ? "STARTS the cart anyway" : "leaves it stopped"}`);

  // Guards the instrument, not the finding: the cart must have been playing for part 1 to mean anything.
  // What the empty launch does is printed above and recorded in docs/launchpad-plan.md.
  expect(before.playing).toBe(true);
});

// B9 — B8 established that an empty-row launch neither stops the cart nor is ignored: it STARTS a stopped
// one, on a row that is not the one asked for. So where does it land? `predict.ts` currently models this
// case as "park on the launched row, silent", which B8 already disproves, and the predictor cannot be
// corrected without knowing the actual rule. SPARSE_SONG's middle hole makes all four candidate answers
// distinguishable by chain number alone.
test("B9: where does a launch of an empty row actually land?", () => {
  const p = LsdjProbe.create({ song: SPARSE_SONG });
  if (!p) return skip("aboy ROM not found / unsupported version");

  p.launchRaw(3); // empty, with populated rows at 0-1 below it and 5-6 above
  const samples = p.runTicks(200);

  const rows = transitions(samples, (s) => s.channels.pu1.songRow);
  const chains = transitions(samples, (s) => s.channels.pu1.chain);
  dump("B9 after launching empty row 3 of a sparse song", samples, 40);
  console.log(`[B9] pu1 songRow: ${JSON.stringify(rows)}`);
  console.log(`[B9] pu1 chain:   ${JSON.stringify(chains)}`);
  console.log(`[B9] playing samples: ${samples.filter((s) => s.playing).length}/${samples.length}`);

  const landed = rows[0];
  const rule = landed === 1 ? "scans BACK to the nearest populated row"
    : landed === 5 ? "scans FORWARD to the nearest populated row"
    : landed === 6 ? "clamps to the LAST populated row"
    : landed === 0 ? "wraps to the START of the song"
    : `landed somewhere unexplained (row ${landed})`;
  console.log(`[B9] VERDICT(launch): an empty-row launch ${rule}`);

  // And the second, separate rule this sequence exposes: having landed on row 1, where does the cart go
  // when its chain ends and row 2 is EMPTY? Skipping the hole would put it on row 5; ending the song puts
  // it back at 0. These are different rules from the launch one, and both matter to the predictor.
  const after = rows[1];
  console.log(`[B9] VERDICT(advance): advancing into an empty row ${after === 0 ? "ENDS the song and wraps to the start" : after === 5 ? "SKIPS the hole to the next populated row" : `did something else (row ${after})`}`);

  expect(samples.some((s) => s.playing)).toBe(true); // B8 says it starts; if it did not, the rule is moot
  // MEASURED, and both now load-bearing in predict.ts: a launch scans back, an advance ends the song.
  expect(landed).toBe(1);
  expect(after).toBe(0);
});
