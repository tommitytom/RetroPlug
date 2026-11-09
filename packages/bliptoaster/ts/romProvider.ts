// BlipToaster's ROM provider: what gives a BlipToaster cartridge its feature roles. Attached by ROM identity, not by
// platform, because the cart is an NES ROM with no distinguishing iNES header - the "bliptoaster" ASCII marker baked
// into its head is the only thing that says what it is (romDetect.ts). It has no song battery, so there is no song
// marker either; the tracker integration is asset-only.
import type { RoleInstance, RomProvider } from "@retroplug/core/systemRoles";
import { isBlipToasterRomHeader } from "./romDetect";

// BlipToaster (the NES MIDI synth) -> the `bliptoaster` marker role that gates its asset menu, plus the
// `bliptoaster-assets` role holding non-destructive DMC-kit/CHR-font ROM overrides. The MARKER is all the provider
// answers: which build the ROM is (the role's `chip`) is a fact the role derives from the file itself, so the provider
// needs only the short header it is shown, and the chip has one writer.
export const BLIPTOASTER_ROM_PROVIDER: RomProvider = (rom): RoleInstance[] =>
  rom.platform === "nes" && isBlipToasterRomHeader(rom.header)
    ? [
        { kind: "bliptoaster", config: {} },
        { kind: "bliptoaster-assets", config: { overrides: [] } },
      ]
    : [];
