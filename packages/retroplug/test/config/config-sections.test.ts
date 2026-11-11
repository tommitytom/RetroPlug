// config.json's sections (docs/modular-graph-phase-f5b-design.md section 6.1): a package owns a root key of the user
// config exactly as it owns a block of a project's `settings`, and the shell keeps every key it has no contributor for.
// That second half is the one a plugin needs: it lists none of the audio or MIDI packages, so the file it writes must
// still carry a standalone's `audio` and `controlSurface` blocks.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { UserConfigStore } from "../../src/userConfigStore";
import { parseUserConfigResult, serializeUserConfig, USER_CONFIG_SCHEMA } from "../../src/userConfigSerialization";
import { DEFAULT_USER_CONFIG, type ConfigModel, configFileShape } from "../../src/userConfig";
import {
  assertConfigSectionKey, configSectionEntryOf, CORE_CONFIG_KEYS, putConfigSection, type ConfigSectionEntry,
} from "../../src/configSections";
import { userConfigSchema } from "../../src/userConfig";
import { SHELL_CONFIG_KEYS } from "../../src/generated/packages";
import type { ProjectSection } from "@retroplug/core/definePackage";
import { stringifyConfig } from "../../src/configSchema";

const CONFIG = "/config/config.json";

/** A section a package could contribute, with the version and one step a real one would carry when it breaks its shape:
 *  v1's `knob` (an integer index) becomes v2's `knob` (a mode name). Idempotent-safe, as every step must be. */
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
const VERSIONED_ENTRY: ConfigSectionEntry = { pkg: "acme", section: VERSIONED };

/** The `audio` section as the standalone's package contributes it: the fields this build reads, and nothing about what a
 *  newer build might have written beside them. */
const AUDIO: ProjectSection<{ backend: string; driver: string }> = {
  key: "audio",
  schema: {
    parse: (raw: unknown) => {
      const block = (raw ?? {}) as Record<string, unknown>;
      return {
        backend: typeof block.backend === "string" ? block.backend : "auto",
        driver: typeof block.driver === "string" ? block.driver : "",
      };
    },
  },
  defaults: () => ({ backend: "auto", driver: "" }),
};

/** A config.json as a build BEFORE this phase wrote it: the shell's fields in the order it wrote them, and the control
 *  surface last. (The audio device had its own audio.json then, which is why there is no `audio` block.) */
function preSectionFile(): string {
  return stringifyConfig({
    schemaVersion: USER_CONFIG_SCHEMA,
    activeKeyboardBindings: "default",
    activeGamepadBindings: "default",
    defaultZoom: 4,
    sramAutoSave: "OnProjectSave",
    useNativeFileDialogs: true,
    render: { split: "mix", sampleRate: 44100, maxDurationSec: 600, outputDir: "/music/out", onExists: "overwrite" },
    controlSurface: { input: "LPProMK3 MIDI", output: "LPProMK3 MIDI", enabled: true, device: "Launchpad Pro [MK3]" },
  });
}

function captureWarnings(): { lines: string[]; restore: () => void } {
  const lines: string[] = [];
  const original = console.warn;
  console.warn = (...args: unknown[]) => {
    lines.push(args.map((arg) => String(arg)).join(" "));
  };
  return { lines, restore: () => { console.warn = original; } };
}

// ---- the key rules -------------------------------------------------------------------------------------------

test("a core key is refused as a config section, and a prototype key with it", () => {
  // The shell's own fields are its own: a section under one would be read as the field and written back as an object
  // the next load clamps to the default.
  for (const key of CORE_CONFIG_KEYS) {
    let threw = "";
    try {
      assertConfigSectionKey(key);
    } catch (e) {
      threw = (e as Error).message;
    }
    expect(threw).toBe(`${key} is one of the shell's own config keys, not a config section`);
    // The generated registry refuses a CONTRIBUTOR of one; the store refuses a reader/writer of one.
    expect(configSectionEntryOf(key, [])).toBe(undefined);
    const store = new UserConfigStore(new MockBackend("/config"));
    let read = "";
    try {
      store.section(key);
    } catch (e) {
      read = (e as Error).message;
    }
    expect(read).toBe(threw);
    let wrote = "";
    try {
      store.setSection(key, { anything: true });
    } catch (e) {
      wrote = (e as Error).message;
    }
    expect(wrote).toBe(threw);
  }
  let proto = "";
  try {
    assertConfigSectionKey("__proto__");
  } catch (e) {
    proto = (e as Error).message;
  }
  expect(proto).toBe("__proto__ is not a config section: it names a prototype");
});

test("a root key no package contributes is kept verbatim, through a read and through a write", () => {
  // The plugin's case in miniature: this store has no contributors at all, and the file carries a standalone's blocks.
  const future = { anything: [1, 2, "three"], nested: { deep: true } };
  const audio = { backend: "portaudio", driver: "pipewire", device: "Speakers", sampleRate: 48000, blockSize: 512,
    outChannels: 2 };
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG, futureSection: future, audio }));
  const store = new UserConfigStore(be, () => {}, []);
  store.load();
  expect(store.section("futureSection")).toEqual(future); // raw JSON: no contributor, nothing to parse
  expect(store.section("audio")).toEqual(audio);
  // A core write re-serializes the whole file; both blocks come back byte for byte.
  expect(store.setDefaultZoom(2)).toBeTruthy();
  const written = JSON.parse(be.readText(CONFIG)!) as Record<string, unknown>;
  expect(written.futureSection).toEqual(future);
  expect(written.audio).toEqual(audio);
});

test("a config.json written before this phase reads and round-trips byte-identical", () => {
  const text = preSectionFile();
  const parsed = parseUserConfigResult(text);
  if (!parsed.ok) throw new Error(`the pre-section file did not parse: ${parsed.reason}`);
  // No default block is invented for a section the file never carried: the model holds exactly the keys the file had.
  expect(Object.keys(parsed.model.sections)).toEqual(["controlSurface"]);
  expect(serializeUserConfig(parsed.model)).toBe(text);
  // And a store that contributes no sections writes the same bytes again after a real change elsewhere.
  const be = new MockBackend("/config");
  be.seed(CONFIG, text);
  const store = new UserConfigStore(be, () => {}, []);
  store.load();
  expect(store.setDefaultZoom(2)).toBeTruthy();
  const lines = be.readText(CONFIG)!.split("\n");
  expect(lines.includes('  "controlSurface": {')).toBe(true);
  expect(be.readText(CONFIG)!.includes('"audio"')).toBe(false); // the file never had one, and nothing invented it
});

test("a fresh file carries each contributor's defaults, and a section patch lands through its merge", () => {
  const be = new MockBackend("/config");
  const store = new UserConfigStore(be, () => {}, [VERSIONED_ENTRY]);
  store.load(); // no file: the shell's defaults and the contributor's
  expect(store.section("versioned")).toEqual({ knob: "a", size: 0 });
  // The contributor declares a version, so its block is stamped with it.
  expect(JSON.parse(be.readText(CONFIG)!).versioned).toEqual({ knob: "a", size: 0, schemaVersion: 2 });
  expect(store.setSection("versioned", { size: 3 })).toBeTruthy();
  expect(store.section("versioned")).toEqual({ knob: "a", size: 3 }); // a reader gets the value, not the stamp
  expect(JSON.parse(be.readText(CONFIG)!).versioned).toEqual({ knob: "a", size: 3, schemaVersion: 2 });
});

test("a section stamped newer reads its defaults, refuses a patch once, and is written back as it came", () => {
  const fromTheFuture = { schemaVersion: 9, knob: { anything: true } };
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG, versioned: fromTheFuture }));
  const store = new UserConfigStore(be, () => {}, [VERSIONED_ENTRY]);
  const warnings = captureWarnings();
  try {
    store.load();
    // A reader gets what it can honestly type: the contributor's defaults, not a block it could not parse.
    expect(store.section("versioned")).toEqual({ knob: "a", size: 0 });
    // The writer keeps the block, so two refused patches warn once and change nothing.
    expect(store.setSection("versioned", { knob: "b" })).toBeFalsy();
    expect(store.setSection("versioned", { knob: "c" })).toBeFalsy();
    expect(warnings.lines.filter((line) => line.includes("versioned")).length).toBe(1);
    // A change to the shell's own fields still writes the file, and the block goes back exactly as it came.
    expect(store.setDefaultZoom(2)).toBeTruthy();
    expect(JSON.parse(be.readText(CONFIG)!).versioned).toEqual(fromTheFuture);
  } finally {
    warnings.restore();
  }
});

test("a contributor's schema that throws is isolated: its defaults read, and the file loads and warns once", () => {
  const THROWING: ProjectSection<{ ok: string }> = {
    key: "throwing",
    schema: { parse: () => { throw new Error("a block this build cannot read"); } },
    defaults: () => ({ ok: "default" }),
  };
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG, throwing: { anything: true } }));
  const warnings = captureWarnings();
  try {
    const store = new UserConfigStore(be, () => {}, [{ pkg: "acme", section: THROWING }]);
    store.load();
    expect(store.section("throwing")).toEqual({ ok: "default" });
    expect(store.config().defaultZoom).toBe(DEFAULT_USER_CONFIG.defaultZoom); // the rest of the file is untouched
    expect(warnings.lines.filter((line) => line.includes("throwing")).length).toBe(1);
  } finally {
    warnings.restore();
  }
});

test("a write keeps a section's own key order and the stamp the contributor writes", () => {
  const STAMPED: ProjectSection<{ a: number; b: string }> = {
    key: "stamped",
    version: 1,
    schema: {
      parse: (raw: unknown) => {
        const block = (raw ?? {}) as Record<string, unknown>;
        return { a: typeof block.a === "number" ? block.a : 0, b: typeof block.b === "string" ? block.b : "" };
      },
    },
    defaults: () => ({ a: 0, b: "" }),
  };
  const be = new MockBackend("/config");
  const store = new UserConfigStore(be, () => {}, [{ pkg: "acme", section: STAMPED }]);
  store.load();
  expect(store.setSection("stamped", { a: 2, b: "x" })).toBeTruthy();
  const written = JSON.parse(be.readText(CONFIG)!).stamped;
  expect(written).toEqual({ a: 2, b: "x", schemaVersion: 1 });
});

test("a section write is computed over the file on disk, so another process's change survives it", () => {
  // Two standalones (or a plugin and a standalone) share config.json. A loaded "a", B wrote "b" into the same block,
  // and A's write must merge over B's value: the old path merged over A's in-memory copy and put "a" back.
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG,
    versioned: { knob: "a", size: 0 } }));
  const store = new UserConfigStore(be, () => {}, [VERSIONED_ENTRY]);
  store.load();
  // B's write, behind this store's back.
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG,
    versioned: { knob: "b", size: 0 } }));
  expect(store.setSection("versioned", { size: 3 })).toBeTruthy();
  expect(JSON.parse(be.readText(CONFIG)!).versioned).toEqual({ knob: "b", size: 3, schemaVersion: 2 });
});

test("a section write over a block a NEWER build wrote is refused, not overwritten", () => {
  // The refuse-newer rule at section scope, over the file as it is now: the in-memory block was this build's, and only
  // the re-read can see that a newer one landed.
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG,
    versioned: { knob: "a", size: 0 } }));
  const store = new UserConfigStore(be, () => {}, [VERSIONED_ENTRY]);
  store.load();
  const newer = { schemaVersion: 9, knob: "z", size: 7 };
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG, versioned: newer }));
  const warnings = captureWarnings();
  try {
    expect(store.setSection("versioned", { knob: "b" })).toBeFalsy();
    expect(warnings.lines.some((line) => line.includes("stamped schemaVersion 9"))).toBe(true);
    expect(JSON.parse(be.readText(CONFIG)!).versioned).toEqual(newer);
  } finally {
    warnings.restore();
  }
});

test("a shell-field write keeps a contributor's raw block, additive fields and all", () => {
  // The product that owns the section is the one that writes config.json most: the standalone saves on every zoom,
  // binding and render-dir change. A block only a NEWER build authored goes back as the file carried it, so the field
  // this build's contributor does not parse survives a write it had nothing to do with.
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({
    schemaVersion: USER_CONFIG_SCHEMA,
    ...DEFAULT_USER_CONFIG,
    audio: { backend: "portaudio", bufferCount: 4 },
  }));
  const store = new UserConfigStore(be, () => {}, [{ pkg: "audio", section: AUDIO }]);
  store.load();
  expect(store.setDefaultZoom(2)).toBeTruthy();
  expect(JSON.parse(be.readText(CONFIG)!).audio).toEqual({ backend: "portaudio", bufferCount: 4 });
});

test("a block this session did not set is written back verbatim, additive fields and all", () => {
  // An older build beside a newer one: the newer one's `audio` block carries a field this build's contributor does not
  // parse, and a change to the shell's own fields must not drop it.
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({
    schemaVersion: USER_CONFIG_SCHEMA,
    ...DEFAULT_USER_CONFIG,
    audio: { backend: "portaudio", driver: "pipewire", bufferCount: 4 },
  }));
  const store = new UserConfigStore(be, () => {}, []);
  store.load();
  expect(store.setDefaultZoom(2)).toBeTruthy();
  expect(JSON.parse(be.readText(CONFIG)!).audio).toEqual({ backend: "portaudio", driver: "pipewire", bufferCount: 4 });
});

test("a section this session set is written from its parsed value, and only that block moves", () => {
  const OTHER = { ...VERSIONED, key: "other" };
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({
    schemaVersion: USER_CONFIG_SCHEMA,
    ...DEFAULT_USER_CONFIG,
    versioned: { knob: "a", size: 0, later: "kept while untouched" },
    other: { knob: "a", size: 0 },
  }));
  const store = new UserConfigStore(be, () => {}, [VERSIONED_ENTRY, { pkg: "acme", section: OTHER }]);
  store.load();
  expect(store.setSection("other", { size: 5 })).toBeTruthy();
  const written = JSON.parse(be.readText(CONFIG)!) as Record<string, any>;
  // The untouched block keeps the field this build's schema drops.
  expect(written.versioned).toEqual({ knob: "a", size: 0, later: "kept while untouched" });
  expect(written.other).toEqual({ knob: "a", size: 5, schemaVersion: 2 });
});

test("a hand-edited __proto__ block is written back as an own key, not swallowed as a prototype", () => {
  // A file may carry a root key spelled `__proto__` (the section registry REFUSES contributing one, so no build
  // declares it), and the verbatim-keep rule says the writer keeps what it cannot attribute. A plain assignment would
  // set the shape object's prototype and drop the key from the file, so the write goes through defineProperty.
  const raw = JSON.parse('{"schemaVersion": 3, "__proto__": {"knob": "from a newer build"}}') as Record<string, unknown>;
  // The store's own shape for an unknown root key: an own, enumerable property of `sections` (parseConfigSections).
  const sections: Record<string, unknown> = {};
  putConfigSection(sections, "__proto__", raw["__proto__"]);
  const model: ConfigModel = { core: { ...DEFAULT_USER_CONFIG }, sections, raw };
  const shape = configFileShape(model);
  expect(Object.prototype.hasOwnProperty.call(shape, "__proto__")).toBe(true);
  expect(JSON.stringify(shape).includes('"__proto__":{"knob":"from a newer build"}')).toBe(true);
  expect(Object.getPrototypeOf(shape)).toBe(Object.prototype);  // the shape's own prototype did not move
});

test("the shell's core key list is the schema's, and the generator's refusal list is the same list", () => {
  // One derived list (userConfig.ts's CORE_CONFIG_KEYS) and the generated registry's copy of it, held together so a new
  // shell field cannot be parsed as a raw section by this build while the generator still accepts a contributor of it.
  expect([...CORE_CONFIG_KEYS]).toEqual(["schemaVersion", ...Object.keys(userConfigSchema.shape)]);
  expect([...SHELL_CONFIG_KEYS]).toEqual([...CORE_CONFIG_KEYS]);
});

test("configFileShape writes the shell's fields first and a section cannot shadow one", () => {
  const model: ConfigModel = {
    core: { ...DEFAULT_USER_CONFIG },
    // A section literally named like a core field is refused by every writer, but the shape function is the last gate.
    sections: { zoomish: { a: 1 } },
  };
  const shape = configFileShape(model);
  expect(Object.keys(shape)[0]).toBe("activeKeyboardBindings");
  expect((shape.sections as unknown) === undefined).toBe(true);
  expect(shape.zoomish).toEqual({ a: 1 });
});
