// Rebase a single path field between relative (as stored in a saved project,
// portable across machines) and absolute (as the emulator needs to slurp it).
// Ports of packages/native/src/project/ProjectPaths.hpp, single-field: the
// ProjectConfig walk that applies these across romPath / savPath / kit sample WAVs
// belongs to the later project domain (visitPaths).
//
// toAbsolute is a pure lexical join — the OS resolves the result at slurp time and
// a later native save re-canonicalizes, so no realpath is needed on load (same
// reasoning as packages/retroplug's missingFiles.ts). toRelative is realpath-hard:
// it canonicalizes both operands first, hence the injected `canonicalize` fn
// (Backend.canonicalize — real symlink resolution in prod, lexical in tests).
// Stored relative form is forward-slash (generic); reconstructed absolute form
// keeps whatever separators the join produced.

import { isAbsolute } from "./pathUtil";

/** Rebase a relative `field` to absolute against `baseDir`, collapsing `.`/`..`.
 *  Empty, already-absolute, or no-base fields are returned unchanged. */
export function rebaseToAbsolute(field: string, baseDir: string): string {
  if (!field || isAbsolute(field) || !baseDir) return field;
  const combined = baseDir.replace(/[\\/]+$/, "") + "/" + field;
  const out: string[] = [];
  for (const s of combined.split(/[\\/]+/)) {
    if (s === "" || s === ".") continue;
    if (s === "..") {
      if (out.length && out[out.length - 1] !== "..") out.pop();
      else out.push("..");
    } else {
      out.push(s);
    }
  }
  const body = out.join("/");
  return combined.startsWith("/") ? "/" + body : body;
}

// Split a path into its root ("" relative, "/" posix, or "C:/" drive) and its
// non-empty, non-"." components.
function components(p: string): { root: string; parts: string[] } {
  let root = "";
  let rest = p;
  const drive = /^[a-zA-Z]:/.exec(rest);
  if (drive) {
    root = drive[0];
    rest = rest.slice(2);
  }
  if (/^[\\/]/.test(rest)) {
    root += "/";
    rest = rest.replace(/^[\\/]+/, "");
  }
  const parts = rest.split(/[\\/]+/).filter((s) => s !== "" && s !== ".");
  return { root, parts };
}

/** The path of `path` relative to `base` (std::filesystem::lexically_relative):
 *  `""` when the roots differ (e.g. a different Windows drive), `"."` when equal,
 *  otherwise a `..`-chain up to the common ancestor then down into `path`. Forward
 *  slashes. Assumes both are already canonical. */
export function lexicallyRelative(path: string, base: string): string {
  const P = components(path);
  const B = components(base);
  if (P.root !== B.root) return "";
  let i = 0;
  while (i < P.parts.length && i < B.parts.length && P.parts[i] === B.parts[i]) i++;
  const up: string[] = [];
  for (let k = i; k < B.parts.length; k++) up.push("..");
  const rel = [...up, ...P.parts.slice(i)];
  return rel.length === 0 ? "." : rel.join("/");
}

/** Rebase an absolute `field` to be relative to `baseDir`, but only when it sits
 *  at/under the base — an asset outside it (a `..` chain, or a different root) is
 *  kept absolute rather than emitting fragile `../` paths. Empty and already-
 *  relative fields are returned unchanged. `canonicalize` normalizes both operands
 *  first (Backend.canonicalize). */
export function rebaseToRelative(
  field: string,
  baseDir: string,
  canonicalize: (p: string) => string,
): string {
  if (!field || !isAbsolute(field)) return field;
  const base = canonicalize(baseDir);
  const p = canonicalize(field);
  if (!base || !p) return field;
  const rel = lexicallyRelative(p, base);
  if (rel === "" || rel.startsWith("..")) return field; // outside base → keep absolute
  return rel;
}
