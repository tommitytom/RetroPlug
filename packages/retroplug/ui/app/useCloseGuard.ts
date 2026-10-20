// useCloseGuard — the standalone "confirm on close" seam.
//
// The editor's native onClose() (PluginUI) calls __rp_onCloseRequested when the user tries to
// close the window. This hook installs that global: if there are unsaved changes it raises the in-app
// Save/Discard/Cancel prompt and returns true so native VETOES the close (keeps the window open);
// otherwise it returns false and the window closes normally. Once the user resolves the prompt, Save /
// Discard call __rp_quitWindow to actually close (Cancel just dismisses). Mirrors the file-browser seam
// (__rp_openFileBrowser / __rp_onFileBrowserResult) and is inert in the headless harness / a DAW, where
// neither global is installed (onClose never fires outside the standalone anyway).

import { useCallback, useEffect, useState } from "react";

import type { AppStores } from "../../src/appStores";
import { hasUnsavedChanges } from "../../src/unsavedChanges";
import { saveProjectInteractive } from "./saveProjectInteractive";
import { unsavedRows } from "./unsavedRows";
import type { Overlay } from "./overlay";
import { native } from "../host/nativeHooks";
import type { MenuTree } from "../screens/menu/menuTree";
import { button } from "../screens/menu/menuItems";

/** The unsaved-changes prompt as an overlay: Save & Quit / Discard & Quit / Cancel (= onClose). */
export type CloseGuard = Overlay;

export function useCloseGuard(stores: AppStores): CloseGuard {
  const [active, setActive] = useState(false);

  // Install the native close-request hook: veto (and show the prompt) only when something is unsaved.
  useEffect(() => {
    native.__rp_onCloseRequested = (): boolean => {
      if (!hasUnsavedChanges(stores.backend, stores.project)) return false; // clean → let it close
      setActive(true);
      return true; // veto: the overlay renders next frame
    };
    return () => {
      delete native.__rp_onCloseRequested;
    };
  }, [stores]);

  const onCancel = useCallback(() => setActive(false), []);

  const onDiscard = useCallback(() => {
    setActive(false);
    native.__rp_quitWindow?.();
  }, []);

  const onSave = useCallback(() => {
    void (async () => {
      if (!(await saveProjectInteractive(stores))) return; // Save-As cancelled → keep the prompt open
      setActive(false);
      native.__rp_quitWindow?.();
    })();
  }, [stores]);

  // It leads with WHAT is unsaved (the project file + each dirty battery's target .sav, greyed and
  // nav-skipped). Built each render rather than latched when the prompt opened, so a battery the cart
  // writes while it's up shows on the next render.
  const tree: MenuTree | null = active
    ? {
        title: "Unsaved changes",
        items: [
          ...unsavedRows(stores.backend, stores.project),
          button("close-save", "Save & Quit", onSave),
          button("close-discard", "Discard & Quit", onDiscard),
          button("close-cancel", "Cancel", onCancel),
        ],
      }
    : null;
  return { active, tree, onClose: onCancel };
}
