// Unit tests for the shared missing-files scan/relink logic (missingFiles.ts),
// the TS port of packages/native/src/project/ProjectMissingFiles.hpp. No emulator
// needed. "Blob present" (romBytes / sram / savestate / compiled-kit) is modelled
// as the keyed zip entry being in the blobKeys set, via the same blobKey contract
// the native codec uses. Ported from the deleted [MissingFiles]/[relink] Catch2
// cases. Run: pnpm test:cli project/missing_files
import { test, expect } from "harness";
import {
  scanMissingFiles, relinkInConfig, autoFindSiblings, toAbsolute, blobKey,
  type ProjectConfig,
} from "@retroplug/retroplug";

const NONE = new Set<string>();
const exists = (present: string[]) => (p: string) => present.includes(p);
const romKey = (i: number) => blobKey({ systemIndex: i, kind: "rom" });
const cfg = (systems: unknown[]): ProjectConfig =>
  ({ schemaVersion: "1", systems } as unknown as ProjectConfig);

test("scan: flags only absent, needed ROMs", () => {
  const c = cfg([
    { kind: "sameboy", romPath: "/nope/a.gb" },  // present via rom blob (zip)
    { kind: "sameboy", romPath: "/real/b.gb" },  // path exists
    { kind: "sameboy", romPath: "/nope/c.gb" },  // path-only, gone
  ]);
  const blobs = new Set([romKey(0)]);
  const missing = scanMissingFiles(c, blobs, exists(["/real/b.gb"]));
  expect(missing.length).toBe(1);
  expect(missing[0].systemIndex).toBe(2);
  expect(missing[0].itemKind).toBe("rom");
  expect(missing[0].path).toBe("/nope/c.gb");
});

test("scan: kit samples flagged only when the kit isn't bundled", () => {
  const c = cfg([{
    kind: "sameboy", romPath: "/r.gb",
    roles: [{ type: "lsdj-kit-patch", kits: [{ slot: 3, samples: [{ path: "/nope/kick.wav" }] }] }],
  }]);
  // Not bundled (only the rom blob present) -> the missing WAV is flagged.
  const needs = scanMissingFiles(c, new Set([romKey(0)]), exists([]));
  expect(needs.length).toBe(1);
  expect(needs[0].itemKind).toBe("sample");
  expect(needs[0].kitSlot).toBe(3);
  expect(needs[0].sampleIndex).toBe(0);
  // Bundled (the compiled-kit blob is present) -> self-sufficient.
  const bundled = new Set([romKey(0), blobKey({ systemIndex: 0, kind: "kit", roleIndex: 0, kitIndex: 0 })]);
  expect(scanMissingFiles(c, bundled, exists([])).length).toBe(0);
});

test("scan: a missing paired savPath is flagged sram; negatives are present", () => {
  const withSav = (sav?: string) => cfg([{ kind: "sameboy", romPath: "/r.gb", savPath: sav }]);
  const m = scanMissingFiles(withSav("/nope/x.sav"), new Set([romKey(0)]), exists([]));
  expect(m.length).toBe(1);
  expect(m[0].itemKind).toBe("sram");
  expect(m[0].path).toBe("/nope/x.sav");

  // empty savPath (suffix sibling), an existing file, or an sram/state blob = present.
  expect(scanMissingFiles(withSav(undefined), new Set([romKey(0)]), exists([])).length).toBe(0);
  expect(scanMissingFiles(withSav("/real.sav"), new Set([romKey(0)]), exists(["/real.sav"])).length).toBe(0);
  const withSram = new Set([romKey(0), blobKey({ systemIndex: 0, kind: "sram" })]);
  expect(scanMissingFiles(withSav("/nope/x.sav"), withSram, exists([])).length).toBe(0);
  const withState = new Set([romKey(0), blobKey({ systemIndex: 0, kind: "state" })]);
  expect(scanMissingFiles(withSav("/nope/x.sav"), withState, exists([])).length).toBe(0);
});

test("scan: an embedded-mGB system is present (no path, no bytes)", () => {
  expect(scanMissingFiles(cfg([{ kind: "sameboy", embeddedRom: "mgb" }]), NONE, exists([])).length).toBe(0);
  // Sanity: a genuinely missing ROM (no marker/blob/file) IS flagged.
  expect(scanMissingFiles(cfg([{ kind: "sameboy", romPath: "/nope.gb" }]), NONE, exists([])).length).toBe(1);
});

test("scan: missing both ROM + paired save yields two distinct-kind items", () => {
  const m = scanMissingFiles(cfg([{ kind: "sameboy", romPath: "/nope.gb", savPath: "/nope.sav" }]), NONE, exists([]));
  expect(m.length).toBe(2);
  expect(m[0].systemIndex).toBe(m[1].systemIndex);
  expect(m[0].kitSlot).toBe(m[1].kitSlot);          // both -1
  expect(m[0].itemKind !== m[1].itemKind).toBeTruthy(); // only itemKind differs
});

test("relink: sram sets savPath and leaves romPath untouched", () => {
  const c = cfg([{ kind: "sameboy", romPath: "/orig.gb", savPath: "/old.sav" }]);
  expect(relinkInConfig(c, { systemIndex: 0, itemKind: "sram", path: "/old.sav", kitSlot: -1, sampleIndex: -1 }, "/located.sav")).toBeTruthy();
  const sb = c.systems[0] as { romPath: string; savPath: string };
  expect(sb.savPath).toBe("/located.sav");
  expect(sb.romPath).toBe("/orig.gb");
});

test("relink + autoFindSiblings repair a moved folder", () => {
  const c = cfg([{
    kind: "sameboy", romPath: "/old/song.gb",
    roles: [{ type: "lsdj-kit-patch", kits: [{ slot: 0, samples: [{ path: "/old/kick.wav" }] }] }],
  }]);
  const ex = exists(["/new/song.gb", "/new/kick.wav"]);
  const m = scanMissingFiles(c, NONE, ex);
  expect(m.length).toBe(2); // rom + sample
  const romItem = m.find((x) => x.itemKind === "rom")!;
  expect(relinkInConfig(c, romItem, "/new/song.gb")).toBeTruthy();
  expect(autoFindSiblings(c, "/new", NONE, ex)).toBe(1); // sibling WAV located
  expect(scanMissingFiles(c, NONE, ex).length).toBe(0);
  const sb = c.systems[0] as { roles: { kits: { samples: { path: string }[] }[] }[] };
  expect(sb.roles[0].kits[0].samples[0].path).toBe("/new/kick.wav");
});

test("toAbsolute rebases relative paths, leaves absolute + empty", () => {
  const c = cfg([{
    kind: "sameboy", romPath: "song.gb", savPath: "/abs/keep.sav",
    roles: [{ type: "lsdj-kit-patch", kits: [{ slot: 0, samples: [{ path: "kits/kick.wav" }] }] }],
  }]);
  toAbsolute(c, "/base/dir");
  const sb = c.systems[0] as { romPath: string; savPath: string; roles: { kits: { samples: { path: string }[] }[] }[] };
  expect(sb.romPath).toBe("/base/dir/song.gb");
  expect(sb.savPath).toBe("/abs/keep.sav");                 // absolute untouched
  expect(sb.roles[0].kits[0].samples[0].path).toBe("/base/dir/kits/kick.wav");
});
