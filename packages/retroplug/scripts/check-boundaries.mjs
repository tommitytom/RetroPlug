#!/usr/bin/env node
// The workspace packages' boundary check (docs/modular-graph-phase-f1-design.md section 4), the TS half: every package
// imports what it declares and nothing else, and no package imports the shell.
//
// What it judges: every workspace package that has a ts/ tree, which means every file under packages/<name>/ts, sources
// and tests alike. packages/retroplug (the app) and packages/native (the engine's own tree) are the shells: they may
// import anything and are not judged, and nothing may import them.
//
// Each file's imports come from TypeScript's own scanner (ts.preProcessFile), so every form a compiler follows is
// judged: a value import, an `import type`, an `export type ... from`, a type-position `import("x").T` and a
// `require`. A relative specifier is resolved against the importing file; a bare one is a package (nameOf) or a
// third-party module, judged against the package's manifest. Type-only imports count on purpose: a package's boundary
// is what it needs to compile on its own, not what survives a bundler.
//
//   1. a relative import that leaves the package's own tree (a ../../retroplug/... reach into the shell, a
//      ../other/ts/... reach into another package's files);
//   2. an @retroplug/<name> the package does not declare in its dependencies (a test file's devDependencies count);
//   3. a third-party module (zod, anything under node_modules) the package does not declare; a subpath ("zod/v4") is
//      judged by its package ("zod");
//   4. a dependency on the shell, @retroplug/retroplug or @retroplug/native, imported or declared: no package may;
//   5. a cycle in the declared dependencies;
//   6. an #include in the package's native sources that has a `..` segment and climbs out of the package, quoted or
//      angled, or a declared @retroplug dependency that names no package.
//
// A declared dependency no file uses is a warning, not a refusal: the native check (cmake/packages.cmake) reads the same
// declaration, so a package whose native targets link a dependency's library declares it without its TS importing
// anything of it. A dependency's target named in the package's native/CMakeLists.txt counts as that use (a textual
// search: CMake is the truthful source, but this warning does not deserve the machinery to ask it).
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
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../..");
const PACKAGES = "packages";
// The shells, by package name: the app, whose own tree holds the generated registries and every module F1 has not moved
// yet, and the engine's tree (packages/native), which holds the node types no package owns. Both may import anything;
// neither may be imported.
const SHELLS = ["retroplug", "native"];

// TypeScript's scanner, resolved from the workspace root's devDependency. The package's own `typescript` (7) works too,
// but its API is the one the typechecker runs, so the check reads imports with the compiler that will see them.
const ts = createRequire(import.meta.url)("typescript-5");

/** Imports that break a rule today and are allowed until the stage that moves the code they reach. Each entry names the
 *  file, the specifier, the rule it breaks and the stage that deletes it; the stage deletes the entry in the same
 *  change as the import, and an entry no refusal matches fails this check, so a fixed line cannot outlive its fix. */
const EXCEPTIONS = [
  // Stage 3 carves core's TS half (defineNode, settingsSchema, script/register) into @retroplug/core.
  {
    file: "packages/bliptoaster/ts/index.ts",
    import: "../../retroplug/src/graph/defineNode",
    rule: "relative",
    removedBy: "stage 3 (core's TS half)",
  },
  {
    file: "packages/bliptoaster/ts/index.ts",
    import: "../../retroplug/src/script/register",
    rule: "relative",
    removedBy: "stage 3 (core's TS half)",
  },
  {
    file: "packages/devtest/ts/index.ts",
    import: "../../retroplug/src/graph/defineNode",
    rule: "relative",
    removedBy: "stage 3 (core's TS half)",
  },
  {
    file: "packages/devtest/ts/index.ts",
    import: "../../retroplug/src/graph/settingsSchema",
    rule: "relative",
    removedBy: "stage 3 (core's TS half)",
  },
  {
    file: "packages/devtest/ts/index.ts",
    import: "../../retroplug/src/script/register",
    rule: "relative",
    removedBy: "stage 3 (core's TS half)",
  },
  // Stage 3 makes packages/retroplug/testing a package of its own, @retroplug/testing.
  ...["link-client", "mapConfig", "menuImage", "saveState", "session-client", "sniffer", "sysInfo"].map((name) => ({
    file: `packages/n8/ts/test/${name}.test.ts`,
    import: "../../../retroplug/testing/harness",
    rule: "relative",
    removedBy: "stage 3 (@retroplug/testing)",
  })),
];

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
      // The manifest's names, as nameOf spells them: a declared "@retroplug/beta" is "beta"'s package, and a declared
      // "zod" is the package "zod/v4" names too.
      dependencies: Object.keys(manifest.dependencies ?? {}).map(nameOf),
      devDependencies: Object.keys(manifest.devDependencies ?? {}).map(nameOf),
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

/** A test file may use its package's devDependencies: a file under ts/test, or one named *.test.ts(x). */
function isTestFile(pkg, file) {
  const rel = relative(pkg.dir, file).split("\\").join("/");
  return rel.startsWith("ts/test/") || /\.test\.(ts|tsx)$/.test(rel);
}

/** Whether an import of `name` (a nameOf name) by `file` is declared: dependencies everywhere, devDependencies in a
 *  test file. */
function declares(pkg, file, name) {
  if (pkg.dependencies.includes(name)) return true;
  return pkg.devDependencies.includes(name) && isTestFile(pkg, file);
}

// A relative import's target, tried the way TypeScript and esbuild resolve one: the specifier itself, then each
// extension, then each directory's index. A `.js` specifier that names a `.ts` source resolves, as the bundlers do.
const EXTENSIONS = [".ts", ".tsx", ".mts", ".cts", ".d.ts", ".js", ".jsx", ".mjs", ".cjs", ".json"];

function resolveRelative(dir, specifier) {
  const base = resolve(dir, specifier);
  const candidates = [base, ...EXTENSIONS.map((ext) => base + ext)];
  for (const ext of EXTENSIONS) candidates.push(join(base, `index${ext}`));
  return candidates.find((candidate) => existsSync(candidate)) ?? null;
}

/** The line and column of a scanner position, one-based, for the unresolvable report. */
function lineAndColumn(text, pos) {
  const before = text.slice(0, pos);
  const line = before.split("\n").length;
  const column = pos - before.lastIndexOf("\n");
  return { line, column };
}

/** Every import in a package's TS, from TypeScript's scanner: the specifier as written, and for a relative one the
 *  file it names (or null, reported as unresolvable). Type-only forms are included: the scanner lists an `import type`,
 *  an `export type ... from` and an `import("x").T` alike. A file that cannot be read is a problem of its own.
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
    for (const found of scanned.importedFiles) {
      const specifier = found.fileName;
      if (isBare(specifier)) {
        imports.push({ file, specifier, target: null });
        continue;
      }
      const target = resolveRelative(dirname(file), specifier);
      if (target === null) {
        const { line, column } = lineAndColumn(text, found.pos);
        problems.push({
          file,
          message: `${shown(root, file)}:${line}:${column}: cannot be resolved: ${specifier} names no file`,
        });
        continue;
      }
      imports.push({ file, specifier, target });
    }
  }
  return imports;
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
    for (const name of [...pkg.dependencies, ...pkg.devDependencies]) {
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

    // Rules 1 to 4, the import half.
    const problems = [];
    const imports = importsOf(root, pkg, walk(pkg.ts, isTypeScript), problems);
    for (const problem of problems) refuse("unresolvable", shown(root, problem.file), "", problem.message);
    for (const { file, specifier, target } of imports) {
      if (!isBare(specifier)) {
        // A relative import, resolved against the importing file: it must stay in the package's own tree, so a package
        // reaches another as a declared @retroplug/<name> and the shell not at all.
        if (target !== null && under(pkg.ts, target)) continue;
        refuse(
          "relative",
          shown(root, file),
          specifier,
          `${shown(root, file)}: imports "${specifier}", which resolves into ` +
            `${describeOwner(ownerOf(packages, root, target ?? ""))}, outside ${shown(root, pkg.ts)}: a package ` +
            "reaches another as @retroplug/<name>, and the shell not at all",
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

    // Rule 6, the native half: an #include with a `..` segment that climbs out of the package, quoted or angled. A bare
    // include name is an include directory's (node-sdk's graph/node.hpp), which the configure check rules on.
    for (const file of walk(pkg.native, isNativeSource)) {
      const text = readFileSync(file, "utf8");
      for (const match of text.matchAll(/^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)/gm)) {
        const specifier = match[1] ?? match[2];
        if (!specifier.split(/[\\/]/).includes("..")) continue;
        const target = resolve(dirname(file), specifier);
        if (under(pkg.dir, target)) continue;
        refuse(
          "include",
          shown(root, file),
          specifier,
          `${shown(root, file)}: includes "${specifier}", which resolves into ` +
            `${describeOwner(ownerOf(packages, root, target))}, outside packages/${pkg.name}: a package's native ` +
            "sources include their own by path",
        );
      }
    }

    // A dependency the package's native tree links counts as used: one of its CMakeLists names one of the dependency's
    // targets, and the configure check reads the same declaration for the same reason. Every CMakeLists under the
    // package counts (a tool in a subdirectory is the package's too). The search is textual.
    const text = walk(pkg.dir, (name) => name === "CMakeLists.txt")
      .map((file) => readFileSync(file, "utf8"))
      .join("\n");
    for (const name of pkg.dependencies) {
      const short = packageOf(name);
      const targets = short === null ? [] : (packages.get(short)?.targets ?? []);
      if (targets.some((target) => new RegExp(`(^|[^A-Za-z0-9_])${target}([^A-Za-z0-9_]|$)`, "m").test(text))) {
        markUsed(pkg, name);
      }
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
    for (const name of [...pkg.dependencies, ...pkg.devDependencies]) {
      if (!used.get(pkg.name)?.has(name)) {
        warnings.push(`${pkg.name}: declares ${name}, which no file of its imports and no native target of it names`);
      }
    }
  }

  // The exceptions: a refusal an entry names is allowed, and an entry nothing matches is itself a failure (the stage it
  // names has removed the import, so its line must go). An entry names the rule it allows, so it cannot allow the same
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
