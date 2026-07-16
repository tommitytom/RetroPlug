// projectBinaries: the pure `.rplg` blob key contract + entry-list transforms (no
// Backend, no IO). Locks the key spellings native shares and the partition/blobKeys
// helpers the import path leans on.
import { test, expect } from "../../testing/harness";
import {
  PROJECT_JSON,
  romKey,
  sramKey,
  stateKey,
  blobKeysFromEntries,
  partitionEntries,
} from "../../src/projectBinaries";

test("keys: the per-system blob key contract (shared with native)", () => {
  expect(PROJECT_JSON).toBe("project.json");
  expect(romKey(0)).toBe("systems/0/rom");
  expect(sramKey(1)).toBe("systems/1/sram");
  expect(stateKey(2)).toBe("systems/2/state");
});

test("partition: splits project.json from the blob map; blobKeys excludes it", () => {
  const entries = [
    { name: PROJECT_JSON, bytes: new Uint8Array([1, 2]) },
    { name: stateKey(0), bytes: new Uint8Array([3]) },
    { name: sramKey(0), bytes: new Uint8Array([4]) },
  ];
  const { config, blobs } = partitionEntries(entries);
  expect(config).toEqual(new Uint8Array([1, 2]));
  expect([...blobs.keys()].sort()).toEqual(["systems/0/sram", "systems/0/state"]);
  expect(blobs.get(stateKey(0))).toEqual(new Uint8Array([3]));
  expect([...blobKeysFromEntries(entries)].sort()).toEqual(["systems/0/sram", "systems/0/state"]);
});

test("partition: an archive without project.json yields config null", () => {
  const { config, blobs } = partitionEntries([{ name: stateKey(0), bytes: new Uint8Array([9]) }]);
  expect(config).toBe(null);
  expect(blobs.size).toBe(1);
});
