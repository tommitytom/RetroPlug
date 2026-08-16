// useSongImport - the React state wrapper around the pure ./songImport logic. Owns one pending object and
// builds the overlay as a plain MenuTree each render (so handlers never go stale), rendered full-window by
// App and gated exactly like useProjectModals. All console-specific work lives in the resolved SongCatalog.

import { useState } from "react";

import type { AppStores } from "../../src/appStores";
import type { SystemView } from "../../src/systemsStore";
import type { MenuTree } from "../screens/menu/menuTree";
import { planImport, buildImportModal, applyImport, type ImportPending } from "./songImport";
import { savEditWouldDiscard } from "../../src/tracker";

export interface SongImport {
  /** The overlay to render (null = nothing showing). */
  modal: MenuTree | null;
  /** True while the overlay is up (App gates Esc + game input on this). */
  active: boolean;
  /** Esc / dismiss - clears the overlay. */
  onClose: () => void;
  /** Begin an import: validate `source` against `sys`'s console, then show the picker (or an error notice). */
  begin: (sys: SystemView, source: Uint8Array) => void;
}

export function useSongImport(stores: AppStores): SongImport {
  const [pending, setPending] = useState<ImportPending | null>(null);

  const onClose = (): void => setPending(null);
  const begin = (sys: SystemView, source: Uint8Array): void =>
    // Importing rewrites the battery and cold-boots the cart like every other song edit, so on a console
    // whose working song lives outside the battery it discards unsaved work too.
    setPending(
      planImport(
        sys,
        source,
        savEditWouldDiscard(stores.project.systems, sys) ? "! Unsaved changes to the working song will be lost" : undefined,
      ),
    );

  const toggle = (index: number): void =>
    setPending((p: ImportPending | null) => {
      if (p?.kind !== "pick") return p;
      const checked = new Set(p.checked);
      if (checked.has(index)) checked.delete(index);
      else checked.add(index);
      return { ...p, checked };
    });

  const toggleAll = (): void =>
    setPending((p: ImportPending | null) => {
      if (p?.kind !== "pick") return p;
      const all = p.songs.length === p.checked.size;
      return { ...p, checked: all ? new Set<number>() : new Set(p.songs.map((s) => s.index)) };
    });

  const apply = (): void => {
    if (pending?.kind !== "pick") return;
    const { requested, imported } = applyImport(stores, pending);
    // A full import closes the picker; a partial / failed one (the target filled up) surfaces a notice so
    // the user isn't told it succeeded when songs were dropped.
    if (imported < requested) {
      setPending({
        kind: "notice",
        title: imported === 0 ? "Nothing imported" : "Import incomplete",
        // Don't name a cause we can't know: a short import is usually a full target, but a song the
        // source can't decode is skipped too, and telling someone their cart is full when it isn't
        // sends them deleting songs to make room they already have.
        body: imported === 0 ? "No songs were imported." : `Imported ${imported} of ${requested} songs; the rest would not fit or could not be read.`,
      });
      return;
    }
    setPending(null);
  };

  const modal = pending ? buildImportModal(pending, { toggle, toggleAll, apply, onClose }) : null;
  return { modal, active: pending !== null, onClose, begin };
}
