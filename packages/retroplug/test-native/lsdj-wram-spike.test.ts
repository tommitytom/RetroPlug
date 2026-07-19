// DE-RISK SPIKE for the LSDj runtime-WRAM reader (plan Part "Spike"). Proves three things on the
// bundled stock ROM (lsdj9_4_2.gb) before any reader/detector code is built:
//   1. backend.readMemory(id, MemoryRegion.Ram) returns real GB work RAM on a SameBoy core (no GB
//      test exercised this before — only NES via cli-observe).
//   2. The old OffsetCalculator differential technique reproduces headlessly: a 4-byte run that is
//      all-0x00 while stopped and all-0x01 while playing locates the per-channel PLAYING flags, and
//      a byte that increments across mid-playback snapshots locates a play-position register.
//   3. Whether 9.4.2 matches the LSDisJ 9.2.L seed addresses (ARE_CHANNELS_PLAYING=0xC0E0,
//      PLAYING_PHRASE_ROWS=0xC16C, PLAYING_SONG_ROWS=0xC200) — logged, so we know if the seed table
//      can be trusted for the newest stock build or if the detector must supply it.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";

declare const __RESOURCES_DIR__: string;
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start
const WRAM = 0xc000; // GB work-RAM base; readMemory(Ram) returns the region starting here

// A free-running (SYNC=None) song that drives ALL FOUR channels (PU1/PU2/WAV/NOI) at once — the four
// song-row columns each point at their own chain/phrase — so the per-channel PLAYING flags at 0xC0E0
// all flip 0→1, exactly the "all-4-bytes" signature the old OffsetCalculator relied on. START plays
// it with no external clock, so the phrase-row registers also advance every step.
const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;
const SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "None", tempo: 128 },
    rows: [{ chains: [0, 1, 2, 3] }],          // one row, all four channel columns active
    chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }, { phrases: [3] }],
    phrases: [
      { notes: [1], instruments: [0] },        // PU1
      { notes: [1], instruments: [1] },        // PU2
      { notes: [1], instruments: [2] },        // WAV
      { notes: [1], instruments: [3] },        // NOI
    ],
    instruments: [pulse, pulse, { type: "wave" }, { type: "noise" }],
  },
};

const hex = (n: number) => "0x" + n.toString(16);

test("LSDj WRAM spike: readMemory(Ram) on SameBoy + differential offset detection on 9.4.2", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-wram-spike: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom(SONG),
  }, id)).toBeTruthy();

  const readWram = (): Uint8Array => {
    const w = be.readMemory(id, MemoryRegion.Ram);
    if (!w) throw new Error("readMemory(Ram) returned null on SameBoy");
    return w;
  };

  audio.renderAudio(6000); // to the SONG screen (valid sav skips the self-test); still stopped
  const stopped = readWram();
  console.log(`[spike] WRAM length = ${stopped.length} (${stopped.length === 0x2000 ? "DMG 8K" : stopped.length === 0x8000 ? "CGB 32K" : "?"})`);

  // START → free-running playback.
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(400);
  const playing1 = readWram();
  audio.renderAudio(250);
  const playing2 = readWram();

  // (1) readMemory works and returns a sane region.
  expect(stopped.length >= 0x1000).toBeTruthy();

  // (2a) Differential active-flag scan (the old tool's `active` field): a 4-byte run all-0 stopped,
  // all-1 playing. Search the first 0x600 bytes (where LSDj's runtime block lives).
  const activeCandidates: number[] = [];
  for (let i = 0; i + 3 < 0x600; i++) {
    let ok = true;
    for (let k = 0; k < 4; k++) if (stopped[i + k] !== 0x00 || playing1[i + k] !== 0x01) { ok = false; break; }
    if (ok) activeCandidates.push(i);
  }
  console.log(`[spike] active-flag candidates (WRAM offsets): ${activeCandidates.map(hex).join(", ") || "(none)"}`);
  console.log(`[spike]   → absolute: ${activeCandidates.map((o) => hex(o + WRAM)).join(", ") || "(none)"}`);

  // (2b) Position scan: a byte that increased (and stays a valid LSDj row <= 0x7f) between the two
  // mid-playback snapshots — the phrase/chain/song row registers.
  const posCandidates: number[] = [];
  for (let i = 0; i < 0x600; i++) {
    if (playing1[i] <= 0x7f && playing2[i] <= 0x7f && playing2[i] !== playing1[i]) posCandidates.push(i);
  }
  console.log(`[spike] position-change candidates near seeds: ${posCandidates.filter((o) => o >= 0x140 && o <= 0x210).map(hex).join(", ") || "(none)"}`);

  // (3) Report against the LSDisJ 9.2.L seeds.
  const seed = { active: 0xc0e0, phrase: 0xc16c, chain: 0xc17c, song: 0xc200 };
  const at = (buf: Uint8Array, abs: number) => buf[abs - WRAM];
  console.log(`[spike] seed 0xC0E0 active flags  stopped=[${[0, 1, 2, 3].map((k) => at(stopped, seed.active + k)).join(",")}] playing=[${[0, 1, 2, 3].map((k) => at(playing1, seed.active + k)).join(",")}]`);
  console.log(`[spike] seed 0xC16C phrase rows   playing1=[${[0, 1, 2, 3].map((k) => at(playing1, seed.phrase + k)).join(",")}] playing2=[${[0, 1, 2, 3].map((k) => at(playing2, seed.phrase + k)).join(",")}]`);
  console.log(`[spike] seed 0xC200 song rows     playing1=[${[0, 1, 2, 3].map((k) => at(playing1, seed.song + k)).join(",")}] playing2=[${[0, 1, 2, 3].map((k) => at(playing2, seed.song + k)).join(",")}]`);
  const seedActiveMatches = activeCandidates.includes(seed.active - WRAM);
  console.log(`[spike] seed 0xC0E0 among detected active candidates? ${seedActiveMatches}`);

  // The load-bearing assertions, all validated by the run:
  //  - readMemory(Ram) reflects LIVE state: the PU1 PLAYING flag at 0xC0E0 goes 0 (stopped) → 1 (playing).
  //  - The 0xC16C phrase-row register tracks playback position (changes between mid-play snapshots).
  //  - The old differential technique reproduces headlessly: the all-0→all-1 4-byte scan locates the
  //    channel-playing block, and it sits at exactly the 9.2.L seed 0xC0E0 (so the seed table is trusted
  //    for the newest stock build).
  expect(at(stopped, seed.active) === 0 && at(playing1, seed.active) === 1).toBeTruthy();
  expect(at(playing1, seed.phrase) !== at(playing2, seed.phrase)).toBeTruthy();
  expect(activeCandidates.includes(seed.active - WRAM)).toBeTruthy();
});
