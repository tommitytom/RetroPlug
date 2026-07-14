// FileSelection.browseAdd / browseReplace — the async dialog flows that mutate the
// systems store. Each opens the OS ROM-or-sav browser and resolves to the FINAL
// outcome after every dialog settles, so an unpaired .sav simply awaits a 2nd
// (ROM-only) browser inside the same Promise — no pending latch. The resolve-only
// "Load…" branch lives in route.test.ts.
import { test, expect } from "../../testing/harness";
import { MockBackend, sramBytesFor } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { FileSelection } from "../../src/fileSelection";
import { buildAppRegistry } from "../../src/appHost";
import { gbRom } from "../systems/fixtures";

function newFs() {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be);
  const fs = new FileSelection(be, systems);
  return { be, systems, fs };
}

// A FileSelection over a role-registry-backed store, so added systems carry their `sameboy` role — needed
// for the link-group inheritance test (the bare store above builds roleless systems).
function newFsWithRoles() {
  const be = new MockBackend("/cfg");
  const systems = new SystemsStore(be, () => {}, buildAppRegistry());
  const fs = new FileSelection(be, systems);
  return { be, systems, fs };
}

test("browseAdd: opens the ROM-or-sav dialog and appends the picked ROM", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.queueBrowse("/roms/a.gb");
  const out = await fs.browseAdd();
  expect(out.kind).toBe("added");
  expect(systems.view().length).toBe(1);
  expect(be.fileBrowserCalls.length).toBe(1);
  expect(be.fileBrowserCalls[0].patterns.includes("*.sav")).toBeTruthy(); // ROM-or-sav
});

test("browseAdd: a cancelled dialog yields cancelled and builds nothing", async () => {
  const { be, fs, systems } = newFs();
  be.queueBrowse(null); // user closed the dialog
  const out = await fs.browseAdd();
  expect(out).toEqual({ kind: "cancelled" });
  expect(systems.view().length).toBe(0);
});

test("browseAdd(parentId): the new instance inherits the parent's link group, promoting a lone parent to 1", async () => {
  const { be, fs, systems } = newFsWithRoles();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  const parent = systems.addSystem("/roms/a.gb")!; // group 0
  const groupOf = (id: number) =>
    (systems.view().find((s) => s.id === id)!.roles.find((r) => r.kind === "sameboy")!.config as { linkGroupId: number }).linkGroupId;

  be.queueBrowse("/roms/b.gb");
  const out = await fs.browseAdd(parent);
  expect(out.kind).toBe("added");
  const child = (out as { system: number }).system;
  expect(groupOf(parent)).toBe(1); // the lone parent promoted
  expect(groupOf(child)).toBe(1); // the added instance joined it
});

test("browseAdd: an unpaired .sav awaits a 2nd ROM-only browser and pairs the result", async () => {
  const { be, fs } = newFs();
  be.seed("/saves/orphan.sav", "battery"); // no sibling ROM in its folder
  be.seed("/roms/thegame.gb", gbRom());
  be.queueBrowse("/saves/orphan.sav", "/roms/thegame.gb"); // 1st dialog, then the 2nd
  const out = await fs.browseAdd();
  expect(out.kind).toBe("added");
  expect(be.fileBrowserCalls.length).toBe(2); // opened a 2nd browser
  expect(be.fileBrowserCalls[1].patterns.includes("*.sav")).toBeFalsy(); // ROM-only
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.romPath).toBe("/roms/thegame.gb"); // paired with the chosen ROM
  expect(spec.savPath).toBe("/saves/orphan.sav"); // the picked save crossed as the override
});

test("browseAdd: a 2nd-browser pick that isn't a ROM is an error", async () => {
  const { be, fs } = newFs();
  be.seed("/saves/orphan.sav", "battery");
  be.seed("/roms/notes.txt", "hello");
  be.queueBrowse("/saves/orphan.sav", "/roms/notes.txt");
  const out = await fs.browseAdd();
  expect(out).toEqual({ kind: "error", path: "/roms/notes.txt" });
});

test("browseAdd: a cancelled 2nd browser yields cancelled", async () => {
  const { be, fs } = newFs();
  be.seed("/saves/orphan.sav", "battery");
  be.queueBrowse("/saves/orphan.sav", null);
  const out = await fs.browseAdd();
  expect(out).toEqual({ kind: "cancelled" });
});

test("browseReplace: swaps the target instance in place (replaceId), leaving the list length", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  const id = systems.addSystem("/roms/a.gb")!;
  be.queueBrowse("/roms/b.gb");
  const out = await fs.browseReplace(id);
  expect(out.kind).toBe("replaced");
  expect(systems.view().length).toBe(1); // in place — not appended
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id); // swapped the target id
  expect(spec.romPath).toBe("/roms/b.gb");
});

test("browseReplace: an unpaired .sav pairs via the 2nd browser before replacing", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/saves/orphan.sav", "battery");
  be.seed("/roms/thegame.gb", gbRom());
  const id = systems.addSystem("/roms/a.gb")!;
  be.queueBrowse("/saves/orphan.sav", "/roms/thegame.gb");
  const out = await fs.browseReplace(id);
  expect(out.kind).toBe("replaced");
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id);
  expect(spec.romPath).toBe("/roms/thegame.gb");
  expect(spec.savPath).toBe("/saves/orphan.sav"); // paired save crossed as the override
});

test("browseReplace: a cancelled dialog leaves the instance untouched", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  const id = systems.addSystem("/roms/a.gb")!;
  const before = systems.view().find((s) => s.id === id)!.romPath;
  be.queueBrowse(null);
  const out = await fs.browseReplace(id);
  expect(out).toEqual({ kind: "cancelled" });
  expect(systems.view().find((s) => s.id === id)!.romPath).toBe(before);
});

test("browseSwap: opens a ROM-only browser and swaps in place, carrying the live SRAM", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  const id = systems.addSystem("/roms/a.gb")!;
  be.queueBrowse("/roms/b.gb");
  const out = await fs.browseSwap(id);
  expect(out.kind).toBe("swapped");
  expect(systems.view().length).toBe(1); // in place — not appended
  expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].patterns.includes("*.sav")).toBeFalsy(); // ROM-only
  const spec = be.constructCalls[be.constructCalls.length - 1];
  expect(spec.replaceId).toBe(id); // swapped the target id
  expect(spec.romPath).toBe("/roms/b.gb");
  expect(new Uint8Array(spec.sramBytes!)).toEqual(sramBytesFor(id)); // the old battery crossed into the new cart
});

test("browseSwap: a non-ROM pick is an error and swaps nothing", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/notes.txt", "hello");
  const id = systems.addSystem("/roms/a.gb")!;
  be.queueBrowse("/notes.txt");
  const out = await fs.browseSwap(id);
  expect(out).toEqual({ kind: "error", path: "/notes.txt" });
  expect(systems.view().find((s) => s.id === id)!.romPath).toBe("/roms/a.gb"); // untouched
});

test("browseSwap: a cancelled dialog leaves the instance untouched", async () => {
  const { be, fs, systems } = newFs();
  be.seed("/roms/a.gb", gbRom());
  const id = systems.addSystem("/roms/a.gb")!;
  be.queueBrowse(null);
  const out = await fs.browseSwap(id);
  expect(out).toEqual({ kind: "cancelled" });
  expect(systems.view().find((s) => s.id === id)!.romPath).toBe("/roms/a.gb");
});
