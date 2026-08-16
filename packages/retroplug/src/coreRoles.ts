// The built-in core-config "system" roles — a core's tunable emulator settings, carried as roles
// rather than baked into the generic system config (so core-specific knobs like SameBoy's `highpass`
// never sit in the generic config). Keyed by the `core` value (platform.ts), so the role's kind IS its
// core. These are built-in, but ride the same generic registry a third-party extension would use. Each
// role's config is a zod schema (roleSchema.ts): shape + defaults + clamping in one place. Ranges
// mirror the native enums (SameBoyConfig.hpp: model 0..13, highpass 0..2, linkGroupId 0..255).

import type { RoleRegistry } from "./systemRoles";
import { z, clampedInt, clampedNumber, boolField, enumField } from "./configSchema";
import {
  MODEL_VALUES,
  HIGHPASS_VALUES,
  REGION_VALUES,
  CHANNEL_EXPORT_VALUES,
  CARTRIDGE_ACCURACY_VALUES,
  COLOR_CORRECTION_VALUES,
  DMG_PALETTE_VALUES,
} from "./settingsEnums";

/** Register the built-in core-config system roles into `registry`. */
export function registerCoreRoles(registry: RoleRegistry): void {
  // SameBoy: model / highpass / link group / fast boot, plus the display knobs below. model, highpass,
  // colorCorrection and dmgPalette are string enums; the native reflect-cpp SameBoyRoleConfig takes
  // their integer ordinals (converted at the boundary, settingsEnums).
  //
  // The display group is additive and every default reproduces what the core did when these were
  // hardcoded (SameBoySystem.cpp), so an existing project loads pixel-identical and needs no migration.
  // Which one bites is decided by the model, and the split is exact rather than a guess: the core
  // applies colour correction and light temperature only when GB_is_cgb (model >= GB_MODEL_CGB_0) and
  // the DMG palette only when it isn't. `auto` is not ambiguous here - RetroPlug maps it to CGB-C
  // (toSameBoyModel), so it counts as CGB. The menu gates the palette row on that (menuDefs.ts).
  registry.registerRole({
    kind: "sameboy",
    category: "system",
    schema: z.object({
      model: enumField(MODEL_VALUES, "cgbC"),
      highpass: enumField(HIGHPASS_VALUES, "accurate"),
      linkGroupId: clampedInt(0, 255, 0),
      fastBoot: boolField(true),
      // --- display ---
      colorCorrection: enumField(COLOR_CORRECTION_VALUES, "disabled"),
      dmgPalette: enumField(DMG_PALETTE_VALUES, "grey"),
      // Ambient light tint, CGB only. SameBoy's own range: -1 (cool/blue) .. +1 (warm/red), 0 neutral.
      lightTemperature: clampedNumber(-1, 1, 0),
    }),
  });

  // Mesen: keyed by core ("mesen"), so this ONE role attaches to any Mesen system (NES, GBA, SMS/GG).
  // The knobs are per-platform and the settings menu gates each group on `platform` (menuDefs.ts): the
  // first three are NES-only, enableFm is SMS/GG-only, and GBA has none of its own yet - it carries
  // them all as inert bytes until it does, at which point the schema grows again.
  //
  // Native decodes this one blob into a per-platform struct (MesenNesRoleConfig / MesenSmsRoleConfig),
  // each reflect-cpp DefaultIfMissing-tolerant, so a field that means nothing on a platform is simply
  // never read there.
  registry.registerRole({
    kind: "mesen",
    category: "system",
    schema: z.object({
      region: enumField(REGION_VALUES, "auto"), // ConsoleRegion: auto / ntsc / pal / dendy / ntscJapan
      removeSpriteLimit: boolField(false),
      // SMS/GG: route the YM2413 (FM). Applied at construct (configureSms runs before LoadRom). Not a
      // cosmetic toggle - Mesen models the $F2 audio-control port as a MUX whose PSG branch memsets the
      // buffer, so a tracker that writes $F2 = $01 at boot goes silent on the PSG with this on.
      enableFm: boolField(true),
      // APU flush window as a latency in ms (the worst-case NES audio latency the resampler batching adds).
      // Live knob; native converts ms→CPU cycles per region clock. ~1.4ms ≈ the historical 2500-cycle window.
      apuLatencyMs: clampedNumber(0.25, 6.0, 1.4),
      // Cartridge-accuracy switches. Both default to "n8", NOT to the documented chip behaviour: RetroPlug
      // is music software, and NES music is overwhelmingly played back through an Everdrive N8 Pro, so the
      // useful default is the one where what you hear here is what the cartridge will do. Choose "chip" for
      // playback accuracy on a stock cartridge.
      //
      // The N8's FPGA cores measurably differ (both verified on hardware, see cartridge-accuracy.test.ts):
      // its 5B has no noise generator, so enabling noise MUTES the channel rather than rasping (the mixer
      // ANDs tone with noise); and its MMC5 pulse does not restart the duty sequencer on a $5003 write, so
      // a phase-reset hack holds a 50% duty and full level and only shifts pitch.
      s5bNoise: enumField(CARTRIDGE_ACCURACY_VALUES, "n8"),
      mmc5PhaseReset: enumField(CARTRIDGE_ACCURACY_VALUES, "n8"),
      // CLI-only per-channel export mode (spec/10 §5/§5b): mix, stereoModPins (Pulse | TND + Expansion),
      // individualMono (5 core channels). Set at construct (via adopt) — the settings menu doesn't surface
      // it. Additive. (pinsPlusRef = pins + a mix reference, native/test-only.)
      channelExportMode: enumField(CHANNEL_EXPORT_VALUES, "mix"),
    }),
  });
}
