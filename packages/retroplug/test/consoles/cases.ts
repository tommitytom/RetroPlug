// The inputs the console registry's recorded tables were taken over (test/consoles/registry.test.ts): the ROM fixtures
// every store test builds its headers from, the synthetic headers test/paths/rom-format.test.ts pins detection on, the
// polyglots its priority cases are built from, and the battery of values each system-node field is probed with.
//
// The tables themselves are in recorded.ts, recorded from the code as it was BEFORE the descriptors existed
// (detectPlatform and romHasBattery in src/platform.ts, buildGraph's SETTING_FIELDS / CONSTRUCT_FIELDS / CONFIG_PARAMS
// and streamsOf). Nothing here knows a platform: it is bytes, extensions and values.
import {
  blipToasterRom,
  gbRom,
  gbRomBattery,
  gbaRom,
  garbage,
  lsdjRom,
  nesRom,
  nesRomBattery,
  risaRom,
  smsggdjRom,
} from "../systems/fixtures";

export interface ClassifyCase {
  readonly name: string;
  readonly bytes: Uint8Array;
  /** Lowercase, dot included; absent leaves the sniffer no extension to fall back on. */
  readonly ext?: string;
}

// iNES magic "NES\x1A" at offset 0, the GBA logo (first 32 bytes) at 0x04 and the GB logo (48 bytes) at 0x104: the same
// byte arrays rom-format.test.ts transcribes independently from the C++ gates.
const NES_MAGIC = [0x4e, 0x45, 0x53, 0x1a];
const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];
const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];
const SEGA_MAGIC = [0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41]; // "TMR SEGA"

function buf(len: number, at: number, bytes: number[]): Uint8Array {
  const b = new Uint8Array(len);
  b.set(bytes, at);
  return b;
}

// A Sega ROM image with its 16-byte header at `at`: the magic, then `region` as the high nibble of the header's last
// byte. 3/4 are Master System (Japan/Export), 5/6/7 Game Gear, 0 an unfilled homebrew field.
function segaRom(at: number, region: number, len = 0x20000): Uint8Array {
  const b = buf(len, at, SEGA_MAGIC);
  b[at + 0xf] = (region << 4) | 0x0c;
  return b;
}

// A raw GB header prefix carrying a given cartridge-type byte at $147, and a raw iNES header carrying a flags6 byte.
function gbHeader(cartType: number): Uint8Array {
  const b = new Uint8Array(0x150);
  b[0x147] = cartType;
  return b;
}
function nesHeader(flags6: number): Uint8Array {
  const b = new Uint8Array(0x10);
  b[6] = flags6;
  return b;
}

const nesOverGb = buf(0x8000, 0x104, GB_LOGO);
nesOverGb.set(NES_MAGIC, 0);
const gbaOverGb = buf(0x8000, 0x104, GB_LOGO);
gbaOverGb.set(GBA_LOGO, 0x04);

/** The extensions every fixture is also offered: none, each console's own, and one naming no console. */
const EXTENSIONS: readonly (string | undefined)[] = [undefined, ".gb", ".nes", ".sms", ".gg", ".bin"];

/** The ROM fixtures every store test builds, and the ones whose header a test may mislabel, each crossed with the
 *  extensions the sniffer consults. */
export const CLASSIFY_CASES: readonly ClassifyCase[] = [
  // The fixtures, as they are on disk (no extension offered) ...
  { name: "gbRom", bytes: gbRom() },
  { name: "gbRomBattery", bytes: gbRomBattery() },
  { name: "lsdjRom", bytes: lsdjRom() },
  { name: "gbaRom", bytes: gbaRom() },
  { name: "nesRom", bytes: nesRom() },
  { name: "nesRomBattery", bytes: nesRomBattery() },
  { name: "smsggdjRom", bytes: smsggdjRom() },
  { name: "risaRom", bytes: risaRom() },
  { name: "blipToasterRom", bytes: blipToasterRom() },
  { name: "garbage", bytes: garbage() },
  // ... and every one of them behind each extension, since only the extension tiers read it.
  ...(
    [
      ["gbRom", gbRom()],
      ["gbRomBattery", gbRomBattery()],
      ["lsdjRom", lsdjRom()],
      ["gbaRom", gbaRom()],
      ["nesRom", nesRom()],
      ["nesRomBattery", nesRomBattery()],
      ["smsggdjRom", smsggdjRom()],
      ["risaRom", risaRom()],
      ["blipToasterRom", blipToasterRom()],
      ["garbage", garbage()],
    ] as const
  ).flatMap(([name, bytes]) => EXTENSIONS.filter((ext) => ext !== undefined).map((ext) => ({ name: `${name}${ext}`, bytes, ext }))),
  // The synthetic headers rom-format.test.ts pins the detectors on.
  { name: "nes magic, 64 bytes", bytes: buf(64, 0, NES_MAGIC) },
  { name: "nes magic, exactly 4 bytes", bytes: new Uint8Array(NES_MAGIC) },
  { name: "gba logo at 0x04", bytes: buf(0x2000, 0x04, GBA_LOGO) },
  { name: "gba logo, exactly to the guard", bytes: buf(0x04 + 32, 0x04, GBA_LOGO) },
  { name: "gb logo at 0x104", bytes: buf(0x8000, 0x104, GB_LOGO) },
  { name: "gb logo, exactly to the guard", bytes: buf(0x104 + 48, 0x104, GB_LOGO) },
  { name: "all zero", bytes: new Uint8Array(0x8000) },
  { name: "six bytes", bytes: new Uint8Array([1, 2, 3, 4, 5, 6]) },
  { name: "empty", bytes: new Uint8Array(0) },
  { name: "three bytes of the nes magic", bytes: new Uint8Array([0x4e, 0x45, 0x53]) },
  { name: "gb logo one byte short", bytes: buf(0x104 + 47, 0x104, GB_LOGO.slice(0, 47)) },
  // The polyglots (rom-format.test.ts:76): the crafted files whose two signatures make the detection ORDER visible.
  { name: "polyglot: nes over gb", bytes: nesOverGb },
  { name: "polyglot: gba over gb", bytes: gbaOverGb },
  // Sega 8-bit: each bank-end offset, the copier-header shift, and every region nibble.
  { name: "sega at 0x7ff0", bytes: segaRom(0x7ff0, 4) },
  { name: "sega at 0x3ff0", bytes: segaRom(0x3ff0, 4) },
  { name: "sega at 0x1ff0", bytes: segaRom(0x1ff0, 4) },
  { name: "sega at 0x7ff0 + copier", bytes: segaRom(0x7ff0 + 0x200, 4) },
  { name: "sega at 0x1ff0 + copier, gg", bytes: segaRom(0x1ff0 + 0x200, 6) },
  { name: "sega region 3", bytes: segaRom(0x7ff0, 3) },
  { name: "sega region 4", bytes: segaRom(0x7ff0, 4) },
  { name: "sega region 5", bytes: segaRom(0x7ff0, 5) },
  { name: "sega region 6", bytes: segaRom(0x7ff0, 6) },
  { name: "sega region 7", bytes: segaRom(0x7ff0, 7) },
  { name: "sega unfilled region", bytes: segaRom(0x7ff0, 0) },
  { name: "sega unfilled region, .gg", bytes: segaRom(0x7ff0, 0), ext: ".gg" },
  { name: "sega unfilled region, .sms", bytes: segaRom(0x7ff0, 0), ext: ".sms" },
  { name: "sega unfilled region, .bin", bytes: segaRom(0x7ff0, 0), ext: ".bin" },
  { name: "headerless, .sms", bytes: new Uint8Array(0x8000), ext: ".sms" },
  { name: "headerless, .gg", bytes: new Uint8Array(0x8000), ext: ".gg" },
  { name: "headerless, .gb", bytes: new Uint8Array(0x8000), ext: ".gb" },
  { name: "sega truncated to the cheap tier", bytes: segaRom(0x7ff0, 4).subarray(0, 0x134) },
  { name: "sega one byte short of its header", bytes: segaRom(0x7ff0, 4).subarray(0, 0x7ff0 + 7) },
  // Precedence: real content always beats the extension.
  { name: "nes, .sms", bytes: buf(0x8000, 0, NES_MAGIC), ext: ".sms" },
  { name: "gb, .gg", bytes: buf(0x8000, 0x104, GB_LOGO), ext: ".gg" },
  { name: "gba, .sms", bytes: buf(0x8000, 0x04, GBA_LOGO), ext: ".sms" },
  { name: "sega region 6, .sms", bytes: segaRom(0x7ff0, 6), ext: ".sms" },
  { name: "sega region 4, .gg", bytes: segaRom(0x7ff0, 4), ext: ".gg" },
];

export interface BatteryCase {
  readonly name: string;
  readonly header: Uint8Array;
  readonly platform: string;
}

// Every GB cartridge type the rule names, a few it does not, and each console's own reading of a battery flag.
const GB_BATTERY_TYPES = [0x03, 0x06, 0x09, 0x0d, 0x0f, 0x10, 0x13, 0x1b, 0x1e, 0x22, 0xff];
const GB_PLAIN_TYPES = [0x00, 0x01, 0x02, 0x08, 0x1a, 0xfe];

/** The headers the battery rule is read on, each with the platform it is read for. */
export const BATTERY_CASES: readonly BatteryCase[] = [
  { name: "nesRom (flags6 0)", header: nesRom(), platform: "nes" },
  { name: "nesRomBattery (flags6 0x12)", header: nesRomBattery(), platform: "nes" },
  { name: "iNES flags6 0x02", header: nesHeader(0x02), platform: "nes" },
  { name: "iNES flags6 0x01", header: nesHeader(0x01), platform: "nes" },
  { name: "iNES header, four bytes", header: new Uint8Array(4), platform: "nes" },
  { name: "gbRom (type 0x00)", header: gbRom(), platform: "gb" },
  { name: "gbRomBattery (type 0x03)", header: gbRomBattery(), platform: "gb" },
  { name: "lsdjRom (type 0x03)", header: lsdjRom(), platform: "gb" },
  ...GB_BATTERY_TYPES.map((type) => ({
    name: `gb cartridge type 0x${type.toString(16)}`,
    header: gbHeader(type),
    platform: "gb",
  })),
  ...GB_PLAIN_TYPES.map((type) => ({
    name: `gb cartridge type 0x${type.toString(16)}`,
    header: gbHeader(type),
    platform: "gb",
  })),
  { name: "gb header, four bytes", header: new Uint8Array(4), platform: "gb" },
  { name: "gbaRom", header: gbaRom(), platform: "gba" },
  { name: "sms zeroed header", header: new Uint8Array(0x8000), platform: "sms" },
  { name: "gg zeroed header", header: new Uint8Array(0x8000), platform: "gg" },
  { name: "sms with no header read", header: new Uint8Array(0), platform: "sms" },
  { name: "smsggdjRom", header: smsggdjRom(), platform: "sms" },
];

/** The node types each system-node table was keyed by. */
export const KNOB_TYPES = ["rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"] as const;

/** The fields each type's OWN tables named (SETTING_FIELDS, CONSTRUCT_FIELDS and CONFIG_PARAMS as buildGraph kept them),
 *  so each is probed with every value of the battery below. rp.sameboy.gb's `linkGroupId` is here because the role
 *  config carries it and no table keys on it: it is wiring, not a knob. */
export const OWN_FIELDS: Readonly<Record<string, readonly string[]>> = {
  "rp.sameboy.gb": ["model", "fastBoot", "highpass", "colorCorrection", "dmgPalette", "lightTemperature", "linkGroupId"],
  "rp.mesen.nes": [
    "region",
    "removeSpriteLimit",
    "apuLatencyMs",
    "s5bNoise",
    "mmc5PhaseReset",
    "expansionVolume",
    "sdRoot",
    "fifo",
    "fifoDepth",
    "fifoBytesPerSecond",
    "n8Link",
    "n8LinkRttUs",
    "channelExportMode",
  ],
  "rp.mesen.gba": [],
  "rp.mesen.sms": ["enableFm"],
};

/** Fields a type does NOT own, probed with the same battery: a label field of another node, a whole number of another
 *  node, and a name no node has. A node carries none of them, whatever the value: a stored knob whose name is misspelt
 *  must be left out rather than sent to a core that would ignore it. */
export const FOREIGN_FIELDS: Readonly<Record<string, readonly string[]>> = {
  "rp.sameboy.gb": ["fifoDepth", "sdRoot", "nope"],
  "rp.mesen.nes": ["enableFm", "fastBoot", "nope"],
  "rp.mesen.gba": ["enableFm", "fifoDepth", "nope"],
  "rp.mesen.sms": ["region", "fifoDepth", "nope"],
};

/** The values each field is probed with: absent (the config holds no value), null, each accepted label and number, and
 *  values a node refuses (another field's label, a fraction where a whole number is read, a number out of range). */
export const KNOB_VALUES: readonly unknown[] = [
  undefined,
  null,
  0,
  1,
  2,
  3,
  0.25,
  -1,
  1.5,
  6,
  100,
  2200,
  1 << 20,
  100_000_000,
  true,
  false,
  "",
  "mix",
  "individualMono",
  "instant",
  "auto",
  "pal",
  "n8",
  "removeDcOffset",
  "gbl",
  "/sd",
];

/** The export modes streamsOf is recorded for: every branch it has (mix, the three pin layouts, the five channels). */
export const STREAM_MODES: readonly number[] = [0, 1, 2, 3];
