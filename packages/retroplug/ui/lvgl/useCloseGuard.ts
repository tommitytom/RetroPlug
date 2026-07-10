// useCloseGuard — the standalone "confirm on close" seam.
//
// The editor's native onClose() (PluginGreenfieldUI) calls __rp_onCloseRequested when the user tries to
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

export interface CloseGuard {
  /** True while the unsaved-changes prompt is showing. */
  active: boolean;
  /** Save everything (dirty SRAM + the project), then quit. Opens Save-As when the project has no path
   *  yet; a cancelled Save-As keeps the prompt open. */
  onSave: () => void;
  /** Quit without saving. */
  onDiscard: () => void;
  /** Dismiss the prompt and stay open. */
  onCancel: () => void;
}

function quitWindow(): void {
  (globalThis as { __rp_quitWindow?: () => void }).__rp_quitWindow?.();
}

export function useCloseGuard(stores: AppStores): CloseGuard {
  const [active, setActive] = useState(false);

  // Install the native close-request hook: veto (and show the prompt) only when something is unsaved.
  useEffect(() => {
    const g = globalThis as { __rp_onCloseRequested?: () => boolean };
    g.__rp_onCloseRequested = (): boolean => {
      if (!hasUnsavedChanges(stores.backend, stores.project)) return false; // clean → let it close
      setActive(true);
      return true; // veto: the overlay renders next frame
    };
    return () => {
      delete g.__rp_onCloseRequested;
    };
  }, [stores]);

  const onCancel = useCallback(() => setActive(false), []);

  const onDiscard = useCallback(() => {
    setActive(false);
    quitWindow();
  }, []);

  const onSave = useCallback(() => {
    void (async () => {
      if (!(await saveProjectInteractive(stores))) return; // Save-As cancelled → keep the prompt open
      setActive(false);
      quitWindow();
    })();
  }, [stores]);

  return { active, onSave, onDiscard, onCancel };
}
