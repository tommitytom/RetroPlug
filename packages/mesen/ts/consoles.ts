// Mesen's three consoles as descriptors (docs/modular-graph-phase-f4a-design.md section 3): what the shell needs to
// classify an NES, a GBA or a Master System / Game Gear ROM, place the node each runs as, wire that node's knobs and
// name its streams, without knowing which console it is looking at. The data is the shell's own, moved here unchanged
// (src/platform.ts's detection and battery rules, graph/buildGraph.ts's node tables and stream layouts, the render's
// split names). One descriptor is one graph node type, and the NES's node is the one whose core can be split.
import {
  CARTRIDGE_ACCURACY_VALUES,
  CHANNEL_EXPORT_VALUES,
  FIFO_PROFILE_VALUES,
  N8_LINK_VALUES,
  REGION_VALUES,
} from "./enums";
import {
  flag,
  intIn,
  label,
  within,
  type ConsolesContribution,
  type ConsoleDescriptor,
} from "@retroplug/core/consoles";

// GBA Nintendo logo (first 32 of 156 bytes) at $0004: the boot ROM CRC-checks it.
const GBA_LOGO = [
  0x24, 0xff, 0xae, 0x51, 0x69, 0x9a, 0xa2, 0x21, 0x3d, 0x84, 0x82, 0x0a, 0x84, 0xe4, 0x09, 0xad,
  0x11, 0x24, 0x8b, 0x98, 0xc0, 0x81, 0x7f, 0x21, 0xa3, 0x52, 0xbe, 0x19, 0x93, 0x09, 0xce, 0x20,
];
const GBA_LOGO_OFFSET = 0x04;

// Sega 8-bit header magic ("TMR SEGA"), which unlike the Nintendo logos lives at the END of a bank. $7FF0 is the usual
// spot; $1FF0/$3FF0 are where an 8 KB/16 KB cart puts it. Each is doubled by the +$200 copier-header variant Mesen
// strips at load (SmsConsole.cpp: `(size % 0x400) == 0x200`): such a ROM boots fine, so it must classify too.
const SEGA_MAGIC = [0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41]; // "TMR SEGA"
const SEGA_HEADER_OFFSETS = [0x7ff0, 0x3ff0, 0x1ff0].flatMap((o) => [o, o + 0x200]);
// Bytes needed to reach a Sega 8-bit header: $7FF0 for a 32 KB-or-larger cart, plus $200 more for a copier-headered
// dump. Far past the Nintendo tier, which is why a file is read twice and only one the cheap tier declines pays this.
const SEGA_SNIFF_LEN = 0x8200;

// Region/machine code, high nibble of the header's last byte (magic + $F). 3/4 are Master System (Japan/Export),
// 5/6/7 Game Gear (Japan/Export/International). Anything else is a homebrew that never filled it in, and falls through
// to the extension.
const GG_REGION_NIBBLES = new Set([5, 6, 7]);
const SMS_REGION_NIBBLES = new Set([3, 4]);

// A system's stream layouts (spec/10's per-channel export): a NES's three pin streams, the same plus a mix reference,
// its five core channels, and the mix every console falls back to. The ports are the core's.
const PINS = ["pins.pulse", "pins.tnd", "pins.expansion"];
const PINS_PLUS_REF = [...PINS, "pins.mixref"];
const NES_CHANNELS = ["channels.sq1", "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"];
const MIX = [{ port: "mix", stereo: true }];

function matchesAt(bytes: Uint8Array, offset: number, sig: number[]): boolean {
  if (bytes.length < offset + sig.length) return false;
  for (let i = 0; i < sig.length; i++) {
    if (bytes[offset + i] !== sig[i]) return false;
  }
  return true;
}

/** `"sms"`/`"gg"` for a `.sms`/`.gg` extension, else null. Lowercase, dot included. */
function segaExtension(ext: string | undefined): "sms" | "gg" | null {
  if (ext === ".gg") return "gg";
  if (ext === ".sms") return "sms";
  return null;
}

// Which Sega machine a located header describes. The region nibble decides when it's one of the five defined values;
// otherwise the extension breaks the tie, and failing that we default to Master System - exactly what Mesen does for
// any extension that isn't .gg (SmsConsole.cpp:46-60).
function segaMachine(bytes: Uint8Array, headerOffset: number, ext: string | undefined): string {
  const nibble = (bytes[headerOffset + 0xf] ?? 0) >> 4;
  if (GG_REGION_NIBBLES.has(nibble)) return "gg";
  if (SMS_REGION_NIBBLES.has(nibble)) return "sms";
  return segaExtension(ext) ?? "sms";
}

/** The NES's descriptor. */
export const NES: ConsoleDescriptor = {
  id: "mesen.nes",
  core: "mesen",
  nodeType: "rp.mesen.nes",
  // The one platform whose systems may drive a registered physical cart, and the core-config field that opts one out
  // (docs/modular-graph-phase-f4a-design.md decision 12: the field stays Mesen's, where saved projects already carry
  // it).
  platforms: [{ id: "nes", label: "NES", extensions: [".nes"], realCartOptOut: "driveRealCart" }],
  detect: {
    prefixBytes: 0x134,
    // Rank 0: the iNES magic wins a crafted file that also carries a GB or GBA logo.
    rank: 0,
    run: (bytes) =>
      bytes.length >= 4 && bytes[0] === 0x4e && bytes[1] === 0x45 && bytes[2] === 0x53 && bytes[3] === 0x1a ? "nes" : null,
  },
  // iNES flags6 bit 1.
  hasBattery: (header) => header.length > 6 && (header[6] & 0x02) !== 0,
  settingFields: {
    region: label(REGION_VALUES),
    removeSpriteLimit: flag,
    apuLatencyMs: within(0.25, 6),
    s5bNoise: label(CARTRIDGE_ACCURACY_VALUES),
    mmc5PhaseReset: label(CARTRIDGE_ACCURACY_VALUES),
  },
  // The fields the node reads once, when it builds its core at activation, so a change to one is a rebuild under a new
  // system id rather than a document.
  constructFields: {
    sdRoot: { takes: (v) => typeof v === "string", fallback: "" },
    fifo: { takes: label(FIFO_PROFILE_VALUES), fallback: "instant" },
    fifoDepth: { takes: intIn(0, 1 << 20), fallback: 0 },
    fifoBytesPerSecond: { takes: intIn(0, 100_000_000), fallback: 0 },
    n8Link: { takes: label(N8_LINK_VALUES), fallback: "auto" },
    n8LinkRttUs: { takes: intIn(0, 100_000), fallback: 2200 },
    channelExportMode: { takes: label(CHANNEL_EXPORT_VALUES), fallback: "mix" },
  },
  configParams: ["expansionVolume"],
  streams: (mode) => {
    const mono = (ports: readonly string[]) => ports.map((port) => ({ port, stereo: false }));
    if (mode === 1) return mono(PINS);
    if (mode === 2) return mono(PINS_PLUS_REF);
    if (mode === 3) return mono(NES_CHANNELS);
    return MIX;
  },
  // The one console whose core is built in more than one output layout (spec/10's per-channel export): the config names
  // the mode, and the index is what `streams` is asked for. `pinsPlusRef` has no split of its own (the render library and
  // the CLI's export sessions build it), which is why the modes are the descriptor's data rather than the splits'.
  exportModes: { field: "channelExportMode", values: CHANNEL_EXPORT_VALUES },
  capabilities: { channelTap: true },
  // Each split's tapMode is the mode its configPatch names, as exportModes' index (stereoModPins 1, individualMono 3):
  // the mode a node is built in and the mode the engine arms for a lone system under that split.
  splits: {
    channels: {
      names: ["square1", "square2", "triangle", "noise", "dmc"],
      configPatch: { channelExportMode: "individualMono" },
      tapMode: 3,
    },
    pins: { names: ["pulse", "tnd", "expansion"], configPatch: { channelExportMode: "stereoModPins" }, tapMode: 1 },
  },
  render: { mixLanes: "left" }, // NES streams are mono in the left lane
};

/** The GBA's descriptor: no knob of its own yet (it carries Mesen's role config as inert bytes), and no battery rule. */
export const GBA: ConsoleDescriptor = {
  id: "mesen.gba",
  core: "mesen",
  nodeType: "rp.mesen.gba",
  platforms: [{ id: "gba", label: "Game Boy Advance", extensions: [".gba"] }],
  detect: {
    prefixBytes: 0x134,
    // Rank 1: the GBA logo wins a crafted file that also carries the Game Boy's.
    rank: 1,
    run: (bytes) => (matchesAt(bytes, GBA_LOGO_OFFSET, GBA_LOGO) ? "gba" : null),
  },
  settingFields: {},
  constructFields: {},
  configParams: [],
};

/** The Master System and the Game Gear: one core, one node, and the node's `gameGear` setting picks the machine. */
export const SMS_GG: ConsoleDescriptor = {
  id: "mesen.sms",
  core: "mesen",
  nodeType: "rp.mesen.sms",
  platforms: [
    { id: "sms", label: "Master System", extensions: [".sms"], extensionOnly: true },
    // One node type, one machine: the gameGear setting is what a Game Gear system's node carries (buildGraph writes
    // it from the platform, never from the stored config).
    { id: "gg", label: "Game Gear", extensions: [".gg"], nodeSettings: { gameGear: true }, extensionOnly: true },
  ],
  detect: {
    prefixBytes: SEGA_SNIFF_LEN,
    // Rank 3, and the only detector on the deep tier: it reads the extension for the one decision the content cannot
    // make (an unfilled region nibble).
    rank: 3,
    run: (bytes, ext) => {
      for (const off of SEGA_HEADER_OFFSETS) {
        if (matchesAt(bytes, off, SEGA_MAGIC)) return segaMachine(bytes, off, ext);
      }
      // No magic anywhere: the registry's extension-only fallback claims a headerless .sms/.gg file, from the platforms
      // that declare they can be recognised that way.
      return null;
    },
  },
  // The header sits at the END of a bank, so the ROM providers are shown the whole prefix it lives in (smsggdj's
  // marker is at $3640, past the short prefix every other console is read at).
  providerHeaderBytes: SEGA_SNIFF_LEN,
  settingFields: {},
  constructFields: { enableFm: { takes: flag, fallback: true } },
  configParams: [],
};

// The manifest's retroplug.consoles member names this module (docs/modular-graph-phase-f4a-design.md section 3), which
// the generator concatenates into src/generated/consoles.ts: the shell classifies a ROM and places a system through
// that list, so nothing here reaches an app module or zod. The GBA precedes the NES in the list because the platform
// order is behaviour: the ROM extension lists (the file dialogs' globs and the sibling-ROM probe) take it, and the
// shell's own list always put the GBA first.
const MESEN_CONSOLES: ConsolesContribution = { consoles: [GBA, NES, SMS_GG] };
export default MESEN_CONSOLES;
