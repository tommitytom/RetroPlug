// The boundary check over its fixture packages (packages/retroplug/scripts/check-boundaries.mjs, docs/modular-graph-
// phase-f1-design.md section 4): one fixture tree per refusal, each breaking exactly one rule, and the check fails it
// with that rule's message, naming the file, the import and the owner:
//
// - an @retroplug import the package does not declare;
// - a relative reach into the shell;
// - a third-party module (zod) the package does not declare;
// - a cycle in the declared dependencies;
// - an #include that climbs out of the package;
// - a dependency on the shell, declared and imported (the two halves of one rule);
// - an import nothing resolves, which is reported by the file and the specifier rather than as a bundler failure, and
//   does not hide the rest of the package's refusals;
// - a type-only reach into the shell (`import type`, `export type ... from`, `import("x").T`);
// - a triple-slash reference path into the shell, and an undeclared `reference types`;
// - an `exports` entry that publishes a test module (a literal target, or a pattern whose `*` can reach one), one that
//   reaches outside ts/, and one naming no file;
// - a devDependency a source file imports, which only a test file may;
// - source that imports one of its own package's test modules: the same devDependency leak, reached by a relative path;
// - a host module imported from a file its surface does not own (a `cli` module from plain source, not examples/ or
//   ts/cli/, and React from a package's CLI tree);
// - a .tsx outside the package's ts/ui/ tree, and a package whose .tsx has no react/jsx-runtime declaration (automatic
//   JSX imports it with no specifier, so the .tsx is its only use);
// - a declared package that does not exist;
// - a `workspace:` or `link:` alias of the shell, which the key alone would present as a third-party name;
// - an `npm:` alias naming a package the workspace does not have;
// - a declared dependency a native target names by path (a use) beside one only a CMake comment names (no use, and the
//   warning says so);
// - a shell source file importing a cart's module, which rule 9 admits only through the generated registries beside the
//   contract modules the list names (and the positive tree imports a contract module, with a cart reached from
//   src/generated/ and not judged);
//
// and three positive trees: one where a package declares a package it imports by name, a declared third-party subpath, a
// devDependency from a test file (a type package's globals included), its own interface by a relative path, a
// specifier that spells a module's emitted name ("./types.js" for types.ts, "./types.mjs" for types.mts), the
// compiler its typecheck script runs, and exports that stay inside ts/; and the real tree, where no exception is left.
//
// The checker is Node's (it reads TypeScript's scanner), so the test spawns test/boundaries/run-checker.mjs, which runs
// it over a fixture root and gives back its output as JSON, as test/packages/packages-generator.test.ts runs the
// generator.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn, a child's pipe and its file reads, declared locally as bundle-no-zod.test.ts does.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  spawn(
    args: string[],
    opts: { stdout: "pipe"; stderr: "pipe" },
  ): {
    stdout: ByteStream | null;
    stderr: ByteStream | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

async function text(stream: ByteStream | null): Promise<string> {
  if (!stream) return "";
  const chunks: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) chunks.push(value);
  }
  let total = 0;
  for (const c of chunks) total += c.length;
  const all = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    all.set(c, at);
    at += c.length;
  }
  return new TextDecoder().decode(all);
}

/** The checker's report for one tree, parsed. */
async function drive(which: string): Promise<Record<string, any>> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/boundaries/run-checker.mjs", which], {
    stdout: "pipe",
    stderr: "pipe",
  });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the checker driver failed: ${err || out}`);
  return JSON.parse(out) as Record<string, any>;
}

test(
  "an @retroplug import a package does not declare is refused, naming the file, the import and owner",
  async () => {
  const report = await drive("undeclared-package");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "@retroplug/beta", which is the beta package (packages/beta), and alpha ' +
      "does not declare it in its dependencies (its tests may also use devDependencies)",
  ]);
});

test("a relative reach into the shell is refused, naming the file, the import and the shell", async () => {
  const report = await drive("relative-reach");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/gamma/ts/index.ts: imports "../../retroplug/src/shell", which resolves into the shell ' +
      "(packages/retroplug), outside packages/gamma/ts: a package reaches another as @retroplug/<name>; the shell " +
      "not at all",
  ]);
});

test("a third-party module a package does not declare is refused", async () => {
  const report = await drive("undeclared-third-party");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/delta/ts/index.ts: imports "zod", a third-party module, which delta does not declare in its ' +
      "dependencies (its tests may also use devDependencies)",
  ]);
});

test("a cycle in the declared dependencies is refused, naming the path", async () => {
  const report = await drive("cycle");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual(["the packages' declared dependencies are a cycle: epsilon -> zeta -> epsilon"]);
});

test("an #include that climbs out of the package is refused, naming the file, the include and the owner", async () => {
  const report = await drive("climbing-include");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/theta/native/src/x.cpp: includes "../../../other/native/src/y.hpp", which resolves into the other ' +
      "package (packages/other), outside packages/theta: a package's native sources include their own by path",
  ]);
});

test("a dependency on the shell is refused, declared in the manifest and imported alike", async () => {
  const report = await drive("shell-dependency");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    "packages/iota/package.json: declares @retroplug/retroplug in its dependencies, which is the shell " +
      "(packages/retroplug): no package may depend on the shell",
    'packages/iota/ts/index.ts: imports "@retroplug/retroplug", which is the shell (packages/retroplug): no package ' +
      "may depend on the shell",
  ]);
});

test("an import nothing resolves is reported by its file and specifier, and the package is still judged", async () => {
  const report = await drive("unresolvable");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/kappa/ts/index.ts:3:22: cannot be resolved: ./nope names no file',
    'packages/kappa/ts/other.ts: imports "zod", a third-party module, which kappa does not declare in its ' +
      "dependencies (its tests may also use devDependencies)",
  ]);
});

test(
  "a package that declares everything it reaches holds, its interface and its compiler included",
  async () => {
    const report = await drive("allowed");
    console.log(`# ${(report.refused as string[]).join("\n# ")}`);
    expect(report.status).toBe(0);
    expect(report.refused).toEqual([]);
    expect(report.warnings).toEqual([]);
    expect(report.stale).toEqual([]);
  },
);

test("type-only imports are judged: an import type, an export type ... from and an import(\"x\").T alike", async () => {
  const report = await drive("type-only-reach");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "../../retroplug/src/a", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell " +
      "not at all",
    'packages/alpha/ts/index.ts: imports "../../retroplug/src/b", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell " +
      "not at all",
    'packages/alpha/ts/index.ts: imports "../../retroplug/src/c", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell " +
      "not at all",
  ]);
});

test("a triple-slash reference path into the shell is refused, and an undeclared reference types with it", async () => {
  const report = await drive("reference");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: a /// <reference path> "../../retroplug/src/a.ts", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell " +
      "not at all",
    'packages/beta/ts/index.ts: a /// <reference types="node"> names @types/node, which beta does not declare in its ' +
      "dependencies (its tests may also use devDependencies)",
  ]);
});

test(
  "publishing a test module, a module outside ts/, or one naming no file, is refused",
  async () => {
    const report = await drive("exports");
    console.log(`# ${(report.refused as string[]).join("\n# ")}`);
    expect(report.status).toBe(1);
    expect(report.refused).toEqual([
      'packages/delta/package.json: exports "./test/rig" at ./ts/test/rig.ts, which is a test module: a test file ' +
        "may use devDependencies, so publishing one lets another package's source reach them through it",
      'packages/epsilon/package.json: exports "./app" at ./app.ts, which is outside packages/epsilon/ts, so no ' +
        "check reads the code it publishes",
      // A pattern is judged by every path its `*` can name, so it cannot publish what the file it spells may not be.
      'packages/eta/package.json: exports "./*" at ./*.ts, which can name files outside packages/eta/ts, so ' +
        "no check reads the code it publishes",
      'packages/missing/package.json: exports "." at ./ts/gone.ts, which names no file: the module moved or ' +
        "was never there, and the entry is the one thing still saying the package publishes it",
      'packages/zeta/package.json: exports "./*" at ./ts/*.ts, which can name a test module: a test file may use ' +
        "devDependencies, so publishing one lets another package's source reach them through it",
    ]);
  },
);

test("a devDependency is a test file's: a source file that imports one is refused", async () => {
  const report = await drive("dev-dependency-source");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "@retroplug/testing", which is the testing package (packages/testing), and ' +
      "alpha does not declare it in its dependencies (its tests may also use devDependencies)",
  ]);
});

test(
  "source that imports its own package's test module is refused",
  async () => {
    const report = await drive("source-into-test");
    console.log(`# ${(report.refused as string[]).join("\n# ")}`);
    expect(report.status).toBe(1);
    expect(report.refused).toEqual([
      'packages/alpha/ts/nes/find.ts: imports "../test/support", which is a test module of its own package, from a ' +
        "file that is not a test: a test file may use the package's devDependencies, so importing one from source " +
        "hands them to every importer of the source",
    ]);
  },
);

test("an alias of the shell is refused, spelled workspace: or link:", async () => {
  const report = await drive("aliased-shell");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/beta/package.json: declares app as "workspace:@retroplug/retroplug@*", which is the shell ' +
      "(packages/retroplug): no package may depend on the shell",
    'packages/beta/package.json: declares @retroplug/core as "link:../retroplug", which is the shell ' +
      "(packages/retroplug): no package may depend on the shell",
  ]);
});

test("an alias naming a package the workspace does not have is refused, naming the alias", async () => {
  const report = await drive("aliased-undeclared");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/beta/package.json: declares ghostdep as "npm:@retroplug/ghost@1.0.0", which names @retroplug/ghost, ' +
      "not a package in packages",
  ]);
});

test("a declared package that does not exist is refused on its own", async () => {
  const report = await drive("declared-missing");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    "packages/alpha/package.json: declares @retroplug/ghost, which is not a package in packages",
  ]);
});

test("a native target's path is a use and a CMake comment is not, which the warning distinguishes", async () => {
  const report = await drive("native-use");
  console.log(`# ${(report.warnings as string[]).join("\n# ")}`);
  // The one devDependency the DEFS names by path is used (it is imported by no TypeScript file), and the one only a
  // comment names is not: the checker strips the comments before its textual search, so a path in prose is no reach.
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.warnings).toEqual(["alpha: declares @retroplug/gamma, which no file of its imports and no native target of it names"]);
});

test("a shell source importing a cart's module is refused, naming the file and the specifier (rule 9)", async () => {
  const report = await drive("shell-source-forbidden");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/retroplug/src/index.ts: imports "@retroplug/alpha/app", which is not one of the contract modules the ' +
      "shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated " +
      "registries",
  ]);
});

test("a shell source's relative import that lands outside src is refused (rule 9)", async () => {
  // Rules 1 to 8 do not judge the shell's src, so a relative reach into a package's own files or into the app's ui/ is
  // the one form only rule 9 sees.
  const report = await drive("shell-source-relative");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/retroplug/src/index.ts: imports "../../alpha/ts/app", which resolves outside packages/retroplug/src ' +
      "(rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and " +
      "the shell's other trees are not a source's to import",
    'packages/retroplug/src/index.ts: imports "../../ui/App", which resolves outside packages/retroplug/src ' +
      '(rule 9): ' +
      "a cart's or a console's code is reached through the generated registries the generator writes, and the " +
      "shell's other trees are not a source's to import",
  ]);
});

test("a shell source may import a contract module and its own, and src/generated is not judged (rule 9)", async () => {
  // The shell's registry modules import every package by name, which is the one place the rule leaves alone, and a
  // relative import of another src module is the shell's own.
  const report = await drive("shell-source-allowed");
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.stale).toEqual([]);
});

test("a cli source may import a contract module and reach src, and names no package it does not declare", async () => {
  // The CLI is a surface over the shell's control plane: a contract module is on the list, and a relative reach into
  // src is its own tree. A cart's code arrives through the generated tool table, which the rule does not judge.
  const report = await drive("shell-cli-allowed");
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
});

test("a cli source may not import a cart or a console (rule 9 over cli/)", async () => {
  const report = await drive("shell-cli-forbidden");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/retroplug/cli/index.ts: imports "@retroplug/alpha/app", which is not one of the contract modules the ' +
      "shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated " +
      "registries",
  ]);
});

test("rule 9's relative half is per root: a src file reaches src, a cli file cli or src", async () => {
  // The union of the two roots would admit both of these, so each direction has a file: src into cli/ is refused, and
  // so is cli/ into ui/ (the UI is a surface above the control plane, not a tree the dispatcher's modules import).
  const report = await drive("shell-cli-relative");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/retroplug/src/index.ts: imports "../cli/run", which resolves outside packages/retroplug/src ' +
      '(rule 9): a ' +
      "cart's or a console's code is reached through the generated registries the generator writes, and the shell's " +
      "other trees are not a source's to import",
    'packages/retroplug/cli/evil.ts: imports "../ui/App", which resolves outside packages/retroplug/cli and ' +
      "packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the " +
      "generator writes, and the shell's other trees are not a source's to import",
  ]);
});

test("a host module the package declares is admitted, from the files its surface owns", async () => {
  // The positive tree imports it from ts/cli/ and from examples/, which are the cli surface's own files.
  const report = await drive("host-module-declared");
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.warnings).toEqual([]);
});

test("a host module imported from a file its surface does not own is refused, naming the surface", async () => {
  const report = await drive("host-module-surface");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "fixture-lib", a cli host module, from a file that surface does not own: ' +
      "a cli host module is reached only from the package's examples/ or ts/cli/ trees",
  ]);
});

test("a host module the package does not declare is refused, naming the file and the module", async () => {
  const report = await drive("host-module-undeclared");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "fixture-lib", which is a host module, and alpha does not declare it in ' +
      "retroplug.host",
  ]);
});

test("a host module imported by an example session is refused too, naming that file", async () => {
  // A package's examples/ are its code: the shell's own program compiles them (`../*/examples`), so a host module
  // reached from one needs the same declaration a ts/ file's does.
  const report = await drive("host-module-examples");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/examples/session.ts: imports "fixture-lib", which is a host module, and alpha does not declare ' +
      "it in retroplug.host",
  ]);
});

test("a host declaration the shell's list does not have is refused", async () => {
  const report = await drive("host-module-unknown");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/package.json: declares retroplug.host "ghost", which the shell\'s host-modules.json does not list',
  ]);
});

test("a host declaration nothing imports warns, as an unused dependency does", async () => {
  const report = await drive("host-module-stale");
  console.log(`# ${(report.warnings as string[]).join("\n# ")}`);
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.warnings).toEqual(["alpha: declares the host module fixture-lib, which no file of its imports"]);
});

test("a .tsx under the package's ts/ui/ tree with the React host modules declared is admitted", async () => {
  // The good shape: the component in ts/ui/, the four host modules in retroplug.host, and the .tsx counted as the use
  // of react/jsx-runtime (which no import names), so nothing is declared unused.
  const report = await drive("react-allowed");
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.warnings).toEqual([]);
});

test("a .tsx outside the package's ts/ui/ tree is refused, naming the file and the tree", async () => {
  const report = await drive("react-outside-ui");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    "packages/alpha/ts/Badge.tsx: is a .tsx file outside packages/alpha/ts/ui/, the one tree rule 3 admits React " +
      "into: a component belongs to the package's UI surface, whose files are the only ones a React host module is " +
      "reachable from",
  ]);
});

test("a package with a .tsx and no react/jsx-runtime declaration is refused", async () => {
  // Automatic JSX imports react/jsx-runtime with no specifier in the source, so a declaration check that read only
  // what an import names would admit a component whose React was never declared.
  const report = await drive("react-undeclared");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    "packages/alpha/package.json: has a .tsx file (packages/alpha/ts/ui/Badge.tsx) and does not declare " +
      "react/jsx-runtime in retroplug.host: automatic JSX imports it with no specifier, so the declaration is the only " +
      "place the dependency shows",
  ]);
});

test("a React import from a package's CLI tree is refused as a surface error", async () => {
  const report = await drive("react-cli-import");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/cli/tool.ts: imports "react", a ui host module, from a file that surface does not own: ' +
      "a ui host module is reached only from the package's ts/ui/ trees",
  ]);
});

test("the real tree holds its boundaries with no exception left", async () => {
  const report = await drive("--tree");
  const allowed = report.allowed as string[];
  console.log(`# ${allowed.length} allowed by EXCEPTIONS:`);
  for (const line of allowed) console.log(`#   ${line}`);
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.stale).toEqual([]);
  // The reaches the split moved into packages are gone, so no entry is left.
  expect(allowed.length).toBe(0);
});
