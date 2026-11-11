// The project's sections (docs/modular-graph-phase-f5a-design.md section 6.1): a package owns a block under the .rplg's
// `settings`, and the shell keeps every key it does not own - through a contributor when the product carries one, raw
// when it does not, and never stamped unless the contributor declares a version. The controller is the one section a
// product ships today, so the versioned half of the mechanism is held here through the two functions that implement it.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { lsdjRom } from "../systems/fixtures";
import * as R from "@retroplug/lsdj/codec/regions";
import {
  buildConfig,
  DEFAULT_SETTINGS,
  K_PROJECT,
  parseConfig,
  parseProjectSection,
  projectSection,
  projectSectionOf,
  serializeProjectSection,
} from "../../src/projectConfig";
import type { ProjectSection } from "@retroplug/core/definePackage";
import { buildGraph } from "../../src/graph/buildGraph";
import { type ControllerSection } from "@retroplug/launchpad/projectSection";

/** A section a package could contribute, with the version and one step a real one would carry when it breaks its shape:
 *  v1's `knob` (an integer index) becomes v2's `knob` (a mode name). The step is idempotent-safe, as every step must
 *  be: it runs on a block already in v2's shape without touching it, which is what an unstamped current-shape block
 *  is. */
const VERSIONED: ProjectSection<{ knob: string; size: number }> = {
  key: "versioned",
  version: 2,
  migrations: {
    1: (raw) => ({
      ...raw,
      knob: typeof raw.knob === "string" ? raw.knob : (["a", "b"][Number(raw.knob)] ?? "a"),
    }),
  },
  schema: {
    parse: (raw: unknown) => {
      const block = (raw ?? {}) as Record<string, unknown>;
      return {
        knob: typeof block.knob === "string" ? block.knob : "a",
        size: typeof block.size === "number" ? block.size : 0,
      };
    },
  },
  defaults: () => ({ knob: "a", size: 0 }),
};

// ---- the controller's section, through a project ---------------------------------------------------------------

test("a project that never set the controller reads the contributor's defaults", () => {
  const raw = JSON.stringify({ schemaVersion: K_PROJECT, settings: { layout: "column" }, systems: [] });
  const settings = parseConfig(raw).settings;
  expect(projectSectionOf("controller")?.key).toBe("controller");
  expect(projectSection<ControllerSection>(settings, "controller")).toEqual({
    enabled: false, app: "lsdj-midimap", target: "system", systemId: 0, appConfig: {},
  });
  // And so does a fresh project, without a file at all.
  expect(projectSection(DEFAULT_SETTINGS, "controller")).toEqual(
    projectSection<ControllerSection>(settings, "controller"),
  );
});

test("a hand-edited block is parsed by the contributor, and a patch lands through its merge", () => {
  const raw = JSON.stringify({
    schemaVersion: K_PROJECT,
    settings: { controller: { enabled: "yes", target: "nowhere", systemId: -4, appConfig: { quantise: 3 } } },
    systems: [],
  });
  const settings = parseConfig(raw).settings;
  // Junk snaps to defaults per field, so one bad value cannot reset the project's layout and zoom.
  expect(projectSection<ControllerSection>(settings, "controller")).toEqual({
    enabled: false, app: "lsdj-midimap", target: "system", systemId: 0, appConfig: { quantise: 3 },
  });
});

test("a settings key no package contributes is kept verbatim and written back unchanged (E5)", () => {
  // The shell cannot tell a key a newer build wrote from one of its packages' sections, so it keeps it: stripping it
  // would destroy a downgrade's data on the next save.
  const future = { anything: [1, 2], nested: { deep: true } };
  const cfg = parseConfig(JSON.stringify({
    schemaVersion: K_PROJECT,
    settings: { layout: "grid", futureSection: future },
    systems: [],
  }));
  expect(projectSection(cfg.settings, "futureSection")).toEqual(future);
  const written = buildConfig(cfg.settings, []).settings as unknown as Record<string, unknown>;
  expect(written.futureSection).toEqual(future);
});

test("the controller's bytes are what they always were: a saved project round-trips byte for byte", () => {
  // The key, its contents and its order: a section is parsed by its package, and the file is where it stays.
  const withController = {
    ...DEFAULT_SETTINGS,
    sections: {
      ...DEFAULT_SETTINGS.sections,
      controller: { enabled: true, app: "lsdj-midimap", target: "midiOut", systemId: 0, appConfig: { quantise: "beat" } },
    },
  };
  const json = JSON.stringify(buildConfig(withController, []).settings);
  expect(json).toBe(
    JSON.stringify({
      layout: "auto", midiRouting: "sendToAll", audioRouting: "stereo", zoom: 0,
      // The midi package's section is a fresh project's (docs/modular-graph-phase-f5b-design.md E2: a project that
      // never set ports has none, and the block says so where it sits), and the controller's bytes are unchanged.
      midi: { inputs: [], allInputs: false, outputs: [], clockBpm: 120 },
      controller: { enabled: true, app: "lsdj-midimap", target: "midiOut", systemId: 0, appConfig: { quantise: "beat" } },
    }),
  );
  // A second save of the parsed file is the same bytes again (nothing gains a stamp, nothing is reordered).
  const again = parseConfig(JSON.stringify(buildConfig(withController, [])));
  expect(JSON.stringify(buildConfig(again.settings, []).settings)).toBe(json);
});

test("setSection merges through the contributor and refuses what it refuses", () => {
  const stores = composeAppStores({ backend: new MockBackend("/cfg") });
  stores.project.setSection("controller", { enabled: true, appConfig: { quantise: "beat" } });
  stores.project.setSection("controller", { appConfig: { follow: false } });
  expect(projectSection<ControllerSection>(stores.project.settings(), "controller")).toEqual({
    enabled: true, app: "lsdj-midimap", target: "system", systemId: 0,
    appConfig: { quantise: "beat", follow: false },
  });
  // The closed launch-target enum is the package's, so the refusal is too, naming the value.
  expect(() => stores.project.setSection("controller", { target: "nowhere" })).toThrow("controller target 'nowhere'");
});

// ---- the versioned half of the mechanism ----------------------------------------------------------------------

test("a section's own migrations run before its schema", () => {
  // The file's stamp is the OLD one, so the step rewrites what v1 wrote (an index) to what v2 reads (a name).
  expect(parseProjectSection(VERSIONED, { schemaVersion: 1, knob: 1, size: 4 })).toEqual({ knob: "b", size: 4 });
});

test("an unstamped section reads as version 1, so a section that starts unversioned can still migrate", () => {
  // A section is not stamped until it first versions, so the shape before that stamp IS v1: the first versioned
  // release declares 2 and adds one step keyed 1, and every file written before it runs through that step. Reading an
  // unstamped block as current would make that first migration unreachable for every existing project.
  expect(parseProjectSection(VERSIONED, { knob: 1, size: 4 })).toEqual({ knob: "b", size: 4 });
  // A hand-written block already in v2's shape survives the step (it is idempotent-safe), and the schema still fills
  // what neither shape has.
  expect(parseProjectSection(VERSIONED, { knob: "b", size: 2 })).toEqual({ knob: "b", size: 2 });
  expect(parseProjectSection(VERSIONED, {})).toEqual({ knob: "a", size: 0 });
});

test("a key the shell owns, or the prototype's, is not a section a host may read or write", () => {
  // `zoom` is the shell's own settings field, and a package writing it as a section would make the saved file's `zoom`
  // an object the next load resets; `__proto__` would set a prototype rather than store a value. Both are refused with
  // the key named rather than answered with undefined or written into the project.
  const stores = composeAppStores({ backend: new MockBackend() });
  for (const key of ["zoom", "layout", "midiRouting", "audioRouting", "__proto__"]) {
    expect(() => stores.project.section(key)).toThrow(key);
    expect(() => stores.project.setSection(key, { value: 1 })).toThrow(key);
  }
});

test("setSection refuses a key no package of this product contributes", () => {
  // Reading an unknown key is E5's forward compatibility (a newer build's block); WRITING one would create a key this
  // build never reads and E5 then keeps in every save forever, and a misspelt section (`controler`) would look
  // written.
  const stores = composeAppStores({ backend: new MockBackend() });
  expect(() => stores.project.setSection("controler", { enabled: true })).toThrow("controler");
});

test("a core key in a sections record never reaches the file", () => {
  // Defensive: a writer that bypassed setSection still cannot make the shell's `zoom` an object in the .rplg.
  const settings = { ...DEFAULT_SETTINGS, sections: { ...DEFAULT_SETTINGS.sections, zoom: 99 } };
  const written = buildConfig(settings, []).settings as unknown as Record<string, unknown>;
  expect(written.zoom).toBe(0);
});

test("a hand-edited __proto__ key is kept as data, and never reaches the prototype chain", () => {
  // E5 keeps every key the shell does not own, so a `__proto__` key a hand-edited file carries is data like any other:
  // it must survive the round trip without becoming the settings object's prototype (which would drop it on save).
  // JSON TEXT, not an object literal: `{ __proto__: ... }` in source sets the prototype, while the same text parsed
  // is an own key, which is what a hand-edited file carries.
  const raw = `{"schemaVersion":${K_PROJECT},"settings":{"__proto__":{"polluted":true}},"systems":[]}`;
  const sections = parseConfig(raw).settings.sections as Record<string, unknown>;
  expect(Object.prototype.hasOwnProperty.call(sections, "__proto__")).toBe(true);
  expect(sections["__proto__"]).toEqual({ polluted: true });
  expect(({} as Record<string, unknown>).polluted).toBe(undefined);
  const written = buildConfig(parseConfig(raw).settings, []).settings as unknown as Record<string, unknown>;
  expect(Object.prototype.hasOwnProperty.call(written, "__proto__")).toBe(true);
  expect(written["__proto__"]).toEqual({ polluted: true });
});

test("a section stamped newer than its contributor is kept verbatim, and written back as it came", () => {
  const fromTheFuture = { schemaVersion: 9, knob: { anything: true } };
  // Kept verbatim, not parsed: this build cannot read it, so it must not rewrite it into something it can.
  expect(parseProjectSection(VERSIONED, fromTheFuture)).toBe(fromTheFuture);
  expect(serializeProjectSection(VERSIONED, fromTheFuture)).toBe(fromTheFuture);
  // A section this build does read gets this build's stamp on the way out, and only then.
  expect(serializeProjectSection(VERSIONED, { knob: "a", size: 1 })).toEqual({
    knob: "a", size: 1, schemaVersion: 2,
  });
});

// ---- one service instance per store ---------------------------------------------------------------------------

/** A full-size LSDj battery with `sync` in the working song's SYNC byte: enough for the byte-level reads. */
function lsdjBattery(sync: number): Uint8Array {
  const b = new Uint8Array(0x20000);
  b[0x813e] = 0x6a; // 'j'
  b[0x813f] = 0x6b; // 'k'
  b[R.kModernRegions.syncMode] = sync;
  return b;
}

test("two project stores share no service state", () => {
  // A plugin host runs one control plane per instance, so a service's polled state is its store's: one store's poll
  // must not answer for another's.
  const be = new MockBackend();
  const a = composeAppStores({ backend: be });
  const b = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", lsdjRom());
  for (const stores of [a, b]) {
    stores.project.systems.addSystem("/roms/a.gb");
    stores.project.setSection("controller", { enabled: true });
  }
  // Each store's own system (the backend allocates the ids), and each one's battery.
  const idOf = (stores: ReturnType<typeof composeAppStores>) => stores.project.systems.view()[0].id;
  be.setSram(idOf(a), lsdjBattery(8)); // MidiMap
  be.setSram(idOf(b), lsdjBattery(8));
  const syncOf = (stores: ReturnType<typeof composeAppStores>) =>
    stores.project.serviceState<{ cartSync: string | null }>("controller")?.cartSync ?? null;

  expect(a.project.pollServices()).toBe(true);
  expect(syncOf(a)).toBe("MidiMap");
  // The other store has not polled, so it has nothing of its own to say yet: one instance is not shared.
  expect(syncOf(b)).toBe(null);
  be.setSram(idOf(b), lsdjBattery(9)); // MI.OUT
  expect(b.project.pollServices()).toBe(true);
  expect(syncOf(b)).toBe("MidiOut");
  expect(syncOf(a)).toBe("MidiMap"); // and its own value did not move under it
});

test("a store builds one instance per contributed service, not per publish", () => {
  // The service's state has to survive publishes (a fresh instance per publish would forget the adopted signature and
  // the anchor), which is what polling twice in a row with no edit shows: the second poll is silent.
  const be = new MockBackend();
  const stores = composeAppStores({ backend: be });
  be.seed("/roms/a.gb", lsdjRom());
  stores.project.systems.addSystem("/roms/a.gb");
  stores.project.setSection("controller", { enabled: true });
  be.setSram(stores.project.systems.view()[0].id, lsdjBattery(8));
  expect(stores.project.pollServices()).toBe(true);
  expect(stores.project.pollServices()).toBe(false);
});


// ---- one package's throw, at a store's seams -----------------------------------------------------------------

/** Capture console.warn for one case: a contributor's throw is reported once, and the test can say which line. */
function captureWarnings(): { lines: string[]; restore: () => void } {
  const lines: string[] = [];
  const original = console.warn;
  console.warn = (...args: unknown[]) => {
    lines.push(args.map((arg) => String(arg)).join(" "));
  };
  return { lines, restore: () => { console.warn = original; } };
}

test("a section stamped newer reads its defaults, refuses a patch once, and is written back as it came", () => {
  const fromTheFuture = { schemaVersion: 9, knob: { anything: true } };
  const be = new MockBackend();
  be.seed(
    "/p/newer.rplg",
    JSON.stringify({ schemaVersion: K_PROJECT, settings: { versioned: fromTheFuture }, systems: [] }),
  );
  const stores = composeAppStores({
    backend: be,
    contributions: { sections: [{ pkg: "acme", section: VERSIONED }] },
  });
  const warnings = captureWarnings();
  try {
    expect(stores.project.load("/p/newer.rplg").kind).toBe("loaded");
    // A reader gets what it can honestly type: the contributor's defaults, not a block it could not parse.
    expect(stores.project.section("versioned")).toEqual({ knob: "a", size: 0 });
    // The writer keeps the block, so two refused patches warn once and change nothing.
    stores.project.setSection("versioned", { knob: "b" });
    stores.project.setSection("versioned", { knob: "b" });
    expect(warnings.lines.filter((line) => line.includes("versioned")).length).toBe(1);
    expect(buildConfig(stores.project.settings(), []).settings.versioned).toEqual(fromTheFuture);
  } finally {
    warnings.restore();
  }
});

test("a contributor's schema that throws is isolated: its defaults read, and the project loads and warns once", () => {
  const THROWING: ProjectSection<{ ok: string }> = {
    key: "throwing",
    schema: { parse: () => { throw new Error("a block this build cannot read"); } },
    defaults: () => ({ ok: "default" }),
  };
  const be = new MockBackend();
  be.seed(
    "/p/throwing.rplg",
    JSON.stringify({ schemaVersion: K_PROJECT, settings: { throwing: { anything: true }, layout: "grid" }, systems: [] }),
  );
  const stores = composeAppStores({
    backend: be,
    contributions: { sections: [{ pkg: "acme", section: THROWING }] },
  });
  const warnings = captureWarnings();
  try {
    expect(stores.project.load("/p/throwing.rplg").kind).toBe("loaded");
    expect(stores.project.section("throwing")).toEqual({ ok: "default" });
    // The rest of the project parsed: the shell's own field is not the throwing section's casualty.
    expect(stores.project.settings().layout).toBe("grid");
    expect(warnings.lines.filter((line) => line.includes("throwing")).length).toBe(1);
  } finally {
    warnings.restore();
  }
});

test("a service or a placer that throws is isolated, and the rest of the project publishes and places", () => {
  const ACME: ProjectSection<Record<string, unknown>> = {
    key: "acme",
    schema: { parse: (raw: unknown) => (raw ?? {}) as Record<string, unknown> },
    defaults: () => ({}),
  };
  const be = new MockBackend();
  const stores = composeAppStores({
    backend: be,
    contributions: {
      sections: [{ pkg: "acme", section: ACME }],
      services: [{
        pkg: "acme",
        service: {
          key: "acme",
          create: () => ({
            poll: () => { throw new Error("poll"); },
            graphModel: () => { throw new Error("model"); },
            state: () => { throw new Error("state"); },
          }),
        },
      }],
    },
  });
  const warnings = captureWarnings();
  try {
    expect(stores.project.pollServices()).toBe(false);
    expect(stores.project.serviceState("acme")).toBe(undefined);
    // A publish still happens: the throwing model is simply no model, so the graph is built for the rest.
    stores.project.setSection("acme", { anything: true });
  } finally {
    warnings.restore();
  }
  // A throwing placer is no placement; the placer after it still places its node.
  const built = buildGraph(
    { systems: [], routing: "stereo", shape: { kind: "multi", channels: 2 }, midiRouting: null,
      models: { acme: {}, survivor: {} } },
    [
      { pkg: "acme", placer: { key: "acme", place: () => { throw new Error("place"); } } },
      {
        pkg: "acme-survivor",
        placer: {
          key: "survivor",
          place: () => ({ session: { nodes: [{ id: "survivor-node", type: "rp.core.gain" }], wires: [] } }),
        },
      },
    ],
  );
  expect(built.document.nodes.some((node) => node.id === "survivor-node")).toBe(true);
});
