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
  // Which ones actually bite depends on the running model, and the core decides that, not us: colour
  // correction and light temperature are CGB-only, the palette is DMG-only, and `model: auto` isn't
  // known until the ROM is sniffed. So the menu offers all of them for any Game Boy and lets the
  // inapplicable one lie inert - that beats a gate that guesses wrong on `auto`.
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
      // Per-layer render kills. Debug hooks in SameBoy, but useful for isolating a tracker's visuals.
      backgroundEnabled: boolField(true),
      objectsEnabled: boolField(true),
    }),
  });

  // Mesen: keyed by core ("mesen"), so this ONE role attaches to any Mesen system (NES today, GBA
  // later). The knobs are NES-only for now — region (ConsoleRegion 0..4) + remove-sprite-limit — and
  // the settings menu gates them on platform === "nes" (menuDefs.ts). GBA carries them as inert bytes
  // until it gets its own knobs, at which point the schema grows.
  registry.registerRole({
    kind: "mesen",
    category: "system",
    schema: z.object({
      region: enumField(REGION_VALUES, "auto"), // ConsoleRegion: auto / ntsc / pal / dendy / ntscJapan
      removeSpriteLimit: boolField(false),
      // APU flush window as a latency in ms (the worst-case NES audio latency the resampler batching adds).
      // Live knob; native converts ms→CPU cycles per region clock. ~1.4ms ≈ the historical 2500-cycle window.
      apuLatencyMs: clampedNumber(0.25, 6.0, 1.4),
      // CLI-only per-channel export mode (spec/10 §5/§5b): mix, stereoModPins (Pulse | TND + Expansion),
      // individualMono (5 core channels). Set at construct (via adopt) — the settings menu doesn't surface
      // it. Additive. (pinsPlusRef = pins + a mix reference, native/test-only.)
      channelExportMode: enumField(CHANNEL_EXPORT_VALUES, "mix"),
    }),
  });
}
