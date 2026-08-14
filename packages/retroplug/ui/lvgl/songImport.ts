// The Songs "import from a .sav" logic - PURE (no React), so the pure-TS test project (which excludes ui/'s
// react types) can exercise it, same split the codebase uses for menuDefs vs the hooks. The React state
// wrapper is ./useSongImport. The console-specific work is entirely in the resolved SongCatalog
// (isValidSav / list / importSongs), so this stays console-agnostic.

import type { AppStores } from "../../src/appStores";
import type { SystemView } from "../../src/systemsStore";
import type { SongCatalog, SongInfo } from "../../src/tracker";
import { resolveTracker } from "../../src/tracker";
import { resolveSavPath } from "../../src/savPaths";
import type { MenuItem, MenuTree } from "../screens/menu/menuTree";

// The picker for a validated source sav: its songs + the currently-checked source indices.
export interface PickState {
  kind: "pick";
  sys: SystemView;
  cat: SongCatalog;
  source: Uint8Array;
  songs: SongInfo[];
  checked: Set<number>;
  /** Set when importing will cold-boot the cart away from unsaved work (a console whose working song
   *  lives outside the battery). Shown in the picker rather than raised as a second prompt: the picker's
   *  own "Import (N)" button is already the confirmation step, and stacking a dialog on a dialog is how
   *  warnings get dismissed unread. */
  warning?: string;
}
// A dismissible error/info notice (invalid sav / no songs).
export interface NoticeState {
  kind: "notice";
  title: string;
  body: string;
}
export type ImportPending = PickState | NoticeState;

export interface ImportHandlers {
  toggle: (index: number) => void;
  toggleAll: () => void;
  apply: () => void;
  onClose: () => void;
}

/** Validate a source sav against a system's console and produce the initial pending state: a `pick` (all
 *  songs checked) when it's a readable save of the same console with songs, else a `notice`. */
export function planImport(sys: SystemView, source: Uint8Array, warning?: string): ImportPending {
  const tracker = resolveTracker(sys.roles);
  if (!tracker) return { kind: "notice", title: "Cannot import", body: "This system has no song catalog." };
  const cat = tracker.songs;
  if (!cat) return { kind: "notice", title: "Cannot import", body: "This system has no song catalog." };
  if (!cat.isValidSav(source)) return { kind: "notice", title: "Cannot import", body: `Not a valid ${tracker.label} save.` };
  const songs = cat.list(source);
  if (songs.length === 0) return { kind: "notice", title: "Cannot import", body: "No songs found in that save." };
  return { kind: "pick", sys, cat, source, songs, checked: new Set(songs.map((s) => s.index)), ...(warning ? { warning } : {}) };
}

/** Import the checked songs into the live battery (readSram -> catalog.importSongs -> write .sav -> cold
/** The outcome of an import: how many of the `requested` (checked) songs actually landed in the battery.
 *  `imported < requested` means the target filled up (a partial import) — the caller surfaces that. */
export interface ImportResult {
  requested: number;
  imported: number;
}

/** Import the checked songs into the live battery (readSram -> catalog.importSongs -> write .sav -> cold
 *  boot), the mutateSavBytes cycle done off `stores`. `imported` counts the songs the catalog actually
 *  gained (import is best-effort fill: a full target keeps what fit) — 0 when nothing was written. */
export function applyImport(stores: AppStores, pick: PickState): ImportResult {
  const indices = pick.songs.map((s) => s.index).filter((i) => pick.checked.has(i)); // keep source order
  const requested = indices.length;
  if (requested === 0) return { requested, imported: 0 };
  const systems = stores.project.systems;
  const target = systems.readSram(pick.sys.id);
  if (!target) return { requested, imported: 0 };
  const before = pick.cat.list(target).length;
  const out = pick.cat.importSongs(target, pick.source, indices);
  if (!out) return { requested, imported: 0 };
  const imported = pick.cat.list(out).length - before; // songs the catalog actually gained
  if (imported <= 0) return { requested, imported: 0 }; // nothing landed → don't write / reboot needlessly
  const savPath = resolveSavPath(pick.sys.romPath, pick.sys.savSuffix, pick.sys.savPath);
  if (!stores.backend.writeFileAtomic(savPath, out)) return { requested, imported: 0 };
  systems.loadSram(pick.sys.id, savPath);
  return { requested, imported };
}

/** Build the overlay tree for `pending`. Song rows and the buttons are keepOpen (the overlay owns its own
 *  dismissal), matching buildModal in useProjectModals. Checkbox is ASCII "[x]"/"[ ]" (no LVGL glyph). */
export function buildImportModal(pending: ImportPending, h: ImportHandlers): MenuTree {
  if (pending.kind === "notice") {
    const btn = (id: string, label: string, onSelect: () => void): MenuItem => ({ id, label, kind: "action", keepOpen: true, onSelect });
    return { title: pending.title, items: [btn("import-notice-body", pending.body, () => {}), btn("import-notice-ok", "OK", h.onClose)] };
  }
  const allChecked = pending.songs.length === pending.checked.size;
  const rows: MenuItem[] = pending.songs.map((s) => ({
    id: `import-song-${s.index}`,
    label: `[${pending.checked.has(s.index) ? "x" : " "}] ${s.name || `Song ${s.index}`}`,
    kind: "action",
    keepOpen: true,
    onSelect: () => h.toggle(s.index),
  }));
  const controls: MenuItem[] = [
    { id: "import-sep", label: "", kind: "separator" },
    ...(pending.warning ? [{ id: "import-warning", label: pending.warning, kind: "action" as const, keepOpen: true, disabled: true, onSelect: () => {} }] : []),
    { id: "import-all", label: allChecked ? "Select None" : "Select All", kind: "action", keepOpen: true, onSelect: h.toggleAll },
    { id: "import-do", label: `Import (${pending.checked.size})`, kind: "action", keepOpen: true, onSelect: h.apply, ...(pending.checked.size === 0 ? { disabled: true } : {}) },
    { id: "import-cancel", label: "Cancel", kind: "action", keepOpen: true, onSelect: h.onClose },
  ];
  return { title: "Import Songs", items: [...rows, ...controls] };
}
