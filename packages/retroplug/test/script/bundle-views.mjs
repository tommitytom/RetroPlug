#!/usr/bin/env node
// The driver behind test/script/bundle-view-package.test.ts (docs/modular-graph-phase-f5a-design.md section 5.4): the
// three bundles that prove the views split, each built in memory and reported as the modules that went in.
//
//   node test/script/bundle-views.mjs fixture-view   the fixture package's views module, as the UI bundle builds it
//   node test/script/bundle-views.mjs pure           the shell's menu and views modules, as the pure tier bundles them
//   node test/script/bundle-views.mjs views-react    a fixture tree whose views module imports react, generated and
//                                                    bundled both ways
//
// The build is Node's (esbuild is not a txiki module), so the test runs this in a node child, as bundle-split.mjs runs
// the surface bundles.
import { cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { fixtureAliases } from "../packages/fixtureAliases.mjs";

const require = createRequire(import.meta.url);
const { esbuild } = require("../../../../tools/esbuild-shared");
const { uiBuildOptions } = require("../../../../tools/build-ui.js");
const { generate } = await import("../../scripts/lib/packages.mjs");

const HERE = dirname(fileURLToPath(import.meta.url));
const SHELL = resolve(HERE, "../..");
const REPO = resolve(SHELL, "../..");
const MODE = process.argv[2];

/** The options the pure tier bundles a test with (scripts/run-tests.mjs): platform neutral, no alias, and nothing of
 *  the UI bundle's nodePaths, so React is not resolvable. A copy of run-tests.mjs:122-135, which a test cannot import;
 *  keep the two in step. `root` is esbuild's working directory for the fixture-tree case, whose entry lives outside the
 *  repository. */
function pureOptions(entry, root) {
  return {
    entryPoints: [entry],
    bundle: true,
    format: "esm",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    absWorkingDir: root,
    outfile: join(root, "bundle.js"),
    define: { "process.env.NODE_ENV": '"production"' },
    metafile: true,
  };
}

/** Build `entry` in memory and report the modules that went in (absolute, forward slashes) or the one error. */
function bundle(options) {
  try {
    const result = esbuild.buildSync({ ...options, write: false });
    return { inputs: Object.keys(result.metafile.inputs).map((input) => resolve(options.absWorkingDir, input)) };
  } catch (e) {
    const errors = (e.errors ?? []).map((error) => error.text);
    return { error: errors.length ? errors.join("; ") : String(e) };
  }
}


const result = (() => {
  switch (MODE) {
    case "fixture-view":
      // A view package as the UI bundle builds it: tools/build-ui.js's own options, which alias lvgljs and lvgljs-ui,
      // point nodePaths at the dpf.js React and turn JSX on.
      return bundle(uiBuildOptions(resolve(SHELL, "../ui-fixture/ts/ui/views.ts"), join(REPO, "build/ui/views.js")));
    case "pure": {
      // The three modules the pure tier reaches when it builds a menu, bundled as a test is: no alias. The menus
      // module, the menu composer and the menu definitions (the file about 25 menu tests enter through) must carry
      // neither the generated views module nor a package's views, so a module that re-imported a view function would
      // be caught here rather than only by the first product to list a React view package.
      return {
        menus: bundle(pureOptions(join(SHELL, "ui/generated/menus.ts"), SHELL)),
        composer: bundle(pureOptions(join(SHELL, "ui/host/menuContributions.ts"), SHELL)),
        defs: bundle(pureOptions(join(SHELL, "ui/screens/menu/menuDefs.ts"), SHELL)),
      };
    }
    case "views-react": {
      // A fixture tree whose package contributes a views module that imports react: the generated menus module must
      // still bundle with the pure options (it never reaches a view), and the generated views module must not. That is
      // the cause of the split, held as a failing case rather than a comment.
      const work = mkdtempSync(join(tmpdir(), "rp-views-"));
      try {
        const root = join(work, "tree");
        cpSync(join(HERE, "../packages/fixtures/tree"), root, { recursive: true });
        mkdirSync(join(root, "packages/alpha/ts/ui"), { recursive: true });
        const viewsFile = join(root, "packages/alpha/ts/ui/views.ts");
        writeFileSync(
          viewsFile,
          'import { createElement } from "react";\n' +
            'import { defineViews } from "@retroplug/ui/contribution";\n' +
            'export default defineViews({ views: [{ id: "alpha-react", label: "Alpha React", placement: "tile", ' +
            'body: { kind: "component", component: () => createElement("box", null) } }] });\n',
        );
        const manifest = join(root, "packages/alpha/package.json");
        const json = JSON.parse(readFileSync(manifest, "utf8"));
        json.retroplug.views = "ts/ui/views.ts";
        json.exports["./views"] = "./ts/ui/views.ts";
        writeFileSync(manifest, `${JSON.stringify(json, null, 2)}\n`);
        generate({ root });
        const options = (entry) => ({ ...pureOptions(entry, root), alias: fixtureAliases(root) });
        return {
          menus: bundle(options(join(root, "packages/retroplug/ui/generated/menus.ts"))),
          views: bundle(options(join(root, "packages/retroplug/ui/generated/views.ts"))),
        };
      } finally {
        rmSync(work, { recursive: true, force: true });
      }
    }
    default:
      console.error(`bundle-views.mjs: unknown mode "${MODE}"`);
      process.exit(2);
  }
})();

process.stdout.write(JSON.stringify(result));
