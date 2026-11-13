// genmddj's ROM provider: the cart gets the `genmddj-sync` role that clocks it from the DAW transport. Matched on the
// ROM's identity, not the platform: the transport rides controller port 2's TR and TH, which every Mega Drive game
// reads as player 2.
import type { RomProvider } from "@retroplug/core/systemRoles";
import { isGenmddjRom } from "./romDetect";

export const GENMDDJ_ROM_PROVIDER: RomProvider = (rom) => {
  if (rom.platform !== "md" || !isGenmddjRom(rom.header)) return [];
  return [{ kind: "genmddj-sync", config: {} }];
};
