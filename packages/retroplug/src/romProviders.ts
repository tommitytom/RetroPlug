// The built-in ROM providers: the TS twin of the C++ RomSniffer's default-role step
// (packages/native/src/system/sameboy/RomSniffer.cpp). Given a freshly-classified ROM,
// they attach the FEATURE roles that give it behaviour — mGB gets the `mgb` MIDI-to-serial
// passthrough, LSDj gets `lsdj-sync`. Registered into the CONTROL-PLANE registry only
// (buildAppRegistry in appHost.ts); the bare DSP-context bundle never sees them. Feature
// roles are matched by ROM identity (cartridge title at 0x134, or the embedded marker for a
// baked-in synth whose bytes never reach TS) — the same "meaning lives in TS" split the
// generic RoleRegistry is built around.

import type { RoleRegistry, RomContext } from "./systemRoles";
import { LsdjSyncMode } from "./settingsEnums";
import { isRisaRomHeader } from "./risa";

// The Game Boy cartridge title field is 0x134..0x143. Decode it to an uppercase ASCII
// string, stopping at the first NUL — case-insensitive so both "LSDj-v9.4.2" and older
// "LSDJ" uppercase titles match the same prefix.
function title(header: Uint8Array): string {
  let s = "";
  for (let i = 0x134; i < 0x144 && i < header.length; i++) {
    const c = header[i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.toUpperCase();
}

/** Register the built-in feature-role providers (mGB, LSDj) into `registry`. */
export function registerRomProviders(registry: RoleRegistry): void {
  // mGB: the embedded synth (marker "mgb") or a file-backed mGB cart → MIDI-to-serial passthrough.
  registry.registerRomProvider((rom: RomContext) =>
    rom.embeddedRom === "mgb" || title(rom.header).startsWith("MGB") ? [{ kind: "mgb", config: {} }] : [],
  );

  // LSDj (stock or arduinoboy build) → the lsdj-sync role (defaulting to MidiSync) plus the lsdj-assets
  // role that carries any non-destructive kit/palette/font overrides (empty until the user replaces one).
  registry.registerRomProvider((rom: RomContext) =>
    title(rom.header).startsWith("LSDJ")
      ? [{ kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiSync } }, { kind: "lsdj-assets", config: { overrides: [] } }]
      : [],
  );

  // Any NES ROM → host-MIDI passthrough to the core (the always-attached N8 FIFO). Match the PLATFORM
  // (core is "mesen"); always-on, mirroring the native role — benign for a ROM that ignores $40F0.
  registry.registerRomProvider((rom: RomContext) =>
    rom.platform === "nes" ? [{ kind: "nes-n8-midi", config: {} }] : [],
  );

  // risa (the LSDj-style NES/MMC5 tracker) → the `risa` marker role that gates the Songs menu. Detected
  // by its iNES 2.0 header fingerprint (MMC5 + 64 KB battery) since NES ROMs carry no title field.
  registry.registerRomProvider((rom: RomContext) =>
    rom.platform === "nes" && isRisaRomHeader(rom.header) ? [{ kind: "risa", config: {} }] : [],
  );
}
