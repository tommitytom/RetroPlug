// Port of legacy test/ts/gb/lsdj/sync_pattern.test.ts: LSDj link-cable sync, proven
// with PER-SYSTEM audio (the new renderAudioPerSystem RPC). Two LSDj instances in the same link group,
// both authored SYNC=LSDJ, START on the leader only. Link sync is pure GB serial ferrying in the
// block runner's LinkGroup (no DSP kernel needed), so the follower produces audio ONLY when it
// actually synced to the leader — and, being the same song under the same clock, its level tracks the
// leader's. A healthy-looking two-system MIX can't show this; isolated per-system RMS can.
// (lsdj-sync-negative.test.ts is the control: SYNC=None → the follower stays silent.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SongSettings } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

// row 0 → chain 00 → phrase 00, one C note on a hard-panned pulse (the proven cell set). Only the
// syncMode differs between the leader/follower and the negative control.
const songSav = (sync: SongSettings["syncMode"]) => savFrom({
  workingSong: {
    settings: { syncMode: sync },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
  },
});

const construct = (be: ReturnType<typeof createRealBackend>, id: number, sync: SongSettings["syncMode"]) =>
  be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: songSav(sync),
  }, id);

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("LSDj link-cable sync: the follower plays in lockstep with the leader (per-system audio)", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-sync-pattern: LSDj ROM not found at ${LSDJ}`);
    return;
  }

  const audio = createAudioDriver();

  const leader = 1, follower = 2;
  expect(construct(be, leader, "Lsdj")).toBeTruthy();
  expect(construct(be, follower, "Lsdj")).toBeTruthy(); // same one-note SYNC=LSDJ song
  // Put both in link group 1 → the block runner rebuilds the LinkGroup and ferries serial bits.
  expect(be.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
  expect(be.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();

  audio.renderAudio(6000); // valid savs skip the self-test; still need a few s to the song screen

  // START on the leader only → it becomes the link-clock master.
  audio.pressButton(leader, START, true);
  audio.renderAudio(120);
  audio.pressButton(leader, START, false);

  const bufs = audio.renderAudioPerSystem(4000); // slot order = [leader, follower]
  expect(bufs.length).toBe(2);
  const r0 = rms(bufs[0]), r1 = rms(bufs[1]);
  console.log(`[lsdj-sync-pattern] leader RMS=${r0.toFixed(5)} follower RMS=${r1.toFixed(5)}`);

  expect(r0 > 0.001).toBeTruthy();     // leader is playing
  expect(r1 > 0.001).toBeTruthy();     // follower is playing too → it synced
  // Same song under the same link clock: the follower's level tracks the leader's.
  expect(r1 > r0 * 0.5).toBeTruthy();
  expect(r1 < r0 * 2.0).toBeTruthy();
});
