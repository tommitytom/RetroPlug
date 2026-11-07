// The `lsdj-sync` role's config as its two nodes take it: rp.lsdj.sync, placed before its Game Boy (the host-to-Game
// Boy direction, lsdjSync.ts), and rp.lsdj.serial-out, placed after it (the Game Boy-to-host one, lsdjSerialOut.ts).
// buildGraph hands both the role's config as their settings, the MI.MAP controller's forced mode included, so one
// schema checks both and the two cannot read a config differently.
import { s, type Infer } from "@retroplug/core/settingsSchema";
import { LSDJ_MODE_VALUES } from "../../settingsEnums";

// mode: LsdjSyncMode. tempoDivisor subdivides the 24-PPQN clock (24/divisor) for MidiSync, MidiSyncArduinoboy and
// MidiMap; the menu offers 1/2/4/8. autoStart taps START on the host transport's rise to auto-arm a SYNC=MIDI cart
// (MidiSync, MidiSyncArduinoboy), which a headless DAW render needs; off by default, so a cart arms by hand.
export const lsdjSyncSettings = s.object({
  mode: s.enum(LSDJ_MODE_VALUES, "midiSync"),
  tempoDivisor: s.int({ min: 1, max: 8, default: 1 }),
  autoStart: s.bool(false),
});

export type LsdjSyncSettings = Infer<typeof lsdjSyncSettings>;
