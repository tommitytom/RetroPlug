// risa ROM detection (M2 UI plumbing): the iNES-header fingerprint + the provider that attaches the
// `risa` marker role. Pure-TS (no core) — mirrors how the LSDj provider is exercised via the registry.
import { test, expect } from "../../testing/harness";
import { isRisaRomHeader, isRisaSyncRom, risaSyncVersion } from "../../src/risa";
import { buildAppRegistry } from "../../src/appHost";

// The real risa 2.2.1 iNES 2.0 header (bytes 0..15): NES2.0, mapper 5, battery, 512KB PRG, 32KB CHR,
// 64KB PRG-NVRAM. (Matches build/risa-pal.nes + src/crt0.s.)
const RISA_HEADER = new Uint8Array([
  0x4e, 0x45, 0x53, 0x1a, 0x20, 0x04, 0x53, 0x08, 0x00, 0x00, 0xa0, 0x00, 0x01, 0x00, 0x00, 0x00,
]);
// NROM game (mapper 0, iNES 1.0, no battery).
const NROM_HEADER = new Uint8Array([
  0x4e, 0x45, 0x53, 0x1a, 0x02, 0x01, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,
]);
// An MMC5 (mapper 5) NES 2.0 cart WITH battery but only 8KB PRG-RAM — NOT risa (no 64KB NVRAM).
const MMC5_8K_HEADER = new Uint8Array([
  0x4e, 0x45, 0x53, 0x1a, 0x10, 0x08, 0x52, 0x08, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
]);

test("isRisaRomHeader accepts the risa fingerprint and rejects other NES ROMs", () => {
  expect(isRisaRomHeader(RISA_HEADER)).toBeTruthy();
  expect(isRisaRomHeader(NROM_HEADER)).toBeFalsy(); // mapper 0, not NES2.0, no battery
  expect(isRisaRomHeader(MMC5_8K_HEADER)).toBeFalsy(); // MMC5 + battery but 8KB PRG-RAM, not 64KB NVRAM
  expect(isRisaRomHeader(new Uint8Array(4))).toBeFalsy(); // too short
});

test("the risa ROM provider attaches the `risa` marker role to a risa ROM only", () => {
  const reg = buildAppRegistry();
  const risaRoles = reg.defaultRoles("mesen", "nes", RISA_HEADER).map((r) => r.kind);
  expect(risaRoles.includes("risa")).toBeTruthy();
  expect(risaRoles.includes("nes-n8-midi")).toBeTruthy(); // still gets the always-on NES MIDI role

  const nromRoles = reg.defaultRoles("mesen", "nes", NROM_HEADER).map((r) => r.kind);
  expect(nromRoles.includes("risa")).toBeFalsy(); // a plain NES ROM is not risa
  expect(nromRoles.includes("nes-n8-midi")).toBeTruthy();
});

// --- host-sync capability marker ("RISA-SYNC" + version byte in the header prefix) ---------------------

// A full 0x150 RomContext header prefix: the 16-byte iNES header + the ASCII "RISA-SYNC" tag and a version
// byte at `at` (16 in the real ROMs — right after the header).
function withSyncMarker(base: Uint8Array, version = 0x01, at = 16): Uint8Array {
  const marker = "RISA-SYNC";
  const h = new Uint8Array(0x150);
  h.set(base.subarray(0, Math.min(base.length, 0x150)), 0);
  for (let i = 0; i < marker.length; i++) h[at + i] = marker.charCodeAt(i);
  h[at + marker.length] = version;
  return h;
}

test("risaSyncVersion reads the RISA-SYNC marker's version byte; -1 / false when absent", () => {
  expect(risaSyncVersion(withSyncMarker(RISA_HEADER, 0x01))).toBe(0x01);
  expect(risaSyncVersion(withSyncMarker(RISA_HEADER, 0x02))).toBe(0x02);
  expect(risaSyncVersion(withSyncMarker(RISA_HEADER, 0x01, 100))).toBe(0x01); // found anywhere in the prefix
  expect(risaSyncVersion(RISA_HEADER)).toBe(-1); // a bare 16-byte header carries no marker
  expect(isRisaSyncRom(withSyncMarker(RISA_HEADER))).toBeTruthy();
  expect(isRisaSyncRom(RISA_HEADER)).toBeFalsy();
});

test("the provider attaches risa-sync only to a marker-bearing risa ROM", () => {
  const reg = buildAppRegistry();
  const syncRoles = reg.defaultRoles("mesen", "nes", withSyncMarker(RISA_HEADER)).map((r) => r.kind);
  expect(syncRoles.includes("risa")).toBeTruthy();
  expect(syncRoles.includes("risa-sync")).toBeTruthy();
  expect(syncRoles.includes("nes-n8-midi")).toBeTruthy(); // sync + host-note passthrough coexist on the FIFO

  const plainRoles = reg.defaultRoles("mesen", "nes", RISA_HEADER).map((r) => r.kind);
  expect(plainRoles.includes("risa")).toBeTruthy(); // still a risa ROM
  expect(plainRoles.includes("risa-sync")).toBeFalsy(); // but not sync-capable → no risa-sync

  // The marker on a NON-risa (NROM) header must NOT attach risa-sync — the provider gates on the risa
  // fingerprint first.
  const nromSync = reg.defaultRoles("mesen", "nes", withSyncMarker(NROM_HEADER)).map((r) => r.kind);
  expect(nromSync.includes("risa-sync")).toBeFalsy();
});
