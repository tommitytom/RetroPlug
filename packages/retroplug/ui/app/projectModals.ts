// The project overlays' trees (see useProjectModals, which owns the pending state and the operations) -
// pure data, no React, so the pure-TS tests can check them, the same split as songImport / useSongImport.

import type { AppStores } from "../../src/appStores";
import type { LoadOutcome } from "../../src/projectStore";
import type { MissingFile } from "../../src/projectMissing";
import { basename } from "../../src/pathUtil";
import { SAV_PATTERNS } from "../../src/savPaths";
import { ROM_PATTERNS } from "../../src/fileSelection";
import { unsavedRows } from "./unsavedRows";
import type { MenuTree } from "../screens/menu/menuTree";
import { button, infoRow, noticeTree } from "../screens/menu/menuItems";

const SRAM_PATTERNS = SAV_PATTERNS; // .sav / .srm battery saves

// One overlay at a time: a discard confirm before a destructive New/Load, an OK notice, or the relink
// prompt for a project that loaded with files missing.
export type PendingModal =
  | { kind: "confirm"; proceed: () => void; saving?: boolean }
  | { kind: "notice"; title: string; body: string }
  | { kind: "relink"; missing: MissingFile[] }
  // A recent SONG row's load, when the cart it lands on holds uncommitted working-song work. The row asked
  // to open song X; silently destroying song Y on the way is not what was agreed to.
  | { kind: "songDiscard"; song: string; working: string }
  // An op that REPLACES what one cart holds (a dropped .sav or ROM), when that cart's working song lives
  // outside its battery and is unsaved. `error` is set when "Save song first" could not store it.
  | { kind: "songReplace"; id: number; proceed: () => void; error?: string; saving?: boolean };

export interface ProjectModalHandlers {
  /** Route a load's outcome to the next overlay (a relink's load finishes through here too). */
  handleOutcome: (o: LoadOutcome) => void;
  /** Esc / Cancel: dismiss (withdrawing a held relink or song request). */
  onClose: () => void;
  /** "Discard & load": confirm the parked song request. */
  proceedSong: () => void;
  /** "Save song first": save system `id`'s working song into its battery and that to its .sav, then `proceed`. */
  saveSongThen: (id: number, proceed: () => void) => void;
  /** The New / Load confirm's Save: save the project (its working songs too), then `proceed`. */
  saveThen: (proceed: () => void) => void;
}

/** Why a working song could not be saved, as a one-line readout. */
export const SONG_SAVE_REFUSED: Record<"full" | "format" | "corrupt" | "unwritable" | "busy", string> = {
  full: "Cart full - the song can't be saved",
  format: "Old save format - the song can't be saved",
  corrupt: "Damaged save - the song can't be saved",
  unwritable: "The battery couldn't be written",
  busy: "The cart kept writing its battery - try again",
};

/** The overlay tree for `pending`. Every button is keepOpen (the overlay owns dismissal itself), and
 *  read-only lines are infoRows, so the cursor opens on the FIRST BUTTON - which is therefore always the
 *  safe one: Menu puts the cursor there, and one stray Enter (or the second half of the double-tap that
 *  raised the prompt) runs it. */
export function buildProjectModal(pending: PendingModal, stores: AppStores, h: ProjectModalHandlers): MenuTree {
  const { handleOutcome, onClose, proceedSong, saveSongThen, saveThen } = h;
  if (pending.kind === "confirm") {
    const { proceed, saving } = pending;
    // While its Save runs, Save and Don't Save are inert rather than disabled (a disabled Save would hand the cursor
    // to Don't Save): Don't Save pressed mid-save used to start the new project, and the save then resumed and wrote
    // the NEW, empty project over the user's .rplg.
    const live = (onSelect: () => void) => (saving ? () => {} : onSelect);
    // Leads with WHAT is unsaved (the project file + each dirty battery's target .sav), greyed and skipped
    // by nav, so the user can see what Save writes / Don't Save throws away. Same block as the close guard.
    return {
      title: "Unsaved changes",
      items: [
        ...unsavedRows(stores.backend, stores.project),
        button("discard-save", saving ? "Saving..." : "Save", live(() => saveThen(proceed))),
        button("discard-nosave", "Don't Save", live(proceed)),
        button("discard-cancel", "Cancel", onClose),
      ],
    };
  }

  if (pending.kind === "songReplace") {
    const { id, proceed, error, saving } = pending;
    const go = (): void => {
      onClose();
      proceed();
    };
    // While a save is in flight only Cancel does anything: a second Save would run the op twice, and a
    // Discard would replace the cart out from under the write. The cursor opens on "Save song first", which
    // keeps the song either way.
    const busy = (onSelect: () => void) => (saving ? () => {} : onSelect);
    return {
      title: "Unsaved song",
      items: [
        infoRow("song-replace-what", saving ? "Saving the song..." : (error ?? "The working song has unsaved changes")),
        button("song-replace-save", "Save song first", busy(() => saveSongThen(id, proceed))),
        button("song-replace-discard", "Discard song", busy(go)),
        button("song-replace-cancel", "Cancel", onClose),
      ],
    };
  }

  if (pending.kind === "songDiscard") {
    const { song, working } = pending;
    // No "save first" here (unlike the Songs menu): the cart has only just booted, so this is a plain
    // proceed-or-cancel. Cancel leaves the project loaded showing whatever song the cart came up on, and
    // comes FIRST so the cursor opens on it: "Discard" destroys the working song and must be a choice.
    return {
      title: "Unsaved song",
      items: [
        infoRow("song-discard-what", `"${working}" has unsaved changes`),
        button("song-discard-cancel", "Keep current song", onClose),
        button("song-discard-go", `Discard & load "${song}"`, proceedSong),
      ],
    };
  }

  if (pending.kind === "notice") return noticeTree("notice", pending.title, pending.body, onClose);

  // relink: one "Locate …" per missing file (the store auto-resolves folder-mates, so one pick often
  // clears the rest), then Cancel.
  const items = pending.missing.map((m, i) =>
    button(`relink-${i}`, `Locate ${basename(m.path) || m.itemKind}...`, () =>
      void stores.backend
        .openFileBrowser({ title: `Locate ${basename(m.path) || m.itemKind}`, patterns: m.itemKind === "rom" ? ROM_PATTERNS : SRAM_PATTERNS })
        .then((p) => {
          if (p) handleOutcome(stores.project.relink(m, p));
        }),
    ),
  );
  items.push(button("relink-cancel", "Cancel", onClose));
  return { title: "Missing files", items };
}
