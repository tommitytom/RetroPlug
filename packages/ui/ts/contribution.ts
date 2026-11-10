// What a package's UI module declares (docs/modular-graph-phase-f4b-design.md section 4.2): the system-settings rows it
// adds as DATA (a role-config knob, its choices and its gate), the menu fragments a function builds where data cannot
// say it (a state machine over a native host, a peer count), its tracker specs, and the services its device needs
// polled or subscribed to. A manifest's `retroplug.ui` member names the module, which default-exports
// defineUi({ ... }); the generator concatenates them in product order into packages/retroplug/ui/generated/menus.ts.
//
// Everything here is plain TypeScript: the contract a package's module is checked against, and the helpers that read a
// row's stored value the way the shell renders it. The React kit is F4c's, in this package beside the menu model.
import type { Host, SystemInfo } from "@retroplug/core/host";
import type { AssetCatalog, AssetTypeInfo } from "@retroplug/tracker/assetCatalog";
import type { SongCatalog } from "@retroplug/tracker/songCatalog";
import { cycler, type MenuItem } from "./menu";

/** The groups a UI surface's host has: what a package's UI code may reach. The shell builds this host once per UI
 *  (ui/host/uiHost.ts) and a contribution is handed it; core declares the groups, so this is a selection, not a new
 *  contract. */
export type UiHost = Host<
  "files" | "png" | "systems" | "songs" | "liveSongs" | "dialogs" | "devices" | "views" | "controller" | "config"
>;

/** One choice of a config row: the value the role schema stores and the label the cycler shows. */
export interface ConfigChoice {
  readonly value: string | number;
  readonly label: string;
}

/** A row over one config key: a fixed choice list, an Off/On toggle, or a numeric range offered in steps. */
export type ConfigRow = { readonly key: string; readonly label: string } & (
  | { readonly choices: readonly ConfigChoice[] } // exact match; a value the list does not have reads as index 0
  | { readonly toggle: true } // Off/On, a boolean key
  | { readonly range: { readonly steps: readonly number[]; readonly labels: readonly string[] } } // nearest step
);

/** A row over a system's role config: what the shell renders as a cycler and edits with systems.setRoleConfig. Its id
 *  is the row's menu id, byte-identical to the hand-built row it replaces. */
export type RoleRow = ConfigRow & {
  /** The row id, byte-identical to today's ("sys-model"). */
  readonly id: string;
  /** The role kind whose config holds `key` ("sameboy", "mesen", "lsdj-sync"). */
  readonly role: string;
  /** Shown only on these platforms (the NES block): absent means every platform the role attaches to. */
  readonly platforms?: readonly string[];
  /** Shown only when the sibling key `key` is one of `in` / none of `notIn` (the DMG/CGB display split). */
  readonly when?: { readonly key: string; readonly in?: readonly string[]; readonly notIn?: readonly string[] };
  /** A separator id emitted before the first visible row carrying it, so a fenced group keeps its fence whichever of
   *  its rows shows first. */
  readonly separatorBefore?: string;
};

/** Where a builder's rows land. Rows (RoleRow) take "system.settings" alone; a builder takes whichever placement it
 *  fills. "instance.footer" is the block after Remove Instance and before Exit (the Link Group). The list is data so
 *  the generated registry reads the same one the type is built from: a placement added here reaches its check without
 *  a second copy to update. */
export const PLACEMENTS = [
  "instance.tracker",
  "instance.devices",
  "instance.footer",
  "system.settings",
  "start",
  "settings.midi",
] as const;
export type Placement = (typeof PLACEMENTS)[number];

/** What a builder reads that is not the host: the project's systems, the shell's per-path base-ROM cache (a catalog
 *  parses the ROM once per path rather than per menu build), and the ROM globs a file dialog offers (every console the
 *  product registers, which only the shell's console registry can name). */
export interface MenuBuildContext {
  readonly systems: readonly SystemInfo[];
  assetRom(romPath: string): Uint8Array | null;
  readonly romPatterns: readonly string[];
}

/** A menu fragment a package builds itself, because its content is state the row data cannot hold. `[]` hides the
 *  entry (no device attached, no peers to link). */
export interface MenuBuilder {
  readonly placement: Placement;
  build(host: UiHost, sys: SystemInfo | null, ctx: MenuBuildContext): MenuItem[];
}

/** A tracker's Songs submenu: the shared menu's structure over a SongCatalog, and the file-dialog actions its cart
 *  owns (only a cart knows its song formats). Callbacks take the host because every file op goes through it. */
export interface SongMenuSpec {
  /** Row-id prefix, e.g. "lsdj" / "risa". */
  readonly id: string;
  readonly catalog: SongCatalog;
  exportSong(host: UiHost, sys: SystemInfo, index: number, name: string): void;
  replaceSong(host: UiHost, sys: SystemInfo, index: number): void;
  addSong(host: UiHost, sys: SystemInfo): void;
  /** The synthetic working-song row's actions (only for a console whose catalog reports an unsaved working song). */
  exportWorking?(host: UiHost, sys: SystemInfo, name: string): void;
  saveWorkingToCatalog?(host: UiHost, sys: SystemInfo): void;
  /** Commit the live working song into the catalog: the pure byte-op behind "Save Working Song & Load". Null
   *  when declined, so the menu greys the row rather than offering a save that fails. */
  commitWorking?(sav: Uint8Array, name?: string): Uint8Array | null;
  /** True when the working song has no name to inherit and the user must supply one. */
  workingNeedsName?(sav: Uint8Array): boolean;
  /** Whether there is anywhere to put it: a cheap test, asked once per menu build. Omitted = assume yes. */
  canCommitWorking?(sav: Uint8Array): boolean;
}

/** A tracker's asset submenus: the shared structure over an AssetCatalog, and the per-cart file actions. */
export interface AssetMenuSpec {
  /** Row-id and submenu-id prefix, e.g. "lsdj" / "risa". */
  readonly id: string;
  readonly catalog: AssetCatalog;
  exportAsset(host: UiHost, sys: SystemInfo, type: AssetTypeInfo, slot: number, label: string): void;
  replaceAsset(host: UiHost, sys: SystemInfo, type: AssetTypeInfo, slot: number): void;
  /** A console where one slot of an asset type is the LIVE one returns it, so the list is also the picker; null for a
   *  type that has no such notion. Omitted by a console whose rows are the plain override list. The build context is
   *  passed because a live slot can be BAKED in the ROM (BlipToaster's theme and font are), and reading that file per
   *  row per build would copy the whole image once for every row: `ctx.assetRom` is the shell's per-path cache. */
  selectedSlot?(host: UiHost, sys: SystemInfo, kind: string, ctx: MenuBuildContext): number | null;
  selectSlot?(host: UiHost, sys: SystemInfo, kind: string, slot: number, ctx: MenuBuildContext): void;
}

/** A tracker's UI: its song and asset specs, its role-config rows and any fragment it builds. Keyed to its integration
 *  by id, so the shell finds this for the tracker a system resolved. */
export interface TrackerUi {
  /** The integration id (TRACKER_INTEGRATIONS' key, e.g. "lsdj"). */
  readonly id: string;
  /** The cart's song and asset specs: a cart contributes the ones it has (an asset-only cart has no Songs submenu). */
  readonly song?: SongMenuSpec;
  readonly asset?: AssetMenuSpec;
  readonly extras?: {
    readonly rows?: readonly RoleRow[];
    build?(host: UiHost, sys: SystemInfo, ctx: MenuBuildContext): MenuItem[];
  };
}

/** A UI service: a device's state the menu shows, kept current without a React hook. `start` runs at every MOUNT of
 *  the UI (so it must be idempotent: it is where a service puts its version-and-edge latches back for the mount that
 *  is starting, and a host that hides and shows its UI runs it again rather than once per process), `subscribe` asks
 *  for a repaint on a native-host change, and `poll` runs every `frames` render frames (true = repaint). A throw in
 *  either is reported and skips only that service. */
export interface UiService {
  readonly id: string;
  start?(host: UiHost): void;
  subscribe?(listener: () => void): () => void;
  poll?: { readonly frames: number; run(host: UiHost): boolean };
}

/** Everything a package's UI module contributes. */
export interface UiContribution {
  /** Role-config rows, which the shell renders in the System submenu for every system whose roles they name. */
  readonly rows?: readonly RoleRow[];
  readonly menus?: readonly MenuBuilder[];
  readonly trackers?: readonly TrackerUi[];
  readonly services?: readonly UiService[];
}

/** The identity function with types: a UI module's default export, checked against what a package may contribute. */
export function defineUi(contribution: UiContribution): UiContribution {
  return contribution;
}

/** How a row reads: the labels a cycler steps through, where the stored value points, and the value a step applies. */
export interface RowReading {
  readonly names: readonly string[];
  readonly index: number;
  /** The value to store when the cycler steps to `step`. */
  at(step: number): string | number | boolean;
}

/** The index of the preset in `steps` nearest `value`, so a cycler over a continuous value still shows the current
 *  setting when it is off-grid (a project written by an older build, or by hand). */
export function nearestIndex(steps: readonly number[], value: number): number {
  let best = 0;
  for (let i = 1; i < steps.length; i++) {
    if (Math.abs(steps[i] - value) < Math.abs(steps[best] - value)) best = i;
  }
  return best;
}

/** Whether a value of the wrong type reads as the schema's default: the shell's long-standing rule, so a project
 *  written with a string where a number belongs shows the default rather than a blank. */
function typed(value: unknown, fallback: unknown): unknown {
  return typeof value === typeof fallback ? value : fallback;
}

/** Read a config row's stored value the way the shell renders it: the three fallbacks are the default for a wrong
 *  type, index 0 for a choice the list does not have, and the nearest step for a range. `fallback` is the role
 *  schema's default for the row's key (`schema.parse({})[key]`). */
export function readConfigRow(row: ConfigRow, stored: unknown, fallback: unknown): RowReading {
  const value = typed(stored, fallback);
  if ("choices" in row) {
    const names = row.choices.map((choice) => choice.label);
    const found = row.choices.findIndex((choice) => choice.value === value);
    return { names, index: Math.max(0, found), at: (step) => row.choices[step].value };
  }
  if ("toggle" in row) {
    const on = value === true;
    return { names: ["Off", "On"], index: on ? 1 : 0, at: (step) => step === 1 };
  }
  const numeric = typeof value === "number" ? value : Number(value);
  return {
    names: row.range.labels,
    index: nearestIndex(row.range.steps, Number.isFinite(numeric) ? numeric : row.range.steps[0]),
    at: (step) => row.range.steps[step],
  };
}

/** One config row as the menu draws it: `readConfigRow`'s labels and step values, at `index` when the caller has one
 *  of its own (the controller-app rows keep the index they always showed: `legacyConfigIndex`). The STEP writes the
 *  value the row's table gives, so a role row and a controller-app row apply the same thing they display. */
export function configRowCycler(
  id: string,
  row: ConfigRow,
  stored: unknown,
  fallback: unknown,
  apply: (value: string | number | boolean) => void,
  index?: number,
): MenuItem {
  const reading = readConfigRow(row, stored, fallback);
  return cycler(id, row.label, [...reading.names], index ?? reading.index, (step) => apply(reading.at(step)));
}

/** Where a controller app's row points for a stored value, the way those rows have always read it: the rule predates
 *  the typed fallback `readConfigRow` gives a role row, and an app's knobs keep it so a hand-edited project's menu reads
 *  exactly as it did. A toggle is On unless it is `false`; a choice list with no match shows its first label. The app's
 *  RUNTIME still falls back to its schema's default (`parseAppConfig`), so an invalid stored value can read one way and
 *  run another: that latent display bug is owed, not fixed by a move. A `range` row (which these rows did not have) has
 *  no legacy reading, so the caller reads it the typed way. */
export function legacyConfigIndex(row: ConfigRow, value: unknown): number | undefined {
  if ("range" in row) return undefined;
  if ("toggle" in row) return value === false ? 0 : 1;
  return Math.max(0, row.choices.findIndex((choice) => choice.value === value));
}

/** Whether a row is shown on `platform` (no `platforms` means the role's own reach is the gate). */
export function rowShownOn(row: RoleRow, platform: string): boolean {
  return row.platforms === undefined || row.platforms.includes(platform);
}

/** Whether a row's `when` passes, given the sibling key's stored value and its schema default. A row with no `when`
 *  always passes. */
export function rowShownFor(row: RoleRow, stored: unknown, fallback: unknown): boolean {
  if (!row.when) return true;
  const value = String(typed(stored, fallback));
  if (row.when.in && !row.when.in.includes(value)) return false;
  if (row.when.notIn && row.when.notIn.includes(value)) return false;
  return true;
}
