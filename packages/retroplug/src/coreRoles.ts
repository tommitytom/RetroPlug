// The built-in core-config "system" roles — a core's tunable emulator settings, carried as roles
// rather than baked into the generic system config (so core-specific knobs like SameBoy's `highpass`
// never sit in the generic config). Keyed by the `core` value (platform.ts), so the role's kind IS its
// core. These are built-in, but ride the same generic registry a third-party extension would use. Each
// role's config is a zod schema (roleSchema.ts): shape + defaults + clamping in one place. Ranges
// mirror the native enums (SameBoyConfig.hpp: model 0..13, highpass 0..2, linkGroupId 0..255).

import type { RoleRegistry } from "./systemRoles";
import { z, clampedInt, boolField } from "./configSchema";

/** Register the built-in core-config system roles into `registry`. */
export function registerCoreRoles(registry: RoleRegistry): void {
  // SameBoy: model / highpass / link group / fast boot.
  registry.registerRole({
    kind: "sameboy",
    category: "system",
    schema: z.object({
      model: clampedInt(0, 13, 9), // SameBoyModel 0..13 (CgbC=9)
      highpass: clampedInt(0, 2, 1), // Off / Accurate / RemoveDcOffset
      linkGroupId: clampedInt(0, 255, 0),
      fastBoot: boolField(true),
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
      region: clampedInt(0, 4, 0), // ConsoleRegion: Auto / NTSC / PAL / Dendy / NTSC-J
      removeSpriteLimit: boolField(false),
      // CLI-only per-channel export mode (spec/10 §5/§5b): 0 = Mix, 1 = StereoModPins (Pulse | TND +
      // Expansion), 3 = IndividualMono (5 core channels). Set at construct (via adopt) — the settings menu
      // doesn't surface it. Additive. (2 = pins + a mix reference, native/test-only.)
      channelExportMode: clampedInt(0, 3, 0),
    }),
  });
}
