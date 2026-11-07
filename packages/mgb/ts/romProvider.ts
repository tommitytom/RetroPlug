// mGB's ROM provider: the embedded synth (marker "mgb") or a file-backed mGB cart gets the `mgb` MIDI-to-serial
// passthrough role. Matched by ROM identity, as every provider is: the embedded ROM's bytes never reach TS, so its
// marker alone says which cart it is.
import type { RomProvider } from "@retroplug/core/systemRoles";
import { cartridgeTitle } from "@retroplug/sameboy/cartridge";

export const MGB_ROM_PROVIDER: RomProvider = (rom) =>
  rom.embeddedRom === "mgb" || cartridgeTitle(rom.header).startsWith("MGB") ? [{ kind: "mgb", config: {} }] : [];
