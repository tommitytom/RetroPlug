// The console descriptor registry (src/consoles.ts) held to the answers the shell gave before the descriptors existed:
// the classification table and the battery table are the ones recorded in ./recorded.ts from detectPlatform and
// romHasBattery, and the system-node field and stream tables are the ones recorded from buildGraph's SETTING_FIELDS,
// CONSTRUCT_FIELDS, CONFIG_PARAMS and streamsOf. Moving where a console's data lives must not move what it answers, so
// the recorded tables are the oracle here and the registry's own code paths are what is measured against them: the knob
// rows go through systemKnobs and droppedKnobs, the streams through the perChannel shape buildGraph wires, and the
// classification table through classifyRom's tiered reader.
//
// The rest of the file is what the descriptor's own shape has to hold: the registered platform and core sets
// projectConfig validates against, the splits' tap modes, a console that misdeclares itself, and the contributions the
// generated list builds.
import { test, expect } from "@retroplug/testing";
import { classifyRom } from "../../src/systemsStore";
import {
  CONSOLE_DESCRIPTORS,
  consoleFor,
  detectPlatform,
  detectorAnswer,
  platformEntry,
  registeredCores,
  registeredPlatforms,
  realCartOf,
  romHasBattery,
  sniffLengths,
} from "../../src/consoles";
import {
  buildGraph,
  droppedKnobs,
  exportModeOf,
  systemKnobs,
  type GraphModel,
  type GraphSystem,
} from "../../src/graph/buildGraph";
import { defaultCoreFor, systemNodeTypeFor } from "../../src/platform";
import type { ConsoleDescriptor } from "../../src/consoles";
import type { HostBackend } from "../../src/backend";
import { coreSchema, parseConfig, platformSchema } from "../../src/projectConfig";
import { BATTERY_CASES, CLASSIFY_CASES, FOREIGN_FIELDS, KNOB_TYPES, KNOB_VALUES, OWN_FIELDS } from "./cases";
import { RECORDED_BATTERY, RECORDED_CLASSIFY, RECORDED_KNOBS, RECORDED_STREAMS } from "./recorded";

// The recorded answers are JSON text ("-" for absent), so a value round-trips by that spelling.
const ABSENT = "-";
const spell = (v: unknown): string => (v === undefined ? "undefined" : JSON.stringify(v));
const decode = (s: string): unknown => (s === "undefined" ? undefined : JSON.parse(s));

test("a ROM classifies through the tiered reader, with the answers and the reads the old classifyRom made", () => {
  const wrong: string[] = [];
  const readWrong: string[] = [];
  // A reader that records what it was asked for: classifyRom is the one place ROM bytes enter TS, so its reads are the
  // registry's.
  const reads: number[] = [];
  const bytes = new Map<string, Uint8Array>();
  const reader = {
    readFilePrefix: (path: string, length: number) => {
      reads.push(length);
      return (bytes.get(path) ?? new Uint8Array()).subarray(0, length);
    },
  } as unknown as HostBackend;
  CLASSIFY_CASES.forEach((c, i) => {
    const path = `/x${c.ext ?? ""}`;
    bytes.set(path, c.bytes);
    reads.length = 0;
    const got = classifyRom(reader, path);
    const where = `${c.name}${c.ext === undefined ? "" : ` ${c.ext}`}`;
    if (got !== RECORDED_CLASSIFY[i]) wrong.push(`${where}: ${String(got)} (recorded ${RECORDED_CLASSIFY[i]})`);
    // The cheap tier is the 0x134 detectors over the file's first 0x134 bytes: a file one of them claims costs that one
    // read, and anything else pays the deep read too (the Sega header sits at a bank's end).
    const cheap = detectPlatform(c.bytes.subarray(0, 0x134), undefined);
    const want = cheap === "unknown" ? [0x134, 0x8200] : [0x134];
    if (JSON.stringify(reads) !== JSON.stringify(want)) {
      readWrong.push(`${where}: the reads ${reads.join(",")} (want ${want.join(",")})`);
    }
    if (cheap !== "unknown" && got !== cheap) {
      wrong.push(`${where}: answered ${String(got)} at the deep tier, the cheap one said ${cheap}`);
    }
  });
  if (wrong.length) console.log(`# classification differs:\n#   ${wrong.join("\n#   ")}`);
  if (readWrong.length) console.log(`# reads differ:\n#   ${readWrong.join("\n#   ")}`);
  expect(wrong).toEqual([]);
  expect(readWrong).toEqual([]);
});

test("the battery rule gives the answers the pre-descriptor rule gave", () => {
  const wrong: string[] = [];
  BATTERY_CASES.forEach((c, i) => {
    const got = romHasBattery(c.header, c.platform);
    if (got !== RECORDED_BATTERY[i]) wrong.push(`${c.name}: ${got} (recorded ${RECORDED_BATTERY[i]})`);
  });
  if (wrong.length) console.log(`# battery differs:\n#   ${wrong.join("\n#   ")}`);
  expect(wrong).toEqual([]);
});

test("a ROM is read in two tiers, the cheap one first, and the deep one only when the first declines", () => {
  // A reader that records what it was asked for: classifyRom is the one place ROM bytes enter TS, so its reads are the
  // registry's reads.
  const reads: number[] = [];
  const bytes: Record<string, Uint8Array> = {};
  const reader = {
    readFilePrefix: (path: string, length: number) => {
      reads.push(length);
      const whole = bytes[path] ?? new Uint8Array();
      return whole.subarray(0, Math.min(length, whole.length));
    },
  } as unknown as HostBackend;
  const load = (path: string, b: Uint8Array) => {
    bytes[path] = b;
    reads.length = 0;
    return classifyRom(reader, path);
  };

  // A Nintendo header answers in the first read and pays nothing more, whatever its extension says.
  expect(load("/roms/cart.gb", bytesOf(gbLike(), 0x8000))).toBe("gb");
  expect(reads).toEqual([0x134]);
  // A Sega header sits at the end of a bank, so a file the cheap tier declines costs the deep read.
  expect(load("/roms/cart.sms", bytesOf(segaLike(), 0x20000))).toBe("sms");
  expect(reads).toEqual([0x134, 0x8200]);
  // And a file carrying nothing at all is the second read's extension fallback.
  expect(load("/roms/cart.gg", new Uint8Array(0x8000))).toBe("gg");
  expect(reads).toEqual([0x134, 0x8200]);
  // The registry's tiers are the descriptors' distinct prefix lengths, ascending.
  expect(sniffLengths()).toEqual([0x134, 0x8200]);
});

test("each system node's setting, construct-time and param fields are the ones the old tables kept", () => {
  const wrong: string[] = [];
  for (const [type, field, spelledValue, settings, construct, params, dropped] of RECORDED_KNOBS) {
    if (!consoleFor(type)) {
      wrong.push(`${type}: no descriptor`);
      continue;
    }
    const config = { [field]: decode(spelledValue) };
    // The row is what the shell's own code answers: systemKnobs for the node's settings, construct fields and params,
    // and droppedKnobs for what the document leaves out.
    const answers = systemKnobs({ type, coreConfig: config });
    const has = (o: Record<string, unknown>) => Object.prototype.hasOwnProperty.call(o, field);
    const got = {
      settings: has(answers.settings) ? spell(answers.settings[field]) : ABSENT,
      construct: has(answers.construct) ? spell(answers.construct[field]) : ABSENT,
      params: has(answers.params) ? spell(answers.params[field]) : ABSENT,
      dropped: droppedKnobs(type, config).length > 0,
    };
    const want = [settings, construct, params, dropped] as const;
    const have = [got.settings, got.construct, got.params, got.dropped] as const;
    if (want.join("|") !== have.join("|")) {
      wrong.push(`${type} ${field} ${spelledValue}: ${have.join(" ")} (recorded ${want.join(" ")})`);
    }
  }
  if (wrong.length) console.log(`# fields differ:\n#   ${wrong.slice(0, 40).join("\n#   ")}`);
  expect(wrong).toEqual([]);
});

test("every stream layout the old streamsOf reported is the one buildGraph wires", () => {
  // The recorder read the streams through the one shape that prints them: a lone system in the perChannel shape wires
  // stream k to its own output bus, so the out node's `buses` and the ports the wires leave say which streams a mode
  // reports - and exportModeOf is measured on the way, since it is what the shape chooses the mode by.
  const wrong: string[] = [];
  const labels = ["mix", "stereoModPins", "pinsPlusRef", "individualMono"];
  for (const [type, mode, buses, ports] of RECORDED_STREAMS) {
    // Reach each mode the way a store does: the pins and channels splits by the routing (the engine's arm), the pin
    // reference layout by the stored export label. Only a NES reads either.
    const split = type === "rp.mesen.nes" && (mode === 1 || mode === 3);
    const routing = split ? (mode === 1 ? "pinSplit" : "channelSplit") : "onePerInstance";
    const system = {
      id: 1,
      platform: PLATFORM_OF[type],
      type,
      linkGroup: 0,
      exportMode: exportModeOf(type, { channelExportMode: labels[mode] }),
      resources: { rom: 1 },
      romName: "rom1",
      pipeline: [],
      coreConfig: { channelExportMode: labels[mode] },
    } as unknown as GraphSystem;
    const built = buildGraph({
      systems: [system],
      routing: routing as GraphModel["routing"],
      shape: { kind: "perChannel" },
    });
    const out = built.document.nodes.find((node) => node.id === "out")!;
    const wired = built.document.wires
      .filter((wire) => wire.to.startsWith("out.out."))
      .map((wire) => ({
        at: Number(wire.to.slice("out.out.".length).split("-")[0]),
        port: wire.from.slice("sys-1.".length),
      }))
      .sort((a, b) => a.at - b.at)
      .map((wire) => wire.port);
    if (JSON.stringify([out.settings?.buses, wired]) !== JSON.stringify([buses, ports])) {
      const got = JSON.stringify([out.settings?.buses, wired]);
      wrong.push(`${type} mode ${mode}: ${got} (recorded ${JSON.stringify([buses, ports])})`);
    }
  }
  if (wrong.length) console.log(`# streams differ:\n#   ${wrong.join("\n#   ")}`);
  expect(wrong).toEqual([]);
});

// The platform each recorded node type runs, as the store places it: the model only needs one a console registers.
const PLATFORM_OF: Readonly<Record<string, string>> = {
  "rp.sameboy.gb": "gb",
  "rp.mesen.nes": "nes",
  "rp.mesen.gba": "gba",
  "rp.mesen.sms": "sms",
};

test("the fields cases.ts probes are the ones the descriptors hold, and a field another node owns is on none", () => {
  const wrong: string[] = [];
  for (const type of KNOB_TYPES) {
    const descriptor = consoleFor(type);
    if (!descriptor) {
      wrong.push(`${type}: no descriptor`);
      continue;
    }
    const fields = [
      ...Object.keys(descriptor.settingFields ?? {}),
      ...Object.keys(descriptor.constructFields ?? {}),
      ...(descriptor.configParams ?? []),
    ];
    // Every field the record probed as this type's own is one the descriptor carries, and nothing else is: a table
    // entry the case list missed would be a knob no test measures.
    const own = OWN_FIELDS[type].filter((field) => field !== "linkGroupId");
    if (JSON.stringify([...fields].sort()) !== JSON.stringify([...own].sort())) {
      wrong.push(`${type}: ${fields.join(",")} (own ${own.join(",")})`);
    }
    for (const field of FOREIGN_FIELDS[type]) if (fields.includes(field)) wrong.push(`${type} carries ${field}`);
  }
  expect(wrong).toEqual([]);
  // The value list covers each field's own accepted labels and numbers, which is what makes the record meaningful.
  const values = KNOB_VALUES.map(spell);
  expect(RECORDED_KNOBS.filter((record) => !values.includes(record[2])).map((record) => record[2])).toEqual([]);
});

test("the registry holds the consoles the product's packages contribute, each node type once", () => {
  const types = KNOB_TYPES.map((type) => consoleFor(type));
  expect(types.every((d) => d !== undefined)).toBe(true);
  const nodeTypes = types.map((d) => d!.nodeType);
  expect(new Set(nodeTypes).size).toBe(nodeTypes.length);
  // One descriptor per platform, and each platform's node type is the one the store places it with.
  const wrong: string[] = [];
  for (const platform of registeredPlatforms()) {
    const descriptor = consoleFor(systemNodeTypeFor(platform.id));
    if (!descriptor) wrong.push(`${platform.id}: no descriptor for its node`);
    else if (defaultCoreFor(platform.id) !== descriptor.core) {
      wrong.push(`${platform.id}: core ${String(defaultCoreFor(platform.id))}`);
    }
    if (platformEntry(platform.id)?.id !== platform.id) wrong.push(`${platform.id}: not in the platform list`);
  }
  expect(wrong).toEqual([]);
  // Every core a descriptor names is one the registry knows: the core-config role is named after its core, so a core
  // with no descriptor could not be configured by any role either.
  const cores = registeredCores();
  expect(cores).toEqual(["sameboy", "mesen", "blastem"]);
  expect(types.filter((d) => !cores.includes(d!.core)).map((d) => d!.core)).toEqual([]);
  // The N8 is the one registered real cart, and it names a platform a descriptor registers.
  const cart = realCartOf("nes");
  expect(cart).toEqual({ platform: "nes", cartNode: "rp.n8.nes" });
  expect(platformEntry(cart!.platform) !== undefined).toBe(true);
  // The Game Gear is the one platform whose node needs a setting to be the right machine.
  expect(platformEntry("gg")?.nodeSettings).toEqual({ gameGear: true });
});

test("each split's tap mode is the export mode its config patch names", () => {
  // The mode a render builds a node in (the split's configPatch label) and the mode the engine arms for a lone system
  // (its tapMode) must be the same ordinal, or a split renders one layout and arms another.
  const wrong: string[] = [];
  for (const descriptor of CONSOLE_DESCRIPTORS) {
    for (const [name, split] of Object.entries(descriptor.splits ?? {})) {
      // A split that changes no config and arms no tap (the Game Boy's four channels) has no mode to agree on.
      if (split.tapMode === undefined && split.configPatch === undefined) continue;
      const modes = descriptor.exportModes;
      const label = modes === undefined ? undefined : split.configPatch?.[modes.field];
      const want = modes !== undefined && typeof label === "string" ? modes.values.indexOf(label) : -1;
      if (want < 0) {
        wrong.push(`${descriptor.id} ${name}: its patch names no export mode (tapMode ${String(split.tapMode)})`);
      } else if (split.tapMode !== want) {
        wrong.push(`${descriptor.id} ${name}: tapMode ${String(split.tapMode)}, the mode is ${want}`);
      }
    }
  }
  if (wrong.length) console.log(`# tap modes differ:\n#   ${wrong.join("\n#   ")}`);
  expect(wrong).toEqual([]);
});

test("a detector that answers a platform its own descriptor does not list is refused, naming the console", () => {
  // The registry's list is the product's, so a console that misdeclares itself cannot be registered in a test: the
  // classification's guard is exercised with the descriptor the registry would be handed.
  const misdeclared: ConsoleDescriptor = {
    id: "fixture.bad",
    core: "mesen",
    nodeType: "rp.fixture.bad",
    platforms: [{ id: "gb", label: "Game Boy", extensions: [".gb"] }],
    detect: { prefixBytes: 4, rank: 9, run: () => "n64" },
  };
  expect(detectorAnswer(misdeclared, "gb")).toBe("gb");
  expect(() => detectorAnswer(misdeclared, "n64")).toThrow('the console fixture.bad detects "n64"');
  expect(() => detectorAnswer(misdeclared, "n64")).toThrow("which its platforms do not list (gb)");
});

test("projectConfig refuses an unknown platform and core, naming the registered set", () => {
  const known = registeredPlatforms().map((p) => p.id);
  // The order is the descriptors': the product's, then a package's own list (mesen declares its GBA before its NES, so
  // the extension lists keep the order the shell's own lists had).
  expect(known).toEqual(["gb", "gba", "nes", "sms", "gg", "md"]);
  expect(registeredCores()).toEqual(["sameboy", "mesen", "blastem"]);
  const platform = platformSchema.safeParse("n64");
  expect(platform.success).toBe(false);
  expect(platform.error?.issues[0]?.message).toBe(`unknown platform "n64" (known: ${known.join(", ")})`);
  const core = coreSchema.safeParse("gambatte");
  expect(core.success).toBe(false);
  expect(core.error?.issues[0]?.message).toBe(`unknown core "gambatte" (known: sameboy, mesen, blastem)`);
  // parseConfig drops the entries that fail the strict parse silently, as it dropped an unknown platform's before, so
  // a file with one bad entry still loads the others.
  const loaded = parseConfig(
    JSON.stringify({
      schemaVersion: "4",
      systems: [
        { platform: "n64", core: "mesen" },
        { platform: "gb", core: "sameboy" },
      ],
    }),
  );
  expect(loaded.systems.map((s) => s.platform)).toEqual(["gb"]);
});

// ---- the byte builders the two-read test needs, kept minimal: the case list's fixtures are elsewhere ----------------

function bytesOf(at: { at: number; bytes: number[] }, len: number): Uint8Array {
  const b = new Uint8Array(len);
  b.set(at.bytes, at.at);
  return b;
}
function gbLike(): { at: number; bytes: number[] } {
  // The Game Boy logo's first bytes: enough for the 0x134 tier to answer, which is all this test is about.
  return {
    at: 0x104,
    bytes: [
      0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
      0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
      0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
    ],
  };
}
function segaLike(): { at: number; bytes: number[] } {
  return { at: 0x7ff0, bytes: [0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41] };
}
