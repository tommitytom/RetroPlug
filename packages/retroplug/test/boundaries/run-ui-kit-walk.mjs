#!/usr/bin/env node
// Drives the kit-move walk for test/boundaries/ui-kit-imports.test.ts: the shell's React widgets live in
// @retroplug/ui-react, the kit's index re-exports every one of them, and no file under packages/retroplug/ui still
// reaches a moved module at its old path.
//
// The walk is the compiler's, as check-boundaries.mjs's is: TypeScript's scanner (ts.preProcessFile) finds every
// specifier a compiler follows, so an old path is caught however it is spelled (a value import, a type import, an
// `export ... from`, a type-position `import("x")`), and its parser names what each import declaration binds, so the
// kit's index is checked against the names the shell actually imports. Nothing here greps for a path, which would match
// the comments that still describe the move.
//
//   node test/boundaries/run-ui-kit-walk.mjs [--root <dir>]
//
// Prints one JSON line: { kitIndex, movedModules, kitModules, unexported, oldPathImports, foreignBindings, kitBindings },
// where a module is a path from the root.
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const at = process.argv.indexOf("--root");
const root = at === -1 ? REPO : resolve(process.argv[at + 1] ?? "");

// TypeScript's scanner and parser, resolved from the workspace root's `typescript-5` alias (the typecheck uses 7,
// which exports no scanner). This is boundaried code, so a missing alias is a broken checkout, not a case.
const ts = createRequire(import.meta.url)("typescript-5");

const KIT = "packages/ui-react/ts/ui";
const KIT_INDEX = `${KIT}/index.ts`;
const SHELL_UI = "packages/retroplug/ui";

/** The modules that moved into the kit, as the shell had them: a relative import landing on one of these is the reach
 *  the move removed. */
const MOVED = [
  "packages/retroplug/ui/lvgl/lv.ts",
  "packages/retroplug/ui/lvgl/Box.tsx",
  "packages/retroplug/ui/lvgl/PixelCanvas.tsx",
  "packages/retroplug/ui/lvgl/useNativeEvent.ts",
  "packages/retroplug/ui/lvgl/useFramePoll.ts",
  "packages/retroplug/ui/hooks/useRerender.ts",
  "packages/retroplug/ui/screens/grid/DebugReadout.tsx",
];

const shown = (path) => relative(root, path).split("\\").join("/");

function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const entry of readdirSync(dir).sort()) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) {
      // The generated menu table names every package, so it is no shell file a module is moved out of.
      if (entry !== "generated") walk(path, out);
    } else if (/\.(ts|tsx)$/.test(entry)) {
      out.push(path);
    }
  }
  return out;
}

function parse(file) {
  return ts.createSourceFile(file, readFileSync(file, "utf8"), ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
}

/** Every specifier a compiler follows in `file`, whatever form it is written in. */
function scannedSpecifiers(file) {
  return ts.preProcessFile(readFileSync(file, "utf8"), true, true).importedFiles.map((entry) => entry.fileName);
}

/** Every import of `source`: its specifier and the names it binds (the default import as "default", a namespace as
 *  "*"). */
function imports(source) {
  const out = [];
  for (const statement of source.statements) {
    if (!ts.isImportDeclaration(statement) || !ts.isStringLiteral(statement.moduleSpecifier)) continue;
    const specifier = statement.moduleSpecifier.text;
    const names = [];
    const clause = statement.importClause;
    if (clause?.name) names.push(clause.name.text);
    const bindings = clause?.namedBindings;
    if (bindings) {
      if (ts.isNamespaceImport(bindings)) names.push("*");
      else for (const element of bindings.elements) names.push((element.propertyName ?? element.name).text);
    }
    out.push({ specifier, names });
  }
  return out;
}

/** The names a module's index exports, from its export declarations: the clause's names, and a re-exported module's
 *  own names through `export *`. */
function exportsOf(source, file, root, seen = new Set()) {
  const names = new Set();
  const modules = [];
  for (const statement of source.statements) {
    if (!ts.isExportDeclaration(statement)) continue;
    const target = statement.moduleSpecifier && ts.isStringLiteral(statement.moduleSpecifier)
      ? statement.moduleSpecifier.text
      : null;
    if (target !== null) modules.push(target);
    if (!statement.exportClause) {
      // `export * from "./x"`: the names are that module's, resolved one level (the kit's index spells its own out).
      const resolved = target === null ? null : resolveFile(dirname(file), target);
      if (resolved === null || seen.has(resolved)) continue;
      seen.add(resolved);
      for (const name of exportsOf(parse(resolved), resolved, root, seen)) names.add(name);
    } else if (ts.isNamedExports(statement.exportClause)) {
      for (const element of statement.exportClause.elements) names.add(element.name.text);
    }
  }
  return names;
}

const EXTENSIONS = [".ts", ".tsx", ".d.ts", ".js", ".jsx", ".mjs", ".cjs"];

/** A specifier's file, resolved as a compiler resolves one: the path, then each extension, then each directory index. */
function resolveFile(fromDir, specifier) {
  const base = resolve(fromDir, specifier);
  return [base, ...EXTENSIONS.map((e) => base + e), ...EXTENSIONS.map((e) => join(base, `index${e}`))].find((path) =>
    existsSync(path),
  ) ?? null;
}

/** The kit's modules: every file of its ts/ui tree but the index, which is the one that exports them. */
const kitIndex = join(root, KIT_INDEX);
const kitFiles = walk(join(root, KIT));
const kit = existsSync(kitIndex) ? parse(kitIndex) : null;
const kitExports = kit === null ? new Set() : exportsOf(kit, kitIndex, root);
const reexported = new Set();
if (kit !== null) {
  for (const statement of kit.statements) {
    if (!ts.isExportDeclaration(statement) || !statement.moduleSpecifier) continue;
    if (!ts.isStringLiteral(statement.moduleSpecifier)) continue;
    const resolved = resolveFile(dirname(kitIndex), statement.moduleSpecifier.text);
    if (resolved !== null) reexported.add(resolved);
  }
}

const unexported = kitFiles.filter((file) => file !== kitIndex && !reexported.has(file)).map(shown);

const oldPaths = new Set(MOVED.map((path) => join(root, path)));
const oldPathImports = [];
const foreignBindings = [];
let kitBindings = 0;
for (const file of walk(join(root, SHELL_UI))) {
  // The scanner's specifiers, not the parser's: an `export { Box } from "../lvgl/Box"` reaches an old path exactly as an
  // import does, and only the scanner sees it.
  for (const specifier of scannedSpecifiers(file)) {
    if (!specifier.startsWith(".")) continue;
    const landed = resolveFile(dirname(file), specifier);
    if (landed !== null && oldPaths.has(landed)) {
      oldPathImports.push({ file: shown(file), specifier });
      continue;
    }
    // A specifier that lands on no file but spells a moved module's own name is the same reach, told by the path it
    // would resolve at rather than by a file that still exists.
    const spelled = [...oldPaths].find((path) => path.replace(/\.tsx?$/, "") === resolve(dirname(file), specifier));
    if (landed === null && spelled !== undefined) oldPathImports.push({ file: shown(file), specifier });
  }
  for (const { specifier, names } of imports(parse(file))) {
    if (specifier !== "@retroplug/ui-react") continue;
    for (const name of names) {
      if (name === "*") continue;
      kitBindings += 1;
      if (!kitExports.has(name)) foreignBindings.push({ file: shown(file), specifier, name });
    }
  }
}

console.log(
  JSON.stringify({
    kitIndex: existsSync(kitIndex) ? KIT_INDEX : null,
    movedModules: MOVED,
    kitModules: kitFiles.filter((file) => file !== kitIndex).map(shown),
    unexported,
    oldPathImports,
    foreignBindings,
    kitBindings,
  }),
);
