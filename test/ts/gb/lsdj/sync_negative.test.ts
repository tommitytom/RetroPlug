// Replacement for examples/scripts/lsdj_sync_negative.json.
//
// The control for sync_pattern.test.ts: same two linked LSDj instances and the
// same one-note song, but SYNC is left OFF (no link-cable sync). The JSON entered
// the song without setting SYNC and confirmed by screenshot that the LEAD/SYNC
// indicators never appear. We author SYNC=None directly and assert the audible
// consequence: pressing START on the leader plays only the leader — the follower
// never starts, so its per-system audio stays silent. If this test ever shows the
// follower producing audio, the positive test (sync_pattern) is not measuring
// real sync.
import { test, expect, emu, Button, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const fill = <T>(n: number, f: () => T): T[] => Array.from({ length: n }, f);
const SYNC_OFF = 0x3fbd;

function songSav(sync: string): ArrayBuffer {
  const rows = fill(256, () => ({ chains: [null, null, null, null] as (number | null)[] }));
  rows[0].chains[0] = 0;
  const chains = fill(128, () => null as unknown);
  chains[0] = { phrases: [0, ...fill(15, () => null)], transpositions: fill(16, () => 0) };
  const phrases = fill(256, () => null as unknown);
  phrases[0] = {
    notes: [1, ...fill(15, () => 0)],
    instruments: [0, ...fill(15, () => null)],
    commands: fill(16, () => "None"), commandValues: fill(16, () => 0),
  };
  const instruments = fill(64, () => null as unknown);
  instruments[0] = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 };
  return emu.savFromJson(JSON.stringify({
    workingSong: { formatVersion: 22, settings: { syncMode: sync }, rows, chains, phrases, instruments },
  }));
}

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("LSDj link-cable sync control: follower stays silent without SYNC=LSDJ", () => {
  const leader = emu.loadRom(LSDJ, songSav("None"), undefined, 1);
  emu.loadRom(LSDJ, songSav("None"), undefined, 1); // follower, same link group
  emu.runMs(6000);

  expect(new Uint8Array(emu.readMemory(leader, Mem.Sram))[SYNC_OFF]).toBe(0); // SYNC = OFF

  emu.tap(leader, Button.Start, 100); // START on the leader only
  const [a0, a1] = emu.runMsPerSystem(4000);
  const r0 = rms(a0), r1 = rms(a1);
  console.log(`sync_negative: leader RMS=${r0.toFixed(5)} follower RMS=${r1.toFixed(5)}`);

  expect(r0).toBeGreaterThan(0.001); // leader plays
  expect(r1).toBeLessThan(0.0005);   // follower never started -> silent (no sync)
});
