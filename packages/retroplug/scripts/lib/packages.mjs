#!/usr/bin/env node
// The TS package generator (docs/modular-graph-phase-f1-design.md section 3): from the product list and the packages'
// manifests, writes the three gitignored modules the shell builds against.
//
//   packages/retroplug/src/generated/packages.ts     the app's registrations, in product order (the list's, then the
//                                                    shell): the roles buildAppRegistry applies, the ROM providers, the
//                                                    tracker integrations and the kit formats, and a duplicate in any
//                                                    list a key or an exclusive marker identifies refused at load
//   packages/retroplug/src/generated/protocol.ts     PROTOCOL_NODES and ROLE_NODES, the wiring buildGraph reads, and
//                                                    CONTROLLER_SYNC, the sync mode the cart an app's launches drive
//                                                    must be in. It imports nothing but types, so a bundle of
//                                                    buildGraph carries the tables without a single node's code, no
//                                                    role's schema and no app's
//   packages/retroplug/src/generated/scriptNodes.ts  the script-node bundle's entry, registering each package's
//                                                    controller app runtime and then its node definitions, in
//                                                    span-label order
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
// as). The app's lists are the product order's, and only a ROM provider's place in it is behaviour: the providers run
// in order, and the first to match a ROM fixes the feature roles' order. A role's kind, a kit format's id and a
// tracker integration's marker role are looked up, not sequenced (one package's roles interleave another's without
// consequence), so the generated module refuses a duplicate rather than let one silently replace another.
//
// A @retroplug dependency the list does not carry is refused only when it contributes (its manifest has an app or
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

// The controller app sync override's fields: the node type it applies to, the mode it sets, the polled SYNC value that
// counts as the cart listening and the mode a cart that is not listening is put in. All data: nothing is imported.
const SYNC_KEYS = ["node", "mode", "listening", "off"];

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
  // The name a consumer imports is the directory's, here and in the boundary check (which reads packages/<name>): a
  // manifest that names another package would have one of the two say something neither the tree nor pnpm links.
  const expected = `@retroplug/${name}`;
  if (json.name !== expected) {
    throw new Error(`${manifest} names the package ${JSON.stringify(json.name)}, not ${expected}: a package's name is ` +
      "its directory's");
  }
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

  // A package that contributes an app module or protocol nodes is imported by the generated modules BY ITS PACKAGE NAME,
  // and pnpm links it into the shell's node_modules only when the shell's own manifest declares it: without the
  // declaration the bundle cannot resolve it, and the bundler's message names no manifest. The shell is last in
  // `packages`, so its own manifest is the list's own.
  const shell = packages[packages.length - 1];
  const shellDeps = isObject(shell.manifestJson.dependencies) ? Object.keys(shell.manifestJson.dependencies) : [];
  for (const pkg of packages.slice(0, -1)) {
    if (contributes(pkg) && !shellDeps.includes(`@retroplug/${pkg.name}`)) {
      throw new Error(`${SHELL} does not declare @retroplug/${pkg.name} in its dependencies, which its generated ` +
        "modules import by name: a package that contributes to the app is the shell's dependency");
    }
  }

  return packages;
}

/** A controller app's sync override, from its manifest entry: plain DATA, because the planner applies it and a bundle
 *  of the planner must carry no app and no zod. It names the cart sync node type the app's launches need, the mode it
 *  puts that node in, and the polled SYNC value that counts as the cart listening: a cart polled at anything else is
 *  put in `off` instead, and one that cannot be read at all is assumed willing (the mode). */
function syncRule(pkg, appId, sync) {
  const where = `${pkg.name}: the controller app ${appId}'s syncOverride`;
  if (!isObject(sync)) throw new Error(`${where} must be an object, not ${JSON.stringify(sync)}`);
  for (const key of Object.keys(sync)) {
    if (!SYNC_KEYS.includes(key)) {
      throw new Error(`${where} has ${JSON.stringify(key)}, which nothing reads (its keys are ${SYNC_KEYS.join(", ")})`);
    }
  }
  if (typeof sync.node !== "string" || sync.node === "") {
    throw new Error(`${where} needs a "node": the cart sync node type its mode is set on`);
  }
  if (typeof sync.mode !== "string" || sync.mode === "") {
    throw new Error(`${where} needs a "mode": the mode that node is put in`);
  }
  if (sync.listening === undefined) {
    if (sync.off !== undefined) {
      throw new Error(`${where} names an "off" mode and no "listening" value, so the mode it is for is never used`);
    }
    return { node: sync.node, mode: sync.mode };
  }
  if (typeof sync.listening !== "string" || sync.listening === "") {
    throw new Error(`${where}'s "listening" must be the polled SYNC value that counts as listening, not ` +
      JSON.stringify(sync.listening));
  }
  if (typeof sync.off !== "string" || sync.off === "") {
    throw new Error(`${where} needs an "off": the mode a cart polled at anything but ${JSON.stringify(sync.listening)} ` +
      "is put in");
  }
  return { node: sync.node, mode: sync.mode, listening: sync.listening, off: sync.off };
}

/** What one package contributes, from its manifest: its app module and its protocol nodes, each checked for what the
 *  generated modules need. */
function contributions(pkg) {
  const out = { app: null, protocol: null, nodes: [], roles: {}, apps: [] };
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
  // `module` is the file that default-exports the nodes, so a package that contributes none (an app bridge does) has
  // nothing to name: requiring one made it ship an empty list nobody reads, which is what a new pack would copy. A
  // module with no nodes to export is the other half of the same mistake, and is refused.
  const nodeList = protocol.nodes ?? [];
  if (!Array.isArray(nodeList)) throw new Error(`${pkg.name}: retroplug.protocol.nodes is not a list`);
  if (protocol.module === undefined) {
    if (nodeList.length) {
      throw new Error(`${pkg.name}: retroplug.protocol needs a "module", the file that default-exports its nodes`);
    }
  } else {
    if (typeof protocol.module !== "string" || protocol.module === "") {
      throw new Error(`${pkg.name}: retroplug.protocol's module must be a module path, not ` +
        JSON.stringify(protocol.module));
    }
    if (!nodeList.length) {
      throw new Error(`${pkg.name}: retroplug.protocol names the module ${JSON.stringify(protocol.module)} and ` +
        "declares no nodes, so the module would export nothing: drop the module");
    }
    out.protocol = entryFile(pkg, "protocol's module", protocol.module);
  }
  // Built as new objects: the parsed manifest is read again by later checks, and writing derived fields onto it
  // would make its own keys look unknown to them.
  const apps = protocol.apps ?? [];
  if (!Array.isArray(apps)) throw new Error(`${pkg.name}: retroplug.protocol.apps is not a list`);
  for (const app of apps) {
    if (!isObject(app) || typeof app.id !== "string" || app.id === "") {
      throw new Error(`${pkg.name}: every retroplug.protocol.apps entry needs an "id", the app's stable id`);
    }
    if (typeof app.module !== "string" || app.module === "") {
      throw new Error(`${pkg.name}: the controller app ${app.id} needs a "module", the file that default-exports its ` +
        "runtime (its factory, its playback model and its target)");
    }
    for (const key of Object.keys(app)) {
      if (key !== "id" && key !== "module" && key !== "syncOverride") {
        throw new Error(`${pkg.name}: the controller app ${app.id} has ${JSON.stringify(key)}, which no runtime reads ` +
          "(an app's keys are id, module, syncOverride)");
      }
    }
    out.apps.push({
      id: app.id,
      file: entryFile(pkg, "protocol's apps", app.module),
      sync: app.syncOverride === undefined ? null : syncRule(pkg, app.id, app.syncOverride),
    });
  }
  for (const node of nodeList) {
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

/** Every package's controller app runtimes as one table: an id is looked up by the script bundle and stored in a
 *  project, so an id two packages declare is refused here, naming both, rather than one silently replacing the other. */
function appTable(packages) {
  const byId = new Map();
  for (const { pkg, apps } of packages) {
    for (const app of apps) {
      const at = byId.get(app.id);
      if (at) {
        throw new Error(`two packages declare the controller app ${app.id}: ${at.pkg} and ${pkg.name}`);
      }
      byId.set(app.id, { app, pkg: pkg.name });
    }
  }
  return byId;
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

/** A manifest's `exports` as its entries: the key and the target string it maps to. Only a plain string target is read:
 *  a package that publishes a contribution under a condition (`{ "import": ... }`) must spell it plainly, because this
 *  is the one place a generated import's specifier is decided. */
function exportEntries(manifest) {
  const exports = manifest.exports;
  if (typeof exports === "string") return [{ key: ".", target: exports }];
  if (!isObject(exports)) return [];
  const entries = [];
  for (const [key, value] of Object.entries(exports)) {
    if (typeof value === "string") entries.push({ key, target: value });
  }
  return entries;
}

/** How a generated module imports a package's contribution: the shell's by a relative path (it is the tree the
 *  generated file lives in), every other package by its name and the export key its manifest publishes the file at
 *  ("ts/app.ts", published at "./app", is "@retroplug/core/app"; at ".", just the name). The manifest names a FILE, and
 *  a consumer resolves the name and path a package's `exports` declare, so the contribution must be published at the key
 *  whose target is that same file: a convention-derived specifier would load another module (a wrong target) or nothing
 *  (a missing key), and only the bundler would say so. */
function moduleSpecifier(outDir, pkg, file) {
  if (pkg.name === SHELL) return specifier(outDir, file);
  const rel = relative(pkg.dir, file).split(/[\\/]/).join("/");
  if (!rel.startsWith("ts/")) {
    throw new Error(`${pkg.name}: ${rel} is outside its ts/ tree, so its exports cannot publish it`);
  }
  const target = `./${rel}`;
  const entries = exportEntries(pkg.manifestJson);
  const published = entries.find((entry) => entry.target === target);
  if (!published) {
    const listed = entries.length ? entries.map((entry) => `${entry.key} -> ${entry.target}`).join(", ") : "none";
    throw new Error(`${pkg.name}: its manifest names ${rel}, which its exports do not publish: the generated import ` +
      `needs the entry whose target is ${target} (its exports: ${listed})`);
  }
  const name = pkg.manifestJson.name;
  return published.key === "." ? name : `${name}/${published.key.replace(/^\.\//, "")}`;
}

function contributionLines(packages, member) {
  return packages.filter((p) => p.app).map((p) => `  ...(${identifier(p.pkg.name)}.${member} ?? []),`);
}

function renderPackages({ packages, outDir }) {
  const app = packages.filter((p) => p.app);
  // The manifests' control apps' runtimes (their protocol.apps), as the ids the app modules' control-plane halves are
  // paired with below.
  const runtimeApps = packages.flatMap((p) => p.apps);
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
    'import type { RoleType, RomProvider } from "@retroplug/core";',
    'import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";',
    'import type { KitFormat } from "@retroplug/dsp/kitContract";',
    'import type { ControllerAppContribution } from "@retroplug/launchpad/registry";',
  );
  for (const p of app) lines.push(`import ${identifier(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.app)}";`);
  lines.push(
    "",
    "/** The app's roles, in product order: the role contract's objects, which the registry holds by kind. */",
    "export const ROLES: readonly RoleType[] = [",
    ...contributionLines(app, "roles"),
    "];",
    "const roleKinds = new Set<string>();",
    "for (const role of ROLES) {",
    "  if (roleKinds.has(role.kind)) {",
    "    throw new Error(`the app's roles declare the role kind ${role.kind} twice`);",
    "  }",
    "  roleKinds.add(role.kind);",
    "}",
    "",
    "/** The ROM providers, in product order: the first to match a ROM fixes its feature roles' order, so this list's",
    " *  order is behaviour. */",
    "export const ROM_PROVIDERS: readonly RomProvider[] = [",
    ...contributionLines(app, "romProviders"),
    "];",
    "",
    "/** The tracker integrations, in product order: a system takes the first whose markerRole it has, and the marker",
    " *  roles are exclusive, so a duplicate is the hazard, not the order. */",
    "export const TRACKER_INTEGRATIONS: readonly TrackerIntegration[] = [",
    ...contributionLines(app, "trackerIntegrations"),
    "];",
    "const markerRoles = new Set<string>();",
    "for (const integration of TRACKER_INTEGRATIONS) {",
    "  if (markerRoles.has(integration.markerRole)) {",
    "    throw new Error(`the app's tracker integrations declare the marker role ${integration.markerRole} twice`);",
    "  }",
    "  markerRoles.add(integration.markerRole);",
    "}",
    "",
    "/** The kit bank formats, by the id the audio driver dispatches with. */",
    "export const KIT_FORMATS: readonly KitFormat<any>[] = [",
    ...contributionLines(app, "kitFormats"),
    "];",
    "const kitIds = new Set<string>();",
    "for (const format of KIT_FORMATS) {",
    "  if (kitIds.has(format.id)) {",
    "    throw new Error(`the app's kit formats declare the id ${format.id} twice`);",
    "  }",
    "  kitIds.add(format.id);",
    "}",
    "",
    "/** The controller apps' CONTROL-PLANE halves, in product order: each app's id, its menu label and the schema its",
    " *  config is parsed with. The runtime half (the app itself, its playback model and its target) is the same",
    " *  package's protocol.apps entry, registered by the script bundle, and the sync mode its launches need is that",
    " *  entry's syncOverride data, in ./protocol.ts. The halves are joined by id, so each id has both and only one of",
    " *  each. */",
    "const CONTROLLER_APP_SOURCES: readonly { pkg: string; apps: readonly ControllerAppContribution[] }[] = [",
    ...app.map((p) => `  { pkg: ${JSON.stringify(p.pkg.name)}, apps: ${identifier(p.pkg.name)}.controllerApps ?? [] },`),
    "];",
    "// A duplicate id is refused naming BOTH packages: a project stores only the id, so one of the two apps would be",
    "// unreachable without a word.",
    "const controllerAppOwners = new Map<string, string>();",
    "for (const source of CONTROLLER_APP_SOURCES) {",
    "  for (const app of source.apps) {",
    "    const owner = controllerAppOwners.get(app.id);",
    "    if (owner !== undefined) {",
    "      throw new Error(`two packages declare the controller app ${app.id}: ${owner} and ${source.pkg}`);",
    "    }",
    "    controllerAppOwners.set(app.id, source.pkg);",
    "  }",
    "}",
    "export const CONTROLLER_APPS: readonly ControllerAppContribution[] = [",
    ...contributionLines(app, "controllerApps"),
    "];",
    "// The runtime halves the manifests declare (their protocol.apps), which the script bundle registers. An app with one",
    "// half and not the other is a document that names an app nothing runs, or a runtime no menu offers.",
    `const RUNTIME_APP_IDS: readonly string[] = [${runtimeApps.map((app) => JSON.stringify(app.id)).join(", ")}];`,
    "for (const id of RUNTIME_APP_IDS) {",
    "  if (!controllerAppOwners.has(id)) {",
    "    throw new Error(`the controller app ${id} has no control-plane half: a manifest declares its runtime and no ` +",
    '      "package\'s app contributes it (its label or its config\'s schema)");',
    "  }",
    "}",
    "for (const controllerApp of CONTROLLER_APPS) {",
    "  if (!RUNTIME_APP_IDS.includes(controllerApp.id)) {",
    "    throw new Error(`the controller app ${controllerApp.id} has no runtime half: no package's protocol.apps ` +",
    '      "declares it (the app, its playback model and its target)");',
    "  }",
    "}",
    "",
  );
  return lines.join("\n");
}

function renderProtocol({ byType, roles, apps }) {
  const lines = [];
  lines.push(
    "// Generated by packages/retroplug/scripts/lib/packages.mjs from the desktop product's package list and the",
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The protocol tables buildGraph, the stores and a DirectGraph wire a system's roles through, as the manifests",
    "// spell them. Nothing but a type is imported here: the data is apart from the node modules it describes, so a",
    "// bundle that reads it carries no node's code, no role's schema and nothing of the app.",
    'import type { ProtocolNodeSpec } from "../graph/protocolNodes";',
    'import type { ControllerSyncRule } from "../graph/controller";',
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
  lines.push(
    "};",
    "",
    "/** The controller apps' sync overrides, by app id: the cart sync node type an app's launches need, the mode it",
    " *  puts that node in, and the polled SYNC value that counts as the cart listening (a cart polled at anything else",
    " *  is put in `off`; one that cannot be read yet is assumed willing). DATA, so the planner applies it without an",
    " *  app or a schema - the app modules have both - and it reaches the DOCUMENT only, never a project: turning the",
    " *  controller off restores whatever the user had. */",
    "export const CONTROLLER_SYNC: Readonly<Record<string, ControllerSyncRule>> = {",
  );
  for (const { apps: list } of apps) {
    for (const app of list) if (app.sync) lines.push(`  ${JSON.stringify(app.id)}: ${JSON.stringify(app.sync)},`);
  }
  lines.push("};", "");
  return lines.join("\n");
}

function renderScriptNodes({ packages, outDir }) {
  const spec = packages.filter((p) => p.protocol);
  const withApps = packages.filter((p) => p.apps.length > 0);
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
  if (!spec.length && !withApps.length) {
    return [
      ...head,
      'import { registerScriptNodes } from "@retroplug/core/register";',
      "",
      "// No package in the desktop product contributes a protocol node or a controller app.",
      "registerScriptNodes([]);",
      "",
    ].join("\n");
  }
  const lines = [
    ...head,
    'import type { DefinedNode } from "@retroplug/core/defineNode";',
    'import { registerScriptNodes } from "@retroplug/core/register";',
  ];
  // Generated local names, checked as they are made: a package and an app id both lose their punctuation to the same
  // underscore, so "a-b" + "c" and "a" + "b-c" would both become app_a_b_c, and tsc would fail on a duplicate import
  // with nothing said about which manifests collided.
  const locals = new Map();
  const local = (name, what) => {
    const at = locals.get(name);
    if (at) throw new Error(`two contributions take the generated identifier ${name}: ${at} and ${what}`);
    locals.set(name, what);
    return name;
  };
  if (withApps.length) {
    lines.push('import { registerControllerApps, type ControllerAppRuntime } from "@retroplug/launchpad/apps";');
  }
  const nodeLocals = new Map();
  for (const p of spec) {
    const name = local(`nodes_${identifier(p.pkg.name).slice(4)}`, `the package ${p.pkg.name}'s nodes module`);
    nodeLocals.set(p.pkg.name, name);
    lines.push(`import ${name} from "${moduleSpecifier(outDir, p.pkg, p.protocol)}";`);
  }
  const appLocals = [];
  for (const p of withApps) {
    for (const app of p.apps) {
      const name = local(`app_${p.pkg.name.replace(/[^A-Za-z0-9_]/g, "_")}_${app.id.replace(/[^A-Za-z0-9_]/g, "_")}`,
        `the package ${p.pkg.name}'s controller app ${app.id}`);
      appLocals.push({ pkg: p.pkg.name, app, name });
      lines.push(`import ${name} from "${moduleSpecifier(outDir, p.pkg, app.file)}";`);
    }
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
  for (const p of spec) lines.push(`  ${nodeLocals.get(p.pkg.name)},`);
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
    "",
  );
  if (withApps.length) {
    lines.push(
      "// The controller apps' runtimes, in product order: the manifests' protocol.apps. Each import is one manifest",
      "// entry's module, so the check is per entry: a module that registers another id would leave a document",
      "// naming an app nothing runs, and the message names the module and both ids rather than a set difference.",
      "const apps: readonly ControllerAppRuntime[] = [",
    );
    for (const entry of appLocals) lines.push(`  ${entry.name},`);
    lines.push(
      "];",
      "const appModules: readonly string[] = [",
    );
    for (const entry of appLocals) lines.push(`  ${JSON.stringify(entry.name)},`);
    lines.push(
      "];",
      "const appIds: readonly string[] = [",
    );
    for (const entry of appLocals) lines.push(`  ${JSON.stringify(entry.app.id)},`);
    lines.push(
      "];",
      "for (let i = 0; i < apps.length; i++) {",
      "  if (apps[i].id !== appIds[i]) {",
      "    throw new Error(`the script bundle's controller app ${appModules[i]} registers ${apps[i].id}, but its ` +",
      "      `manifest entry declares ${appIds[i]}`);",
      "  }",
      "}",
      "registerControllerApps(apps);",
      "",
    );
  }
  lines.push("registerScriptNodes(nodes);", "");
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
  appTable(contributionsOf); // refused here for a duplicate id, naming both packages
  // A sync override names the cart sync node type its mode is set on: a misspelt one would plan and do nothing, since a
  // C++ node ignores a setting its type does not read.
  for (const { pkg, apps } of contributionsOf) {
    for (const app of apps) {
      if (!app.sync) continue;
      if (!byType.has(app.sync.node)) {
        throw new Error(`${pkg.name}: the controller app ${app.id}'s syncOverride names the node ${app.sync.node}, ` +
          "which no package declares");
      }
    }
  }

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
    {
      path: join(outDir, "packages.ts"),
      content: renderPackages({ packages: contributionsOf, outDir }),
    },
    { path: join(outDir, "protocol.ts"), content: renderProtocol({ byType, roles, apps: contributionsOf }) },
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
