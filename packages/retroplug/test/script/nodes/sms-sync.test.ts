// rp.sms.sync (src/script/nodes/smsSync.ts): the DAW transport as smsggdj's sync transport, a 2-bit counter held on
// two port lines at 24 PPQN, each step a LEVEL WORD on its `bytes` output. Both machines: a Master System's controller
// port 2 (TR + TH), a Game Gear's EXT port (PC4/PC5 + PC6), picked by `machine`. The cases the DSP kernel's sms-sync
// role was held to until B3b, at the node; the encoding's pure cases and the ROM provider's are
// test/dsp/sms-sync.test.ts's.
//
// Protocol reference: smsggdj's GGSYNC.md and src/engine.asm:576-590 / :646-661.
import { test, expect } from "../../../testing/harness";
import { SMS_SYNC_NODE_TYPE, smsSyncNode } from "../../../src/script/nodes/smsSync";
import { ggSyncLevels, smsSyncLevels, SMS_SYNC_MAX_CLOCKS_PER_POLL, SMS_SYNC_PPQN } from "../../../src/smsSync";
import { NodeRig, type BlockSpec, type Sent } from "./_rig";

const machine = (m: "sms" | "gg") => new NodeRig(smsSyncNode, { machine: m });
const sms = () => machine("sms");
const gg = () => machine("gg");
// 22050 frames @ 44100 / 120 bpm = exactly 1 beat (24 ticks at 24 PPQN).
const beat = (over: Partial<BlockSpec> = {}): BlockSpec => ({ frames: 22050, ...over });
const levels = (sent: Sent[]): number[] => sent.map((s) => s.data[0]);

test("one output, the core's byte input, sized as the kernel's core-bytes port; machine defaults to sms", () => {
  expect(sms().ports()).toEqual(["out bytes bytes"]);
  expect(smsSyncNode.descriptor().id).toBe(SMS_SYNC_NODE_TYPE);
  expect(smsSyncNode.parseSettings({})).toEqual({ machine: "sms" });
  expect(() => smsSyncNode.parseSettings({ machine: "md" })).toThrow("machine: Invalid option");
  expect(smsSyncNode.layout(smsSyncNode.parseSettings({})).ports[0].capacity).toEqual({ messages: 256, bytes: 1024 });
  expect(smsSyncNode.reshapes).toBe(true);
});

test("idle (transport off) emits nothing", () => {
  expect(sms().run(beat())).toEqual([]);
});

test("transport emits one level per 24-PPQN tick, no arm and no start message", () => {
  const out = sms().run(beat({ playing: true }));
  // 24, not 23: unlike risa there is no armed clock the ROM primes itself, so none is withheld.
  expect(out.length).toBe(SMS_SYNC_PPQN);
  // Every payload is a single level word - nothing that could be mistaken for a protocol byte.
  for (const s of out) expect(s.data.length).toBe(1);
});

test("the counter ADVANCES one step per tick and wraps - the ROM reads deltas, not absolutes", () => {
  const want: number[] = [];
  for (let i = 1; i <= SMS_SYNC_PPQN; i++) want.push(smsSyncLevels(i));
  expect(levels(sms().run(beat({ playing: true })))).toEqual(want);
});

test("no level is ever repeated back-to-back, or the ROM would read a delta of zero", () => {
  const seq = levels(sms().run(beat({ playing: true })));
  expect(seq.filter((v, i) => i > 0 && v === seq[i - 1]).length).toBe(0);
});

test("the counter carries across blocks rather than restarting", () => {
  const rig = sms();
  const a = levels(rig.run(beat({ playing: true })));
  const b = levels(rig.run(beat({ playing: true, ppq: 1 })));
  // The first tick of block 2 must still step the counter: restarting it would repeat a level across the boundary.
  expect(b[0] === a[a.length - 1]).toBeFalsy();
  expect(b[0]).toBe(smsSyncLevels(SMS_SYNC_PPQN + 1));
});

test("no flush is ever requested - a held level has no undelivered-stream hazard", () => {
  for (const s of sms().run(beat({ playing: true }))) expect(s.flags).toBe(0);
});

test("stop is implicit: clocks simply cease, with no stop message", () => {
  const rig = sms();
  rig.run(beat({ playing: true }));
  expect(rig.run(beat({ playing: false, ppq: 1 }))).toEqual([]); // not "one stop byte" - nothing at all
});

test("a seek does NOT relocate: the node is positionless by design", () => {
  const rig = sms();
  rig.run(beat({ playing: true }));
  const out = rig.run(beat({ playing: true, ppq: 64 })); // big jump
  expect(out.length).toBe(SMS_SYNC_PPQN);
  for (const s of out) expect(s.data.length).toBe(1); // no arm packet appeared
});

test("ticks land at spread sample offsets, not bunched at block start", () => {
  const frames = sms().run(beat({ playing: true })).map((s) => s.frame);
  expect(new Set(frames).size).toBe(frames.length); // strictly distinct
  expect(frames.filter((f, i) => i > 0 && f <= frames[i - 1]).length).toBe(0); // strictly ascending
  expect(frames.filter((f) => f < 0 || f >= 22050).length).toBe(0); // all inside the block
});

test("the documented aliasing ceiling matches the tempo the node can actually clock", () => {
  const ntscFps = 59.9227434;
  expect(Math.round((SMS_SYNC_MAX_CLOCKS_PER_POLL * ntscFps * 60) / SMS_SYNC_PPQN)).toBe(449);
  // At the ceiling a block still yields more clocks than a beat's worth: the limit is the ROM's poll rate.
  expect(sms().run(beat({ playing: true, tempo: 449 })).length > SMS_SYNC_PPQN).toBeTruthy();
});

test("a gg node emits GG level words at the same 24 PPQN cadence", () => {
  const got = levels(gg().run(beat({ playing: true })));
  expect(got.length).toBe(SMS_SYNC_PPQN);
  expect(got[0]).toBe(ggSyncLevels(1));
  expect(got[1]).toBe(ggSyncLevels(2));
  expect(got[3]).toBe(ggSyncLevels(0));
  // ...and nothing in the stream is an SMS word: those set bit 7, which is not a GG pin at all.
  expect(got.some((v) => (v & 0x80) !== 0)).toBeFalsy();
});

test("a machine change keeps the counter and the clock, and encodes the next step on the new pins", () => {
  const rig = sms();
  rig.run(beat({ playing: true }));
  expect(rig.set({ machine: "gg" })).toBe(true);
  const next = levels(rig.run(beat({ playing: true, ppq: 1 })));
  expect(next.length).toBe(SMS_SYNC_PPQN);
  expect(next[0]).toBe(ggSyncLevels(SMS_SYNC_PPQN + 1));
  expect(rig.creates).toBe(1);
});
