// rp.risa.sync (src/script/nodes/risaSync.ts): the DAW transport as risa's N8-FIFO host-sync byte protocol (arm+locate,
// start, 24-PPQN clock, stop) on its `bytes` output, as the DSP kernel's risa-sync role sent it until B3b; the locate's
// pure cases are test/risa/sync-locate.test.ts's.
// Protocol reference: risa's docs/sync/host-sync-protocol.md (2.3.0 and later).
import { test, expect } from "../../../testing/harness";
import { RISA_SYNC_NODE_TYPE, risaSyncNode } from "../../../src/script/nodes/risaSync";
import { NodeRig, type BlockSpec, type Sent } from "./_rig";

const risa = () => new NodeRig(risaSyncNode);
// 22050 frames @ 44100 / 120 bpm = exactly 1 beat (24 ticks at 24 PPQN).
const beat = (over: Partial<BlockSpec> = {}): BlockSpec => ({ frames: 22050, ...over });
const packets = (sent: Sent[]): number[][] => sent.filter((s) => s.port === "bytes").map((s) => s.data);
const clocks = (sent: Sent[]): number => sent.filter((s) => s.data.length === 1 && s.data[0] === 0xf8).length;
const arms = (sent: Sent[]) => sent.filter((s) => s.data[0] === 0xf9);

test("one output, the core's byte input, sized as the kernel's core-bytes port; no settings", () => {
  const rig = risa();
  expect(rig.ports()).toEqual(["out bytes bytes"]);
  expect(risaSyncNode.descriptor().id).toBe(RISA_SYNC_NODE_TYPE);
  expect(risaSyncNode.descriptor().settings).toEqual({ type: "object" });
  expect(risaSyncNode.layout({} as never).ports[0].capacity).toEqual({ messages: 256, bytes: 1024 });
  expect(risaSyncNode.reshapes).toBe(true);
});

test("idle (transport off) emits nothing", () => {
  expect(risa().run(beat())).toEqual([]);
});

test("transport rise arms+locates from ppqStart, starts, then streams the beat's remaining clocks", () => {
  const out = risa().run(beat({ playing: true })); // ppqStart 0 → clock 0
  const p = packets(out);
  expect(p[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 0x00]); // F9 52: arm+locate song 0 chain 0 tick 0
  expect(p[1]).toEqual([0xfa]); // FA: start
  // 23, not 24: risa primes the armed clock itself, and an F8 for it would double-advance the row.
  expect(clocks(out)).toBe(23);
});

test("the arm is a barrier: it flushes the FIFO, the start and clocks do not", () => {
  const out = risa().run(beat({ playing: true }));
  expect(arms(out).length).toBe(1);
  expect(arms(out)[0].flags).toBe(1);
  expect(out.filter((s) => s.data[0] !== 0xf9).every((s) => s.flags === 0)).toBe(true);
});

test("clocks go out at their ticks' frames, after the arm and start at frame 0", () => {
  const out = risa().run(beat({ playing: true }));
  expect(out.slice(0, 2).map((s) => s.frame)).toEqual([0, 0]);
  // The first clock is tick 1 (tick 0 is primed): 22050 / 24 frames in.
  expect(out.slice(2, 5).map((s) => s.frame)).toEqual([918, 1837, 2756]);
});

test("a mid-song start locates from the current ppq (song row / chain row / tick offset)", () => {
  // ppq 64 → clock 1536 → phrase 16 → song 1, chain 0, tick 0.
  expect(packets(risa().run(beat({ playing: true, ppq: 64 })))[0]).toEqual([0xf9, 0x52, 0x01, 0x00, 0x00]);
  // ppq 5 → clock 120 → phrase 1 (song 0, chain 1), tick 120-96 = 24 into the phrase.
  expect(packets(risa().run(beat({ playing: true, ppq: 5 })))[0]).toEqual([0xf9, 0x52, 0x00, 0x01, 24]);
  // The last grid position in a phrase: clock 95 → tick 0x5f, the protocol's documented maximum.
  expect(packets(risa().run(beat({ playing: true, ppq: 95 / 24 })))[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 0x5f]);
});

test("a locate onto a non-grid ppq keeps the tick offset exact, and suppresses no clock", () => {
  // ppq 0.25 → clock 6 exactly: on the six-clock grid, so that clock is the primed one.
  const onGrid = risa().run(beat({ playing: true, ppq: 0.25 }));
  expect(packets(onGrid)[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 6]);
  expect(clocks(onGrid)).toBe(23); // clock 6 primed, so one fewer than the beat's 24
  // ppq 0.26 → clock floor(6.24) = 6 armed, but the first clock in the block is 7: nothing to suppress.
  const offGrid = risa().run(beat({ playing: true, ppq: 0.26 }));
  expect(packets(offGrid)[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 6]);
  expect(clocks(offGrid)).toBe(24);
});

test("continued playback streams clocks with no new arm/start", () => {
  const rig = risa();
  rig.run(beat({ playing: true })); // rise at ppq 0 (ppqEnd → 1)
  const cont = rig.run(beat({ playing: true, ppq: 1 })); // contiguous
  expect(cont.some((s) => s.data[0] === 0xf9 || s.data[0] === 0xfa)).toBeFalsy(); // no re-arm
  expect(clocks(cont)).toBe(24); // a full beat: only the block carrying the arm loses a clock
});

test("a ppq discontinuity (seek/loop) re-arms at the new position", () => {
  const rig = risa();
  rig.run(beat({ playing: true })); // rise at ppq 0
  const seek = rig.run(beat({ playing: true, ppq: 64 })); // jumped to phrase 16
  expect(packets(seek)[0]).toEqual([0xf9, 0x52, 0x01, 0x00, 0x00]); // fresh arm at song 1 chain 0
  expect(packets(seek)[1]).toEqual([0xfa]);
  expect(arms(seek)[0].flags).toBe(1); // the re-arm discards clocks queued for the old position
  expect(clocks(seek)).toBe(23); // its own armed clock is primed too
});

test("a loop back to the SAME position re-arms and re-primes that clock", () => {
  const rig = risa();
  rig.run(beat({ playing: true, ppq: 4 })); // rise at phrase 1
  const loop = rig.run(beat({ playing: true, ppq: 4 })); // looped back to it
  expect(packets(loop)[0]).toEqual([0xf9, 0x52, 0x00, 0x01, 0x00]);
  expect(clocks(loop)).toBe(23);
});

test("transport fall stops (FC) with no clocks", () => {
  const rig = risa();
  rig.run(beat({ playing: true })); // playing
  expect(packets(rig.run(beat({ playing: false, ppq: 1 })))).toEqual([[0xfc]]); // fell
});

test("a stop then a restart at the same position arms again and re-primes", () => {
  const rig = risa();
  rig.run(beat({ playing: true })); // rise at ppq 0
  rig.run(beat({ playing: false, ppq: 1 })); // stop
  const again = rig.run(beat({ playing: true })); // start over at ppq 0
  expect(packets(again)[0]).toEqual([0xf9, 0x52, 0x00, 0x00, 0x00]);
  expect(packets(again)[1]).toEqual([0xfa]);
  expect(clocks(again)).toBe(23);
});

test("the song row wraps at 0x7f and the chain row at 0x0f, as the protocol's masks require", () => {
  // phrase 0x7ff → songRow (0x7ff >> 4) & 0x7f = 0x7f, chainRow 0x0f. ppq = phrase * 4.
  expect(packets(risa().run(beat({ playing: true, ppq: 0x7ff * 4 })))[0]).toEqual([0xf9, 0x52, 0x7f, 0x0f, 0x00]);
});

test("1-frame blocks clock the same stream as one block does", () => {
  const whole = risa().run(beat({ playing: true }));
  const rig = risa();
  const pieces: number[][] = [];
  for (let f = 0; f < 22050; f++) for (const s of rig.run({ frames: 1, playing: true, ppq: f / 22050 })) pieces.push(s.data);
  expect(pieces).toEqual(packets(whole));
});
