// useProjectModals — the app-level overlays around New Project / Load Project.
//
// Two gaps this closes: (1) New/Load used to discard unsaved work with no prompt; (2) project.load()'s
// LoadOutcome (incompatible / missing / error) was voided at every call site, so a project saved by a
// newer build, or one whose ROM/sav moved, failed silently. This owns a single modal at a time and the
// guarded operations the menu drives (ctx.newProject / ctx.loadProject).
//
// Load outcomes arrive AFTER the file dialog closes (the menu is gone by then), so they surface as a
// full-window overlay — App renders `modal` above everything, exactly like the close-guard prompt. The
// overlay is a plain MenuTree (data), rebuilt each render from the pending state, so its handlers never
// go stale. Mirrors useCloseGuard's Save/Discard/Cancel flow via the shared saveProjectInteractive.

import { useRef, useState } from "react";

import type { AppStores } from "../../src/appStores";
import type { LoadOutcome } from "../../src/projectStore";
import type { MissingFile } from "../../src/projectMissing";
import { hasUnsavedChanges } from "../../src/unsavedChanges";
import { basename } from "../../src/pathUtil";
import { SAV_PATTERNS } from "../../src/savPaths";
import { loadSongInPrimary } from "../../src/tracker";
import { saveProjectInteractive } from "./saveProjectInteractive";
import { unsavedRows } from "./unsavedRows";
import type { MenuItem, MenuTree } from "../screens/menu/menuTree";

const ROM_PATTERNS = ["*.gb", "*.gbc", "*.gba", "*.nes"];
const SRAM_PATTERNS = SAV_PATTERNS; // .sav / .srm battery saves

// One overlay at a time: a discard confirm before a destructive New/Load, an OK notice, or the relink
// prompt for a project that loaded with files missing.
type PendingModal =
  | { kind: "confirm"; proceed: () => void }
  | { kind: "notice"; title: string; body: string }
  | { kind: "relink"; missing: MissingFile[] };

export interface ProjectModals {
  /** The overlay to render (null = nothing showing). */
  modal: MenuTree | null;
  /** True while an overlay is up (App gates Esc + game input on this). */
  active: boolean;
  /** Esc / dismiss — cancels a pending relink's load, then clears the overlay. */
  onClose: () => void;
  /** Start a new project, guarding unsaved changes first. */
  newProject: () => void;
  /** Load `path`, guarding unsaved changes first and surfacing the outcome. `song` (a recent row's) is
   *  loaded into the cart once the project lands, so picking a song row reopens that song. */
  loadProject: (path: string, song?: string) => void;
  /** Open `romPath` as a fresh project (with an optional paired sav), guarding unsaved changes first. */
  loadRomAsProject: (romPath: string, explicitSav?: string) => void;
}

export function useProjectModals(stores: AppStores): ProjectModals {
  const [pending, setPending] = useState<PendingModal | null>(null);
  const project = stores.project;
  // The song a recent row asked for, held until its load actually lands. A ref (not state) because it must
  // survive the relink round-trip - locate the missing ROM, and the song still gets loaded - without
  // re-rendering anything itself.
  const pendingSong = useRef<string>("");

  // A load result → the next overlay: loaded clears; incompatible/error notify; missing offers relink.
  const handleOutcome = (outcome: LoadOutcome): void => {
    // Every completed load funnels through here (including one finished by a relink), so this is the one
    // place the requested song can be applied. A load that didn't land drops it - except "missing", which
    // is still in flight awaiting the relink.
    const song = pendingSong.current;
    if (outcome.kind !== "missing") pendingSong.current = "";
    switch (outcome.kind) {
      case "loaded":
        if (song) loadSongInPrimary(stores.backend, project.systems, song); // reopen the row's song
        setPending(null);
        break;
      case "incompatible":
        setPending({ kind: "notice", title: "Incompatible Project", body: "Saved by a newer version of RetroPlug." });
        break;
      case "error":
        setPending({ kind: "notice", title: "Load Failed", body: "The project file couldn't be read." });
        break;
      case "missing":
        setPending({ kind: "relink", missing: outcome.missing });
        break;
    }
  };

  // Run `proceed` now if the project is clean, else raise the discard confirm first.
  const guard = (proceed: () => void): void => {
    if (hasUnsavedChanges(stores.backend, project)) setPending({ kind: "confirm", proceed });
    else proceed();
  };

  const newProject = (): void =>
    guard(() => {
      setPending(null);
      project.newProject();
    });

  const loadProject = (path: string, song?: string): void =>
    guard(() => {
      pendingSong.current = song ?? "";
      handleOutcome(project.load(path));
    });

  const loadRomAsProject = (romPath: string, explicitSav?: string): void =>
    guard(() => {
      setPending(null);
      project.openRom(romPath, explicitSav ? { explicitSav } : undefined);
    });

  const onClose = (): void =>
    setPending((p) => {
      if (p?.kind === "relink") {
        project.cancelLoad(); // abandoning the relink drops the held load
        pendingSong.current = ""; // …and the song it was going to open
      }
      return null;
    });

  const modal = pending ? buildModal(pending, stores, handleOutcome, onClose) : null;
  return { modal, active: pending !== null, onClose, newProject, loadProject, loadRomAsProject };
}

// Build the overlay tree for `pending`. Every button uses keepOpen (the overlay owns dismissal itself),
// mirroring the close-guard tree in App.tsx.
function buildModal(
  pending: PendingModal,
  stores: AppStores,
  handleOutcome: (o: LoadOutcome) => void,
  onClose: () => void,
): MenuTree {
  const btn = (id: string, label: string, onSelect: () => void): MenuItem => ({ id, label, kind: "action", keepOpen: true, onSelect });

  if (pending.kind === "confirm") {
    const { proceed } = pending;
    // Leads with WHAT is unsaved (the project file + each dirty battery's target .sav), greyed and skipped
    // by nav, so the user can see what Save writes / Don't Save throws away. Same block as the close guard.
    return {
      title: "Unsaved changes",
      items: [
        ...unsavedRows(stores.backend, stores.project),
        btn("discard-save", "Save", () => void saveProjectInteractive(stores).then((saved) => saved && proceed())),
        btn("discard-nosave", "Don't Save", proceed),
        btn("discard-cancel", "Cancel", onClose),
      ],
    };
  }

  if (pending.kind === "notice") {
    return {
      title: pending.title,
      items: [btn("notice-body", pending.body, () => {}), btn("notice-ok", "OK", onClose)],
    };
  }

  // relink: one "Locate …" per missing file (the store auto-resolves folder-mates, so one pick often
  // clears the rest), then Cancel.
  const items = pending.missing.map((m, i) =>
    btn(`relink-${i}`, `Locate ${basename(m.path) || m.itemKind}...`, () =>
      void stores.backend
        .openFileBrowser({ title: `Locate ${basename(m.path) || m.itemKind}`, patterns: m.itemKind === "rom" ? ROM_PATTERNS : SRAM_PATTERNS })
        .then((p) => {
          if (p) handleOutcome(stores.project.relink(m, p));
        }),
    ),
  );
  items.push(btn("relink-cancel", "Cancel", onClose));
  return { title: "Missing files", items };
}
