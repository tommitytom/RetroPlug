// The risa-sync DSP role driven through the kernel: it turns the DAW transport into risa's N8-FIFO host-sync
// byte protocol (arm+locate / start / 24-PPQN clock / stop), delivered over the coreBytes sink. NOT MIDI —
// a raw byte stream reusing MIDI status values. Mirrors dsp/lsdj-modes.test.ts. Pure TS, no backend.
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
const packets = (out: { coreBytes: { data: number[] }[] }): number[][] => out.coreBytes.map((e) => e.data);
const clocks = (out: { coreBytes: { data: number[] }[] }): number =>
  out.coreBytes.filter((e) => e.data.length === 1 && e.data[0] === 0xf8).length;

test("idle (transport off) emits nothing", () => {
  expect(risa().processBlock({ ...baseDyn() }).coreBytes.length).toBe(0);
});

test("transport rise arms+locates from ppqStart, starts, then streams a full beat of clocks", () => {
  const out = risa().processBlock({ ...baseDyn(), transport: true }); // ppqStart 0 → phrase 0
  const p = packets(out);
  expect(p[0]).toEqual([0xf9, 0x52, 0x00, 0x00]); // F9 52 — arm+locate song 0 chain 0
  expect(p[1]).toEqual([0xfa]); // FA — start
  expect(clocks(out)).toBe(24); // 24 PPQN over one beat
});

test("a mid-song start locates from the current ppq (phrase math)", () => {
  const out = risa().processBlock({ ...baseDyn(), transport: true, ppqStart: 64 }); // phrase 16 → song 1 chain 0
  expect(packets(out)[0]).toEqual([0xf9, 0x52, 0x01, 0x00]);
});

test("continued playback streams clocks with no new arm/start", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // rise at ppq 0 (ppqEnd → 1)
  const cont = k.processBlock({ ...baseDyn(), transport: true, ppqStart: 1 }); // contiguous
  expect(cont.coreBytes.some((e) => e.data[0] === 0xf9 || e.data[0] === 0xfa)).toBeFalsy(); // no re-arm
  expect(clocks(cont)).toBe(24);
});

test("a ppq discontinuity (seek/loop) re-arms at the new position", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // rise at ppq 0
  const seek = k.processBlock({ ...baseDyn(), transport: true, ppqStart: 64 }); // jumped to phrase 16
  expect(packets(seek)[0]).toEqual([0xf9, 0x52, 0x01, 0x00]); // fresh arm at song 1 chain 0
  expect(packets(seek)[1]).toEqual([0xfa]);
  expect(clocks(seek)).toBe(24);
});

test("transport fall stops (FC) with no clocks", () => {
  const k = risa();
  k.processBlock({ ...baseDyn(), transport: true }); // playing
  const stop = k.processBlock({ ...baseDyn(), transport: false, ppqStart: 1 }); // fell
  expect(packets(stop)).toEqual([[0xfc]]);
});
