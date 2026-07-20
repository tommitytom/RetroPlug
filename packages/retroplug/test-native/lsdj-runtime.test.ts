// End-to-end proof of the LSDj runtime-WRAM reader (plan Part B) against a LIVE SameBoy core: boot the
// bundled stock 9.4.2, identify it from its real header, read WRAM via readMemory(Ram), and decode.
// Also the per-version confirmation that the DRIFTING screen offset (CURRENT_SCREEN 0xC402, seeded from
// LSDisJ 9.2.L) is correct on 9.4.2 — the reader reports "song" while parked on the SONG screen.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { MemoryRegion } from "../src/backend";
import { LsdjReader } from "../src/lsdj/runtime";

declare const __RESOURCES_DIR__: string;
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7;

// All four channels driven, so every ARE_CHANNELS_PLAYING flag flips (see lsdj-wram-spike).
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

test("LsdjReader decodes live 9.4.2 WRAM: identify, screen, playing flags, positions", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-runtime: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom(SONG),
  }, id)).toBeTruthy();

  const reader = LsdjReader.fromHeader(be.readFilePrefix(LSDJ, 0x150)!);
  expect(reader.supported).toBeTruthy();
  expect(reader.version!.major).toBe(9);
  expect(reader.version!.minor).toBe(4);

  const readState = () => reader.read(be.readMemory(id, MemoryRegion.Ram)!);

  // Parked on the SONG screen, stopped. Confirms the drifting offsets (CURRENT_SCREEN 0xC401,
  // TEMPO 0xC529) are right on 9.4.2.
  audio.renderAudio(6000);
  const stopped = readState();
  console.log(`[lsdj-runtime] stopped: screen=${stopped.screen} playing=${stopped.playing} tempo=${stopped.tempo}`);
  expect(stopped.screen).toBe("song");
  expect(stopped.playing).toBeFalsy();
  expect(stopped.tempo).toBe(128); // authored tempo

  // START → free-running playback on all four channels.
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(400);
  const p1 = readState();
  audio.renderAudio(250);
  const p2 = readState();

  console.log(`[lsdj-runtime] playing: any=${p1.playing} pu1.phraseRow=${p1.channels.pu1.phraseRow}→${p2.channels.pu1.phraseRow} songRow=${p1.songRow}`);
  expect(p1.playing).toBeTruthy();
  expect(p1.channels.pu1.playing).toBeTruthy();
  expect(p1.channels.pu2.playing).toBeTruthy();
  expect(p1.channels.wav.playing).toBeTruthy();
  expect(p1.channels.noi.playing).toBeTruthy();
  // The phrase-row register tracks playback position (advances between snapshots).
  expect(p1.channels.pu1.phraseRow != null && p2.channels.pu1.phraseRow != null).toBeTruthy();
  expect(p1.channels.pu1.phraseRow !== p2.channels.pu1.phraseRow).toBeTruthy();
  // Song has one row → song position stays 0 (valid, not null) while playing.
  expect(p1.songRow).toBe(0);
});
