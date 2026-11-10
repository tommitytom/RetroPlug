// What a package's UI module declares (docs/modular-graph-phase-f4b-design.md section 4.2): the system-settings rows it
// adds as DATA (a role-config knob, its choices and its gate), the menu fragments a function builds where data cannot
// say it (a state machine over a native host, a peer count), its tracker specs, the services its device needs polled or
// subscribed to, and its app actions. A manifest's `retroplug.ui` member names the module, which default-exports
// defineUi({ ... }); the generator concatenates them in product order into packages/retroplug/ui/generated/menus.ts.
//
// The views are a module of their own (docs/modular-graph-phase-f5a-design.md section 5): `retroplug.views` names it,
// it default-exports defineViews({ views }), and the generator writes packages/retroplug/ui/generated/views.ts from
// them. Apart because a component view imports React, which the pure tier (the menu tests, the control plane, the CLI,
// the planner) cannot resolve: the menu modules must import no view, or every test that builds a menu would need the
// UI bundle's aliases.
//
// Everything here is plain TypeScript: the contract a package's module is checked against, and the helpers that read a
// row's stored value the way the shell renders it. The React kit is @retroplug/ui-react, a package of its own, so a
// package that draws no React never meets it.
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

/** A value a package owns and the shell watches: a view's `visible`, a debug flag. `get` is synchronous and cheap
 *  (the shell reads it during a render), and `subscribe` asks for a repaint when it moves. */
export interface Readable<T> {
  get(): T;
  subscribe(fn: () => void): () => void;
}

/** The writable half, so a package's own module can flip what it exposed. `createSignal` (`@retroplug/ui/menu`) is the
 *  one implementation, so a flag's get/set/subscribe is written once. */
export interface Signal<T> extends Readable<T> {
  set(value: T): void;
}

/** One frame of a pixels view: the packed pixels and the size they are laid out in. The array is the view's own (the
 *  HD canvas hands the same one every frame) or a subarray of a shared wire buffer; the shell copies only the
 *  latter. */
export interface PixelFrame {
  readonly pixels: Uint32Array;
  readonly width: number;
  readonly height: number;
}

/** A pixels view's session, opened by the shell for one system. */
export interface PixelSession {
  /** The next frame, or null when nothing moved (the shell skips the copy and the invalidate). The shell asks for a
   *  frame only once it has a canvas to draw the result into, so a flush-and-diff session (the HD canvas reports just
   *  the tiles that changed since its last flush) cannot have a frame swallowed. */
  frame(): PixelFrame | null;
  close?(): void;
}

/** One frame of a readout view: the lines it shows and whether the cart is playing (the colour). */
export interface ReadoutFrame {
  readonly lines: readonly string[];
  readonly playing: boolean;
}

/** A readout view's session. */
export interface ReadoutSampler {
  /** The current readout: null = show nothing (an unsupported cart), undefined = unreadable this frame, keep what
   *  shows. */
  sample(): ReadoutFrame | null | undefined;
  /** Dedupe key for a frame; omitted = the lines and the flag themselves, which is exactly what a readout draws, so a
   *  sampler whose state moves without its lines changing narrows this and one that shows everything needs nothing.
   *  It can only coarsen what the default dedupes, and a signature that throws falls back to the default. */
  signature?(frame: ReadoutFrame): string;
  close?(): void;
}

/** What the shell hands a component view: its host, the system it is drawn for, and the space it has. */
export interface ViewComponentProps {
  readonly host: UiHost;
  readonly systemId: number;
  readonly width: number;
  readonly height: number;
}

/** A view's body, one kind each: the shell draws pixels and readout itself, and mounts a component as it is given
 *  (`reactView`, in @retroplug/ui-react). `component` is opaque here because this contract imports no React. */
export type ViewBody =
  | {
      readonly kind: "pixels";
      /** The text to show when `open` returns null (an unsupported cart); absent = draw nothing. */
      readonly unsupported?: string;
      open(host: UiHost, systemId: number): PixelSession | null;
    }
  | { readonly kind: "readout"; open(host: UiHost, systemId: number): ReadoutSampler | null }
  | { readonly kind: "component"; readonly component: unknown };

/** The component kind's body: what `reactView` builds, with the component still opaque here. */
export type ComponentBody = Extract<ViewBody, { kind: "component" }>;

interface ViewBase {
  /** Also the view's test id: "lsdj-hd", "lsdj-overlay", "risa-overlay". Unique across the product. */
  readonly id: string;
  readonly label: string;
  /** The systems the view applies to; absent = every system. */
  when?(sys: SystemInfo): boolean;
  readonly body: ViewBody;
}

/** A view that takes the whole window in place of the grid, opened by host.views.open(id, systemId). */
export interface WindowView extends ViewBase {
  readonly placement: "window";
}

/** A view that is a layer over every tile whose system `when` accepts. */
export interface TileView extends ViewBase {
  readonly placement: "tile";
  /** Shown only while true; absent = always. A hidden tile view opens no session and does no work. */
  readonly visible?: Readable<boolean>;
}

export type ViewDef = WindowView | TileView;

/** One choice of a config row: the value the role schema stores and the label the cycler shows. */
export interface ConfigChoice {
  readonly value: string | number;
  readonly label: string;
}

/** A row over one config key: a fixed choice list, an Off/On toggle, or a numeric range offered in steps. The three
 *  are exclusive on purpose: a row declaring two of them would read as whichever member `readConfigRow` checks first
 *  rather than as its author meant, so the compiler refuses it. */
export type ConfigRow = { readonly key: string; readonly label: string } & (
  // Exact match; a value the list does not have reads as index 0.
  | { readonly choices: readonly ConfigChoice[]; readonly toggle?: never; readonly range?: never }
  | { readonly toggle: true; readonly choices?: never; readonly range?: never } // Off/On, a boolean key
  | { // The nearest step.
      readonly range: { readonly steps: readonly number[]; readonly labels: readonly string[] };
      readonly choices?: never;
      readonly toggle?: never;
    }
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

/** An app action a package contributes: a rebindable key or gamepad press, carried by the binding profiles and shown in
 *  the bindings menus beside the shell's own three. The shell dispatches it on every press that resolves to it in a
 *  state with no full-window overlay, EXCEPT while the menu is modal (a text prompt, or a capture row armed): a prompt
 *  reads the printable keys off the same bus, so a key a user types there must not also run an action. It runs once per
 *  matching press, and a throw is reported rather than letting it stop the others. Not persisted as a contribution: the
 *  profile stores the id, and an id absent from a stored profile reads the default here (src/bindingMap.ts
 *  `withActionDefaults`), so adding an action needs no migration. */
export interface UiAction {
  /** "<package>.<name>", the manifest's package name and a dot: the profile stores it, so it names its owner, and the
   *  generated registry refuses an id its own package does not prefix. */
  readonly id: string;
  /** The bindings menus' row text ("LSDj Debug Overlay"); the row shows it, then the bound names. */
  readonly label: string;
  /** Default keyboard bindings, by the names the profiles store ("`", "f"). A key the shell already binds by default
   *  (a shell action's or a Game Boy button's) is refused at load, since one press would fire both, and so is a single
   *  uppercase letter: the hosts deliver a letter unshifted, so a default "F" could never fire. */
  readonly keys?: readonly string[];
  /** Default gamepad bindings, by SDL's button names ("leftshoulder", "leftstick"); absent = unbound, which the
   *  gamepad bindings menu shows as "-". */
  readonly pad?: readonly string[];
  /** Run the action. `systemId` is the focused system, null on the start screen. */
  run(host: UiHost, ctx: { readonly systemId: number | null }): void;
}

/** A contributed action with the package it came from: the generated registry's shape (`ACTIONS`), which the shell's
 *  dispatch, the key maps' "known" set and the bindings menus all read. One declaration, so a consumer cannot drift
 *  from the generated list. */
export interface ContributedAction {
  readonly pkg: string;
  readonly action: UiAction;
}

/** Everything a package's UI module contributes. */
export interface UiContribution {
  /** Role-config rows, which the shell renders in the System submenu for every system whose roles they name. */
  readonly rows?: readonly RoleRow[];
  readonly menus?: readonly MenuBuilder[];
  readonly trackers?: readonly TrackerUi[];
  readonly services?: readonly UiService[];
  /** The app actions the package contributes, which the key and gamepad maps dispatch and the bindings menus list. */
  readonly actions?: readonly UiAction[];
}

/** The identity function with types: a UI module's default export, checked against what a package may contribute. */
export function defineUi(contribution: UiContribution): UiContribution {
  return contribution;
}

/** What a package's views module contributes: the window and tile views it draws (declarative: the shell renders
 *  pixels and readout, and mounts a component the package built with @retroplug/ui-react). A package may declare one
 *  and no `ui` contribution, or the reverse. */
export interface ViewsContribution {
  readonly views: readonly ViewDef[];
}

/** The identity function with types: a views module's default export, checked against what a package may contribute. */
export function defineViews(contribution: ViewsContribution): ViewsContribution {
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
  // The three shapes are exclusive, so a defined member is also the row's only one (the other two are `never`, which is
  // why the check is against undefined rather than `in`: an absent optional member would satisfy `in` too).
  if (row.choices !== undefined) {
    const choices = row.choices;
    const names = choices.map((choice) => choice.label);
    const found = choices.findIndex((choice) => choice.value === value);
    return { names, index: Math.max(0, found), at: (step) => choices[step].value };
  }
  if (row.toggle !== undefined) {
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

/** One config row as the menu draws it: `readConfigRow`'s labels and step values. The STEP writes the value the row's
 *  table gives, so a role row and a controller-app row apply the same thing they display. */
export function configRowCycler(
  id: string,
  row: ConfigRow,
  stored: unknown,
  fallback: unknown,
  apply: (value: string | number | boolean) => void,
): MenuItem {
  const reading = readConfigRow(row, stored, fallback);
  return cycler(id, row.label, [...reading.names], reading.index, (step) => apply(reading.at(step)));
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
