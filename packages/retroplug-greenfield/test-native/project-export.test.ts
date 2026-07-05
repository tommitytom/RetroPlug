// ProjectStore export/import over the REAL native Backend — now with real SameBoy cores. The
// pump reads real savestate/SRAM, frames a real-miniz PKZIP, and round-trips. Savestates are
// always present (any booted core); SRAM only for battery carts, so the file-backed system
// uses gbRomBattery() and we don't assume mGB has SRAM. Observable outcomes only.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { gbRomBattery } from "../test/systems/fixtures";

declare const __CONFIG_DIR__: string;

const dec = new TextDecoder();

function newProject() {
  const be = createRealBackend();
  const recent = new RecentStore(be);
  recent.load();
  return { be, project: new ProjectStore(be, recent) };
}

test("export: a real PKZIP of project.json + each core's savestate (+ SRAM for battery carts)", () => {
  const { be, project } = newProject();
  const rom = __CONFIG_DIR__ + "/a.gb";
  be.writeFile(rom, gbRomBattery());
  const song = __CONFIG_DIR__ + "/song.rplg";

  project.systems.loadMgb(); // embedded → index 0
  project.systems.addSystem(rom); // file-backed battery cart → index 1
  project.setLayout(3);

  expect(project.export(song)).toBeTruthy();
  expect(be.fileExists(song)).toBeTruthy();

  const archive = be.readFile(song)!;
  expect(Array.from(archive.slice(0, 4))).toEqual([0x50, 0x4b, 0x03, 0x04]); // PK magic
  const names = be.unzip(archive)!.map((e) => e.name);
  // Savestates are always present (both cores booted); the battery cart also has SRAM.
  for (const n of ["project.json", "systems/0/state", "systems/1/state", "systems/1/sram"])
    expect(names.includes(n)).toBeTruthy();

  // project.json re-parses to the thin config (embedded marker + relative ROM path).
  const doc = JSON.parse(dec.decode(be.unzip(archive)!.find((e) => e.name === "project.json")!.bytes));
  expect(doc.settings.layout).toBe(3);
  expect(doc.systems.length).toBe(2);
  expect(doc.systems[0].embeddedRom).toBe("mgb");
  expect(doc.systems[1].romPath).toBe("a.gb"); // rebased relative, not embedded as a blob
});

test("export then load: round-trips systems + settings; the archive re-seeds the real core", () => {
  const { be, project } = newProject();
  const rom = __CONFIG_DIR__ + "/rt.gb";
  be.writeFile(rom, gbRomBattery());
  const song = __CONFIG_DIR__ + "/rt.rplg";

  project.systems.loadMgb();
  project.systems.addSystem(rom);
  project.setZoom(4);
  expect(project.export(song)).toBeTruthy();

  // Capture the exported file-backed core's SRAM blob from the real archive.
  const exportedSram = be.unzip(be.readFile(song)!)!.find((e) => e.name === "systems/1/sram")!.bytes;

  project.newProject();
  expect(project.systems.view().length).toBe(0);

  const out = project.load(song);
  expect(out).toEqual({ kind: "loaded", systems: 2 });
  const v = project.systems.view();
  expect(v.map((s) => s.embedded)).toEqual([true, false]); // mgb, then the file-backed ROM
  expect(v.map((s) => s.romPath)).toEqual(["", rom]); // absolutized back
  expect(project.settings().zoom).toBe(4);

  // The import re-seeded the real core from the archive; the pump now reads those same SRAM
  // bytes back — proving seed → boot → snapshot → zip → unzip → reboot round-trips real bytes.
  expect(be.readSram(v[1].id)!).toEqual(exportedSram);
});
