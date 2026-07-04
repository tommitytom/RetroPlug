// Headless UI: drive the real "Save Project As..." and "Export Zip" menu flows
// end-to-end and assert what lands on disk. Both now run the shared TS
// orchestration (@retroplug/retroplug projectSerialization.ts) over the plugin's
// native byte-mover primitives (snapshotProjectConfig / zipEntries / writeFile),
// exercised through the REAL PluginRpcService + UI bundle. Save writes a thin
// path-only JSON `.rplg`; Export writes a self-contained PKZIP.
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb"; // repo-relative (runner cwd = repo root)
const RPLG = "/tmp/rp_save_roundtrip.rplg";
const ZIP  = "/tmp/rp_save_roundtrip.zip";

const dec = new TextDecoder();
function latin1(bytes: Uint8Array): string {
  let s = "";
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return s;
}

// Open the Project submenu from the instance menu, activate a labelled row, then
// complete the file dialog it opens by selecting `path`.
function saveVia(row: string, path: string) {
  ui.tapKey(Key.Esc); // open the instance menu
  ui.pump(30);
  const proj = ui.findByTextContaining("Project >");
  expect(proj).toBeTruthy();
  ui.clickAt(proj!.x + (proj!.width >> 1), proj!.y + (proj!.height >> 1));
  ui.pump(20);
  const item = ui.findByText(row);
  expect(item).toBeTruthy();
  ui.clickAt(item!.x + (item!.width >> 1), item!.y + (item!.height >> 1));
  ui.pump(20);
  // The dialog routes to onFileBrowserSelected -> emit "..-path-selected" ->
  // the UI runs the shared save/export orchestration.
  ui.selectFile(path);
  ui.pump(30);
}

test("Save + Export write a thin .rplg and a self-contained PKZIP via shared TS", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  ui.loadRom(MGB); // adopts directly — no currentProjectPath_
  ui.pump(60);

  // --- Save Project As... -> thin path-only JSON -------------------------
  saveVia("Save Project As...", RPLG);
  const saved = ui.readFile(RPLG);
  expect(saved.length > 0).toBeTruthy();
  expect(saved[0]).toBe(0x7b); // '{' — JSON, not a PK zip
  const cfg = JSON.parse(dec.decode(saved)) as {
    schemaVersion?: string;
    systems?: { kind?: string }[];
  };
  expect(typeof cfg.schemaVersion).toBe("string");
  expect(Array.isArray(cfg.systems) && cfg.systems!.length === 1).toBeTruthy();
  expect(cfg.systems![0].kind).toBe("sameboy");

  // --- Export Zip -> self-contained PKZIP --------------------------------
  saveVia("Export Zip", ZIP);
  const zip = ui.readFile(ZIP);
  expect(zip.length > 0).toBeTruthy();
  expect(zip[0]).toBe(0x50); // 'P'
  expect(zip[1]).toBe(0x4b); // 'K'
  // Entry names appear verbatim in the local file headers: project.json plus
  // the ROM blob at its keyed entry (systems/0/rom).
  const raw = latin1(zip);
  expect(raw.includes("project.json")).toBeTruthy();
  expect(raw.includes("systems/0/rom")).toBeTruthy();
});
