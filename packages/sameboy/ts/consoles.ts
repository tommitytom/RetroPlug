// The Game Boy as a console descriptor (docs/modular-graph-phase-f4a-design.md section 3): what the shell needs to
// classify a Game Boy ROM, place its rp.sameboy.gb node, wire that node's knobs and name its streams, without knowing
// which console it is looking at. The data is the shell's own, moved here unchanged (src/platform.ts's detection and
// battery rules, graph/buildGraph.ts's node tables and stream layout, the render's split names).
import {
  COLOR_CORRECTION_VALUES,
  DMG_PALETTE_VALUES,
  HIGHPASS_VALUES,
  MODEL_VALUES,
} from "./enums";
import {
  flag,
  label,
  within,
  type ConsolesContribution,
  type ConsoleDescriptor,
} from "@retroplug/core/consoles";

// Game Boy Nintendo logo (48 bytes) at $0104: required by the GB boot ROM, so present in every DMG/CGB cart. (DMG vs CGB
// is a SameBoy `model` knob, not a separate platform.)
const GB_LOGO = [
  0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0c, 0x00, 0x0d,
  0x00, 0x08, 0x11, 0x1f, 0x88, 0x89, 0x00, 0x0e, 0xdc, 0xcc, 0x6e, 0xe6, 0xdd, 0xdd, 0xd9, 0x99,
  0xbb, 0xbb, 0x67, 0x63, 0x6e, 0x0e, 0xec, 0xcc, 0xdd, 0xdc, 0x99, 0x9f, 0xbb, 0xb9, 0x33, 0x3e,
];
const GB_LOGO_OFFSET = 0x104;
// The Game Boy logo ends at $0133, so 0x134 bytes cover the whole header: the same prefix the NES and GBA detectors
// declare, which is why a file's first read is shared between the three.
const GB_SNIFF_LEN = 0x134;

// GB cartridge types (header $147) that carry battery-backed save memory: the set SameBoy allocates a .sav for.
const GB_BATTERY_CART_TYPES = new Set([0x03, 0x06, 0x09, 0x0d, 0x0f, 0x10, 0x13, 0x1b, 0x1e, 0x22, 0xff]);

function matchesAt(bytes: Uint8Array, offset: number, sig: number[]): boolean {
  if (bytes.length < offset + sig.length) return false;
  for (let i = 0; i < sig.length; i++) {
    if (bytes[offset + i] !== sig[i]) return false;
  }
  return true;
}

/** The Game Boy's descriptor. */
export const GAME_BOY: ConsoleDescriptor = {
  id: "sameboy.gb",
  core: "sameboy",
  nodeType: "rp.sameboy.gb",
  platforms: [{ id: "gb", label: "Game Boy", extensions: [".gb", ".gbc"] }],
  detect: {
    prefixBytes: GB_SNIFF_LEN,
    // Rank 2 (docs/modular-graph-phase-f4a-design.md decision 10): a crafted file carrying more than one console's
    // signature classifies as the NES (0), then the GBA (1), then this, whatever order the product lists the packages
    // in.
    rank: 2,
    run: (bytes) => (matchesAt(bytes, GB_LOGO_OFFSET, GB_LOGO) ? "gb" : null),
  },
  hasBattery: (header, platform) =>
    platform === "gb" ? header.length > 0x147 && GB_BATTERY_CART_TYPES.has(header[0x147]) : true,
  settingFields: {
    model: label(MODEL_VALUES),
    fastBoot: flag,
    highpass: label(HIGHPASS_VALUES),
    colorCorrection: label(COLOR_CORRECTION_VALUES),
    dmgPalette: label(DMG_PALETTE_VALUES),
    lightTemperature: within(-1, 1), // SameBoy's own range
  },
  constructFields: {},
  configParams: [],
  streams: () => ["ch1", "ch2", "ch3", "ch4"].map((port) => ({ port, stereo: true })),
  capabilities: { linkCable: { field: "linkGroupId" }, serialOut: true },
  splits: { channels: { names: ["pulse1", "pulse2", "wave", "noise"] } },
  render: { mixLanes: "stereo", defaultPlay: { button: 7 } }, // GameboyButton::Start
};

// The manifest's retroplug.consoles member names this module (docs/modular-graph-phase-f4a-design.md section 3), which
// the generator concatenates into src/generated/consoles.ts: the shell classifies a ROM and places a system through
// that list, so nothing here reaches an app module or zod.
const SAMEBOY_CONSOLES: ConsolesContribution = { consoles: [GAME_BOY] };
export default SAMEBOY_CONSOLES;
