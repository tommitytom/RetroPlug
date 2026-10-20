// useFileBrowser — the app-level in-app file browser overlay, same pattern as useProjectModals / the close
// guard. openFileBrowser() (realBackend) publishes a request; this renders it as a MenuTree the App shows
// above everything, and settles the promise on pick / cancel. The current directory is the module-level
// last-dir (so reopening resumes there); navigate() advances it and bumps a local counter to re-render.

import { useEffect } from "react";

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
import type { Overlay } from "./overlay";
import { useRerenderOn } from "./useRerender";
import { native } from "./nativeHooks";

/** The browse as an overlay; onClose (Esc / B) cancels it (resolves null). */
export type FileBrowserOverlay = Overlay;

export function useFileBrowser(stores: AppStores): FileBrowserOverlay {
  const rerender = useRerenderOn(subscribeFileBrowser);

  // Install the in-app browser as the __rp_openFileBrowser hook, overriding any native host browser. This is
  // the cross-bundle seam: realBackend (control-plane bundle) calls the global hook and awaits
  // __rp_onFileBrowserResult; here (UI bundle) we open the React overlay and hand the pick back. Saving the
  // prior native hook lets the useNativeFileDialogs toggle delegate to the OS dialog later (step 2).
  useEffect(() => {
    const nativeHook = native.__rp_openFileBrowser;
    native.__rp_openFileBrowser = (title, patterns, saving, defaultName, startDir, directory) => {
      // Default: the host's OS dialog. It falls through to the in-app overlay both when the user has opted
      // out and when the host binds no native hook at all (no desktop dialog helper), so this is safe
      // everywhere - a helper-less host just keeps the browser it always had.
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
      }).then((path) => native.__rp_onFileBrowserResult?.(path ?? null));
    };
    return () => {
      native.__rp_openFileBrowser = nativeHook;
    };
  }, [stores]);

  const req = getFileBrowserRequest();
  if (!req) return { active: false, tree: null, onClose: () => {} };

  const dir = getLastBrowseDir(stores.backend.configDir());
  const tree = buildFileBrowserMenu((d) => stores.backend.listDir(d), req.opts, dir, {
    navigate: (d) => {
      setLastBrowseDir(d);
      rerender(); // rebuild the tree for the new dir
    },
    pick: (path) => resolveFileBrowser(path),
  });
  return { active: true, tree, onClose: () => resolveFileBrowser(null) };
}
