// SameBoy's app contribution: the core-config "system" role whose kind IS the core (platform.ts's `Core` value), so
// the generic registry attaches it to every SameBoy system. A core's tunable emulator settings ride a role rather than
// the generic system config (so SameBoy's `highpass` never sits in config every core reads), and the role's config is
// one zod schema (core's configFields): shape, defaults and clamping in one place. Its fields' ranges mirror the
// native enums the core reads (SameBoyConfig.hpp: model 0..13, highpass 0..2).
//
// The display group is additive and every default reproduces what the core did when these were
// hardcoded (SameBoySystem.cpp), so an existing project loads pixel-identical and needs no migration.
// Which one bites is decided by the model, and the split is exact rather than a guess: the core
// applies colour correction and light temperature only when GB_is_cgb (model >= GB_MODEL_CGB_0) and
// the DMG palette only when it isn't. `auto` is not ambiguous here - RetroPlug maps it to CGB-C
// (toSameBoyModel), so it counts as CGB. The menu gates the palette row on that (the app's menuDefs.ts).
//
// The native side is this package's: `rp_sameboy_package` registers the Game Boy node from packages/sameboy/native,
// and graph/buildGraph.ts's systemKnobs turns these labels into the node's settings. linkGroupId is the graph's own,
// turned into the Game Boy's link wires by buildGraph.ts (linkGroupOf), never a core setting.

import type { RoleType } from "@retroplug/core/systemRoles";
import { definePackage } from "@retroplug/core/definePackage";
import { z } from "zod";
import { clampedInt, clampedNumber, boolField, enumField } from "@retroplug/core/configFields";
import { MODEL_VALUES, HIGHPASS_VALUES, COLOR_CORRECTION_VALUES, DMG_PALETTE_VALUES } from "./enums";

// model, highpass, colorCorrection and dmgPalette are string enums; the system node's settings spell them as labels
// (knobs::Label), so the store hands them over as they are.
export const SAMEBOY_ROLE: RoleType = {
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
};

export default definePackage({ roles: [SAMEBOY_ROLE] });
