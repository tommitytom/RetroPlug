// A project saved before a role existed must GAIN that role on load.
//
// `adopt` used to take stored roles wholesale (`config.roles.length ? config.roles : defaultRoles(...)`),
// so a provider only ever ran for a project storing NO roles at all. A `.rplg` written before a feature
// role shipped could therefore never acquire it, and nothing anywhere said so. Met in the wild: an
// smsggdj project authored before `sms-sync` existed loaded, booted, armed, showed WAIT and ignored the
// DAW transport forever - diagnosable only by reading the project file.
//
// The union is safe precisely because a stored list is NOT a curated selection: `setRoleConfig` only
// edits a role's config, and there is no removeRole and no `roles.filter` anywhere in src/ or ui/. So a
// stored list is just whatever the providers produced when the project was first created, and adding
// what today's providers suggest cannot override a user decision - none is expressible.
//
// If a "remove role" affordance is ever added, that stops being true and the union will resurrect a
// removed role. Whoever adds removal has to add a tombstone; there is nothing to tombstone today.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { registerLsdjAssetsRole } from "../../src/lsdjAssetsRole";
import { registerRomProviders } from "../../src/romProviders";
import { buildAppRegistry } from "../../src/appHost";
import { gbRom, lsdjRom, blipToasterRom } from "./fixtures";

function newStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg);
  registerDspRoles(reg);
  registerLsdjAssetsRole(reg); // as buildAppRegistry does - lsdj-assets lives in its own module
  registerRomProviders(reg);
  return { be, store: new SystemsStore(be, undefined, reg) };
}

const kinds = (store: SystemsStore, id: number): string[] =>
  (store.view().find((v) => v.id === id)?.roles ?? []).map((r) => r.kind);

// An LSDj cart is the shape the union has to get right: a `sameboy` system role plus TWO feature roles
// (lsdj-sync, lsdj-assets), so "append the missing one" and "leave the present one alone" are both
// exercised by one fixture.
function seedLsdj(be: MockBackend): string {
  be.seed("/roms/l.gb", lsdjRom());
  return "/roms/l.gb";
}

test("a stored role list gains a feature role that did not exist when it was saved", () => {
  const { be, store } = newStore();
  const romPath = seedLsdj(be);

  // Exactly the failure: a project written when only lsdj-sync existed.
  const id = store.adopt({ romPath, core: "sameboy", roles: [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } },
  ] });
  expect(id != null).toBeTruthy();
  expect(kinds(store, id!).includes("lsdj-assets")).toBeTruthy();
  expect(kinds(store, id!).includes("lsdj-sync")).toBeTruthy(); // and the stored one survived
});

test("a stored role keeps its own config - the union never resets a user's edit", () => {
  const { be, store } = newStore();
  const romPath = seedLsdj(be);
  const id = store.adopt({ romPath, core: "sameboy", roles: [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: "midiSyncArduinoboy", tempoDivisor: 4, autoStart: true } },
  ] });
  const sync = store.view().find((v) => v.id === id)!.roles.find((r) => r.kind === "lsdj-sync")!;
  // The provider suggests mode "midiSync" / divisor 1. Re-deriving instead of unioning, or unioning in
  // the wrong direction, silently reverts a configured cart to the default sync mode.
  expect(sync.config.mode).toBe("midiSyncArduinoboy");
  expect(sync.config.tempoDivisor).toBe(4);
  expect(sync.config.autoStart).toBe(true);
});

test("the union produces no duplicates, and the same role set a fresh add would", () => {
  const { be, store } = newStore();
  const romPath = seedLsdj(be);
  const fresh = store.addSystem(romPath); // full defaultRoles path
  const adopted = store.adopt({ romPath, core: "sameboy", roles: [
    { kind: "sameboy", config: {} },
    { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } },
  ] });
  const a = kinds(store, fresh!);
  const b = kinds(store, adopted!);
  expect(b).toEqual(a); // same kinds, same order - roles map straight to the DSP pipeline
  expect(new Set(b).size).toBe(b.length);
});

test("nothing is invented for a ROM no provider matches", () => {
  const { be, store } = newStore();
  be.seed("/roms/plain.gb", gbRom());
  const id = store.adopt({ romPath: "/roms/plain.gb", core: "sameboy", roles: [{ kind: "sameboy", config: {} }] });
  expect(kinds(store, id!)).toEqual(["sameboy"]);
});

test("a stored role whose kind is no longer registered is left alone", () => {
  // dspKernel skips unknown kinds when building the pipeline, so a stale entry is inert - and dropping
  // it here would silently discard a role belonging to a build the user might go back to.
  const { be, store } = newStore();
  const romPath = seedLsdj(be);
  const id = store.adopt({ romPath, core: "sameboy", roles: [
    { kind: "sameboy", config: {} },
    { kind: "retired-role", config: { keep: 1 } },
  ] });
  expect(kinds(store, id!).includes("retired-role")).toBeTruthy();
  expect(kinds(store, id!).includes("lsdj-sync")).toBeTruthy(); // ...and the union still ran
});

test("an empty stored list still means full re-derive, not an empty system", () => {
  const { be, store } = newStore();
  const romPath = seedLsdj(be);
  const id = store.adopt({ romPath, core: "sameboy", roles: [] });
  const fresh = store.addSystem(romPath);
  expect(kinds(store, id!)).toEqual(kinds(store, fresh!));
});

test("the system role is NOT synthesised - only feature roles are unioned in", () => {
  // Deliberate carve-out. The core-config role (kind === core) feeds adopt's construct-time `settings`
  // blob, which is read from the STORED roles; synthesising one here would start passing zod defaults
  // where `undefined` is passed today and native applies its own. Pinned so a refactor cannot quietly
  // change construct behaviour while looking like a tidy-up.
  const { be, store } = newStore();
  const romPath = seedLsdj(be);
  const id = store.adopt({ romPath, core: "sameboy", roles: [
    { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } },
  ] });
  expect(kinds(store, id!).includes("sameboy")).toBeFalsy();
  expect(kinds(store, id!).includes("lsdj-assets")).toBeTruthy(); // the feature half still unions
});

test("the smsggdj case that prompted this: a mesen-only project gains sms-sync", () => {
  const { be, store } = newStore();
  // A .sms carrying the SMSGGDJ build marker at $3640, as classifyRom + the provider see it.
  const rom = new Uint8Array(0x8200);
  rom.set([0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41], 0x7ff0); // "TMR SEGA"
  rom[0x7ff0 + 0xf] = 0x40; // region nibble 4 -> SMS
  for (let i = 0; i < "SMSGGDJ".length; i++) rom[0x3640 + i] = "SMSGGDJ".charCodeAt(i);
  be.seed("/roms/smsggdj.sms", rom);

  const id = store.adopt({ romPath: "/roms/smsggdj.sms", core: "mesen", roles: [
    { kind: "mesen", config: { enableFm: true } },
  ] });
  expect(id != null).toBeTruthy();
  const roles = store.view().find((v) => v.id === id)!.roles;
  const sync = roles.find((r) => r.kind === "sms-sync");
  expect(sync != null).toBeTruthy();
  expect(sync!.config.machine).toBe("sms"); // tagged with the wire format, as a fresh add would be
});

// --- derived facts vs stored configuration -------------------------------------------------------
//
// The union rule above ("a stored role is never touched") protects CONFIGURATION. It must not protect
// a field that is a fact about the ROM file, because then a project can outlive the truth. That is the
// case for `bliptoaster.chip`, which says which expansion build the ROM is and decides the entire DAW
// parameter map (src/parameterMap.ts): a project written before the field existed stores no chip, and
// reading it as the 2A03 default silently hides every VRC7 / VRC6 / N163 / MMC5 lane.

/** A BlipToaster ROM fixture with its iNES mapper rewritten (85 = VRC7, 24 = VRC6, 0 = base). */
function blipToasterWithMapper(mapper: number): Uint8Array {
  const rom = blipToasterRom();
  rom[6] = (rom[6] & 0x0f) | ((mapper & 0x0f) << 4);
  rom[7] = (rom[7] & 0x0f) | (mapper & 0xf0);
  return rom;
}

function newAppStore() {
  const be = new MockBackend("/cfg");
  return { be, store: new SystemsStore(be, undefined, buildAppRegistry()) };
}

const chipOf = (store: SystemsStore, id: number): string | undefined =>
  (store.view().find((v) => v.id === id)?.roles.find((r) => r.kind === "bliptoaster")?.config as
    | { chip?: string }
    | undefined)?.chip;

test("a project saved before `chip` existed re-derives it from the ROM", () => {
  const { be, store } = newAppStore();
  be.seed("/roms/bt.nes", blipToasterWithMapper(85));
  // Exactly what a pre-field project holds: the role, with an empty config.
  const id = store.adopt({ romPath: "/roms/bt.nes", core: "mesen", roles: [
    { kind: "mesen", config: {} },
    { kind: "bliptoaster", config: {} },
  ] });
  expect(id != null).toBeTruthy();
  expect(chipOf(store, id!)).toBe("vrc7");
});

test("a stale `chip` is corrected when the ROM at that path is a different build", () => {
  const { be, store } = newAppStore();
  be.seed("/roms/bt.nes", blipToasterWithMapper(24));
  const id = store.adopt({ romPath: "/roms/bt.nes", core: "mesen", roles: [
    { kind: "mesen", config: {} },
    { kind: "bliptoaster", config: { chip: "vrc7" } }, // the ROM was swapped since this was written
  ] });
  expect(chipOf(store, id!)).toBe("vrc6");
});

// The two mapper-69 builds are the case the mapper alone cannot serve. Both routes to the answer have
// to survive the store's tiered read: a current ROM declares its build in the SIG block (inside the
// short prefix), an older one only in the on-screen label (which costs the deep read).
test("both mapper-69 builds resolve through the store, tagged or not", () => {
  const withSigChip = (mapper: number, tag: string): Uint8Array => {
    const rom = blipToasterWithMapper(mapper);
    for (let i = 0; i < 4; i++) rom[0x10 + 14 + i] = i < tag.length ? tag.charCodeAt(i) : 0;
    return rom;
  };
  const withLabel = (mapper: number, label: string): Uint8Array => {
    const rom = blipToasterWithMapper(mapper);
    for (let i = 0; i < label.length; i++) rom[0x2f00 + i] = label.charCodeAt(i);
    rom[0x2f00 + label.length] = 0;
    return rom;
  };
  const adopt = (rom: Uint8Array): string | undefined => {
    const { be, store } = newAppStore();
    be.seed("/roms/bt.nes", rom);
    const id = store.adopt({ romPath: "/roms/bt.nes", core: "mesen", roles: [
      { kind: "mesen", config: {} },
      { kind: "bliptoaster", config: {} },
    ] });
    return chipOf(store, id!);
  };

  expect(adopt(withSigChip(69, "S5B"))).toBe("s5b");
  expect(adopt(withSigChip(69, "2A03"))).toBe("2a03");
  expect(adopt(withLabel(69, "S5B"))).toBe("s5b"); // pre-tag ROM, found by the deep read
  expect(adopt(withLabel(69, "2A03"))).toBe("2a03");
  expect(adopt(blipToasterWithMapper(69))).toBe("2a03"); // neither: the subset that can't be wrong
});

test("re-deriving the chip leaves the rest of that role's config alone", () => {
  const { be, store } = newAppStore();
  be.seed("/roms/bt.nes", blipToasterWithMapper(85));
  const id = store.adopt({ romPath: "/roms/bt.nes", core: "mesen", roles: [
    { kind: "mesen", config: {} },
    { kind: "bliptoaster", config: { chip: "2a03", keepMe: 7 } },
    { kind: "bliptoaster-assets", config: { overrides: [{ type: "kit", slot: 3 }] } },
  ] });
  const roles = store.view().find((v) => v.id === id)!.roles;
  expect((roles.find((r) => r.kind === "bliptoaster")!.config as { keepMe?: number }).keepMe).toBe(7);
  // ...and a sibling role's stored config is untouched: this is not a general re-derive
  const assets = roles.find((r) => r.kind === "bliptoaster-assets")!.config as { overrides: unknown[] };
  expect(assets.overrides.length).toBe(1);
});
