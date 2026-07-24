// useFileBrowser — the app-level in-app file browser overlay, same pattern as useProjectModals / the close
// guard. openFileBrowser() (realBackend) publishes a request; this renders it as a MenuTree the App shows
// above everything, and settles the promise on pick / cancel. The current directory is the module-level
// last-dir (so reopening resumes there); navigate() advances it and bumps a local counter to re-render.

import { useEffect, useState } from "react";

import type { AppStores } from "../../src/appStores";
import {
  requestFileBrowser,
  getFileBrowserRequest,
  subscribeFileBrowser,
  resolveFileBrowser,
  getLastBrowseDir,
  setLastBrowseDir,
} from "../../src/fileBrowser";
import { buildFileBrowserMenu } from "../screens/menu/fileBrowserMenu";
import type { MenuTree } from "../screens/menu/menuTree";

export interface FileBrowserOverlay {
  /** True while a browse is open (App gates Esc + game input on this, like the modals). */
  active: boolean;
  /** The overlay tree to render, or null. */
  tree: MenuTree | null;
  /** Esc / B — cancel the browse (resolves null). */
  onClose: () => void;
}

export function useFileBrowser(stores: AppStores): FileBrowserOverlay {
  const [, bump] = useState(0);
  useEffect(() => subscribeFileBrowser(() => bump((n) => n + 1)), []);

  // Install the in-app browser as the __rp_openFileBrowser hook, overriding any native host browser. This is
  // the cross-bundle seam: realBackend (control-plane bundle) calls the global hook and awaits
  // __rp_onFileBrowserResult; here (UI bundle) we open the React overlay and hand the pick back. Saving the
  // prior native hook lets the useNativeFileDialogs toggle delegate to the OS dialog later (step 2).
  useEffect(() => {
    const g = globalThis as {
      __rp_openFileBrowser?: (t: string, p: string, s: boolean, d: string, sd: string, dir: boolean) => void;
      __rp_onFileBrowserResult?: (path: string | null) => void;
    };
    const nativeHook = g.__rp_openFileBrowser;
    g.__rp_openFileBrowser = (title, patterns, saving, defaultName, startDir, directory) => {
      // Opt-in: the host's OS dialog (where it provides one). Default: the in-app overlay.
      if (stores.userConfig.config().useNativeFileDialogs && nativeHook) {
        nativeHook(title, patterns, saving, defaultName, startDir, directory);
        return;
      }
      if (startDir && startDir.length > 0) setLastBrowseDir(startDir); // open where the caller asked (e.g. the render Output Dir)
      void requestFileBrowser({
        title,
        patterns: String(patterns).split(" ").filter((s) => s.length > 0),
        saving: !!saving,
        defaultName,
        startDir,
        directory: !!directory,
      }).then((path) => g.__rp_onFileBrowserResult?.(path ?? null));
    };
    return () => {
      g.__rp_openFileBrowser = nativeHook;
    };
  }, [stores]);

  const req = getFileBrowserRequest();
  if (!req) return { active: false, tree: null, onClose: () => {} };

  const dir = getLastBrowseDir(stores.backend.configDir());
  const tree = buildFileBrowserMenu((d) => stores.backend.listDir(d), req.opts, dir, {
    navigate: (d) => {
      setLastBrowseDir(d);
      bump((n) => n + 1); // re-render → rebuild the tree for the new dir
    },
    pick: (path) => resolveFileBrowser(path),
  });
  return { active: true, tree, onClose: () => resolveFileBrowser(null) };
}
