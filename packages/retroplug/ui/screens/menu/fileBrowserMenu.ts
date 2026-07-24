// The in-app file browser rendered AS a MenuTree, so it reuses the whole Menu component (keyboard + gamepad
// nav, scrolling, the insertChildBefore workaround) — a browser is just a list that rebuilds as you navigate.
// Directories become "navigate" rows (rebuild the tree for the new dir), files become "pick" rows, and a save
// dialog adds a filename prompt at the top. The current directory is React state in App; navigate() advances
// it. Backed by the cross-host listDir RPC (dirs carry a trailing '/').

import type { FileBrowserOpts } from "../../../src/backend";
import { matchesPatterns } from "../../../src/fileBrowser";
import type { MenuItem, MenuTree } from "./menuTree";

export interface FileBrowserActions {
  navigate: (dir: string) => void; // enter a directory (App re-renders with the new dir)
  pick: (path: string | null) => void; // settle the browse: an absolute path, or null to cancel
}

/** Join `name` (possibly a "dir/" entry) onto `dir` into an absolute path, dropping any trailing slash. */
function joinPath(dir: string, name: string): string {
  const clean = name.endsWith("/") ? name.slice(0, -1) : name;
  return dir === "/" ? "/" + clean : dir.replace(/\/+$/, "") + "/" + clean;
}

/** The parent of `dir` ("/" at the root). */
function parentDir(dir: string): string {
  const norm = dir.replace(/\/+$/, "");
  const i = norm.lastIndexOf("/");
  return i <= 0 ? "/" : norm.slice(0, i);
}

export function buildFileBrowserMenu(
  listDir: (dir: string) => string[],
  opts: FileBrowserOpts,
  dir: string,
  h: FileBrowserActions,
): MenuTree {
  const entries = listDir(dir);
  const dirs = entries.filter((e) => e.endsWith("/")).sort((a, b) => a.localeCompare(b));
  // A folder picker (opts.directory, e.g. the render Output Dir) navigates subdirectories only — no files.
  const files = opts.directory
    ? []
    : entries.filter((e) => !e.endsWith("/") && matchesPatterns(e, opts.patterns)).sort((a, b) => a.localeCompare(b));

  const items: MenuItem[] = [];

  // Folder-pick mode: a row that selects the CURRENT directory. Navigate into subdirs, then choose here.
  if (opts.directory) {
    items.push({ id: "fb-choose-dir", label: `[Choose this folder]`, kind: "action", keepOpen: true, onSelect: () => h.pick(dir) });
    items.push({ id: "fb-sep-dir", label: "", kind: "separator" });
  }

  // Save mode: a filename prompt (pre-filled with the suggested default) at the top. Confirm → pick the path.
  if (opts.saving) {
    items.push({
      id: "fb-saveas",
      label: `Save as: ${opts.defaultName ?? ""}`,
      kind: "prompt",
      keepOpen: true,
      prompt: {
        title: `Save in ${dir} as:`,
        initial: opts.defaultName ?? "",
        onConfirm: (v: string) => {
          const name = v.trim();
          if (!name) return "Enter a filename.";
          h.pick(joinPath(dir, name));
          return null;
        },
      },
    });
    items.push({ id: "fb-sep-save", label: "", kind: "separator" });
  }

  // Parent nav (unless at the filesystem root).
  if (dir !== "/")
    items.push({ id: "fb-up", label: "..", kind: "action", keepOpen: true, onSelect: () => h.navigate(parentDir(dir)) });

  // Directories — navigate into.
  for (const d of dirs) {
    const name = d.slice(0, -1);
    items.push({ id: `fb-d-${name}`, label: `${name}/`, kind: "action", keepOpen: true, onSelect: () => h.navigate(joinPath(dir, d)) });
  }

  // Files — pick to open (or overwrite in save mode).
  for (const f of files)
    items.push({ id: `fb-f-${f}`, label: f, kind: "action", keepOpen: true, onSelect: () => h.pick(joinPath(dir, f)) });

  if (!opts.directory && dirs.length === 0 && files.length === 0)
    items.push({ id: "fb-empty", label: opts.saving ? "(empty)" : "(no matching files)", kind: "action", keepOpen: true, disabled: true });

  return { title: `${opts.title}: ${dir}`, items };
}
