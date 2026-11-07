// BlipToaster's ROM provider: what gives a BlipToaster cartridge its feature roles. Attached by ROM identity, not by
// platform, because the cart is an NES ROM with no distinguishing iNES header - the "bliptoaster" ASCII marker baked
// into its head is the only thing that says what it is (romDetect.ts). It has no song battery, so there is no song
// marker either; the tracker integration is asset-only.
import type { RoleInstance, RomProvider } from "@retroplug/core/systemRoles";
import { isBlipToasterRomHeader, blipToasterChip } from "./romDetect";

// BlipToaster (the NES MIDI synth) -> the `bliptoaster` marker role that gates its asset menu, plus the
// `bliptoaster-assets` role holding non-destructive DMC-kit/CHR-font ROM overrides.
export const BLIPTOASTER_ROM_PROVIDER: RomProvider = (rom): RoleInstance[] =>
  rom.platform === "nes" && isBlipToasterRomHeader(rom.header)
    ? [
        { kind: "bliptoaster", config: { chip: blipToasterChip(rom.header) } },
        { kind: "bliptoaster-assets", config: { overrides: [] } },
      ]
    : [];
