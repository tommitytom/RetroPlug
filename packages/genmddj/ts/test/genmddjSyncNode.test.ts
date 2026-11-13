// rp.genmddj.sync: the DAW transport at 24 PPQN, one level word per clock on the `bytes` output, at the clock's own
// frame, with the counter surviving a stopped transport. Both carts are active-high with idle = counter 3; the pins are
// what differ (genmddj's TR and TH are bits 5 and 6 of $A10005, smsggdj's are other bits of another port), so these
// cases also say the shared machine is fed the right encoder.
import { test, expect } from "@retroplug/testing";
import { NodeRig, type BlockSpec, type Sent } from "@retroplug/testing/rig";
import { GENMDDJ_SYNC_NODE_TYPE, genmddjSyncNode } from "../genmddjSyncNode";
import { mdSyncLevels, GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL, GENMDDJ_SYNC_PPQN } from "../mdSync";

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
  // 1.25 beats, rounded down: 30 ticks, so the counter ends on 2. A beat's 24 is 0 mod 4, which encodes the same level
  // a counter restarted with the transport does, so a node that reset on start would pass a 24-tick first block.
  const first = levels(r.run(beat({ frames: 27562, playing: true })));
  expect(first.length).toBe(30);
  expect(r.run(beat({ playing: false, ppq: 1 }))).toEqual([]);  // nothing at all, not a stop message
  const next = levels(r.run(beat({ playing: true, ppq: 1 })));
  expect(next[0]).toBe(mdSyncLevels(31));               // counter 3: the counter the stopped block froze carried over
  expect(next[0] === mdSyncLevels(1)).toBeFalsy();      // what a counter restarted with the transport would encode
});

test("no level is ever repeated back-to-back, or the ROM would read a delta of zero", () => {
  const seq = levels(rig().run(beat({ playing: true })));
  expect(seq.filter((v, i) => i > 0 && v === seq[i - 1]).length).toBe(0);
});

test("no flush is ever requested - a held level has no undelivered-stream hazard", () => {
  for (const s of rig().run(beat({ playing: true }))) expect(s.flags).toBe(0);
});

test("the documented aliasing ceiling matches the tempo the node can actually clock", () => {
  // The ROM polls once a video frame and keeps `(read - last) & 3`, so at most GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL clocks
  // survive a poll: about 449 BPM on an NTSC frame and 373 on a PAL one, past which a clock aliases to a lost beat.
  const ntscFps = 59.9227434;
  const palFps = 49.7014586;
  expect(Math.round((GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL * ntscFps * 60) / GENMDDJ_SYNC_PPQN)).toBe(449);
  expect(Math.round((GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL * palFps * 60) / GENMDDJ_SYNC_PPQN)).toBe(373);
  // At the ceiling a block still yields more clocks than a beat's worth: the limit is the ROM's poll rate.
  expect(rig().run(beat({ playing: true, tempo: 449 })).length > GENMDDJ_SYNC_PPQN).toBeTruthy();
});
