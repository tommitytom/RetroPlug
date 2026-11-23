// The shared module sets (docs/downloadable-packs-m2b-plan.md): one esbuild code-splitting build per set, whose every
// output is a named ES module a txiki host registers before it evaluates its entry, so one context holds one copy of
// every contract module (one set of registries, one React) and a pack's ES modules can link to them by name (M2c).
//
//   app: the control plane (rp:cp) and the UI (rp:ui) and every contract specifier (SHELL_IMPORTS, UI_IMPORTS and the
//        host modules), for the context the two share in the plugin, the SDL standalone and the UI test harness.
//   cli: the CLI's dispatcher (rp:cli) and SHELL_IMPORTS.
//
// The names: a contract module is named by its specifier, so a bare import of it needs no import map (txiki's
// normalizer passes a bare name through and QuickJS finds it in the loaded-module table); a host by rp:<host>; a chunk by
// rp:chunks/<file stem>. esbuild writes the imports between outputs as relative paths, and txiki's normalizer joins a
// relative name wrongly on Windows (it converts the name to backslashes and splits the importer's name on them), so
// every one is rewritten to the bare name of the output it reaches. The shared outputs are ordered dependencies first:
// reading a module's bytecode resolves its imports, so a dependency must already be registered.
//
//   node tools/build-shared-modules.mjs <app|cli> <out-dir> [--product-dir <dir>]
//
// writes <out-dir>/js/** (the rewritten outputs), <out-dir>/shared.tsv (one "<name>\t<file>" per shared module, in load
// order) and <out-dir>/hosts.tsv (the hosts), which CMake hands to retroplug-modc. buildSharedModules() is the same build
// for the tests, in memory when `write` is false.
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { SHELL_IMPORTS, UI_IMPORTS, hostModuleSpecifiers } from "../packages/retroplug/scripts/lib/contracts.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, "..");
const require = createRequire(import.meta.url);
const esbuild = require("esbuild");
const { uiBuildOptions } = require("./build-ui.js");

const SHELL = path.join(REPO, "packages/retroplug");
const HOST_MODULES = JSON.parse(fs.readFileSync(path.join(SHELL, "host-modules.json"), "utf8"));

/** Each set's hosts (entry file and name) and contract specifiers. */
function setOf(name) {
  if (name === "app") {
    return {
      hosts: [
        { name: "rp:cp", entry: path.join(SHELL, "src/pluginControlPlane.ts") },
        { name: "rp:ui", entry: path.join(SHELL, "ui/main.tsx") },
      ],
      contracts: [...new Set([
        ...SHELL_IMPORTS.map((e) => e.specifier),
        ...UI_IMPORTS.map((e) => e.specifier),
        ...hostModuleSpecifiers(HOST_MODULES),
      ])],
      external: ["tjs:path"],
    };
  }
  if (name === "cli") {
    return {
      hosts: [{ name: "rp:cli", entry: path.join(SHELL, "cli/cli.ts") }],
      contracts: [...new Set(SHELL_IMPORTS.map((e) => e.specifier))],
      external: ["tjs:path", "retroplug-cli"],
    };
  }
  throw new Error(`build-shared-modules: no set is named ${name} (the sets are app, cli)`);
}

/** The file esbuild resolves each specifier to, with the options the set builds with: one throwaway build of a stdin
 *  importing every specifier, whose resolver records each answer and stops there. */
async function resolveContracts(base, specifiers) {
  const found = new Map();
  const record = {
    name: "record-contracts",
    setup(build) {
      build.onResolve({ filter: /.*/ }, async (args) => {
        if (args.importer !== "<stdin>" || args.pluginData === "inner") return undefined;
        const r = await build.resolve(args.path, { resolveDir: args.resolveDir, kind: args.kind, pluginData: "inner" });
        if (r.errors.length === 0) found.set(args.path, r.path);
        return { path: args.path, external: true };
      });
    },
  };
  await esbuild.build({
    ...base, entryPoints: undefined, outfile: undefined, write: false, metafile: false, logLevel: "silent",
    stdin: { contents: specifiers.map((s) => `import ${JSON.stringify(s)};`).join("\n"), resolveDir: SHELL },
    plugins: [record, ...(base.plugins ?? [])],
  });
  const missing = specifiers.filter((s) => !found.has(s));
  if (missing.length) throw new Error(`build-shared-modules: cannot resolve ${missing.join(", ")}`);
  return found;
}

const posix = (p) => p.split(path.sep).join("/");

/** Build one set. Returns the shared modules in load order and the hosts, each { name, file, text, imports, exports },
 *  the closure of
 *  source files each host reaches through the outputs, and the metafile. */
export async function buildSharedModules({ set, outDir, productDir, write = true }) {
  const spec = setOf(set);
  const base = uiBuildOptions(spec.hosts[0].entry, path.join(outDir, "unused.js"), productDir);
  delete base.outfile;
  base.entryPoints = undefined;
  base.external = spec.external;
  const contracts = await resolveContracts(base, spec.contracts);
  // A CommonJS host module's entry would export only `default` (esbuild cannot see a CommonJS module's names), so a
  // pack's `import { useState } from "react"` would not link. Each gets a facade that imports the real module (one
  // React for the shell and a pack) and re-exports its default and every own key Node finds on it. The facades are
  // served from memory by a plugin (never written beside the output, so concurrent builds share no file), and the keys
  // are enumerated under NODE_ENV=production in a child process: the bundle defines NODE_ENV so, and React's
  // development build has keys (act, captureOwnerStack) the shipped code lacks.
  const facades = new Map();
  for (const name of ["react", "react/jsx-runtime"].filter((n) => spec.contracts.includes(n))) {
    const real = contracts.get(name);
    const listed = execFileSync(process.execPath, ["-e", "console.log(JSON.stringify(Object.keys(require(process.argv[1]))))", real],
      { env: { ...process.env, NODE_ENV: "production" }, encoding: "utf8" });
    const keys = JSON.parse(listed)
      .filter((k) => /^[A-Za-z_$][\w$]*$/.test(k) && k !== "default" && k !== "__esModule");
    const virtual = `rp-facade:${name}`;
    facades.set(virtual, {
      resolveDir: path.dirname(real),
      contents: [
        `// Generated by tools/build-shared-modules.mjs: ${name} as an ES module with named exports.`,
        `import * as m from ${JSON.stringify(real)};`,
        "const d = m.default ?? m;",
        "export default d;",
        ...keys.map((k) => `export const ${k} = d.${k};`),
        "",
      ].join("\n"),
    });
    contracts.set(name, virtual);
  }
  const facadePlugin = {
    name: "contract-facades",
    setup(build) {
      build.onResolve({ filter: /^rp-facade:/ }, (args) => ({ path: args.path, namespace: "rp-facade" }));
      build.onLoad({ filter: /.*/, namespace: "rp-facade" }, (args) => ({ ...facades.get(args.path), loader: "js" }));
    },
  };

  const jsDir = path.join(outDir, "js");
  const entryPoints = [
    ...spec.hosts.map((h) => ({ in: h.entry, out: `rp/${h.name.slice(3)}` })),
    ...spec.contracts.map((s) => ({ in: contracts.get(s), out: s })),
  ];
  const result = await esbuild.build({
    ...base,
    plugins: [facadePlugin, ...(base.plugins ?? [])],
    entryPoints,
    outdir: jsDir,
    splitting: true,
    format: "esm",
    chunkNames: "chunks/[name]-[hash]",
    metafile: true,
    write: false,
    logLevel: "error",
  });

  // Every output's module name, by its absolute path.
  const outputs = Object.entries(result.metafile.outputs).filter(([o]) => o.endsWith(".js"));
  const nameOf = new Map();
  for (const [o, meta] of outputs) {
    const abs = path.resolve(base.absWorkingDir ?? REPO, o);
    const rel = posix(path.relative(jsDir, abs)).replace(/\.js$/, "");
    if (meta.entryPoint) {
      const host = spec.hosts.find((h) => `rp/${h.name.slice(3)}` === rel);
      nameOf.set(abs, host ? host.name : rel);
    } else {
      nameOf.set(abs, `rp:${rel}`);
    }
  }
  const textOf = new Map(result.outputFiles.filter((f) => f.path.endsWith(".js")).map((f) => [f.path, f.text]));

  // Rewrite each relative import to the bare name of the output it reaches, then refuse any relative specifier left.
  const deps = new Map();
  const externalsOf = new Map();
  const exportsOf = new Map();
  for (const [o, meta] of outputs) {
    const abs = path.resolve(base.absWorkingDir ?? REPO, o);
    let text = textOf.get(abs);
    const mine = [];
    for (const imp of meta.imports) {
      if (imp.external) continue;
      if (imp.kind !== "import-statement") {
        throw new Error(`build-shared-modules: ${o} has a ${imp.kind} of ${imp.path}: only static imports can be named`);
      }
      const target = path.resolve(base.absWorkingDir ?? REPO, imp.path);
      let rel = posix(path.relative(path.dirname(abs), target));
      if (!rel.startsWith(".")) rel = `./${rel}`;
      const quoted = JSON.stringify(rel);
      const re = new RegExp(`(\\bfrom\\s*|\\bimport\\s*)${quoted.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`, "g");
      text = text.replace(re, `$1${JSON.stringify(nameOf.get(target))}`);
      mine.push(target);
    }
    // (A dynamic import is refused above, from the metafile: a text test for `import(` would also match a method named
    // import, which the real UI calls.)
    if (/\b(?:from|import)\s*["']\.\.?\//.test(text)) {
      throw new Error(`build-shared-modules: ${o} keeps a relative import after the rewrite`);
    }
    textOf.set(abs, text);
    deps.set(abs, mine);
    externalsOf.set(abs, meta.imports.filter((i) => i.external).map((i) => i.path));
    exportsOf.set(abs, meta.exports ?? []);
  }

  // Dependencies first; a cycle cannot be read in any order, so it is refused naming its modules.
  const isHost = (abs) => spec.hosts.some((h) => h.name === nameOf.get(abs));
  const order = [];
  const state = new Map();
  const visit = (abs, trail) => {
    if (state.get(abs) === "done") return;
    if (state.get(abs) === "open") {
      throw new Error(`build-shared-modules: an import cycle: ${[...trail, abs].map((a) => nameOf.get(a)).join(" -> ")}`);
    }
    state.set(abs, "open");
    for (const d of deps.get(abs) ?? []) visit(d, [...trail, abs]);
    state.set(abs, "done");
    order.push(abs);
  };
  for (const [abs] of nameOf) visit(abs, []);
  // Each module as the tests read it: its name, file and text, the names it imports (the set's modules, then what the
  // set leaves external) and the names it exports.
  const moduleOf = (abs) => ({
    name: nameOf.get(abs), file: abs, text: textOf.get(abs),
    imports: [...(deps.get(abs) ?? []).map((d) => nameOf.get(d)), ...(externalsOf.get(abs) ?? [])],
    exports: exportsOf.get(abs) ?? [],
  });
  const shared = order.filter((abs) => !isHost(abs)).map(moduleOf);
  const hosts = spec.hosts.map((h) => moduleOf([...nameOf].find(([, n]) => n === h.name)[0]));

  // Each host's closure: the source inputs of every output it reaches.
  const inputsOf = new Map(outputs.map(([o, meta]) => [path.resolve(base.absWorkingDir ?? REPO, o), Object.keys(meta.inputs)]));
  const closures = {};
  for (const h of hosts) {
    const seen = new Set();
    const walk = (abs) => {
      if (seen.has(abs)) return;
      seen.add(abs);
      for (const d of deps.get(abs) ?? []) walk(d);
    };
    walk(h.file);
    closures[h.name] = [...new Set([...seen].flatMap((abs) => inputsOf.get(abs) ?? []))]
      .map((i) => path.resolve(base.absWorkingDir ?? REPO, i));
  }

  if (write) {
    fs.rmSync(jsDir, { recursive: true, force: true });
    for (const m of [...shared, ...hosts]) {
      fs.mkdirSync(path.dirname(m.file), { recursive: true });
      fs.writeFileSync(m.file, m.text);
    }
    fs.writeFileSync(path.join(outDir, "shared.tsv"), shared.map((m) => `${m.name}\t${m.file}`).join("\n") + "\n");
    fs.writeFileSync(path.join(outDir, "hosts.tsv"), hosts.map((m) => `${m.name}\t${m.file}`).join("\n") + "\n");
  }
  return { shared, hosts, closures, metafile: result.metafile };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const argv = process.argv.slice(2);
  const at = argv.indexOf("--product-dir");
  const productDir = at === -1 ? undefined : argv.splice(at, 2)[1];
  const [set, outDir] = argv;
  if (!set || !outDir || (at !== -1 && !productDir)) {
    console.error("usage: node tools/build-shared-modules.mjs <app|cli> <out-dir> [--product-dir <dir>]");
    process.exit(2);
  }
  buildSharedModules({ set, outDir: path.resolve(outDir), productDir: productDir && path.resolve(productDir) })
    .then((r) => console.log(`shared modules (${set}): ${r.shared.length} shared, ${r.hosts.length} hosts -> ${outDir}`))
    .catch((e) => {
      console.error(e.message);
      process.exit(1);
    });
}
