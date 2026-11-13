// rp.genmddj.sync: the DAW transport at 24 PPQN, one level word per clock on the `bytes` output, at the clock's own
// frame, with the counter surviving a stopped transport. Both carts are active-high with idle = counter 3; the pins are
// what differ (genmddj's TR and TH are bits 5 and 6 of $A10005, smsggdj's are other bits of another port), so these
// cases also say the shared machine is fed the right encoder.
import { test, expect } from "@retroplug/testing";
import { NodeRig, type BlockSpec, type Sent } from "@retroplug/testing/rig";
import { GENMDDJ_SYNC_NODE_TYPE, genmddjSyncNode } from "../genmddjSyncNode";
import { mdSyncLevels, GENMDDJ_SYNC_PPQN } from "../mdSync";

const rig = () => new NodeRig(genmddjSyncNode, {});
// 22050 frames at 44100 / 120 bpm is exactly one beat: 24 ticks at 24 PPQN.
const beat = (over: Partial<BlockSpec> = {}): BlockSpec => ({ frames: 22050, ...over });
const levels = (sent: Sent[]): number[] => sent.map((s) => s.data[0]);

test("one output, the system's core-bytes input, and a descriptor with no fields", () => {
  expect(rig().ports()).toEqual(["out bytes bytes"]);
  expect(genmddjSyncNode.descriptor().id).toBe(GENMDDJ_SYNC_NODE_TYPE);
  expect(genmddjSyncNode.parseSettings({})).toEqual({});
});

test("a stopped transport sends nothing", () => {
  expect(rig().run(beat())).toEqual([]);
});

test("a playing transport sends one level per 24-PPQN clock, at the clock's own frame", () => {
  const out = rig().run(beat({ playing: true }));
  expect(out.length).toBe(GENMDDJ_SYNC_PPQN);
  for (const s of out) expect(s.data.length).toBe(1);
  // strictly ascending: a level withheld to a later frame would be a clock the ROM never sees
  expect(out.map((s) => s.frame).filter((f, i, all) => i > 0 && f <= all[i - 1])).toEqual([]);
  expect(out.filter((s) => s.frame < 0 || s.frame >= 22050)).toEqual([]);  // inside the block
});

test("the levels are genmddj's words, not smsggdj's", () => {
  const want: number[] = [];
  for (let i = 1; i <= GENMDDJ_SYNC_PPQN; i++) want.push(mdSyncLevels(i));
  expect(levels(rig().run(beat({ playing: true })))).toEqual(want);
});

test("the counter continues across a stopped transport", () => {
  const r = rig();
  const first = levels(r.run(beat({ playing: true })));
  expect(first.length).toBe(GENMDDJ_SYNC_PPQN);
  expect(r.run(beat({ playing: false, ppq: 1 }))).toEqual([]);  // nothing at all, not a stop message
  const next = levels(r.run(beat({ playing: true, ppq: 2 })));
  expect(next[0]).toBe(mdSyncLevels(GENMDDJ_SYNC_PPQN + 1));
});
