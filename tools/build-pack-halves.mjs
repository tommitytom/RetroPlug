#!/usr/bin/env node
// A package's TS halves built as a pack's (docs/downloadable-packs-loading-design.md section 3,
// docs/downloadable-packs-m2c-plan.md): each half its package.json names (retroplug.app, consoles, ui, views, cli) one
// ES module bundle, js/<half>.js, with every contract module and host module external, so the pack links to its
// RetroPlug's own; the package's other imports are bundled in, except another @retroplug package's module, which a
// pack may not carry a copy of and is refused. Then pack.json: the template's members, its API level this build's,
// and its halves the ones built (a native half the template names is kept).
//
//   node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template>
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { SHELL_IMPORTS, UI_IMPORTS, hostModuleSpecifiers } from "../packages/retroplug/scripts/lib/contracts.mjs";

const esbuild = createRequire(import.meta.url)("esbuild");
const REPO = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const HALVES = ["app", "consoles", "ui", "views", "cli"];

/** RP_API_LEVEL from the header a pack's entry compiles against (cmake/packs.cmake reads the same line). */
function apiLevel() {
  const header = readFileSync(join(REPO, "packages/node-sdk/native/src/graph/rp_pack.h"), "utf8");
  const found = /^#define RP_API_LEVEL (\d+)u\s*$/m.exec(header);
  if (!found) throw new Error("rp_pack.h has no `#define RP_API_LEVEL <n>u` line");
  return Number(found[1]);
}

/** What a pack may import: the contracts and the host modules its contexts register. */
export function packExternals() {
  const hostModules = hostModuleSpecifiers(JSON.parse(readFileSync(join(REPO, "packages/retroplug/host-modules.json"),
    "utf8")));
  // The two lists are { specifier, reason } entries (build-shared-modules.mjs reads them the same way).
  return [...new Set([...SHELL_IMPORTS.map((e) => e.specifier), ...UI_IMPORTS.map((e) => e.specifier), ...hostModules])];
}

export async function buildPackHalves({ packageDir, outDir, template }) {
  const manifest = JSON.parse(readFileSync(join(packageDir, "package.json"), "utf8"));
  const block = manifest.retroplug ?? {};
  const externals = packExternals();
  const pack = JSON.parse(readFileSync(template, "utf8"));
  const halves = pack.halves?.native ? { native: pack.halves.native } : {};
  for (const half of HALVES) {
    const entry = block[half];
    if (typeof entry !== "string") continue;
    const refuseForeign = {
      name: "refuse-foreign-retroplug",
      setup(build) {
        build.onResolve({ filter: /^@retroplug\// }, (args) => {
          if (externals.includes(args.path)) return { path: args.path, external: true };
          return { errors: [{ text: `${args.importer} imports ${args.path}, which is not a contract module: a pack ` +
            "may import only the contracts (scripts/lib/contracts.mjs) and the host modules" }] };
        });
      },
    };
    const outfile = join(outDir, "js", `${half}.js`);
    await esbuild.build({
      entryPoints: [join(packageDir, entry)],
      bundle: true,
      format: "esm",
      platform: "neutral",
      mainFields: ["module", "main"],
      target: "es2020",
      outfile,
      external: externals.filter((name) => !name.startsWith("@retroplug/")),
      plugins: [refuseForeign],
      jsx: "automatic",
      define: { "process.env.NODE_ENV": '"production"' },
      nodePaths: [join(REPO, "node_modules")],
      logLevel: "silent",
    });
    halves[half] = `js/${half}.js`;
  }
  pack.apiLevel = apiLevel();
  pack.halves = halves;
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, "pack.json"), `${JSON.stringify(pack, null, 2)}\n`);
  return pack;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [packageDir, outDir, template] = process.argv.slice(2);
  if (!packageDir || !outDir || !template) {
    console.error("usage: node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template>");
    process.exit(2);
  }
  buildPackHalves({ packageDir: resolve(packageDir), outDir: resolve(outDir), template: resolve(template) })
    .catch((e) => { console.error(e?.message ?? e); process.exit(1); });
}
