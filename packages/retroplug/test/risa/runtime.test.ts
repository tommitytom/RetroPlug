// risa runtime reader — pure unit tests (field mapping, accessor semantics, sentinels, version resolve,
// ROM version scan). The end-to-end proof that the generated addresses match real risa is the native
// test-native/risa-runtime; here we exercise the decode logic over synthetic RAM.
import { test, expect } from "../../testing/harness";
import {
  decodeRisaState,
  resolveRisaLayout,
  supportedRisaVersions,
  identifyRisaVersion,
} from "../../src/risa/runtime";

const layout = resolveRisaLayout("2.2.1")!;

test("resolveRisaLayout resolves a known version and rejects an unknown one", () => {
  expect(resolveRisaLayout("2.2.1") != null).toBeTruthy();
  expect(resolveRisaLayout("1.0.0")).toBe(null);
  expect(resolveRisaLayout(null)).toBe(null);
  expect(supportedRisaVersions().includes("2.2.1")).toBeTruthy();
});

test("decodeRisaState degrades to unsupported for a null layout or an undersized snapshot", () => {
  expect(decodeRisaState(new Uint8Array(0x800), null).supported).toBeFalsy();
  expect(decodeRisaState(new Uint8Array(4), layout).supported).toBeFalsy(); // can't cover the addresses
});

test("decodeRisaState maps global + per-track fields, the last-row fallback, and 0xFF sentinels", () => {
  const ram = new Uint8Array(0x800);
  ram[layout.seqMode] = 1; // song
  ram[layout.seqActive] = 0b00101; // tracks 0 and 2 active
  ram[layout.bpm] = 128; // u16 LE = 128
  ram[layout.currentScreen] = 2; // song
  ram[layout.cursorRow] = 3;
  ram[layout.cursorCol] = 4;
  ram[layout.uiTrack] = 1;
  ram[layout.kitActive] = 5;

  // track 0: last song row is 0xFF → seq_get_song_row falls back to the live row (7); real positions.
  ram[layout.songLastRow + 0] = 0xff;
  ram[layout.songRow + 0] = 7;
  ram[layout.chainId + 0] = 2;
  ram[layout.chainRow + 0] = 1;
  ram[layout.phraseId + 0] = 10;
  ram[layout.phraseLastRow + 0] = 5;
  ram[layout.note + 0] = 60;
  ram[layout.lastInst + 0] = 3;
  // track 1: all 0xFF → every position reads null (parked/inactive).
  for (const base of [layout.songLastRow, layout.songRow, layout.chainId, layout.chainRow, layout.phraseId, layout.phraseLastRow, layout.note, layout.lastInst]) ram[base + 1] = 0xff;
  // track 2: last song row present (4) → used directly (no fallback to the live 9).
  ram[layout.songLastRow + 2] = 4;
  ram[layout.songRow + 2] = 9;

  const s = decodeRisaState(ram, layout);
  expect(s.supported).toBeTruthy();
  expect(s.playing).toBeTruthy();
  expect(s.mode).toBe("song");
  expect(s.bpm).toBe(128);
  expect(s.fourX).toBeFalsy();
  expect(s.screen).toBe("song");
  expect(s.cursor).toEqual({ row: 3, col: 4 });
  expect(s.uiTrack).toBe(1);
  expect(s.kitActive).toBe(5);

  expect(s.tracks[0]).toEqual({ active: true, songRow: 7, chainId: 2, chainRow: 1, phraseId: 10, phraseRow: 5, note: 60, instrument: 3 });
  expect(s.tracks[1].active).toBeFalsy();
  expect(s.tracks[1].chainId).toBe(null); // 0xFF → null
  expect(s.tracks[1].phraseRow).toBe(null);
  expect(s.tracks[2].active).toBeTruthy();
  expect(s.tracks[2].songRow).toBe(4); // last-row present → no fallback
});

test("decodeRisaState reads the 4x tempo sentinel and a stopped state", () => {
  const ram = new Uint8Array(0x800);
  ram[layout.bpm] = 296 & 0xff; // 0x28
  ram[layout.bpm + 1] = 296 >> 8; // 0x01  → u16 296 = TEMPO_MODE_4X
  const s = decodeRisaState(ram, layout);
  expect(s.fourX).toBeTruthy();
  expect(s.bpm).toBe(null); // 4x mode reports no numeric BPM
  expect(s.playing).toBeFalsy(); // seq_mode 0 → stopped
  expect(s.mode).toBe("stopped");
});

test("identifyRisaVersion scans the PRG for the RISA V marker", () => {
  const rom = new Uint8Array(0x400);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // iNES header
  const marker = "RISA V2.2.1";
  for (let i = 0; i < marker.length; i++) rom[0x123 + i] = marker.charCodeAt(i); // buried in the PRG
  expect(identifyRisaVersion(rom)).toBe("2.2.1");
  expect(identifyRisaVersion(new Uint8Array(0x100))).toBe(null); // no marker
});
