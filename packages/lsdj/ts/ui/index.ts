// LSDj's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.3): its sync rows, which live in
// the LSDj instance submenu rather than the System one, so they are a TrackerUi's `extras.rows` over the `lsdj-sync`
// role. The HD Player action and the separator after the sync block stay the shell's until the tracker entries'
// builders land; the rows are data, so a name table or a default lives here beside the role schema that names them.
import { defineUi, type TrackerUi } from "@retroplug/ui/contribution";
import { LSDJ_MODE_VALUES } from "../enums";

/** LSDj sync modes (LsdjSyncMode 0..8), as the menu spells them. All shown; "Keyboard"(4) is not yet driven. "MIDI
 *  Out"(7, SYNC=MI.OUT) and "Master Sync"(8, SYNC=LSDJ) are the two LSDj-to-host MIDI-out modes. */
const LSDJ_MODE_NAMES = [
  "Off",
  "MIDI Sync",
  "MIDI Sync (Arduinoboy)",
  "MIDI Map",
  "Keyboard",
  "Keyboard MIDI",
  "MIDI Passthrough",
  "MIDI Out",
  "Master Sync",
];
/** Tempo divisor subdivides the 24-PPQN clock (24/divisor) for MIDI Sync + MIDI Sync (Arduinoboy). */
const LSDJ_DIVISORS = [1, 2, 4, 8];

/** The tracker's UI: its sync rows today, its Songs and asset specs when those move here. */
export const LSDJ_TRACKER_UI: TrackerUi = {
  id: "lsdj",
  extras: {
    rows: [
      {
        id: "lsdj-mode",
        label: "Mode",
        role: "lsdj-sync",
        key: "mode",
        choices: LSDJ_MODE_VALUES.map((value, i) => ({ value, label: LSDJ_MODE_NAMES[i] })),
      },
      {
        id: "lsdj-divisor",
        label: "Tempo Divisor",
        role: "lsdj-sync",
        key: "tempoDivisor",
        choices: LSDJ_DIVISORS.map((value) => ({ value, label: String(value) })),
      },
      // Auto Start taps START on the host transport rise to auto-arm SYNC=MIDI carts (MidiSync / Arduinoboy): off by
      // default, so the modes keep their manual-arm behaviour.
      { id: "lsdj-autostart", label: "Auto Start", role: "lsdj-sync", key: "autoStart", toggle: true },
    ],
  },
};

export default defineUi({ trackers: [LSDJ_TRACKER_UI] });
