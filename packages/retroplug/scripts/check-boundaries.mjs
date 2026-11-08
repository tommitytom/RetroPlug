#!/usr/bin/env node
// The workspace packages' boundary check (docs/modular-graph-phase-f1-design.md section 4), the TS half: every package
// imports what it declares and nothing else, and no package imports the shell.
//
// What it judges: every workspace package that has a ts/ tree, which means every file under packages/<name>/ts, sources
// and tests alike. packages/retroplug (the app) and packages/native (the engine's own tree) are the shells: they may
// import anything and are not judged, and nothing may import them.
//
// Each file's imports come from TypeScript's own scanner (ts.preProcessFile), so every form a compiler follows is
// judged: a value import, an `import type`, an `export type ... from`, a type-position `import("x").T`, a `require`,
// and the two triple-slash directives a reference is (a `/// <reference path="...">` reaches a file as a relative
// import does, and a `/// <reference types="x">` names a type package as a bare import does, `@types/x` or `x`). A
// relative specifier is resolved against the importing file; a bare one is a package (nameOf) or a third-party module,
// judged against the package's manifest. Type-only imports count on purpose: a package's boundary is what it needs to
// compile on its own, not what survives a bundler.
//
//   1. a relative import (or reference path) that leaves the package's own tree (a ../../retroplug/... reach into the
//      shell, a ../other/ts/... reach into another package's files), or one that stays inside it and lands on a test
//      file while the importing file is not a test itself;
//   2. an @retroplug/<name> (or `reference types`) the package does not declare in its dependencies (a test file's
//      devDependencies count);
//   3. a third-party module (zod, anything under node_modules) the package does not declare; a subpath ("zod/v4") is
//      judged by its package ("zod");
//   4. a dependency on the shell, @retroplug/retroplug or @retroplug/native, imported or declared: no package may;
//   5. a cycle in the declared dependencies;
//   6. an #include in the package's native sources that climbs out of the package (a `..` segment, or an absolute path
//      into another package), quoted or angled;
//   7. a declared @retroplug dependency that names no package, a dependency whose SPEC names a package under another
//      key (`workspace:@retroplug/x@*`, `npm:@retroplug/x@1`, a `link:`/`file:`/`portal:` path into a package's
//      directory): the package it names is judged as rules 2 and 4 judge a key (the shell, a package the workspace does
//      not have), and an alias of a package that exists is refused, because a file imports the KEY and every other rule
//      here reads the key;
//   8. an `exports` entry that publishes a test module (a target, or a pattern's reach, that covers ts/test/ or a
//      *.test.ts), handing a test file's devDependencies to another package's source; one that reaches outside the
//      package's ts/ tree, which no check reads; or one naming no file, an entry still saying the package publishes a
//      module that moved away.
//
// Rule 1's second half and rule 8's first are one leak: a test file may use its package's devDependencies, so source
// that reaches a test file - by a relative import, or because the test file is published - hands those devDependencies
// to every importer of that source.
//
// What this guards is ACCIDENT AND DRIFT, NOT A DETERMINED AUTHOR. It reads the manifest and the compiler's scanner:
// it is a review's first pass, not a sandbox. Deliberately not covered, with the reason each is out of reach:
//
//   - a specifier a scanner cannot see: a computed `import(name)`, a `require` of a variable, an import a bundler
//     plugin or a loader adds. TypeScript's scanner lists literal specifiers, and that is all this reads.
//   - a module a tsconfig `paths` entry resolves, a package's `imports` map, or a symlink that moves a file at load
//     time: the relative-import rule is lexical, as every path judgement here is.
//   - a dependency declared under `peerDependencies` or `optionalDependencies`: only `dependencies` and
//     `devDependencies` are read (as the generator and the native check read them), so an import of one is refused as
//     undeclared rather than resolved through it.
//   - what an `exports` target means to Node: a condition's `require`/`import` split is read as its targets, not
//     resolved, and a package's `files`/`publishConfig` is nobody's boundary here.
//   - the ORDER a dependency's spec or an import reaches a package in, and anything a build's tooling decides: this
//     check judges the declaration, not the build.
//
// A declared dependency no file uses is a warning, not a refusal: the native check (cmake/packages.cmake) reads the same
// declaration, so a package whose native targets link a dependency's library declares it without its TS importing
// anything of it. A dependency's target named in the package's native/CMakeLists.txt counts as that use (a textual
// search: CMake is the truthful source, but this warning does not deserve the machinery to ask it), and so does a
// dependency the package's own scripts run - the `typescript` every package now declares, which its `typecheck` script
// runs as `tsc` and no file imports.
//
// packages/retroplug/src/generated is the shell's (the generator writes it from the manifests, packages.mjs), so a
// generated module is not judged either, and the shell is where a generated read lives.
//
//   node packages/retroplug/scripts/check-boundaries.mjs [--root <dir>] [--quiet]
//
// --root names a tree laid out as this one is (packages/<name>/package.json, packages/<name>/ts, packages/<name>/native),
// which is how the check's own tests drive it over the fixture packages that break one rule each (test/boundaries).
// The command line prints the exceptions' allowed lines, as the maintainer running it wants them; the test runner
// passes verbose only when RP_BOUNDARIES_VERBOSE asks, so `pnpm test` says what it found, not what it forgave.
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, isAbsolute, join, posix, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../..");
const PACKAGES = "packages";
// The shells, by package name: the app, whose own tree holds the generated registries and every module F1 has not moved
// yet, and the engine's tree (packages/native), which holds the node types no package owns. Both may import anything;
// neither may be imported.
const SHELLS = ["retroplug", "native"];

// TypeScript's scanner, resolved from the workspace root's `typescript-5` alias. The packages typecheck with TypeScript
// 7, which exports no `preProcessFile`: the scanner this check reads imports with is 5.7's, kept beside the typechecker
// as an alias rather than pinned into every package, and the alias is why "one TypeScript" stops at the typechecker.
const ts = createRequire(import.meta.url)("typescript-5");

/** Imports that break a rule and are allowed until the change that moves the code they reach. Each entry names the
 *  file, the specifier, the rule it breaks and what removes it; that change removes the entry beside the import, and an
 *  entry no refusal matches fails this check, so a fixed line cannot outlive its fix. Empty: nothing in the tree breaks
 *  a rule today. */
const EXCEPTIONS = [];

function readJson(path) {
  try {
    return JSON.parse(readFileSync(path, "utf8"));
  } catch (e) {
    throw new Error(`${path} cannot be read as JSON (${e.code ?? e.message})`);
  }
}

/** Every file under `dir` whose name `test` accepts, as absolute paths, in a stable order. */
function walk(dir, test, out = []) {
  if (!existsSync(dir)) return out;
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) walk(path, test, out);
    else if (test(entry.name)) out.push(path);
  }
  return out;
}

// TypeScript and the JavaScript a package may hold beside its tests (a runner a test spawns, as this repo's own
// test/boundaries/run-checker.mjs is). The fixtures' runners are outside packages/<name>/ts, so this judges a package's.
const isTypeScript = (name) => /\.(ts|tsx|mts|cts|js|jsx|mjs|cjs)$/.test(name);
const isNativeSource = (name) => /\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ipp)$/.test(name);

/** The package name a bare specifier names: the scoped pair ("@retroplug/core" -> "@retroplug/core") or the first
 *  segment ("zod/v4" -> "zod", "@types/node" -> "@types/node"). The one spelling every comparison uses, so a subpath
 *  is never a package of its own. */
function nameOf(specifier) {
  const parts = specifier.split("/");
  if (specifier.startsWith("@")) return parts.length > 1 ? `${parts[0]}/${parts[1]}` : specifier;
  return parts[0];
}

/** `@retroplug/<name>`'s short package name, or null for a third-party specifier. */
function packageOf(specifier) {
  const name = nameOf(specifier);
  return name.startsWith("@retroplug/") ? name.slice("@retroplug/".length) : null;
}

/** The workspace's packages, by name, in name order: the ones with a manifest under packages/. */
function workspace(root) {
  const base = join(root, PACKAGES);
  const packages = new Map();
  if (!existsSync(base)) return packages;
  for (const entry of readdirSync(base, { withFileTypes: true })) {
    const manifestPath = join(base, entry.name, "package.json");
    if (!entry.isDirectory() || !existsSync(manifestPath)) continue;
    const manifest = readJson(manifestPath);
    const native = manifest.retroplug?.native;
    const entries = native === undefined ? [] : Array.isArray(native) ? native : [native];
    packages.set(entry.name, {
      name: entry.name,
      dir: join(base, entry.name),
      ts: join(base, entry.name, "ts"),
      native: join(base, entry.name, "native"),
      manifestPath,
      manifest,
      // The manifest's names, as nameOf spells them: a declared "@retroplug/beta" is "beta"'s package, and a declared
      // "zod" is the package "zod/v4" names too.
      dependencies: Object.keys(manifest.dependencies ?? {}).map(nameOf),
      devDependencies: Object.keys(manifest.devDependencies ?? {}).map(nameOf),
      // The declarations as written, key and spec alike: a `workspace:` or `npm:` alias, or a `link:` into a package's
      // directory, names a package under a key that is not its name, which no rule reading the key can see.
      declarations: [
        ...Object.entries(manifest.dependencies ?? {}).map(([key, spec]) => ({ key, spec, dev: false })),
        ...Object.entries(manifest.devDependencies ?? {}).map(([key, spec]) => ({ key, spec, dev: true })),
      ],
      // The CMake targets its manifest names: one way a dependency counts as used.
      targets: entries.map((entry) => entry.target).filter((target) => typeof target === "string"),
    });
  }
  return new Map([...packages.entries()].sort(([a], [b]) => (a < b ? -1 : 1)));
}

const isShell = (name) => SHELLS.includes(name);
/** Whether a specifier is a module name rather than a path. */
const isBare = (specifier) => !(specifier.startsWith(".") || specifier.startsWith("/") || /^[A-Za-z]:[\\/]/.test(specifier));
/** A path as the reports name it: from the tree's root, in the build's slash spelling. */
const shown = (root, path) => relative(root, path).split("\\").join("/");

/** Whether `path` is `dir` itself or under it. Both are absolute. */
function under(dir, path) {
  const rel = relative(dir, path);
  return rel === "" || (!rel.startsWith("..") && !/^[A-Za-z]:/.test(rel));
}

/** The workspace package a resolved path belongs to, or null when it is outside every package (deps/, tools/). */
function ownerOf(packages, root, path) {
  const [first, name] = shown(root, path).split("/");
  if (first !== PACKAGES || name === undefined) return null;
  return packages.has(name) ? name : null;
}

/** How a report names a place an import reaches: the shell, a package, or nothing of the tree's. */
function describeOwner(name) {
  if (name === null) return "a file outside every package";
  return isShell(name) ? `the shell (packages/${name})` : `the ${name} package (packages/${name})`;
}

/** Whether a path under a package's directory names one of its test modules: under ts/test/, or named *.test.ts(x). */
function isTestPath(rel) {
  return rel.startsWith("ts/test/") || /\.test\.(ts|tsx)$/.test(rel);
}

/** A test file may use its package's devDependencies, so it is judged against a looser manifest than a source is. */
function isTestFile(pkg, file) {
  return isTestPath(relative(pkg.dir, file).split("\\").join("/"));
}

/** Whether an import of `name` (a nameOf name) by `file` is declared: dependencies everywhere, devDependencies in a
 *  test file. */
function declares(pkg, file, name) {
  if (pkg.dependencies.includes(name)) return true;
  return pkg.devDependencies.includes(name) && isTestFile(pkg, file);
}

/** The package a dependency's spec names, when its spec is not a plain version: `workspace:<name>@<range>` and
 *  `npm:<name>@<range>` are aliases (a key a file imports, standing for another package), and `link:`/`file:`/`portal:`
 *  name a directory, whose manifest's name is the package's. A plain version names the key's own package, which the
 *  rules reading the key already see, so it resolves to null. */
function aliasedPackage(pkg, key, spec) {
  const text = String(spec);
  const workspace = /^(?:workspace|npm):(.+)$/.exec(text);
  if (workspace) {
    const rest = workspace[1];
    const at = rest.lastIndexOf("@");
    // `workspace:*` and `workspace:^` carry no name: the key's own package, which the key names already.
    if (at === -1 && !rest.startsWith("@")) return null;
    return at > 0 ? rest.slice(0, at) : rest;
  }
  const path = /^(?:link|file|portal):(.+)$/.exec(text);
  if (!path) return null;
  const dir = resolve(pkg.dir, path[1]);
  const manifest = join(dir, "package.json");
  if (existsSync(manifest)) {
    const name = readJson(manifest).name;
    if (typeof name === "string") return name;
  }
  return dir.split(/[\\/]/).pop() ?? null;
}

// A relative import's target, tried the way TypeScript and esbuild resolve one: the specifier itself, then each
// extension, then each directory's index. A specifier that spells the module the compiler EMITS (a consumer kit's
// explicit `.js` specifiers, tsc's `moduleResolution: bundler`) names the source beside it: "./x.js" is x.ts or x.tsx,
// "./x.mjs" is x.mts, "./x.cjs" is x.cts, and "./x.jsx" is x.tsx. The counterpart is tried after the literal
// candidates, so a real `.js` file a specifier names is still what it resolves to.
const EXTENSIONS = [".ts", ".tsx", ".mts", ".cts", ".d.ts", ".js", ".jsx", ".mjs", ".cjs", ".json"];
const EMITTED_SOURCES = new Map([
  [".js", [".ts", ".tsx", ".d.ts"]],
  [".jsx", [".tsx"]],
  [".mjs", [".mts"]],
  [".cjs", [".cts"]],
]);

function resolveRelative(dir, specifier) {
  const base = resolve(dir, specifier);
  const candidates = [base, ...EXTENSIONS.map((ext) => base + ext)];
  for (const ext of EXTENSIONS) candidates.push(join(base, `index${ext}`));
  const emitted = EMITTED_SOURCES.get(posix.extname(specifier));
  if (emitted !== undefined) {
    const stem = base.slice(0, base.length - posix.extname(specifier).length);
    for (const ext of emitted) candidates.push(stem + ext);
  }
  return candidates.find((candidate) => existsSync(candidate)) ?? null;
}

/** The line and column of a scanner position, one-based, for the unresolvable report. */
function lineAndColumn(text, pos) {
  const before = text.slice(0, pos);
  const line = before.split("\n").length;
  const column = pos - before.lastIndexOf("\n");
  return { line, column };
}

/** Every import in a package's TS, from TypeScript's scanner: the specifier as written, the form it was written in
 *  (`import`, a `/// <reference path>`, or a `/// <reference types>`), and for a path the file it names (or null,
 *  reported as unresolvable). Type-only forms are included: the scanner lists an `import type`, an
 *  `export type ... from` and an `import("x").T` alike. A file that cannot be read is a problem of its own.
 *  The rest of the package is still judged when one import does not resolve. */
function importsOf(root, pkg, files, problems) {
  const imports = [];
  for (const file of files) {
    let text;
    try {
      text = readFileSync(file, "utf8");
    } catch (e) {
      problems.push({ file, message: `${shown(root, file)}: cannot be read (${e.code ?? e.message})` });
      continue;
    }
    const scanned = ts.preProcessFile(text, true, true);
    // A reference path is a relative reach like an import's, and the scanner returns it the same shape, so the two go
    // through one resolution; a `reference types` names a package the way a bare import does, which the judge handles.
    const found = [
      ...scanned.importedFiles.map((entry) => ({ ...entry, form: "import" })),
      ...scanned.referencedFiles.map((entry) => ({ ...entry, form: "reference" })),
      ...scanned.typeReferenceDirectives.map((entry) => ({ ...entry, form: "types" })),
    ];
    for (const entry of found) {
      const specifier = entry.fileName;
      if (entry.form === "types" || isBare(specifier)) {
        imports.push({ file, specifier, form: entry.form, target: null });
        continue;
      }
      const target = resolveRelative(dirname(file), specifier);
      if (target === null) {
        const { line, column } = lineAndColumn(text, entry.pos);
        problems.push({
          file,
          message: `${shown(root, file)}:${line}:${column}: cannot be resolved: ${specifier} names no file`,
        });
        continue;
      }
      imports.push({ file, specifier, form: entry.form, target });
    }
  }
  return imports;
}

const escapeRegExp = (text) => text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

/** A manifest's `exports` as its entries: the key ("." for the string form) and every target string it maps to, so a
 *  conditional export's members are judged too. A pattern target ("./ts/*.ts") keeps its `*`: Node lets it stand for
 *  any characters, `/` included, so the entry publishes a subtree, and rule 8 judges that subtree. */
function exportTargets(manifest) {
  const exports = manifest.exports;
  if (typeof exports === "string") return [{ key: ".", target: exports }];
  if (exports === null || typeof exports !== "object") return [];
  const out = [];
  for (const [key, value] of Object.entries(exports)) {
    if (typeof value === "string") out.push({ key, target: value });
    else if (value !== null && typeof value === "object") {
      for (const [condition, target] of Object.entries(value)) {
        if (typeof target === "string") out.push({ key: `${key} (${condition})`, target });
      }
    }
  }
  return out;
}

/** The cycles in the packages' declared `dependencies`, each as the package names in order. A devDependency edge does
 *  not order anything (the generator's product order reads `dependencies` alone, and pnpm allows a dev cycle), so it
 *  takes no part here. */
function cycles(packages) {
  const edges = new Map();
  for (const pkg of packages.values()) {
    edges.set(
      pkg.name,
      pkg.dependencies.map(packageOf).filter((name) => name !== null && packages.has(name)),
    );
  }
  const found = [];
  const state = new Map(); // name -> "open" | "done"
  const stack = [];
  const visit = (name) => {
    if (state.get(name) === "done") return;
    if (state.get(name) === "open") {
      found.push(stack.slice(stack.indexOf(name)));
      return;
    }
    state.set(name, "open");
    stack.push(name);
    for (const next of edges.get(name) ?? []) visit(next);
    stack.pop();
    state.set(name, "done");
  };
  for (const name of edges.keys()) visit(name);
  return found;
}

/** The check, over one tree: the refusals (each with the rule it broke and its message, which names the file, the import
 *  and the owner), the warnings, the exceptions that matched a refusal, and the exception lines nothing matches. */
export function checkBoundaries({ root = REPO } = {}) {
  const packages = workspace(root);
  const refusals = [];
  const warnings = [];
  const used = new Map(); // a package's name -> the dependency names some file of it (or a native target) uses
  // The exceptions are this tree's: a fixture tree has its own rules to break, and one of our stale entries would fail
  // every fixture run for a reason that is not the fixture's.
  const exceptions = root === REPO ? EXCEPTIONS : [];
  const refuse = (rule, file, specifier, message) => refusals.push({ rule, file, import: specifier, message });
  const markUsed = (pkg, name) => {
    if (!used.has(pkg.name)) used.set(pkg.name, new Set());
    used.get(pkg.name).add(name);
  };

  for (const pkg of packages.values()) {
    for (const { key, spec } of pkg.declarations) {
      // A `workspace:`/`npm:` alias or a `link:` into a package is a package reached under another name: the shell and
      // missing-package rules read what the spec names, and an alias of a package that exists is refused, because every
      // other rule here (and the generator's product order) reads the KEY, which would hide the dependency.
      const target = aliasedPackage(pkg, key, spec);
      const aliased = target === null ? null : packageOf(target);
      if (aliased !== null) {
        if (isShell(aliased)) {
          refuse(
            "shell",
            shown(root, pkg.manifestPath),
            key,
            `${shown(root, pkg.manifestPath)}: declares ${key} as ${JSON.stringify(spec)}, which is ` +
              `${describeOwner(aliased)}: no package may depend on the shell`,
          );
          continue;
        }
        if (!packages.has(aliased)) {
          refuse(
            "declaration",
            shown(root, pkg.manifestPath),
            key,
            `${shown(root, pkg.manifestPath)}: declares ${key} as ${JSON.stringify(spec)}, which names ` +
              `@retroplug/${aliased}, not a package in ${PACKAGES}`,
          );
          continue;
        }
        if (nameOf(key) !== target) {
          refuse(
            "alias",
            shown(root, pkg.manifestPath),
            key,
            `${shown(root, pkg.manifestPath)}: declares ${key} as ${JSON.stringify(spec)}, an alias of ` +
              `${target}: a file imports "${key}", which every rule here reads as another package, so the dependency ` +
              "it reaches and the order the product lists it in would both be hidden",
          );
          continue;
        }
      }
      const name = nameOf(key);
      // Rule 4, the declared half: the shell is no package a product lists, so no package may name it.
      if (isShell(packageOf(name))) {
        refuse(
          "shell",
          shown(root, pkg.manifestPath),
          name,
          `${shown(root, pkg.manifestPath)}: declares ${name} in its dependencies, which is ` +
            `${describeOwner(packageOf(name))}: no package may depend on the shell`,
        );
      } else if (packageOf(name) !== null && !packages.has(packageOf(name))) {
        refuse(
          "declaration",
          shown(root, pkg.manifestPath),
          name,
          `${shown(root, pkg.manifestPath)}: declares ${name}, which is not a package in ${PACKAGES}`,
        );
      }
    }
    if (isShell(pkg.name)) continue;

    // Rule 8: what a package publishes. The checks read ts/, so an export outside it publishes code no check judges;
    // an export that reaches a test module hands it to other packages, and a test file may import devDependencies,
    // which a source of another package reaching through the export then gets transitively. A pattern entry is judged
    // by every path its `*` can name rather than by the literal it spells: "./ts/*.ts" publishes
    // ts/test/anything.ts as surely as the file it was written for. The comparisons are lexical, as every path
    // judgement here is, so a symlinked checkout judges the same tree.
    for (const { key, target } of exportTargets(pkg.manifest)) {
      const rel = target.replace(/^\.\//, "");
      const star = rel.indexOf("*");
      const isPattern = star !== -1;
      // The fixed part of the spelling: the tree a pattern's `*` may wander from, and the whole file a literal names.
      const prefix = posix.normalize(isPattern ? rel.slice(0, star) : rel);
      // Whether the entry can hand `probe` - one of the paths it could name - to a consumer.
      const canName = isPattern
        ? (probe) => new RegExp(`^${rel.split("*").map(escapeRegExp).join(".*")}$`).test(probe)
        : (probe) => prefix === probe;
      if (!prefix.startsWith("ts/")) {
        refuse(
          "exports",
          shown(root, pkg.manifestPath),
          target,
          `${shown(root, pkg.manifestPath)}: exports "${key}" at ${target}, which ${
            isPattern ? "can name files" : "is"
          } outside packages/${pkg.name}/ts, so no check reads the code it publishes`,
        );
      } else if (isTestPath(prefix) || canName("ts/test/probe.ts") || canName("ts/probe.test.ts")) {
        refuse(
          "exports",
          shown(root, pkg.manifestPath),
          target,
          `${shown(root, pkg.manifestPath)}: exports "${key}" at ${target}, which ${
            isPattern ? "can name a test module" : "is a test module"
          }: a test file may use devDependencies, so publishing one lets another package's source reach them through ` +
            "it",
        );
      } else if (!isPattern && !existsSync(join(pkg.dir, target))) {
        refuse(
          "exports",
          shown(root, pkg.manifestPath),
          target,
          `${shown(root, pkg.manifestPath)}: exports "${key}" at ${target}, which names no file: the module moved or ` +
            "was never there, and the entry is the one thing still saying the package publishes it",
        );
      }
    }

    // Rules 1 to 4, the import half.
    const problems = [];
    const imports = importsOf(root, pkg, walk(pkg.ts, isTypeScript), problems);
    for (const problem of problems) refuse("unresolvable", shown(root, problem.file), "", problem.message);
    for (const { file, specifier, form, target } of imports) {
      if (form === "types") {
        // A `/// <reference types="x">` names the type package `@types/x` (or, less usually, `x` itself), so it is a
        // bare import's declaration rule. Either spelling in the manifest satisfies it, and the one that does is the
        // declaration the unused warning sees used.
        const name = specifier;
        const typesName = `@types/${name}`;
        const declared = declares(pkg, file, typesName) ? typesName : declares(pkg, file, name) ? name : null;
        if (declared !== null) {
          markUsed(pkg, declared);
          continue;
        }
        refuse(
          "third-party",
          shown(root, file),
          specifier,
          `${shown(root, file)}: a /// <reference types="${specifier}"> names ${typesName}, which ${pkg.name} does ` +
            "not declare in its dependencies (its tests may also use devDependencies)",
        );
        continue;
      }
      if (!isBare(specifier)) {
        // A relative import (or reference path), resolved against the importing file: it must stay in the package's
        // own tree, so a package reaches another as a declared @retroplug/<name> and the shell not at all.
        if (target !== null && under(pkg.ts, target)) {
          // Staying inside the package is not the whole rule: a test file may use the package's devDependencies, so
          // source that imports one hands them to every importer of the source, by an edge no manifest declares.
          if (!isTestFile(pkg, file) && isTestFile(pkg, target)) {
            refuse(
              "test-module",
              shown(root, file),
              specifier,
              `${shown(root, file)}: ${
                form === "reference" ? `a /// <reference path> "${specifier}"` : `imports "${specifier}"`
              }, which is a test module of its own package, from a file that is not a test: a test file may use the ` +
                "package's devDependencies, so importing one from source hands them to every importer of the source",
            );
          }
          continue;
        }
        refuse(
          "relative",
          shown(root, file),
          specifier,
          `${shown(root, file)}: ${form === "reference" ? `a /// <reference path> "${specifier}"` : `imports "${specifier}"`}, ` +
            `which resolves into ${describeOwner(ownerOf(packages, root, target ?? ""))}, outside ` +
            `${shown(root, pkg.ts)}: a package reaches another as @retroplug/<name>; the shell not at all`,
        );
        continue;
      }
      if (specifier.startsWith("node:")) continue;
      const name = nameOf(specifier);
      markUsed(pkg, name);
      const owner = packageOf(specifier);
      if (owner === null) {
        if (!declares(pkg, file, name)) {
          refuse(
            "third-party",
            shown(root, file),
            specifier,
            `${shown(root, file)}: imports "${specifier}", a third-party module, which ${pkg.name} does not ` +
              "declare in its dependencies (its tests may also use devDependencies)",
          );
        }
        continue;
      }
      if (owner === pkg.name) continue; // a package's own name is its to import
      if (isShell(owner)) {
        refuse(
          "shell",
          shown(root, file),
          specifier,
          `${shown(root, file)}: imports "${specifier}", which is ${describeOwner(owner)}: no package may depend on ` +
            "the shell",
        );
        continue;
      }
      if (!declares(pkg, file, name)) {
        refuse(
          "package",
          shown(root, file),
          specifier,
          `${shown(root, file)}: imports "${specifier}", which is ${describeOwner(owner)}, and ${pkg.name} does not ` +
            "declare it in its dependencies (its tests may also use devDependencies)",
        );
      }
    }

    // Rule 6, the native half: an #include that climbs out of the package, quoted or angled - one with a `..` segment,
    // or an absolute path, which is how a tree could be named without climbing syntactically. A bare include name is an
    // include directory's (node-sdk's graph/node.hpp), which the configure check rules on. Every native source under the
    // package counts, not only native/'s: a tool's tree (graph-runtime's export-plan, its device check) is the package's
    // too.
    for (const file of walk(pkg.dir, isNativeSource)) {
      const text = readFileSync(file, "utf8");
      for (const match of text.matchAll(/^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)/gm)) {
        const specifier = match[1] ?? match[2];
        const absolute = isAbsolute(specifier);
        if (!absolute && !specifier.split(/[\\/]/).includes("..")) continue;
        const target = resolve(dirname(file), specifier);
        if (under(pkg.dir, target)) continue;
        // A path outside every package is nobody's (a vendored dep's header, the C library's): only a reach into another
        // package or a shell is this rule's to refuse.
        const owner = ownerOf(packages, root, target);
        if (absolute && owner === null) continue;
        refuse(
          "include",
          shown(root, file),
          specifier,
          `${shown(root, file)}: includes "${specifier}", which resolves into ` +
            `${describeOwner(owner)}, outside packages/${pkg.name}: a package's native sources include their own by ` +
            "path",
        );
      }
    }

    // A dependency the package's native tree links counts as used: one of its CMakeLists names one of the dependency's
    // targets, and the configure check reads the same declaration for the same reason. A devDependency too: a test
    // binary links @retroplug/testing's helper library, which no TypeScript file of it imports. Every CMakeLists under
    // the package counts (a tool in a subdirectory is the package's too). The search is textual, so the comments are
    // stripped first: a path a comment mentions is no reach, which is why what DECIDES a reach is the configure check
    // (it reads the target's own properties). This one only attributes a declaration to a use, and a use to a warning.
    const text = walk(pkg.dir, (name) => name === "CMakeLists.txt")
      .map((file) => readFileSync(file, "utf8"))
      .join("\n")
      .replace(/#[^\n]*/g, "");
    for (const name of [...pkg.dependencies, ...pkg.devDependencies]) {
      const short = packageOf(name);
      const targets = short === null ? [] : (packages.get(short)?.targets ?? []);
      if (targets.some((target) => new RegExp(`(^|[^A-Za-z0-9_])${target}([^A-Za-z0-9_]|$)`, "m").test(text))) {
        markUsed(pkg, name);
        continue;
      }
      // Or names a file in its tree, which a test target's DEFS can: the Game Boy's test binary opens the ROM the
      // mgb package embeds, by the path its manifest gives, and links no target of it.
      if (short !== null && text.includes(`packages/${short}/`)) markUsed(pkg, name);
    }
  }

  // Rule 5: a cycle in the declared dependencies, over every package (the shell's declarations can close a loop too).
  for (const cycle of cycles(packages)) {
    const path = [...cycle, cycle[0]].join(" -> ");
    refuse(
      "cycle",
      `${PACKAGES}/${cycle[0]}/package.json`,
      "",
      `the packages' declared dependencies are a cycle: ${path}`,
    );
  }

  // The warnings: a declared dependency nothing uses.
  for (const pkg of packages.values()) {
    if (isShell(pkg.name)) continue;
    // The compiler a package's `typecheck` script runs is that script's use of it: one TypeScript checks the whole
    // workspace, and a package declares it so `pnpm -r typecheck` runs it from the install the shell declares.
    const runsTsc = Object.values(pkg.manifest.scripts ?? {}).some((script) =>
      /(^|\s)tsc(\s|$)/.test(String(script)),
    );
    for (const name of [...pkg.dependencies, ...pkg.devDependencies]) {
      if (name === "typescript" && runsTsc) continue;
      if (!used.get(pkg.name)?.has(name)) {
        warnings.push(`${pkg.name}: declares ${name}, which no file of its imports and no native target of it names`);
      }
    }
  }

  // The exceptions: a refusal an entry names is allowed, and an entry nothing matches is itself a failure (what removed
  // it has taken the import away, so its line must go). An entry names the rule it allows, so it cannot allow the same
  // line for a different reason.
  const allowed = [];
  const spent = new Set();
  const allowedRefusals = new Set();
  for (const refusal of refusals) {
    const at = exceptions.findIndex(
      (exception, index) =>
        !spent.has(index) &&
        exception.file === refusal.file &&
        exception.import === refusal.import &&
        exception.rule === refusal.rule,
    );
    if (at === -1) continue;
    spent.add(at);
    allowedRefusals.add(refusal);
    allowed.push({ ...refusal, removedBy: exceptions[at].removedBy });
  }
  const stale = exceptions.filter((_, index) => !spent.has(index)).map(
    (exception) =>
      `the exception for ${exception.file}'s "${exception.import}" matches nothing: ${exception.removedBy} removed it, ` +
      "so delete the entry",
  );

  return { refusals: refusals.filter((refusal) => !allowedRefusals.has(refusal)), allowed, warnings, stale };
}

/** The check, printed as the command line shows it: what the exceptions allow (when `verbose`), the warnings, the stale
 *  exception lines and the refusals. Returns the failure count, so a caller (the test runner) fails on it without
 *  re-reading the report. */
export function reportBoundaries({ root = REPO, verbose = false, log = console.log, error = console.error } = {}) {
  const report = checkBoundaries({ root });
  if (verbose) {
    for (const allowed of report.allowed) log(`allowed: ${allowed.message} [${allowed.removedBy}]`);
  }
  for (const warning of report.warnings) log(`warning: ${warning}`);
  for (const stale of report.stale) error(`stale: check-boundaries: ${stale}`);
  for (const refusal of report.refusals) error(`refused: ${refusal.message}`);
  if (report.refusals.length || report.stale.length) {
    error(
      `check-boundaries: ${report.refusals.length} refusal(s), ${report.stale.length} stale exception(s) under ` +
        `${shown(REPO, root) || "."}`,
    );
    return report.refusals.length + report.stale.length;
  }
  log(
    `check-boundaries: the packages hold their boundaries (${report.allowed.length} allowed by EXCEPTIONS, ` +
      `${report.warnings.length} warning(s))`,
  );
  return 0;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const at = process.argv.indexOf("--root");
  const root = at === -1 ? REPO : resolve(process.argv[at + 1] ?? "");
  const verbose = !process.argv.includes("--quiet");
  let failed;
  try {
    failed = reportBoundaries({ root, verbose });
  } catch (e) {
    console.error(`check-boundaries: ${e.message}`);
    process.exit(2);
  }
  if (failed) process.exit(1);
}
