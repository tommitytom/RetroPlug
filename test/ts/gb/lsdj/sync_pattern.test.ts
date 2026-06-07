// Replacement for examples/scripts/lsdj_sync_pattern.json.
//
// The JSON booted two LSDj instances on link_group 1, drove ~30 fragile chord/tap
// events on BOTH to set SYNC=LSDJ and enter a one-note song, pressed START on
// instance 0, and screenshotted both at several points to eyeball visual
// lockstep + cross-correlate per-system WAVs. We author the identical SYNC=LSDJ
// song directly on both, link them, START the leader, and assert the sync
// outcome with per-system audio (the canonical method from AGENTS.md).
//
// The decisive proof: the follower produces audio *only* when actually synced to
// the leader — and, being the same song under the same clock, its level tracks
// the leader's. (sync_negative.test.ts is the control: no SYNC -> follower silent.)
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

test("LSDj link-cable sync: follower plays in lockstep with the leader", () => {
  const leader = emu.loadRom(LSDJ, songSav("Lsdj"), undefined, 1);
  emu.loadRom(LSDJ, songSav("Lsdj"), undefined, 1); // follower, same link group
  emu.runMs(6000); // valid savs skip the self-test; still need a few s to song screen

  // SYNC=LSDJ authored on the leader (no UI navigation).
  expect(new Uint8Array(emu.readMemory(leader, Mem.Sram))[SYNC_OFF]).toBe(1);
  emu.screenshot(leader, "/tmp/lsdj_sync_pattern_boot.png");

  emu.tap(leader, Button.Start, 100); // START on the leader only
  const [a0, a1] = emu.runMsPerSystem(4000); // load order = [leader, follower]
  const r0 = rms(a0), r1 = rms(a1);
  console.log(`sync_pattern: leader RMS=${r0.toFixed(5)} follower RMS=${r1.toFixed(5)}`);

  // Emit per-system WAVs for the reaper MCP audio-analysis workflow
  // (make reaper-analyze-lsdj-sync stages these).
  emu.writeWav("/tmp/lsdj-sync-pattern_sys0.wav", a0);
  emu.writeWav("/tmp/lsdj-sync-pattern_sys1.wav", a1);

  expect(r0).toBeGreaterThan(0.001); // leader is playing
  expect(r1).toBeGreaterThan(0.001); // follower is playing too -> it synced
  // Same song under the same link clock: the follower's level tracks the leader's.
  expect(r1).toBeGreaterThan(r0 * 0.5);
  expect(r1).toBeLessThan(r0 * 2.0);
});
