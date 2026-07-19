// The control for lsdj-sync-pattern.test.ts: the same two linked LSDj instances and the same one-note
// song, but SYNC is left OFF (no link-cable sync). Pressing START on the leader plays only the leader
// — the follower never starts, so its per-system audio stays silent. If this ever shows the follower
// producing audio, the positive test isn't measuring real sync.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SongSettings } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

// Identical song to lsdj-sync-pattern; only syncMode differs.
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

test("LSDj link-cable sync control: the follower stays silent without SYNC=LSDJ", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-sync-negative: LSDj ROM not found at ${LSDJ}`);
    return;
  }

  const audio = createAudioDriver();

  const leader = 1, follower = 2;
  expect(construct(be, leader, "None")).toBeTruthy();
  expect(construct(be, follower, "None")).toBeTruthy(); // SYNC=None → no link response
  expect(be.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
  expect(be.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();

  audio.renderAudio(6000);

  audio.pressButton(leader, START, true);
  audio.renderAudio(120);
  audio.pressButton(leader, START, false);

  const bufs = audio.renderAudioPerSystem(4000); // slot order = [leader, follower]
  expect(bufs.length).toBe(2);
  const r0 = rms(bufs[0]), r1 = rms(bufs[1]);
  console.log(`[lsdj-sync-negative] leader RMS=${r0.toFixed(5)} follower RMS=${r1.toFixed(5)}`);

  expect(r0 > 0.001).toBeTruthy();   // leader plays its own song
  expect(r1 < 0.0005).toBeTruthy();  // follower never started → silent (no sync)
});
