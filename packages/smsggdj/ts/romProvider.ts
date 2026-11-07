// smsggdj's ROM provider: the cart gets the `sms-sync` role that clocks it from the DAW transport. Matched on the
// ROM's build MARKER, not the platform: on SMS the transport rides Player 2's button lines, so attaching this to every
// SMS cart would inject phantom presses into any game that reads the port.
//
// `machine` carries the wire format, because the two builds read different pins - SMS controller port 2 ($DD)
// versus the Game Gear EXT parallel port ($01). Both work: the GG path needs Mesen to honour the $02 direction
// mask on an $01 read rather than looping the port straight back, which is the vendored SmsMemoryManager edit.
import type { RomProvider } from "@retroplug/core/systemRoles";
import { isSmsggdjRom } from "./smsSync";

export const SMSGGDJ_ROM_PROVIDER: RomProvider = (rom) => {
  if ((rom.platform !== "sms" && rom.platform !== "gg") || !isSmsggdjRom(rom.header)) return [];
  return [{ kind: "sms-sync", config: { machine: rom.platform } }];
};
