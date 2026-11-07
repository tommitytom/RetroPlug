// LSDj's ROM provider: a stock or Arduinoboy build gets the lsdj-sync role (defaulting to MidiSync) plus the
// lsdj-assets role that carries any non-destructive kit/palette/font overrides (empty until the user replaces one).
// Matched on the cartridge title, case-insensitively, so both "LSDj-v9.4.2" and older "LSDJ" uppercase titles match
// the same prefix.
import type { RomProvider } from "@retroplug/core/systemRoles";
import { cartridgeTitle } from "@retroplug/sameboy/cartridge";
import { LsdjSyncMode } from "./enums";

export const LSDJ_ROM_PROVIDER: RomProvider = (rom) =>
  cartridgeTitle(rom.header).startsWith("LSDJ")
    ? [
        { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiSync } },
        { kind: "lsdj-assets", config: { overrides: [] } },
      ]
    : [];
