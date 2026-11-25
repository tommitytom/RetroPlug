#!/usr/bin/env node
// A package's TS halves built as a pack's (docs/downloadable-packs-loading-design.md section 3,
// docs/downloadable-packs-m2c-plan.md): each half its package.json names (retroplug.app, consoles, ui, views, cli) one
// ES module bundle, js/<half>.js, with every contract module and host module external, so the pack links to its
// RetroPlug's own; the package's other imports are bundled in, except another @retroplug package's module, which a
// pack may not carry a copy of and is refused. Then pack.json: the template's members, its API level this build's,
// and its halves the ones built (a native half the template names is kept).
//
// A package's `retroplug.script`, a device script pack, becomes the script half: the device-pack toolchain
// (packages/retroplug/scripts/build-device-pack.mjs, with its bundling, ES5 lowering, library-call scan and transcript
// run) builds it under the pack's id, and the pack ships its two ES5 bundles (js/script.js, the whole pack;
// js/script.device.js, the device's runtime), which the host compiles to bytecode when it loads the pack, and a `script`
// member naming the runtime, the heap and the types. `--only` builds the halves it names alone.
//
// A package's `retroplug.protocol` member becomes the protocol half, a global-code script-node bundle (an IIFE that
// appends its definitions to __rp_scriptNodes, as the product's own script bundle does) carrying its own copy of
// @retroplug/core, and pack.json's `protocol` member (the member's nodes, less their labels, and its roles).
// `protocol.placer` becomes the placer half, an ES module like the others.
//
// A package's `retroplug.protocol.kinds` (docs/downloadable-packs-m2f-plan.md) becomes pack.json's `kinds` member (each
// entry less its module) and the kinds half, js/kinds.js: a global-code bundle that, per kind, imports its entries'
// runtime modules and registers them through the built-in script bundle's __rp_registerKindItems. It runs in the built-in
// script contexts, so it bundles everything its runtime imports (each @retroplug package through its exports map) and
// refuses zod, react and the UI libraries.
//
//   node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template> [--only <half,...>] [--mqjs <path>] [--runner <path>]
import { readFileSync, readdirSync, writeFileSync, mkdirSync, copyFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import { dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { SHELL_IMPORTS, UI_IMPORTS, hostModuleSpecifiers } from "../packages/retroplug/scripts/lib/contracts.mjs";

const esbuild = createRequire(import.meta.url)("esbuild");
const { tsOptions } = createRequire(import.meta.url)("./ts-options.js");
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

/** The package's device script pack as the pack's script half (see the header). Returns the whole pack's file. */
function buildScriptHalf({ packageDir, outDir, pack, script, mqjs, runner }) {
  if (typeof script !== "object" || script === null || script.device !== true || typeof script.entry !== "string" ||
      !Number.isInteger(script.heapBytes)) {
    throw new Error("retroplug.script is not a device pack ({ entry, device: true, heapBytes }): a desktop script " +
      "pack is reserved");
  }
  const work = mkdtempSync(join(tmpdir(), "rp-pack-script-"));
  try {
    const args = [join(REPO, "packages/retroplug/scripts/build-device-pack.mjs"),
      "--entry", join(packageDir, script.entry), "--name", pack.id, "--heap-bytes", String(script.heapBytes),
      "--out-dir", work, "--pack-dir", packageDir];
    if (mqjs) args.push("--mqjs", mqjs);
    if (runner) args.push("--runner", runner);
    const run = spawnSync(process.execPath, args, { encoding: "utf8" });
    if (run.status !== 0) {
      throw new Error(`the device-pack toolchain refused ${pack.id}'s script half:\n${run.error ? `${run.error}\n` : ""}${run.stderr}${run.stdout}`);
    }
    const types = JSON.parse(readFileSync(join(work, `${pack.id}.types.json`), "utf8"));
    const declared = pack.provides?.nodeTypes ?? [];
    for (const type of types) {
      if (!declared.includes(type)) {
        throw new Error(`the script half defines ${type}, which the template's provides.nodeTypes does not declare`);
      }
    }
    mkdirSync(join(outDir, "js"), { recursive: true });
    copyFileSync(join(work, `${pack.id}.js`), join(outDir, "js", "script.js"));
    copyFileSync(join(work, `${pack.id}.device.js`), join(outDir, "js", "script.device.js"));
    pack.script = { runtime: "js/script.device.js", heapBytes: script.heapBytes, nodeTypes: types };
    return "js/script.js";
  } finally {
    rmSync(work, { recursive: true, force: true });
  }
}

/** The workspace package folder each @retroplug/<name> lives in, read from packages/*\/package.json's names. */
function workspacePackages() {
  const found = new Map();
  for (const entry of readdirSync(join(REPO, "packages"), { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    let name;
    try { name = JSON.parse(readFileSync(join(REPO, "packages", entry.name, "package.json"), "utf8")).name; } catch { continue; }
    if (typeof name === "string") found.set(name, join(REPO, "packages", entry.name));
  }
  return found;
}

/** Resolves a script-realm half's imports: its code runs in an Engine's script contexts, which have no module loader
 *  and no RetroPlug module, so the bundle carries its own copy of what it imports. `packages` is the @retroplug
 *  packages it may import (each resolved through its own exports map, so a package outside the workspace builds the
 *  same), or null for every one; `refuse` lists specifiers (and their subpaths) it may not, with `refuseText` saying
 *  why; `foreignText` is the reason a package outside `packages` gives. */
function scriptRealmImports({ name, packages, refuse, refuseText, foreignText, refuseFiles = new Set(), refuseFilesText = "" }) {
  const workspace = workspacePackages();
  return {
    name,
    setup(build) {
      build.onResolve({ filter: /.*/ }, (args) => {
        for (const r of refuse) {
          if (args.path === r || args.path.startsWith(`${r}/`)) {
            return { errors: [{ text: `${args.importer} imports ${args.path}: ${refuseText}` }] };
          }
        }
        const m = /^(@retroplug\/[^/]+)(?:\/(.+))?$/.exec(args.path);
        if (!m) return undefined;
        if (packages && !packages.includes(m[1])) {
          return { errors: [{ text: `${args.importer} imports ${args.path}: ${foreignText}` }] };
        }
        const dir = workspace.get(m[1]);
        const exportsMap = dir ? JSON.parse(readFileSync(join(dir, "package.json"), "utf8")).exports ?? {} : {};
        const target = exportsMap[m[2] ? `./${m[2]}` : "."];
        if (typeof target !== "string") {
          return { errors: [{ text: `${args.importer} imports ${args.path}: ${args.path} is not a module ${m[1]} exports` }] };
        }
        const path = join(dir, target);
        if (refuseFiles.has(path)) return { errors: [{ text: `${args.importer} imports ${args.path}: ${refuseFilesText}` }] };
        return { path };
      });
    },
  };
}

/** The protocol half's imports: @retroplug/core alone, and no schema library. */
function protocolImports() {
  return scriptRealmImports({
    name: "protocol-imports",
    packages: ["@retroplug/core"],
    refuse: ["zod"],
    refuseText: "a protocol half's nodes run in the script contexts, which carry no schema library",
    foreignText: "a protocol half bundles @retroplug/core alone (its nodes run in the script contexts, which hold no " +
      "other RetroPlug module)",
  });
}

/** Every declaring package's kind runtime module (retroplug.kinds.<kind>.runtime.module): a kinds half that value-imported
 *  one would bundle a private copy of the kind's registry, which the built-in script bundle never sees. */
function kindRuntimeModules() {
  const files = new Set();
  for (const dir of workspacePackages().values()) {
    const kinds = JSON.parse(readFileSync(join(dir, "package.json"), "utf8")).retroplug?.kinds ?? {};
    for (const kind of Object.values(kinds)) if (typeof kind?.runtime?.module === "string") files.add(join(dir, kind.runtime.module));
  }
  return files;
}

/** The kinds half's imports: any @retroplug package, but no schema or UI library and no kind's own registry. */
function kindsImports() {
  return scriptRealmImports({
    name: "kinds-imports",
    packages: null,
    refuse: ["zod", "react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"],
    refuseText: "a kinds half runs in the script contexts, which carry no schema or UI library",
    foreignText: "",
    refuseFiles: kindRuntimeModules(),
    refuseFilesText: "a kinds half registers its items through __rp_registerKindItems, never the kind's own registry",
  });
}

/** The package's protocol member as the pack's protocol half (see the header). Returns the bundle's file. */
async function buildProtocolHalf({ packageDir, outDir, pack, protocol }) {
  if (typeof protocol.module !== "string" || protocol.module === "") {
    throw new Error("retroplug.protocol needs a module, the file that default-exports its nodes");
  }
  // The wiring as the manifest spells it, less the labels: a pack's script nodes take theirs at load.
  const nodes = (protocol.nodes ?? []).map(({ label: _label, ...rest }) => rest);
  const types = nodes.map((n) => n.type);
  const declared = pack.provides?.nodeTypes ?? [];
  for (const type of types) {
    if (!declared.includes(type)) {
      throw new Error(`the protocol node ${type} is not in the template's provides.nodeTypes`);
    }
  }
  // The entry registers the module's definitions in the manifest's order, and refuses a module that defines other
  // types than the manifest declares, as the product's generated entry does.
  const entry = [
    `import nodes from ${JSON.stringify(join(packageDir, protocol.module))};`,
    'import { registerScriptNodes } from "@retroplug/core/register";',
    `const ORDER = ${JSON.stringify(types)};`,
    "const ids = nodes.map((n) => n.id);",
    "if (new Set(ids).size !== ids.length || ids.length !== ORDER.length || ids.some((id) => ORDER.indexOf(id) < 0)) {",
    '  throw new Error("the protocol module defines " + ids.join(", ") + ", and the manifest declares " + ORDER.join(", "));',
    "}",
    "registerScriptNodes(nodes.slice().sort((a, b) => ORDER.indexOf(a.id) - ORDER.indexOf(b.id)));",
  ].join("\n");
  await esbuild.build({
    stdin: { contents: entry, resolveDir: packageDir, sourcefile: "protocol-entry.js", loader: "js" },
    bundle: true,
    format: "iife",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    // As the product's own script bundle (tools/build-session.js): the directive is part of what a script host runs.
    tsconfigRaw: tsOptions({ alwaysStrict: true }),
    outfile: join(outDir, "js", "protocol.js"),
    plugins: [protocolImports()],
    absWorkingDir: packageDir,
    define: { "process.env.NODE_ENV": '"production"' },
    nodePaths: [join(REPO, "node_modules")],
    logLevel: "silent",
  });
  pack.protocol = { nodes, roles: protocol.roles ?? {} };
  return "js/protocol.js";
}

/** The package's protocol.kinds member as the pack's kinds half (see the header). Returns the bundle's file. */
async function buildKindsHalf({ packageDir, outDir, pack, kinds }) {
  if (typeof kinds !== "object" || kinds === null || Array.isArray(kinds)) {
    throw new Error("retroplug.protocol.kinds is { <kind>: [{ id, module, ...data }] }");
  }
  const lines = [];
  const member = {};
  let n = 0;
  for (const [kind, entries] of Object.entries(kinds)) {
    if (!Array.isArray(entries)) throw new Error(`retroplug.protocol.kinds.${kind} is a list of { id, module, ...data }`);
    const names = [];
    const ids = [];
    for (const entry of entries) {
      if (typeof entry?.id !== "string" || entry.id === "") throw new Error(`a retroplug.protocol.kinds.${kind} entry has no id`);
      if (typeof entry.module !== "string" || entry.module === "") {
        throw new Error(`retroplug.protocol.kinds.${kind}: ${entry.id} has no module`);
      }
      const local = `item${n++}`;
      lines.push(`import ${local} from ${JSON.stringify(join(packageDir, entry.module))};`);
      names.push(local);
      ids.push(entry.id);
    }
    lines.push(`globalThis.__rp_registerKindItems(${JSON.stringify(kind)}, [${names.join(", ")}], ${JSON.stringify(ids)});`);
    member[kind] = entries.map(({ module: _module, ...rest }) => rest);
  }
  const result = await esbuild.build({
    stdin: { contents: lines.join("\n"), resolveDir: packageDir, sourcefile: "kinds-entry.js", loader: "js" },
    metafile: true,
    bundle: true,
    format: "iife",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    tsconfigRaw: tsOptions({ alwaysStrict: true }),
    outfile: join(outDir, "js", "kinds.js"),
    plugins: [kindsImports()],
    absWorkingDir: packageDir,
    define: { "process.env.NODE_ENV": '"production"' },
    nodePaths: [join(REPO, "node_modules")],
    logLevel: "silent",
  });
  // A module the entries import in turn can reach a kind's runtime module (launchpad's controllerSession imports ./apps),
  // which the resolver cannot see: the bundle's inputs say so. The refusal names the entry module whose imports lead there.
  const runtimeModules = kindRuntimeModules();
  const inputs = result.metafile.inputs;
  const keyOf = (file) => relative(packageDir, file).split(sep).join("/");
  const wanted = new Set([...runtimeModules].map(keyOf));
  const reaches = (key, seen) => {
    if (wanted.has(key)) return true;
    if (seen.has(key)) return false;
    seen.add(key);
    return (inputs[key]?.imports ?? []).some((i) => reaches(i.path, seen));
  };
  for (const entry of Object.values(kinds).flat()) {
    const key = keyOf(join(packageDir, entry.module));
    if (reaches(key, new Set())) {
      throw new Error(`${join(packageDir, entry.module)} imports a kind's own registry module: a kinds half registers its ` +
        "items through __rp_registerKindItems, never the kind's own registry");
    }
  }
  pack.kinds = member;
  return "js/kinds.js";
}

export async function buildPackHalves({ packageDir, outDir, template, only, mqjs, runner }) {
  const manifest = JSON.parse(readFileSync(join(packageDir, "package.json"), "utf8"));
  const block = manifest.retroplug ?? {};
  if (block.kinds !== undefined) {
    throw new Error("retroplug.kinds: a pack cannot declare a contribution kind of its own (docs/downloadable-packs-m2f-plan.md)");
  }
  const externals = packExternals();
  const pack = JSON.parse(readFileSync(template, "utf8"));
  const halves = pack.halves?.native ? { native: pack.halves.native } : {};
  // A script member a template carries is replaced when the half is built and dropped when it is not.
  delete pack.script;
  delete pack.protocol;
  delete pack.kinds;
  for (const [half, entry] of [...HALVES.map((h) => [h, block[h]]), ["protocol", block.protocol?.module],
    ["kinds", block.protocol?.kinds === undefined ? undefined : "kinds"], ["placer", block.protocol?.placer]]) {
    if (typeof entry !== "string") continue;
    if (only && !only.includes(half)) continue;
    if (half === "protocol") {
      halves.protocol = await buildProtocolHalf({ packageDir, outDir, pack, protocol: block.protocol });
      continue;
    }
    if (half === "kinds") {
      halves.kinds = await buildKindsHalf({ packageDir, outDir, pack, kinds: block.protocol.kinds });
      continue;
    }
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
      // Esbuild writes each bundled file's path, relative to its working directory, into a comment: anchoring it at the
      // package keeps where the build ran (and the repository's layout) out of the pack's bytes.
      absWorkingDir: packageDir,
      define: { "process.env.NODE_ENV": '"production"' },
      nodePaths: [join(REPO, "node_modules")],
      logLevel: "silent",
    });
    halves[half] = `js/${half}.js`;
  }
  if ((!only || only.includes("script")) && block.script !== undefined) {
    halves.script = buildScriptHalf({ packageDir, outDir, pack, script: block.script, mqjs, runner });
  }
  pack.apiLevel = apiLevel();
  pack.halves = halves;
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, "pack.json"), `${JSON.stringify(pack, null, 2)}\n`);
  return pack;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const usage = () => {
    console.error("usage: node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template> " +
      "[--only <half,...>] [--mqjs <path>] [--runner <path>]");
    process.exit(2);
  };
  const positional = [];
  const opts = {};
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i++) {
    if (argv[i].startsWith("--")) {
      if (!["--only", "--mqjs", "--runner"].includes(argv[i]) || i + 1 >= argv.length) usage();
      opts[argv[i].slice(2)] = argv[++i];
    } else positional.push(argv[i]);
  }
  const [packageDir, outDir, template] = positional;
  if (positional.length !== 3) usage();
  buildPackHalves({
    packageDir: resolve(packageDir), outDir: resolve(outDir), template: resolve(template),
    only: opts.only ? opts.only.split(",") : undefined,
    mqjs: opts.mqjs ? resolve(opts.mqjs) : undefined, runner: opts.runner ? resolve(opts.runner) : undefined,
  }).catch((e) => { console.error(e?.message ?? e); process.exit(1); });
}
