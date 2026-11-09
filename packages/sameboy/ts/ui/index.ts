// SameBoy's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.3): the core-role knobs the
// System submenu offers for a Game Boy, as declarative rows. A row says which role config key it edits, its choices (or
// Off/On, or a range's steps) and its gate; the shell renders it as a cycler and edits it with
// systems.setRoleConfig, so a label table or a default lives here, beside the schema whose values it spells.
//
// The display group keeps the split the core makes: colour correction and light temperature apply only to a CGB model
// and the DMG palette only to a DMG one (see app.ts). `auto` counts as CGB, because RetroPlug resolves it to CGB-C, so
// it gets the CGB rows. The separator before the group is `separatorBefore` on the first row of either side, so the
// fence shows whichever of the two groups is up.
import type { SystemInfo } from "@retroplug/core/host";
import { hasRole, roleConfig } from "@retroplug/core/systemRoles";
import { defineUi, type MenuBuilder, type RoleRow, type UiHost } from "@retroplug/ui/contribution";
import { cycler, separator } from "@retroplug/ui/menu";
import {
  COLOR_CORRECTION_VALUES,
  DMG_PALETTE_VALUES,
  HIGHPASS_VALUES,
  MODEL_VALUES,
  type SameBoyModel,
} from "../enums";

/** The model names in MODEL_VALUES' order: SameBoy's enum as the menu spells it, `auto` first. */
const MODEL_NAMES = [
  "Auto",
  "DMG-B",
  "MGB",
  "SGB",
  "SGB PAL",
  "SGB2",
  "CGB-0",
  "CGB-A",
  "CGB-B",
  "CGB-C",
  "CGB-D",
  "CGB-E",
  "AGB",
  "GBP",
];
const HIGHPASS_NAMES = ["Off", "Accurate", "DC-Block"];
const COLOR_CORRECTION_NAMES = [
  "Off",
  "Correct Curves",
  "Balanced",
  "Boost Contrast",
  "Reduce Contrast",
  "Low Contrast",
  "Accurate",
];
const DMG_PALETTE_NAMES = ["Grey", "DMG", "MGB", "GBL"];
/** Light temperature is a continuous -1..1 in the core; the menu has cyclers, not sliders, so it offers 11 steps over
 *  the full range (upstream's own slider is 21, a lot of key presses for a tint). Stored as the double, so a value from
 *  elsewhere still round-trips to the closest row. */
const LIGHT_TEMP_STEPS = [-1, -0.8, -0.6, -0.4, -0.2, 0, 0.2, 0.4, 0.6, 0.8, 1];
const LIGHT_TEMP_NAMES = [
  "Cool 100%",
  "Cool 80%",
  "Cool 60%",
  "Cool 40%",
  "Cool 20%",
  "Neutral",
  "Warm 20%",
  "Warm 40%",
  "Warm 60%",
  "Warm 80%",
  "Warm 100%",
];

/** The models that render in DMG mode, i.e. the ones the DMG palette applies to. Mirrors the core's own split:
 *  GB_is_cgb is `model >= GB_MODEL_CGB_0`, and everything below it takes its colours from GB_update_dmg_palette instead
 *  of the CGB palette RAM. `auto` is NOT one of them: RetroPlug resolves it to CGB-C, so it gets the CGB rows. */
const DMG_MODELS: readonly SameBoyModel[] = ["dmgB", "mgb", "sgb", "sgbPal", "sgb2"];

const choices = <T extends string>(values: readonly T[], labels: readonly string[]) =>
  values.map((value, i) => ({ value, label: labels[i] }));

export const SAMEBOY_ROWS: readonly RoleRow[] = [
  { id: "sys-model", label: "Model", role: "sameboy", key: "model", choices: choices(MODEL_VALUES, MODEL_NAMES) },
  {
    id: "sys-highpass",
    label: "Highpass",
    role: "sameboy",
    key: "highpass",
    choices: choices(HIGHPASS_VALUES, HIGHPASS_NAMES),
  },
  { id: "sys-fastboot", label: "Fast Boot", role: "sameboy", key: "fastBoot", toggle: true },
  {
    id: "sys-dmg-palette",
    label: "DMG Palette",
    role: "sameboy",
    key: "dmgPalette",
    choices: choices(DMG_PALETTE_VALUES, DMG_PALETTE_NAMES),
    when: { key: "model", in: DMG_MODELS },
    separatorBefore: "sys-sep-display",
  },
  {
    id: "sys-color-correction",
    label: "Color Correction",
    role: "sameboy",
    key: "colorCorrection",
    choices: choices(COLOR_CORRECTION_VALUES, COLOR_CORRECTION_NAMES),
    when: { key: "model", notIn: DMG_MODELS },
    separatorBefore: "sys-sep-display",
  },
  {
    id: "sys-light-temp",
    label: "Light Temp",
    role: "sameboy",
    key: "lightTemperature",
    range: { steps: LIGHT_TEMP_STEPS, labels: LIGHT_TEMP_NAMES },
    when: { key: "model", notIn: DMG_MODELS },
  },
];

// Link Group cycles 0..4 (0 = Off), mirroring the core's LINK_GROUP_MAX.
const LINK_GROUP_NAMES = ["Off", "1", "2", "3", "4"];

/** The Game Boy link cable's group: which of the project's systems share a serial link. A peer-only row, so it lives in
 *  the instance footer (after Remove Instance, the whole-instance rows) rather than in the System submenu, where it
 *  would be a dead always-"Off" row for a lone instance. Gate it on the console too: a NES or GBA peer has no link
 *  cable. A stored value that is not a number (a hand-edited project) reads as Off, which is what the core does with it
 *  too. */
const linkGroup: MenuBuilder = {
  placement: "instance.footer",
  build(host: UiHost, sys: SystemInfo | null, ctx) {
    if (!sys || ctx.systems.length < 2 || !hasRole(sys.roles, "sameboy")) return [];
    const stored = roleConfig(sys.roles, "sameboy")?.linkGroupId;
    return [
      separator("inst-sep-link"),
      cycler(
        "inst-link",
        "Link Group",
        LINK_GROUP_NAMES,
        typeof stored === "number" ? stored : 0,
        (n) => host.systems.setRoleConfig(sys.id, "sameboy", { linkGroupId: n }),
      ),
    ];
  },
};

export default defineUi({ rows: SAMEBOY_ROWS, menus: [linkGroup] });
