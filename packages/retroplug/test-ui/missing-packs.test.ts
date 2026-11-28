// A project naming packs this build does not have, in the real app on the headless display
// (docs/downloadable-packs-m3-design.md section 8): the load raises one notice, the placeholder's tile keeps its place
// with the card's text, its menu removes it, and the live tile's menu marks its missing role. No pack is loaded: every
// name here is this file's own, so the case runs the same in the pack mode.
import { test, expect, ui, navTo, Key, type WidgetInfo } from "ui-harness";
import { createRealBackend } from "../src/realBackend";

function clickCenter(w: WidgetInfo): void {
  ui.clickAt(w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2));
}

/** Whether the widget's box holds a pixel near the warning yellow (#ffd54f): the style is on screen, not just set. */
function isWarnColoured(w: WidgetInfo): boolean {
  const snap = ui.snapshot();
  for (let y = w.y; y < w.y + w.height; y++) {
    for (let x = w.x; x < w.x + w.width; x++) {
      const i = (y * snap.width + x) * 4; // B, G, R, A in memory
      const [b, g, r] = [snap.pixels[i], snap.pixels[i + 1], snap.pixels[i + 2]];
      if (r > 0xe0 && g > 0xb0 && b < 0x90) return true;
    }
  }
  return false;
}

test("placeholders and missing roles: the notice, the tile, the [!] row, and Remove", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  const be = createRealBackend();
  const project = ui.romDir() + "/rp-missing-packs.rplg";
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [
      { platform: "gb", core: "sameboy", embeddedRom: "mgb",
        roles: [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }, { kind: "mpui-role", config: {} }],
        provenance: { roles: { "mpui-role": { id: "mpui-roles", name: "MPUI Roles" } } } },
      { platform: "mpui-platform", core: "mpui-core", romPath: ui.romDir() + "/tune.mpui",
        provenance: { platform: { id: "mpui-console", name: "MPUI Console", version: "3.1.0" } } },
    ],
  };
  expect(be.writeFile(project, new TextEncoder().encode(JSON.stringify(doc)))).toBeTruthy();

  ui.fileDrop(project, 0, 0);
  ui.pump(40);
  expect(ui.findByText("Missing Packs") != null).toBeTruthy();
  expect(ui.findByTextContaining("MPUI Console: 1 system") != null).toBeTruthy();
  expect(ui.findByTextContaining("MPUI Roles: 1 role") != null).toBeTruthy();
  expect(navTo("OK")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("placeholder-1") != null).toBeTruthy();
  expect(ui.findByTextContaining("Needs the MPUI Console pack") != null).toBeTruthy();

  // The live tile is focused: its menu leads with the missing role.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("MPUI Roles pack missing [!]") != null).toBeTruthy();
  expect(isWarnColoured(ui.findByTextContaining("MPUI Roles pack missing [!]")!)).toBeTruthy();

  // One live system and a placeholder are two slots: the audio routing offers no split (Channels, Pins).
  expect(navTo("New Project")).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Down);
  ui.pump(2);
  expect(ui.focused()!.text).toBe("Project >");
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Audio Routing: Stereo")).toBeTruthy();
  ui.tapKey(Key.Left);
  ui.pump(6);
  expect(ui.focused()!.text).toBe("Audio Routing: 1 Ch / Inst"); // the last row: neither Channels nor a pin mode
  ui.tapKey(Key.Esc); // the submenu
  ui.pump(10);
  ui.tapKey(Key.Esc); // the menu
  ui.pump(10);
  ui.tapKey(Key.Esc);
  ui.pump(10);

  // The placeholder's menu: Remove only.
  clickCenter(ui.findByTestId("tile-1")!);
  ui.pump(10);
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance")).toBe(null);
  expect(navTo("Remove Instance")).toBeTruthy();
  expect(isWarnColoured(ui.findByTextContaining("Needs the MPUI Console pack")!)).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-1")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  be.deleteFile(project);
});
