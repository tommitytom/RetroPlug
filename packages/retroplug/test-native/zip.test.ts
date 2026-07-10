// The native zip/unzip codec (real miniz) round-trips through the Backend — the same
// entry-shape the .rplg export/import uses ({ name, bytes }).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";

test("zip → unzip round-trips entries through real miniz (PK archive)", () => {
  const be = createRealBackend();
  const entries = [
    { name: "project.json", bytes: new Uint8Array([0x7b, 0x7d]) }, // "{}"
    { name: "systems/0/state", bytes: new Uint8Array([1, 2, 3, 4]) },
  ];

  const archive = be.zip(entries)!;
  expect(archive[0]).toBe(0x50); // "P"
  expect(archive[1]).toBe(0x4b); // "K"

  const out = be.unzip(archive)!;
  const byName = (n: string) => out.find((e) => e.name === n)!;
  expect(out.map((e) => e.name).sort()).toEqual(["project.json", "systems/0/state"]);
  expect(byName("project.json").bytes).toEqual(new Uint8Array([0x7b, 0x7d]));
  expect(byName("systems/0/state").bytes).toEqual(new Uint8Array([1, 2, 3, 4]));
});
