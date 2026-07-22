// The std::filesystem::path member operations RetroPlug's native path code relies
// on, as pure TS string ops. The plugin is cross-platform, so BOTH `/` and `\`
// count as separators (matching the mock canonicalizer and packages/retroplug's
// missingFiles.ts), and derived paths are emitted in forward-slash form. Faithful
// to std::filesystem semantics for the cases that matter to ROM/sav/rplg
// derivation: no-extension, multi-dot names, and leading-dot ("dotfiles", which
// have no extension). No IO — everything here is a pure transform of the string.

const SEP = /[\\/]/;

/** Index of the last path separator (`/` or `\`), or -1 if none. */
function lastSep(p: string): number {
  return Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"));
}

/** Parent directory: everything before the last separator. `""` when the path has
 *  no separator; `"/"` for a root-level entry (matches path::parent_path). */
export function dirname(p: string): string {
  const i = lastSep(p);
  if (i < 0) return "";
  if (i === 0) return "/";
  return p.slice(0, i);
}

/** The last path component (path::filename). */
export function basename(p: string): string {
  const i = lastSep(p);
  return i < 0 ? p : p.slice(i + 1);
}

// The dot that begins the extension of a filename, or -1 when there is none.
// A dot at index 0 (a leading-dot "dotfile") is NOT an extension separator, so
// `.config` has an empty extension and a stem of `.config` — as in std::filesystem.
function extDot(name: string): number {
  const dot = name.lastIndexOf(".");
  return dot <= 0 ? -1 : dot;
}

/** Filename minus the final extension (path::stem). */
export function stem(p: string): string {
  const name = basename(p);
  const dot = extDot(name);
  return dot < 0 ? name : name.slice(0, dot);
}

/** The final extension, including the leading dot; `""` when there is none
 *  (path::extension). */
export function extension(p: string): string {
  const name = basename(p);
  const dot = extDot(name);
  return dot < 0 ? "" : name.slice(dot);
}

/** `extension()` lowercased — the seam native uses to compare against `.sav`. */
export function extensionLower(p: string): string {
  return extension(p).toLowerCase();
}

/** Replace the final extension (or append one when there is none). `ext` includes
 *  the dot (`".rplg"`); an empty `ext` strips the extension (path::replace_extension). */
export function replaceExtension(p: string, ext: string): string {
  const i = lastSep(p);
  const dir = i < 0 ? "" : p.slice(0, i + 1);
  const name = i < 0 ? p : p.slice(i + 1);
  const dot = extDot(name);
  const base = dot < 0 ? name : name.slice(0, dot);
  const suffix = ext === "" ? "" : ext.startsWith(".") ? ext : "." + ext;
  return dir + base + suffix;
}

/** Replace the whole last component, preserving the directory (path::replace_filename). */
export function replaceFilename(p: string, name: string): string {
  const i = lastSep(p);
  return i < 0 ? name : p.slice(0, i + 1) + name;
}

/** Join a directory and a name with a single separator (path::operator/). An empty
 *  dir yields the bare name; a dir already ending in a separator isn't doubled. */
export function joinPath(dir: string, name: string): string {
  if (!dir) return name;
  return SEP.test(dir[dir.length - 1]) ? dir + name : dir + "/" + name;
}

/** True when `p` is absolute: a leading `/` or a drive letter (path::is_absolute,
 *  cross-platform). Mirrors packages/retroplug/src/missingFiles.ts. */
export function isAbsolute(p: string): boolean {
  return /^([a-zA-Z]:[\\/]|[\\/])/.test(p);
}

/** Shorten `s` (e.g. a long directory path) to at most `maxLen` characters by eliding the middle with a
 *  single `…`, keeping the start and end — for display in the narrow menu. A no-op when it already fits.
 *  The end (the target folder) keeps the extra character on an odd budget. */
// ASCII "..." for the elision marker: the LVGL menu font has no "…" (U+2026) glyph (it renders as a tofu
// box), and ".." would read as the parent-directory token inside a path.
const ELLIPSIS = "...";
export function shortenMiddle(s: string, maxLen: number): string {
  if (s.length <= maxLen) return s;
  if (maxLen <= ELLIPSIS.length) return s.slice(0, Math.max(0, maxLen)); // no room for head+tail — hard-truncate
  const budget = maxLen - ELLIPSIS.length;
  const tail = Math.ceil(budget / 2);
  const head = budget - tail;
  return s.slice(0, head) + ELLIPSIS + (tail > 0 ? s.slice(s.length - tail) : "");
}

/** The first of `base`, `base_2`, `base_3`, … for which `isTaken` returns false — the "If Exists: Rename"
 *  render policy. `isTaken(candidate)` reports whether an output for that base already exists (the caller
 *  checks `<base>.wav`, or the split's `<base>_<channel>.wav` files). Pure — the caller owns the fs check. */
export function uniqueBase(base: string, isTaken: (candidate: string) => boolean): string {
  if (!isTaken(base)) return base;
  for (let n = 2; ; n++) {
    const candidate = `${base}_${n}`;
    if (!isTaken(candidate)) return candidate;
  }
}
