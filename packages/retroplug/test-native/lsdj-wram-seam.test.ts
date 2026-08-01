// Proves the per-block WRAM seam (plan Part A): backend.readRam(id) — bound on the EMULATOR facet the
// plugin uses, read from the race-free SnapshotRegistry triple — returns the live core's work RAM, is
// republished EVERY block (so it tracks playback within one render, unlike the ~2Hz readState/readSram),
// and feeds the LSDj runtime reader exactly as the plugin overlay will.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";
import { LsdjReader } from "../src/lsdj/runtime";

declare const __RESOURCES_DIR__: string;
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7;

const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;
const SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "None", tempo: 128 },
    rows: [{ chains: [0, 1, 2, 3] }],
    chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] }],
    phrases: [
      { notes: [1], instruments: [0] },
      { notes: [1], instruments: [1] },
      { notes: [1], instruments: [2] },
      { notes: [1], instruments: [3] },
    ],
    instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
  },
};

const eq = (a: Uint8Array, b: Uint8Array): boolean => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};

test("readRam publishes live WRAM per block on the emulator facet, and drives the LSDj reader", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-wram-seam: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom(SONG),
  }, id)).toBeTruthy();

  audio.renderAudio(6000); // to the SONG screen

  // The snapshot read matches the live-core read (byte-identical 32K WRAM) at a settled point — proving
  // the SnapshotRegistry ram triple carries the real region, not stale/zeroed data.
  const viaSnapshot = be.readRam(id)!;
  const viaLive = be.readMemory(id, MemoryRegion.Ram)!;
  expect(viaSnapshot.length).toBe(0x8000); // 32K CGB WRAM
  expect(eq(viaSnapshot, viaLive)).toBeTruthy();

  // Per-block freshness: START then a SHORT render (< the 0.5s state interval) — readRam already reflects
  // the flipped ARE_CHANNELS_PLAYING flags, which a coarse ~2Hz savestate-derived read could not.
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(200); // total ~0.32s < 0.5s
  const playingRam = be.readRam(id)!;
  expect([0, 1, 2, 3].every((i) => playingRam[0x0e0 + i] === 1)).toBeTruthy();

  // The overlay data path: LsdjReader over readRam decodes runtime state.
  const reader = LsdjReader.fromHeader(be.readFilePrefix(LSDJ, 0x150)!);
  const s = reader.read(playingRam);
  console.log(`[lsdj-wram-seam] playing=${s.playing} screen=${s.screen} pu1.phraseRow=${s.channels.pu1.phraseRow}`);
  expect(s.supported).toBeTruthy();
  expect(s.playing).toBeTruthy();
  expect(s.screen).toBe("song");
});

// Regression: a live SameBoy model switch RESIZES the WRAM region (CGB 32K ↔ DMG 8K). The snapshot slot
// carries a length prefix + headroom instead of being pinned to the construct-time size, so readRam tracks
// the new size every block. (Before the fix, publishAll only republished on an EXACT size match, so any
// resize froze readRam at the pre-switch snapshot forever.) MODEL_VALUES index 1 = "dmgB", 9 = "cgbC".
test("readRam tracks a live model switch that resizes WRAM (CGB 32K → DMG 8K), never freezing", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-wram-model-switch: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const audio = createAudioDriver();
  const id = 2; // a distinct id: the host/engine is shared across a file's tests, so test 1's id=1 lingers
  expect(be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom(SONG),
  }, id)).toBeTruthy();

  audio.renderAudio(2000);
  expect(be.readRam(id)!.length).toBe(0x8000); // cgbC default → 32K CGB WRAM

  // Switch to a DMG-family model → restartEmulator rebuilds the core with an 8K WRAM region.
  expect(be.applyRoleConfig(id, "sameboy", { model: 1 })).toBeTruthy(); // dmgB
  audio.renderAudio(2000); // a block republishes the (now smaller) region

  const dmgRam = be.readRam(id)!;
  expect(dmgRam.length).toBe(0x2000); // 8K DMG WRAM — NOT the frozen 32K pre-switch snapshot
  expect(eq(dmgRam, be.readMemory(id, MemoryRegion.Ram)!)).toBeTruthy(); // and it's the live region, not stale
  console.log(`[lsdj-wram-model-switch] post-switch readRam=${dmgRam.length} bytes (was 0x8000)`);
});
