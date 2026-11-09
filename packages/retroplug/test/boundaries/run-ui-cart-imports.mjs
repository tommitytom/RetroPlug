#!/usr/bin/env node
// Walks the app's ui/ tree for an import of a cart, console or device package, for
// test/boundaries/ui-cart-imports.test.ts (docs/modular-graph-phase-f4b-design.md section 7.3).
//
//   node test/boundaries/run-ui-cart-imports.mjs [--packages <name,...>] [--contracts <specifier,...>] [--root <dir>]
//
// The imports come from the boundary check's own scanner (TypeScript's preProcessFile), so a type-only import, an
// `export ... from` and a `require` all count: ui/ may carry a cart's code only through ui/generated/menus.ts, which is
// not judged (it is the one place that names every package), and a device pack's UI module MAY be reached through the
// generated list, never by a view directly. A RELATIVE import that lands in such a package's tree counts too, so
// reaching one by a path rather than a specifier is the same reach. Prints one JSON line:
// { hits: [{ file specifier }] }, file relative to the repository root.
//
// The package list is DERIVED from the workspace manifests unless --packages names one (a fixture): a cart, a console
// and a device pack each has a control-plane app module (`retroplug.app`), which is the code a view may not name, so a
// package added to the product cannot be left off a hand-kept list. The shell is the one exclusion: its own app module
// is what composes the UI, not a cart's.
import { readdirSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ts = createRequire(import.meta.url)("typescript-5");
const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");

function option(name) {
  const at = process.argv.indexOf(name);
  return at === -1 ? undefined : process.argv[at + 1];
}

const packages = option("--packages");
const contracts = option("--contracts");
const root = resolve(option("--root") ?? REPO);

/** Every workspace package with a control-plane app module but the shell: a cart's, a console's or a device pack's
 *  code, which is what a view may not name. */
function derivedPackages() {
  const names = [];
  for (const entry of readdirSync(join(root, "packages"), { withFileTypes: true })) {
    if (!entry.isDirectory() || entry.name === "retroplug") continue;
    try {
      const manifest = JSON.parse(readFileSync(join(root, "packages", entry.name, "package.json"), "utf8"));
      if (manifest.retroplug?.app) names.push(manifest.name);
    } catch {
      // A directory with no manifest of its own is not a package.
    }
  }
  return names;
}

const PACKAGES = new Set(packages ? packages.split(",").filter((entry) => entry !== "") : derivedPackages());
const CONTRACTS = new Set((contracts ?? "").split(",").filter((entry) => entry !== ""));
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
