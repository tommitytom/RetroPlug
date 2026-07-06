// The built-in ROM providers attach feature roles by ROM identity — the TS twin of the C++
// RomSniffer default-role step. mGB is matched by the embedded marker (its bytes never reach
// TS) OR a file-backed "MGB" title; LSDj by a case-insensitive "LSDJ" title prefix. Driven
// through defaultRoles so the sameboy system role + provider features compose as they do live.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerRomProviders } from "../../src/romProviders";

// A GB header (0x150 bytes) carrying an ASCII cartridge title at 0x134.
function headerWithTitle(title: string): Uint8Array {
  const h = new Uint8Array(0x150);
  for (let i = 0; i < title.length; i++) h[0x134 + i] = title.charCodeAt(i);
  return h;
}

function registry(): RoleRegistry {
  const reg = new RoleRegistry();
  registerCoreRoles(reg); // sameboy system role
  registerDspRoles(reg); // mgb / lsdj-sync / midi-routing role types (schemas)
  registerRomProviders(reg); // the providers under test
  return reg;
}

test("embedded mGB attaches the mgb role (empty header — matched by the marker)", () => {
  const roles = registry().defaultRoles("sameboy", new Uint8Array(), "mgb");
  expect(roles.map((r) => r.kind)).toEqual(["sameboy", "mgb"]);
});

test("a file-backed mGB cart attaches the mgb role by title", () => {
  const roles = registry().defaultRoles("sameboy", headerWithTitle("MGB"));
  expect(roles.map((r) => r.kind)).toEqual(["sameboy", "mgb"]);
});

test("LSDj attaches lsdj-sync (MidiSync default), case-insensitively", () => {
  const lower = registry().defaultRoles("sameboy", headerWithTitle("LSDj-v9.4.2"));
  expect(lower).toEqual([
    { kind: "sameboy", config: { model: 9, highpass: 1, linkGroupId: 0, fastBoot: true } },
    { kind: "lsdj-sync", config: { mode: 1 } },
  ]);
  // Older ROMs stamp an uppercase "LSDJ" title — must still match.
  const upper = registry().defaultRoles("sameboy", headerWithTitle("LSDJ"));
  expect(upper.map((r) => r.kind)).toEqual(["sameboy", "lsdj-sync"]);
});

test("an unrelated GB cart gets only the sameboy system role", () => {
  const roles = registry().defaultRoles("sameboy", headerWithTitle("ZELDA"));
  expect(roles.map((r) => r.kind)).toEqual(["sameboy"]);
});
