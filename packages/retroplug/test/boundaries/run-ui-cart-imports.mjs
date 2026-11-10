#!/usr/bin/env node
// Walks the app's ui/ tree for an import of a cart, console or device package, for
// test/boundaries/ui-cart-imports.test.ts (docs/modular-graph-phase-f4b-design.md section 7.3).
//
//   node test/boundaries/run-ui-cart-imports.mjs [--root <dir>]
//
// The imports come from the boundary check's own scanner (TypeScript's preProcessFile), so a type-only import, an
// `export ... from` and a `require` all count: ui/ may carry a cart's code only through ui/generated/menus.ts, which is
// not judged (it is the one place that names every package), and a device pack's UI module MAY be reached through the
// generated list, never by a view directly. A RELATIVE import that lands in such a package's tree counts too, so
// reaching one by a path rather than a specifier is the same reach. Prints one JSON line:
// { hits: [{ file specifier }] }, file relative to the repository root.
//
// The package list is DERIVED from the workspace manifests: a package a view may not name is one whose manifest
// contributes CODE or baked bytes, any of `retroplug.app` (a cart's, a console's or a device pack's roles, ROM
// providers, tracker integration and kit formats), `ui` (its menu contribution), `cli` (its tools), `examples` (its
// sessions), `consoles` (the descriptors its cores run), `protocol` (its node types) or `embed` / `script` (bytes it
// bakes in, or its device pack). A package with none of those declares a CONTRACT instead - core, ui, tracker, dsp,
// testing - or nothing but native targets, and a view may name it: its code is the API the shell is built on. The
// shell is the other exclusion, because its own UI tree is what this walk judges.
//
// A specifier on rule 9's list (`SHELL_IMPORTS`, the shell's own contract modules, each with the reason it is a
// contract rather than a cart) is allowed even from a guarded package, so a guarded package's contract module a view
// legitimately reaches stays reachable without a second hand-kept list: today that is the Launchpad's enums and app
// registry.
import { readdirSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { SHELL_IMPORTS } from "../../scripts/check-boundaries.mjs";

const ts = createRequire(import.meta.url)("typescript-5");
const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");

function option(name) {
  const at = process.argv.indexOf(name);
  return at === -1 ? undefined : process.argv[at + 1];
}

const root = resolve(option("--root") ?? REPO);

/** The manifest members that make a package code a view may not name: each names a module it contributes (its app, its
 *  UI contribution, its CLI tools, its example sessions, its console descriptors, its node types) or the bytes it bakes
 *  in (an embedded ROM, a device pack's script). A package with none of them contributes a contract or only native
 *  targets. */
const CODE_MEMBERS = ["app", "ui", "cli", "examples", "protocol", "consoles", "embed", "script"];

/** Every workspace package with one of CODE_MEMBERS but the shell: a cart's, a console's or a device pack's code, which
 *  is what a view may not name. */
function derivedPackages() {
  const names = [];
  for (const entry of readdirSync(join(root, "packages"), { withFileTypes: true })) {
    if (!entry.isDirectory() || entry.name === "retroplug") continue;
    try {
      const manifest = JSON.parse(readFileSync(join(root, "packages", entry.name, "package.json"), "utf8"));
      if (CODE_MEMBERS.some((member) => manifest.retroplug?.[member] !== undefined)) names.push(manifest.name);
    } catch {
      // A directory with no manifest of its own is not a package.
    }
  }
  return names;
}

const PACKAGES = new Set(derivedPackages());
const CONTRACTS = new Set(SHELL_IMPORTS.map((entry) => entry.specifier));
/** The directory under packages/ each of those names: a relative import is resolved to a path and matched by it. */
const PACKAGE_DIRS = new Set([...PACKAGES].map((name) => name.split("/").pop()));
const uiRoot = join(root, "packages/retroplug/ui");

function walk(dir, out = []) {
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      if (path === join(uiRoot, "generated")) continue; // the one place that names every package
      walk(path, out);
    } else if (/\.(ts|tsx|mts|cts|js|jsx|mjs|cjs)$/.test(entry.name)) out.push(path);
  }
  return out;
}

/** The package name a bare specifier names: the scoped pair ("@retroplug/lsdj/runtime" -> "@retroplug/lsdj"). */
function nameOf(specifier) {
  const parts = specifier.split("/");
  return specifier.startsWith("@") && parts.length > 1 ? `${parts[0]}/${parts[1]}` : specifier;
}

const hits = [];
for (const file of walk(uiRoot)) {
  const scanned = ts.preProcessFile(readFileSync(file, "utf8"), true, true);
  for (const { fileName } of scanned.importedFiles) {
    if (CONTRACTS.has(fileName)) continue; // a package's contract module the shell reaches through its own list
    const reach = fileName.startsWith(".")
      // A relative import counts by where it LANDS: reaching a cart's tree by a path is the same reach as its specifier.
      ? relative(join(root, "packages"), resolve(dirname(file), fileName)).replaceAll("\\", "/").split("/")[0]
      : null;
    if (reach !== null ? !PACKAGE_DIRS.has(reach) : !PACKAGES.has(nameOf(fileName)) && !PACKAGES.has(fileName)) continue;
    hits.push({ file: relative(root, file).replaceAll("\\", "/"), specifier: fileName });
  }
}
console.log(JSON.stringify({ hits }));
