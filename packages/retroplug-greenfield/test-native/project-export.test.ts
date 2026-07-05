// ProjectStore export/import over the REAL native Backend: the pump → real-miniz zip → disk
// round-trip. Mirrors the mock export tests' setup (test/project/export.test.ts) but asserts
// on observable outcomes — the on-disk PK archive, its unzipped entries, and a byte-exact
// seed→export→import→read round-trip through the real stub — never mock introspection.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { gbRom } from "../test/systems/fixtures";

declare const __CONFIG_DIR__: string;

const dec = new TextDecoder();

function newProject() {
  const be = createRealBackend();
  const recent = new RecentStore(be);
  recent.load();
  return { be, project: new ProjectStore(be, recent) };
}

test("export: a real PKZIP of project.json + each system's sram/state, from the pump", () => {
  const { be, project } = newProject();
  const rom = __CONFIG_DIR__ + "/a.gb";
  be.writeFile(rom, gbRom());
  const song = __CONFIG_DIR__ + "/song.rplg";

  project.systems.loadMgb(); // embedded → index 0
  project.systems.addSystem(rom); // file-backed → index 1
  project.setLayout(3);

  expect(project.export(song)).toBeTruthy();
  expect(be.fileExists(song)).toBeTruthy();

  // On disk it's a real PK zip; unzip it back (real miniz) and check the exact entry set.
  const archive = be.readFile(song)!;
  expect(Array.from(archive.slice(0, 4))).toEqual([0x50, 0x4b, 0x03, 0x04]);
  const entries = be.unzip(archive)!;
  expect(entries.map((e) => e.name).sort()).toEqual([
    "project.json",
    "systems/0/sram",
    "systems/0/state",
    "systems/1/sram",
    "systems/1/state",
  ]);

  // project.json re-parses to the thin config (embedded marker + relative ROM path).
  const doc = JSON.parse(dec.decode(entries.find((e) => e.name === "project.json")!.bytes));
  expect(doc.settings.layout).toBe(3);
  expect(doc.systems.length).toBe(2);
  expect(doc.systems[0].embeddedRom).toBe("mgb");
  expect(doc.systems[1].romPath).toBe("a.gb"); // rebased relative, not embedded as a blob
});

test("export then load: round-trips systems + settings; the archive seeds each stub byte-exactly", () => {
  const { be, project } = newProject();
  const rom = __CONFIG_DIR__ + "/rt.gb";
  be.writeFile(rom, gbRom());
  const song = __CONFIG_DIR__ + "/rt.rplg";

  project.systems.loadMgb();
  project.systems.addSystem(rom);
  project.setZoom(4);
  expect(project.export(song)).toBeTruthy();

  // Capture the exported file-backed system's SRAM blob from the real archive.
  const exportedSram = be.unzip(be.readFile(song)!)!.find((e) => e.name === "systems/1/sram")!.bytes;

  project.newProject();
  expect(project.systems.view().length).toBe(0);

  const out = project.load(song);
  expect(out).toEqual({ kind: "loaded", systems: 2 });
  const v = project.systems.view();
  expect(v.map((s) => s.embedded)).toEqual([true, false]); // mgb, then the file-backed ROM
  expect(v.map((s) => s.romPath)).toEqual(["", rom]); // absolutized back
  expect(project.settings().zoom).toBe(4);

  // The import seeded each stub from the archive; the pump now echoes those exact bytes —
  // proving seed → zip → unzip → seed → read round-trips through real miniz + the stub.
  expect(be.readSram(v[1].id)!).toEqual(exportedSram);
});
