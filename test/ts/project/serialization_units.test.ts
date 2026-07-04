// Unit tests for the pure TS project-serialization helpers moved out of C++
// (schema versioning + the .rplg zip entry-key contract). No emulator needed.
// Run: pnpm test:cli project/serialization_units
import { test, expect } from "harness";
import {
  checkVersion, parseProjectVersion, VersionCheck, K_PROJECT,
  blobKey, parseBlobKey, isBlobEntry, PROJECT_JSON,
} from "@retroplug/retroplug";

test("schema version: compare + legacy-string parse", () => {
  expect(checkVersion(1, 1)).toBe(VersionCheck.Ok);
  expect(checkVersion(1, 2)).toBe(VersionCheck.Older);
  expect(checkVersion(3, 2)).toBe(VersionCheck.Newer);

  expect(parseProjectVersion("1.0")).toBe(1);
  expect(parseProjectVersion("2")).toBe(2);
  expect(parseProjectVersion("  10.3 ")).toBe(10);
  // No leading digits -> floor to the baseline (an old file predating the stamp).
  expect(parseProjectVersion("")).toBe(K_PROJECT);
  expect(parseProjectVersion("garbage")).toBe(K_PROJECT);
});

test("blob key contract: build + parse round-trip", () => {
  expect(blobKey({ systemIndex: 0, kind: "rom" })).toBe("systems/0/rom");
  expect(blobKey({ systemIndex: 2, kind: "sram" })).toBe("systems/2/sram");
  expect(blobKey({ systemIndex: 2, kind: "state" })).toBe("systems/2/state");
  expect(blobKey({ systemIndex: 1, kind: "kit", roleIndex: 0, kitIndex: 3 }))
    .toBe("systems/1/roles/0/kits/3/compiled");

  // (harness deepEqual doesn't handle plain objects — compare via JSON.)
  const j = (v: unknown) => JSON.stringify(v);
  expect(j(parseBlobKey("systems/0/rom"))).toBe(j({ systemIndex: 0, kind: "rom" }));
  expect(j(parseBlobKey("systems/5/state"))).toBe(j({ systemIndex: 5, kind: "state" }));
  expect(j(parseBlobKey("systems/1/roles/0/kits/3/compiled")))
    .toBe(j({ systemIndex: 1, kind: "kit", roleIndex: 0, kitIndex: 3 }));

  // project.json and unknown names are not blob entries.
  expect(parseBlobKey(PROJECT_JSON)).toBe(null);
  expect(parseBlobKey("bogus")).toBe(null);
  expect(isBlobEntry(PROJECT_JSON)).toBe(false);
  expect(isBlobEntry("systems/0/rom")).toBe(true);
});
