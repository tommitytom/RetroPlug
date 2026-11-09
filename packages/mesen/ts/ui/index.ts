// Mesen's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.3): the core-role knobs the
// System submenu offers for a Mesen console, as declarative rows. Mesen's one role holds every platform's settings, so
// a row's gate is the PLATFORM (`platforms`), not the core: region, the sprite limit, the APU flush window, the
// expansion chip's volume and driving a real cart are NES knobs, FM audio is the SMS/GG one, and a GBA has none yet.
//
// Two numeric knobs are RANGES with a handful of steps: the role schema accepts any value inside its bound, so a
// project written elsewhere still shows the nearest step rather than a blank. `driveRealCart` is the descriptor's own
// opt-out field (`realCartOptOut`), read by buildGraph to decide which system drives the physical cart; the NES
// descriptor names that field as this row's key, and mesen's ts/test/ui.test.ts pins the two spellings together.
import { defineUi, type RoleRow } from "@retroplug/ui/contribution";
import { REGION_VALUES } from "../enums";

/** Console region (ConsoleRegion 0..4), the "mesen" role's region knob. */
const REGION_NAMES = ["Auto", "NTSC", "PAL", "Dendy", "NTSC-J"];
/** APU flush-window latency presets (ms). The role accepts any value in [0.25, 6.0]; 1.4 ms is the historical window,
 *  so it is the default the row starts on. */
const APU_LATENCY_MS = [0.5, 1.0, 1.4, 3.0, 5.0];
const APU_LATENCY_NAMES = ["0.5 ms", "1.0 ms", "1.4 ms", "3.0 ms", "5.0 ms"];
/** The expansion chip's level as a percentage of the hardware's mix (never the 2A03's): a cart without an expansion
 *  chip hears nothing from it. A connected Everdrive N8 Pro gets the same value as its node's param. */
const EXP_VOLUMES = [0, 25, 50, 75, 100, 125, 150, 200];
const EXP_VOLUME_NAMES = EXP_VOLUMES.map((v) => (v === 0 ? "Off" : v === 100 ? "100% (unity)" : `${v}%`));

export const MESEN_ROWS: readonly RoleRow[] = [
  {
    id: "sys-nes-region",
    label: "Region",
    role: "mesen",
    key: "region",
    platforms: ["nes"],
    choices: REGION_VALUES.map((value, i) => ({ value, label: REGION_NAMES[i] })),
  },
  {
    id: "sys-nes-spritelimit",
    label: "Remove Sprite Limit",
    role: "mesen",
    key: "removeSpriteLimit",
    platforms: ["nes"],
    toggle: true,
  },
  {
    id: "sys-nes-apu-latency",
    label: "APU Latency",
    role: "mesen",
    key: "apuLatencyMs",
    platforms: ["nes"],
    range: { steps: APU_LATENCY_MS, labels: APU_LATENCY_NAMES },
  },
  {
    id: "sys-nes-exp-volume",
    label: "Expansion Volume",
    role: "mesen",
    key: "expansionVolume",
    platforms: ["nes"],
    range: { steps: EXP_VOLUMES, labels: EXP_VOLUME_NAMES },
  },
  {
    id: "sys-nes-drive-cart",
    label: "Drive Real Cart",
    role: "mesen",
    key: "driveRealCart",
    platforms: ["nes"],
    toggle: true,
  },
  {
    id: "sys-sms-fm",
    label: "FM Audio",
    role: "mesen",
    key: "enableFm",
    platforms: ["sms", "gg"],
    toggle: true,
  },
];

export default defineUi({ rows: MESEN_ROWS });
