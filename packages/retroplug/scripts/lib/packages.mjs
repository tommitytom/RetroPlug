#!/usr/bin/env node
// The TS package generator (docs/modular-graph-phase-f1-design.md section 3): from the product list and the packages'
// manifests, writes the three gitignored modules the shell builds against.
//
//   packages/retroplug/src/generated/packages.ts     the app's registrations, in product order (the list's, then the
//                                                    shell): the roles buildAppRegistry applies, the ROM providers, the
//                                                    tracker integrations and the kit formats
//   packages/retroplug/src/generated/protocol.ts     PROTOCOL_NODES and ROLE_NODES, the wiring buildGraph reads. It
//                                                    imports nothing but types, so a bundle of buildGraph carries the
//                                                    tables without a single node's code or role's schema
//   packages/retroplug/src/generated/scriptNodes.ts  the script-node bundle's entry, importing each package's node
//                                                    definitions and registering them in span-label order
//
// The product list is the root package.json's retroplug.products.desktop, the same array the root CMakeLists.txt reads
// into RP_DESKTOP_PACKAGES (string(JSON)), so the two languages cannot register different products. THE LIST IS ALSO
// THE ORDER: CMake registers a package's native entries in it, and the app lists concatenate in it, so a package must
// be listed after every listed package it depends on. The generator refuses a list that breaks that (naming both
// packages) rather than sorting, because a sort here would put TS in one order and CMake in another.
//
// The shell is the one package name this script fixes rather than reads: packages/retroplug is the app itself, not a
// package a product lists (nothing calls retroplug_package(retroplug)), its own TS tree is where the generated modules
// live, and its manifest contributes the roles and protocol nodes no package owns yet. It closes the list, and no
// package may depend on it.
//
// A package's contributions are its manifest's retroplug.app (a module default-exporting definePackage's lists) and
// retroplug.protocol (its node definitions' module, the nodes' wiring and labels, and which role kind each is placed
// as). A @retroplug dependency the list does not carry is refused only when it contributes (its manifest has an app or
// protocol member), which would silently lose it; one that contributes nothing (a native library a package links, a
// pure TS library with no retroplug member) is left out of the ordering.
//
// It writes only a file whose content changed, so a build that changes nothing touches nothing (CMake and the test
// runners can call it before every bundle without churning mtimes). Run it by hand with:
//
//   node packages/retroplug/scripts/lib/packages.mjs [--root <dir>]
//
// --root names a tree laid out as this one is (a package.json with the same retroplug members, packages/<name>/
// package.json), which is how the generator's tests drive it over fixtures.
import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
// The profiler's span labels start at 16 (spec/08-profiling.md): 1 to 15 are the engine's own stages.
const FIRST_LABEL = 16;
// The shell, the app itself (see the header).
const SHELL = "retroplug";

/** The generated modules' directory inside the shell: src/generated, so a package's module is imported by a path
 *  relative to it and the shell's own contributions read as `../app`. */
const GENERATED = "src/generated";

function readJson(path) {
  let text;
  try {
    text = readFileSync(path, "utf8");
  } catch (e) {
    throw new Error(`${path} cannot be read (${e.code ?? e.message})`);
  }
  try {
    return JSON.parse(text);
  } catch (e) {
    throw new Error(`${path} is not JSON (${e.message})`);
  }
}

function isObject(v) {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}

// The protocol node fields the generated tables carry (renderProtocol's key list): anything else in a manifest's node
// entry is a knob nothing reads, which JSON cannot type-check and esbuild only warns about.
const NODE_KEYS = ["type", "label", "midiIn", "writes", "launches", "serialOut", "midiOut", "out"];

/** `@retroplug/<name>` for a dependency spec, or null for a third-party one. */
function packageOf(spec) {
  const m = /^@retroplug\/(.+)$/.exec(spec);
  return m ? m[1] : null;
}

// A file a manifest names, from the package's directory, refused by name when it is missing so a typo fails the
// generation rather than the bundler.
function entryFile(pkg, member, path) {
  const file = resolve(pkg.dir, path);
  if (!existsSync(file)) {
    throw new Error(`${pkg.name}: retroplug.${member} names ${path}, which does not exist (${file})`);
  }
  return file;
}

function readPackage(root, name) {
  const dir = join(root, "packages", name);
  const manifest = join(dir, "package.json");
  if (!existsSync(manifest)) throw new Error(`the package ${name} has no ${manifest}`);
  const json = readJson(manifest);
  if (!isObject(json.retroplug)) throw new Error(`${manifest} has no retroplug field`);
  return { name, dir, manifest, manifestJson: json, retroplug: json.retroplug };
}

/** Whether a package contributes to the app: its manifest has an `app` or a `protocol` member. */
function contributes(pkg) {
  return pkg.retroplug.app !== undefined || pkg.retroplug.protocol !== undefined;
}

/** Whether an unlisted dependency contributes to the app, judged tolerantly: a package with no manifest, or a manifest
 *  with no retroplug member (a pure TS library a package's tests use), contributes nothing, so it is no product's
 *  business and the generator has no reason to refuse it. */
function unlistedContributes(root, name) {
  const manifest = join(root, "packages", name, "package.json");
  if (!existsSync(manifest)) return false;
  const json = readJson(manifest);
  return isObject(json.retroplug) && contributes({ retroplug: json.retroplug });
}

/** The product's packages, in the list's order with the shell last. A listed package's declared @retroplug
 *  dependencies are read to hold two rules: a dependency the list does not carry is refused when it would contribute
 *  to the app (the product would lose it), and a dependency listed after the package that needs it is refused (the TS
 *  and CMake orders would disagree). Both messages name the pair. */
function order(root, names) {
  const packages = names.map((name) => readPackage(root, name));
  // The shell closes the list: it is not a package a product carries, and no package may depend on it.
  packages.push(readPackage(root, SHELL));
  const listed = new Set(names);

  for (const pkg of packages) {
    const declared = isObject(pkg.manifestJson.dependencies) ? Object.keys(pkg.manifestJson.dependencies) : [];
    const at = names.indexOf(pkg.name);
    for (const spec of declared) {
      const dep = packageOf(spec);
      if (dep === null || !listed.has(dep)) {
        if (dep === null) continue;
        // The shell is not a product's, and nothing may depend on it whatever it contributes: its own rule, so the
        // message names the rule rather than the list.
        if (dep === SHELL) {
          throw new Error(`${pkg.name} depends on the shell (@retroplug/${SHELL}); no package may`);
        }
        // Not the product's: only a package whose contribution the product would drop is refused.
        if (unlistedContributes(root, dep)) {
          throw new Error(
            `${pkg.name} depends on @retroplug/${dep}, which contributes to the app and the product does not list`,
          );
        }
        continue;
      }
      // The shell is not in the list (it closes it), so its own dependencies have no position to hold; every other
      // package must follow the dependency it names.
      if (at !== -1 && names.indexOf(dep) > at) {
        throw new Error(
          `the product lists ${pkg.name} before @retroplug/${dep}, which it depends on: a package must follow its ` +
            "dependencies",
        );
      }
    }
  }

  return packages;
}

/** What one package contributes, from its manifest: its app module and its protocol nodes, each checked for what the
 *  generated modules need. */
function contributions(pkg) {
  const out = { app: null, protocol: null, nodes: [], roles: {} };
  const retroplug = pkg.retroplug;
  if (retroplug.app !== undefined) {
    if (typeof retroplug.app !== "string" || retroplug.app === "") {
      throw new Error(`${pkg.name}: retroplug.app must be a module path, not ${JSON.stringify(retroplug.app)}`);
    }
    out.app = entryFile(pkg, "app", retroplug.app);
  }
  if (retroplug.protocol === undefined) return out;
  const protocol = retroplug.protocol;
  if (!isObject(protocol)) throw new Error(`${pkg.name}: retroplug.protocol is not an object`);
  if (typeof protocol.module !== "string" || protocol.module === "") {
    throw new Error(`${pkg.name}: retroplug.protocol needs a "module", the file that default-exports its nodes`);
  }
  out.protocol = entryFile(pkg, "protocol's module", protocol.module);
  const nodes = protocol.nodes ?? [];
  if (!Array.isArray(nodes)) throw new Error(`${pkg.name}: retroplug.protocol.nodes is not a list`);
  for (const node of nodes) {
    if (!isObject(node) || typeof node.type !== "string" || node.type === "") {
      throw new Error(`${pkg.name}: every retroplug.protocol.nodes entry needs a "type", the node's id`);
    }
    if (!Number.isInteger(node.label)) {
      throw new Error(`${pkg.name}: the node ${node.type} needs a "label", a whole number (its span label)`);
    }
    for (const key of Object.keys(node)) {
      if (!NODE_KEYS.includes(key)) {
        throw new Error(
          `${pkg.name}: the node ${node.type} has ${JSON.stringify(key)}, which no protocol node reads ` +
            `(a node's keys are ${NODE_KEYS.join(", ")})`,
        );
      }
    }
    if (node.writes !== undefined && !Array.isArray(node.writes)) {
      throw new Error(`${pkg.name}: the node ${node.type}'s writes is not a list`);
    }
    // Wired or not is one decision: renderProtocol emits a node into PROTOCOL_NODES on either field, so a node with
    // one and not the other would be placed with a hole (a MIDI route into no input, or an input nothing routes to)
    // that only tsc's `satisfies` would see, and esbuild builds through that.
    const midiIn = Object.hasOwn(node, "midiIn");
    const writes = Object.hasOwn(node, "writes");
    if (midiIn !== writes) {
      throw new Error(
        `${pkg.name}: the node ${node.type} declares ${midiIn ? "midiIn" : "writes"} without ` +
          `${midiIn ? "writes" : "midiIn"}: a wired node declares both (an unwired one neither)`,
      );
    }
    out.nodes.push(node);
  }
  out.roles = protocol.roles ?? {};
  if (!isObject(out.roles)) throw new Error(`${pkg.name}: retroplug.protocol.roles is not an object`);
  for (const [kind, type] of Object.entries(out.roles)) {
    if (typeof type !== "string") throw new Error(`${pkg.name}: the role ${kind} does not name a node type`);
  }
  return out;
}

/** Every package's nodes as one table, with the labels the manifests fix: a type declared twice, a label used twice, a
 *  label below 16 or a gap in the labels is refused here, so the bundle cannot register the wrong type under a label
 *  (spec/08-profiling.md) or leave a hole one would inherit. */
function nodeTable(packages) {
  const byType = new Map();
  const byLabel = new Map();
  for (const { pkg, nodes } of packages) {
    for (const node of nodes) {
      const at = byType.get(node.type);
      if (at) throw new Error(`two packages declare the script node ${node.type}: ${at.pkg} and ${pkg.name}`);
      byType.set(node.type, { node, pkg: pkg.name });
      const holder = byLabel.get(node.label);
      if (holder) {
        throw new Error(
          `two script nodes have the span label ${node.label}: ${holder.node.type} (${holder.pkg}) and ` +
            `${node.type} (${pkg.name})`,
        );
      }
      byLabel.set(node.label, { node, pkg: pkg.name });
    }
  }
  const labels = [...byLabel.keys()].sort((a, b) => a - b);
  const gap = (why) => {
    throw new Error(
      `the script nodes' span labels must run from ${FIRST_LABEL} with no gap: ${labels.join(", ")} (${why})`,
    );
  };
  if (labels.length && labels[0] !== FIRST_LABEL) {
    gap(labels[0] < FIRST_LABEL ? `${labels[0]} is below ${FIRST_LABEL}` : `${FIRST_LABEL} is missing`);
  }
  for (let i = 0; i < labels.length; i++) {
    // Duplicates are refused above, so a label out of place here is one the list skipped over.
    if (labels[i] !== FIRST_LABEL + i) gap(`${FIRST_LABEL + i} is missing`);
  }
  return { byType };
}

/** The role kinds the protocol members declare, each with its node type and the package that declares it: a kind has
 *  one owner, as a node type and a span label do, since the generated ROLE_NODES object would otherwise keep whichever
 *  package came last (or fail tsc, and esbuild only warns). */
function roleTable(packages) {
  const kinds = new Map();
  for (const { pkg, roles } of packages) {
    for (const [kind, type] of Object.entries(roles)) {
      const at = kinds.get(kind);
      if (at) throw new Error(`two packages declare the role kind ${kind}: ${at.pkg} and ${pkg.name}`);
      kinds.set(kind, { kind, type, pkg: pkg.name });
    }
  }
  return kinds;
}

// A module path as the generated file imports it: relative, POSIX separators, no .ts (the bundler and tsc resolve it).
function specifier(fromDir, file) {
  let spec = relative(fromDir, file).replace(/\\/g, "/").replace(/\.ts$/, "");
  if (!spec.startsWith(".")) spec = `./${spec}`;
  return spec;
}

// A package name as an import identifier: graph-runtime -> app_graph_runtime.
function identifier(name) {
  return `app_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
}

function contributionLines(packages, member) {
  return packages.filter((p) => p.app).map((p) => `  ...(${identifier(p.pkg.name)}.${member} ?? []),`);
}

function renderPackages({ packages, outDir }) {
  const app = packages.filter((p) => p.app);
  const lines = [];
  lines.push(
    "// Generated by packages/retroplug/scripts/lib/packages.mjs from the desktop product's package list (the root",
    "// package.json's retroplug.products.desktop) and the packages' manifests. Do not edit; edit a manifest and",
    "// rebuild.",
    "//",
    "// The app's registrations, in product order (the list's, which is a dependency order, then the shell): the roles",
    "// buildAppRegistry applies, the ROM providers it offers, the tracker integrations the Songs and Assets menus",
    "// search, and the kit formats the audio driver dispatches to. The protocol tables buildGraph reads are apart, in",
    "// ./protocol.ts, so a module that needs them imports no package's app.",
    'import type { RoleType, RomProvider } from "../systemRoles";',
    'import type { TrackerIntegration } from "../tracker/trackerIntegration";',
    'import type { KitFormat } from "../kit/compile";',
  );
  for (const p of app) lines.push(`import ${identifier(p.pkg.name)} from "${specifier(outDir, p.app)}";`);
  lines.push(
    "",
    "/** The app's roles, in product order: the role contract's objects, which the registry holds. */",
    "export const ROLES: readonly RoleType[] = [",
    ...contributionLines(app, "roles"),
    "];",
    "",
    "/** The ROM providers, in product order: a ROM's feature roles are attached by the first matches' order. */",
    "export const ROM_PROVIDERS: readonly RomProvider[] = [",
    ...contributionLines(app, "romProviders"),
    "];",
    "",
    "/** The tracker integrations, in product order: a system takes the first whose markerRole it has. */",
    "export const TRACKER_INTEGRATIONS: readonly TrackerIntegration[] = [",
    ...contributionLines(app, "trackerIntegrations"),
    "];",
    "",
    "/** The kit bank formats, by the id the audio driver dispatches with. */",
    "export const KIT_FORMATS: readonly KitFormat<any>[] = [",
    ...contributionLines(app, "kitFormats"),
    "];",
    "",
  );
  return lines.join("\n");
}

function renderProtocol({ byType, roles }) {
  const lines = [];
  lines.push(
    "// Generated by packages/retroplug/scripts/lib/packages.mjs from the desktop product's package list and the",
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The protocol tables buildGraph, the stores and a DirectGraph wire a system's roles through, as the manifests",
    "// spell them. Nothing but a type is imported here: the data is apart from the node modules it describes, so a",
    "// bundle that reads it carries no node's code, no role's schema and nothing of the app.",
    'import type { ProtocolNodeSpec } from "../graph/protocolNodes";',
    "",
    "/** How buildGraph and the stores wire each protocol node type (graph/protocolNodes.ts's ProtocolNodeSpec): the",
    " *  input its system's routed MIDI arrives on, the system inputs it writes, and the wires it takes or makes. In",
    " *  span-label order, so which package declares a node changes nothing here. */",
  );
  const specs = [...byType.values()].sort((a, b) => a.node.label - b.node.label);
  const wired = specs.filter(({ node }) => node.midiIn !== undefined || node.writes !== undefined);
  if (!wired.length) {
    lines.push("export const PROTOCOL_NODES = {} as const satisfies Record<string, ProtocolNodeSpec>;");
  } else {
    lines.push("export const PROTOCOL_NODES = {");
    for (const { node } of wired) {
      const fields = [];
      for (const key of ["midiIn", "writes", "launches", "serialOut", "midiOut", "out"]) {
        if (node[key] === undefined) continue;
        const value = Array.isArray(node[key])
          ? `[${node[key].map((v) => JSON.stringify(v)).join(", ")}]`
          : JSON.stringify(node[key]);
        fields.push(`${key}: ${value}`);
      }
      lines.push(`  ${JSON.stringify(node.type)}: { ${fields.join(", ")} },`);
    }
    lines.push("} as const satisfies Record<string, ProtocolNodeSpec>;");
  }
  lines.push(
    "",
    "/** The role kinds that have a protocol node, each with the type it is placed as (the ids PROTOCOL_NODES wires),",
    " *  in the order of the labels they are placed at. */",
    "export const ROLE_NODES: Readonly<Record<string, keyof typeof PROTOCOL_NODES>> = {",
  );
  for (const { kind, type } of roles) lines.push(`  ${JSON.stringify(kind)}: ${JSON.stringify(type)},`);
  lines.push("};", "");
  return lines.join("\n");
}

function renderScriptNodes({ packages, outDir }) {
  const spec = packages.filter((p) => p.protocol);
  const head = [
    "// Generated by packages/retroplug/scripts/lib/packages.mjs from the desktop product's package list and the",
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The script-node bundle's entry: every package's protocol node definitions, registered in span-label order",
    "// (spec/08-profiling.md: one label a type, 16 up), which the manifests' retroplug.protocol.nodes fix whatever",
    "// order a package lists its exports in. esbuilt to a global-code IIFE, compiled to QuickJS bytecode and",
    "// embedded in the backend (packages/native/cmake/bundles.cmake), so every Engine loads it into both of its",
    "// script contexts.",
  ];
  if (!spec.length) {
    return [
      ...head,
      'import { registerScriptNodes } from "../script/register";',
      "",
      "// No package in the desktop product contributes a protocol node.",
      "registerScriptNodes([]);",
      "",
    ].join("\n");
  }
  const lines = [
    ...head,
    'import type { DefinedNode } from "../graph/defineNode";',
    'import { registerScriptNodes } from "../script/register";',
  ];
  for (const p of spec) {
    const id = identifier(p.pkg.name).slice(4);
    lines.push(`import nodes_${id} from "${specifier(outDir, p.protocol)}";`);
  }
  lines.push(
    "",
    "// The host numbers the nodes it is handed from the first protocol span label up (spec/08-profiling.md).",
    `const FIRST_LABEL = ${FIRST_LABEL};`,
    "",
    "// The span label of each node type the product's manifests declare.",
    "const LABELS: Readonly<Record<string, number | undefined>> = {",
  );
  for (const p of spec) for (const node of p.nodes) lines.push(`  ${JSON.stringify(node.type)}: ${node.label},`);
  lines.push(
    "};",
    "",
    "function labelOf(node: DefinedNode<any>): number {",
    "  const label = LABELS[node.id];",
    "  if (label === undefined) {",
    "    throw new Error(`the script node ${node.id} has no span label (a package's retroplug.protocol.nodes)`);",
    "  }",
    "  return label;",
    "}",
    "",
    "// Each package's definitions, in product order; the order the bundle registers them in is the labels', below.",
    "const lists: readonly (readonly DefinedNode<any>[])[] = [",
  );
  for (const p of spec) lines.push(`  nodes_${identifier(p.pkg.name).slice(4)},`);
  lines.push(
    "];",
    "",
    "const nodes: DefinedNode<any>[] = [];",
    "for (const list of lists) for (const node of list) nodes.push(node);",
    "nodes.sort((a, b) => labelOf(a) - labelOf(b));",
    "",
    "// A label is not a node's own: the host numbers what it is handed, in order (ScriptHost's spanLabelFor), so a",
    "// manifest node its module does not export would shift every later type one label down, and a node no manifest",
    "// declares would take the label of one that is missing. Refuse the bundle unless what the modules define is",
    "// exactly the manifests' declarations, each at its own label.",
    "const declared = Object.keys(LABELS);",
    "const missing = declared.filter((type) => !nodes.some((node) => node.id === type));",
    "const extra = nodes.filter((node) => LABELS[node.id] === undefined).map((node) => node.id);",
    "if (missing.length || extra.length) {",
    "  throw new Error(",
    "    `the script bundle's nodes are not the manifests': ${nodes.length} defined, ${declared.length} declared; ` +",
    "      (missing.length",
    "        ? `${missing.join(\", \")} has no definition`",
    "        : `${extra.join(\", \")} is declared by no manifest`),",
    "  );",
    "}",
    "for (let i = 0; i < nodes.length; i++) {",
    "  if (labelOf(nodes[i]) !== FIRST_LABEL + i) {",
    "    throw new Error(",
    "      `the script node ${nodes[i].id} would take the span label ${FIRST_LABEL + i}, ` +",
    "        `not its ${labelOf(nodes[i])}`,",
    "    );",
    "  }",
    "}",
    "registerScriptNodes(nodes);",
    "",
  );
  return lines.join("\n");
}

// One file's content, written only when it differs from what is there: through a per-run temporary name (two
// generators can run at once: two build trees, or `pnpm test` beside `pnpm test:native`) and a rename.
function writeIfChanged(path, content, stamp) {
  let current = null;
  try {
    current = readFileSync(path, "utf8");
  } catch {
    // no file yet
  }
  if (current === content) return false;
  const temporary = `${path}.${process.pid}.${stamp}.tmp`;
  writeFileSync(temporary, content);
  try {
    renameSync(temporary, path);
  } catch (e) {
    // Windows refuses a rename onto a file another process holds open, which is the other generator of a parallel
    // run: when it has already written these bytes, this one's copy is spent; otherwise try the rename once more.
    let same = false;
    try {
      same = readFileSync(path, "utf8") === content;
    } catch {
      // the file is gone, so the rename is worth another try
    }
    if (same) rmSync(temporary, { force: true });
    else renameSync(temporary, path);
  }
  return true;
}

/** Generate the three modules under `<root>/packages/<shell>/src/generated`. Returns the ordered package names and, for
 *  each file, whether it was written (it is not, when its content is unchanged). */
export function generate({ root = REPO } = {}) {
  const manifestPath = join(root, "package.json");
  const manifest = readJson(manifestPath);
  const retroplug = isObject(manifest.retroplug) ? manifest.retroplug : {};
  const products = isObject(retroplug.products) ? retroplug.products : {};
  const names = products.desktop;
  if (!Array.isArray(names) || !names.length) {
    throw new Error(`${manifestPath} has no retroplug.products.desktop list of package names`);
  }
  if (names.some((name) => typeof name !== "string" || name === "")) {
    throw new Error(`${manifestPath} lists a package name that is not a name in retroplug.products.desktop`);
  }
  if (new Set(names).size !== names.length) {
    throw new Error(`${manifestPath} lists a package twice in retroplug.products.desktop`);
  }
  if (names.includes(SHELL)) {
    throw new Error(`${manifestPath} lists the shell (${SHELL}) in retroplug.products.desktop`);
  }

  const ordered = order(root, names);
  const shell = ordered[ordered.length - 1];
  const outDir = join(shell.dir, GENERATED);
  const contributionsOf = ordered.map((pkg) => ({ pkg, ...contributions(pkg) }));
  const { byType } = nodeTable(contributionsOf);
  const kinds = roleTable(contributionsOf);

  // A role's node must be one PROTOCOL_NODES wires, or buildGraph would place a node it cannot lay out.
  for (const { kind, type, pkg } of kinds.values()) {
    if (!byType.has(type)) throw new Error(`${pkg}: the role ${kind} names ${type}, which no package declares`);
    if (!wiresNode(type, byType)) {
      throw new Error(`${pkg}: the role ${kind} names ${type}, which declares no midiIn and writes to wire it`);
    }
  }
  const roles = [...kinds.values()]
    .map((role) => ({ ...role, label: byType.get(role.type).node.label }))
    .sort((a, b) => a.label - b.label || (a.kind < b.kind ? -1 : a.kind > b.kind ? 1 : 0));

  const files = [
    { path: join(outDir, "packages.ts"), content: renderPackages({ packages: contributionsOf, outDir }) },
    { path: join(outDir, "protocol.ts"), content: renderProtocol({ byType, roles }) },
    { path: join(outDir, "scriptNodes.ts"), content: renderScriptNodes({ packages: contributionsOf, outDir }) },
  ];
  mkdirSync(outDir, { recursive: true });
  const stamp = Math.random().toString(36).slice(2, 10);
  const written = files.map((file) => ({ path: file.path, changed: writeIfChanged(file.path, file.content, stamp) }));
  return { packages: ordered.map((p) => p.name), files: written };
}

// Whether `type` is a node type PROTOCOL_NODES wires: a role is placed as one, so its manifest entry carries its
// midiIn and writes.
function wiresNode(type, byType) {
  const entry = byType.get(type);
  return entry !== undefined && (entry.node.midiIn !== undefined || entry.node.writes !== undefined);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const at = process.argv.indexOf("--root");
  const root = at === -1 ? REPO : resolve(process.argv[at + 1] ?? "");
  try {
    generate({ root });
  } catch (e) {
    console.error(`packages.mjs: ${e.message}`);
    process.exit(1);
  }
}
