// The risa-sync DSP role driven through the kernel: it turns the DAW transport into risa's N8-FIFO host-sync
// byte protocol (arm+locate / start / 24-PPQN clock / stop), delivered over the coreBytes sink. NOT MIDI —
// a raw byte stream reusing MIDI status values. Mirrors dsp/lsdj-modes.test.ts. Pure TS, no backend.
// Protocol reference: risa's docs/sync/host-sync-protocol.md (2.3.0 and later).
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type BlockInput } from "../../src/dspKernel";

// A one-system project carrying just the risa-sync role.
function risa(): DspKernel {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  const k = new DspKernel(reg);
  k.setSystems({ project: [], systems: [{ id: 1, pipeline: [{ kind: "risa-sync", config: {} }] }] });
  return k;
}

// 22050 frames @ 44100 / 120 bpm = exactly 1 beat (24 ticks at 24 PPQN).
const baseDyn = (): BlockInput => ({
  frames: 22050, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false, midiIn: [], buttons: [], keys: [], serialOut: [],
});
type Out = { coreBytes: { data: number[]; flush?: boolean }[] };
const packets = (out: Out): number[][] => out.coreBytes.map((e) => e.data);
const clocks = (out: Out): number =>
  out.coreBytes.filter((e) => e.data.length === 1 && e.data[0] === 0xf8).length;
const arms = (out: Out) => out.coreBytes.filter((e) => e.data[0] === 0xf9);

test("idle (transport off) emits nothing", () => {
  expect(risa().processBlock({ ...baseDyn() }).coreBytes.length).toBe(0);
});

test("transport rise arms+locates from ppqStart, starts, then streams the beat's remaining clocks", () => {
  const out = risa().processBlock({ ...baseDyn(), transport: true }); // ppqStart 0 → clock 0
  const p = packets(out);
  expect(p[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 0x00]); // F9 52 — arm+locate song 0 chain 0 tick 0
  expect(p[1]).toEqual([0xfa]); // FA — start
  // 23, not 24: risa primes the armed clock itself, and an F8 for it would double-advance the row.
  expect(clocks(out)).toBe(23);
});

test("the arm is a barrier — it flushes the FIFO, the start and clocks do not", () => {
  const out = risa().processBlock({ ...baseDyn(), transport: true });
  expect(arms(out).length).toBe(1);
  expect(arms(out)[0].flush).toBe(true);
  expect(out.coreBytes.filter((e) => e.data[0] !== 0xf9).every((e) => !e.flush)).toBe(true);
});

test("a mid-song start locates from the current ppq (song row / chain row / tick offset)", () => {
  // ppq 64 → clock 1536 → phrase 16 → song 1, chain 0, tick 0.
  expect(packets(risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 64 }))[0])
    .toEqual([0xf9, 0x52, 0x01, 0x00, 0x00]);
  // ppq 5 → clock 120 → phrase 1 (song 0, chain 1), tick 120-96 = 24 into the phrase.
  expect(packets(risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 5 }))[0])
    .toEqual([0xf9, 0x52, 0x00, 0x01, 24]);
  // The last grid position in a phrase: clock 95 → tick 0x5f, the protocol's documented maximum.
  expect(packets(risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 95 / 24 }))[0])
    .toEqual([0xf9, 0x52, 0x00, 0x00, 0x5f]);
});

test("a locate onto a non-grid ppq keeps the tick offset exact, and suppresses no clock", () => {
  // ppq 0.25 → clock 6 exactly: on the six-clock grid, so that clock is the primed one.
  const onGrid = risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 0.25 });
  expect(packets(onGrid)[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 6]);
  expect(clocks(onGrid)).toBe(23); // clock 6 primed, so one fewer than the beat's 24

  // ppq 0.26 → clock floor(6.24) = 6 armed, but the first clock in the block is 7 — nothing to
  // suppress, matching "the next F8 is the first clock after the locate".
  const offGrid = risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 0.26 });
  expect(packets(offGrid)[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 6]);
  expect(clocks(offGrid)).toBe(24);
});

test("continued playback streams clocks with no new arm/start", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // rise at ppq 0 (ppqEnd → 1)
  const cont = k.processBlock({ ...baseDyn(), transport: true, ppqStart: 1 }); // contiguous
  expect(cont.coreBytes.some((e) => e.data[0] === 0xf9 || e.data[0] === 0xfa)).toBeFalsy(); // no re-arm
  expect(clocks(cont)).toBe(24); // a full beat: only the block carrying the arm loses a clock
});

test("a ppq discontinuity (seek/loop) re-arms at the new position", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // rise at ppq 0
  const seek = k.processBlock({ ...baseDyn(), transport: true, ppqStart: 64 }); // jumped to phrase 16
  expect(packets(seek)[0]).toEqual([0xf9, 0x52, 0x01, 0x00, 0x00]); // fresh arm at song 1 chain 0
  expect(packets(seek)[1]).toEqual([0xfa]);
  expect(arms(seek)[0].flush).toBe(true); // the re-arm discards clocks queued for the old position
  expect(clocks(seek)).toBe(23); // its own armed clock is primed too
});

test("a loop back to the SAME position re-arms and re-primes that clock", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true, ppqStart: 4 }); // rise at phrase 1
  const loop = k.processBlock({ ...baseDyn(), transport: true, ppqStart: 4 }); // looped back to it
  expect(packets(loop)[0]).toEqual([0xf9, 0x52, 0x00, 0x01, 0x00]);
  expect(clocks(loop)).toBe(23);
});

test("transport fall stops (FC) with no clocks", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // playing
  const stop = k.processBlock({ ...baseDyn(), transport: false, ppqStart: 1 }); // fell
  expect(packets(stop)).toEqual([[0xfc]]);
});

test("a stop then a restart at the same position arms again and re-primes", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // rise at ppq 0
  k.processBlock({ ...baseDyn(), transport: false, ppqStart: 1 }); // stop
  const again = k.processBlock({ ...baseDyn(), transport: true }); // start over at ppq 0
  expect(packets(again)[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 0x00]);
  expect(packets(again)[1]).toEqual([0xfa]);
  expect(clocks(again)).toBe(23);
});

test("the song row wraps at 0x7f and the chain row at 0x0f, as the protocol's masks require", () => {
  // phrase 0x7ff → songRow (0x7ff >> 4) & 0x7f = 0x7f, chainRow 0x0f. ppq = phrase * 4.
  expect(packets(risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 0x7ff * 4 }))[0])
    .toEqual([0xf9, 0x52, 0x7f, 0x0f, 0x00]);
});
