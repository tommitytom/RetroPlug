// BlipToaster's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.4): its asset spec and the
// settings rows it builds. It is ASSET-ONLY (no song battery), and its assets are the shared `.rit` / `.chr` / `.rkit`
// formats - risa's surface exactly, which is why the actions are the tracker package's.
//
// THEME and FONT have no row of their own: the cart's baked settings block names one of each as the live one, so their
// lists ARE the picker (a `*` on the live slot, `Select` on every other - see selectedSlot/selectSlot below). The DMC
// kit has none either: ch5 CC 14 picks it, which a song does before it plays anything.
//
// The cart has no settings memory - no battery, and nothing survives a RESET on purpose - so what you would set once
// for your rig is baked into the `.nes`: a 16-byte block in its code bank (@retroplug/bliptoaster/rom/settings).
// These rows edit it exactly the way the asset rows edit assets: the pick is persisted on the `bliptoaster-assets` role
// and folded into the ROM IN MEMORY at construct, and the file on disk is untouched. Every row also tells the running
// cart over SysEx, so it takes effect now; the pin is what a later LOAD replays and what a bake writes.
import type { Host, SystemInfo } from "@retroplug/core/host";
import { roleConfig } from "@retroplug/core/systemRoles";
import { action, cycler, separator, type MenuItem } from "@retroplug/ui/menu";
import {
  defineUi,
  type AssetMenuSpec,
  type MenuBuildContext,
  type TrackerUi,
} from "@retroplug/ui/contribution";
import { exportRitAsset, replaceRitAsset, type RitAssetOps } from "@retroplug/tracker/ritAssetOps";
import { bliptoasterAssetCatalog } from "../bliptoasterAssetCatalog";
import { readSettings as readPinnedSettings } from "../bliptoasterAssetsRole";
import {
  BlipToasterRom,
  blipToasterSettingsSysex,
  type BlipToasterSettings,
  type BlipToasterSettingsPatch,
} from "../rom";

/** The host groups this cart's rows reach: narrowed to what they use, so a third-party pack copied
 *  from this one takes exactly these and nothing more. */
type BlipToasterUiHost = Host<"files" | "dialogs" | "systems">;

/** The baked-settings fields this PROJECT pins (not the ROM's own bytes) - only what the user has changed. */
const pinnedSettings = (sys: SystemInfo): BlipToasterSettingsPatch =>
  readPinnedSettings(roleConfig(sys.roles, "bliptoaster-assets"));

/** The cart's baked block with the project's pins on top - what it is set to right now. Null when the ROM has no
 *  readable block (one predating it, or a format this build does not read). */
function effectiveSettings(romBytes: Uint8Array | null, sys: SystemInfo): BlipToasterSettings | null {
  if (!romBytes) return null;
  const rom = BlipToasterRom.fromBytes(romBytes);
  if (!rom.isBlipToaster || !rom.hasSettings) return null;
  return { ...rom.settings()!, ...pinnedSettings(sys) };
}

/** Pin `patch` on the role AND declare the resulting rig to the RUNNING cart. Both halves matter and neither is
 *  redundant: the pin is what a load replays and a bake writes, the SysEx is what the cart in front of you hears.
 *  `romBytes` is the base ROM the build context cached, so a step over the settings rows costs no extra read.
 *
 *  The message is staged at this system's node, so it reaches THIS cart whatever the project's MUSICAL routing is
 *  doing, and at its rp.n8.nes too when the document places one FOR THIS SYSTEM (the real console, in the block the
 *  emulated core hears it). Every NES drives a real cart by default, so in a project with more than one the placed cart
 *  node belongs to the FIRST in slot order: staging there for another system's cart would send this cart's rig to a
 *  console running a different ROM. */
function pinSettings(
  host: BlipToasterUiHost,
  sys: SystemInfo,
  romBytes: Uint8Array | null,
  patch: BlipToasterSettingsPatch,
): void {
  const effective = effectiveSettings(romBytes, sys);
  if (!effective) return;
  const next = { ...pinnedSettings(sys), ...patch };
  host.systems.setRoleConfig(sys.id, "bliptoaster-assets", { settings: next });
  const sysex = blipToasterSettingsSysex({ ...effective, ...patch });
  host.systems.stageNodeMidi(host.systems.nodeIdOf(sys.id), sysex);
  const cart = host.systems.drivenCart();
  if (cart !== null && cart.id === sys.id) host.systems.stageNodeMidi(cart.nodeId, sysex);
}

/** The cart's settings block names one THEME and one FONT as the live ones, so for those two the asset list is also the
 *  picker: the row that IS live wears a `*`, every other row offers `Select`. That replaces a pair of cyclers at the
 *  menu's root which named a slot by number, in a different place from the list of slots. */
const SELECTABLE: Record<string, "theme" | "font" | undefined> = { theme: "theme", font: "font" };

/** The facet BlipToaster supplies to the shared `.rit` actions: the role kind its override list lives on, and its
 *  ROM. */
const blipToasterOps: RitAssetOps = {
  role: "bliptoaster-assets",
  openRom: (host: Host<"files">, romPath: string) => {
    const bytes = romPath ? host.files.readFile(romPath) : null;
    if (!bytes) return null;
    const rom = BlipToasterRom.fromBytes(bytes);
    return rom.isBlipToaster ? rom : null;
  },
};

const blipToasterAssetSpec: AssetMenuSpec = {
  id: "bliptoaster",
  catalog: bliptoasterAssetCatalog,
  exportAsset: (host, sys, type, slot, label) => exportRitAsset(blipToasterOps, host, sys, type, slot, label),
  replaceAsset: (host, sys, type, slot) => replaceRitAsset(blipToasterOps, host, sys, type, slot),
  selectedSlot(_host, sys, kind, ctx) {
    const field = SELECTABLE[kind];
    if (!field) return null; // kits have no live slot: ch5 CC 14 picks one, and nothing is baked
    const settings = effectiveSettings(ctx.assetRom(sys.romPath), sys);
    return settings ? settings[field] : null;
  },
  selectSlot(host, sys, kind, slot, ctx) {
    const field = SELECTABLE[kind];
    if (field) pinSettings(host, sys, ctx.assetRom(sys.romPath), { [field]: slot });
  },
};

/** The cart's baked rig defaults, as the rows at the top of its instance submenu. Empty when the ROM has no readable
 *  settings block. */
function settingsRows(host: BlipToasterUiHost, sys: SystemInfo, ctx: MenuBuildContext): MenuItem[] {
  const romBytes = ctx.assetRom(sys.romPath);
  const effective = effectiveSettings(romBytes, sys);
  if (!effective) return [];
  const pinned = pinnedSettings(sys);
  const pin = (patch: BlipToasterSettingsPatch): void => pinSettings(host, sys, romBytes, patch);
  const baseChannelNames = Array.from({ length: 16 }, (_v, i) => String(i + 1).padStart(2, "0"));

  return [
    cycler("bliptoaster-set-basech", "Base MIDI Channel", baseChannelNames, effective.baseChannel, (n) =>
      pin({ baseChannel: n }),
    ),
    cycler("bliptoaster-set-ppu", "PPU Enabled", ["Off", "On"], effective.ppu ? 1 : 0, (n) => pin({ ppu: n === 1 })),
    // "not on the VRC7 build" is the ROM's own behaviour (it has no velocity curve), not something to hide here:
    // the byte is still baked and still honoured by every other build of the same project's ROM.
    cycler("bliptoaster-set-curve", "Velocity Curve", ["Linear", "Log"], effective.velCurve ? 1 : 0, (n) =>
      pin({ velCurve: n === 1 }),
    ),
    // Reset is the one row left. There is no "Apply" any more: every row above already reached the cart over SysEx as
    // it was stepped, so there was nothing left for a reboot to do that the row had not done. Reset still reboots,
    // because dropping the pins means going back to the ROM's OWN bytes, which are only read at boot. Offered only once
    // something IS pinned - with nothing pinned the rows show the ROM's bytes already.
    ...(Object.keys(pinned).length
      ? [
          separator("bliptoaster-set-apply-sep"),
          action("bliptoaster-set-reset", "Reset to ROM Defaults", () => {
            host.systems.setRoleConfig(sys.id, "bliptoaster-assets", { settings: {} });
            host.systems.reload(sys.id);
          }),
        ]
      : []),
  ];
}

/** The settings rows sit at the TOP of the BlipToaster menu, above the asset submenus and fenced off from them (they
 *  are what the cart BOOTS with, the asset menus are what it boots FROM). Inline rather than behind a "Settings"
 *  submenu of their own: there are only a handful, and one of the two things you come to this menu to do should not
 *  cost a level. */
function settingsBuild(host: BlipToasterUiHost, sys: SystemInfo, ctx: MenuBuildContext): MenuItem[] {
  const rows = settingsRows(host, sys, ctx);
  return rows.length === 0 ? [] : [...rows, separator("bliptoaster-settings-sep")];
}

/** The tracker's UI: its asset spec, and the settings rows it builds at the top of its submenu. */
export const BLIPTOASTER_TRACKER_UI: TrackerUi = {
  id: "bliptoaster",
  asset: blipToasterAssetSpec,
  extras: { build: settingsBuild },
};

export default defineUi({ trackers: [BLIPTOASTER_TRACKER_UI] });
