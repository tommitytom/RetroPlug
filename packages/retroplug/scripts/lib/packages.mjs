#!/usr/bin/env node
// The TS package generator (docs/modular-graph-phase-f1-design.md section 3): from the product list and the packages'
// manifests, writes the eight gitignored modules the shell builds against.
//
//   packages/retroplug/src/generated/packages.ts     the app's registrations, in product order (the list's, then the
//                                                    shell): the roles buildAppRegistry applies, the ROM providers,
//                                                    the tracker integrations, the kit formats, the project sections
//                                                    and services, the contribution kinds' items, and a duplicate in
//                                                    list a key or an exclusive marker identifies refused at load
//   packages/retroplug/src/generated/consoles.ts      the consoles the shell classifies a ROM with and places systems
//                                                    by, and the physical carts a project can drive, from each
//                                                    package's retroplug.consoles module. It imports only those
//                                                    modules and a type, so a bundle of the planner carries the
//                                                    descriptors without an app module or zod
//   packages/retroplug/src/generated/protocol.ts     PROTOCOL_NODES, ROLE_NODES and DECLARED_NODE_TYPES, the wiring
//                                                    buildGraph reads, and KIND_DATA, the data the contributing
//                                                    manifests declare for each contribution kind's items. It imports
//                                                    nothing but types, so a bundle of buildGraph carries the tables
//                                                    without a single node's code, no role's schema and no item's code
//   packages/retroplug/src/generated/scriptNodes.ts  the script-node bundle's entry, registering each contribution
//                                                    kind's runtime halves (the manifests' entries) and then the node
//                                                    definitions, in span-label order
//   packages/retroplug/src/generated/placers.ts      the graph placers buildGraph reads, one import per declaring
//                                                    package, in product order. Types and constants alone, so the
//                                                    planner's bundle carries no node module and no schema
//   packages/retroplug/ui/generated/menus.ts          the packages' UI contributions, in product order, and the checks
//                                                    their rows and builders are held to at load. Imported only by
//                                                    ui/, so no other bundle carries a menu contribution
//   packages/retroplug/ui/generated/views.ts          the packages' views, in product order, and the checks their ids,
//                                                    placements and bodies are held to at load. Apart from menus.ts so
//                                                    that a React view is reached only from the shell's view slots: the
//                                                    pure tier bundles the menu modules with no alias, and a component
//                                                    view could not resolve React there
//   packages/retroplug/cli/generated/tools.ts         the CLI's subcommands, in product order, with the checks their
//                                                    argument shapes are held to at load
//
// The product list is one of the root package.json's retroplug.products lists, the same ones the root CMakeLists.txt
// reads into RP_PRODUCT_PACKAGES_<name> (through product-lists.mjs, because its JSON reader cannot enumerate an
// object's keys), so the two languages cannot register different products. `--product` names which list (the desktop's
// by default); a list that is not the desktop's must be its subsequence in the same relative order, which the
// configure refuses too. THE LIST IS ALSO THE ORDER: CMake registers a package's native entries in it, and the app
// lists concatenate in it, so a package must be listed after every listed package it depends on. The generator refuses
// a list that breaks that (naming both packages) rather than sorting, because a sort here would put TS in one order and
// CMake in another.
//
// The shell is the one package name this script fixes rather than reads: packages/retroplug is the app itself, not a
// package a product lists (nothing calls retroplug_package(retroplug)), its own TS tree is where the generated modules
// live, and its manifest contributes the roles and protocol nodes no package owns yet. It closes the list, and no
// package may depend on it.
//
// A package's contributions are its manifest's retroplug.app (a module default-exporting definePackage's lists),
// retroplug.consoles (a module default-exporting the console descriptors its cores run and the physical carts one of
// them can drive), retroplug.protocol (its node definitions' module, the nodes' wiring and labels, and which role
// kind each is placed as), retroplug.ui (a module default-exporting defineUi's contribution: the system-settings rows,
// the menu builders, the tracker specs, the UI services and the app actions its package adds) and retroplug.views (a
// module default-exporting defineViews: the window and tile views it draws, apart from the menus because a component
// view reaches React, which the pure tier has no path to). A `ui` or `views` module must sit under the package's
// ts/ui/ tree, where rule 3 admits a React host module from. The consoles are apart from the app
// module because they are data the PLANNER reads, and the app module is where a role's schema and zod are
// (test/script/bundle-protocol.test.ts holds the planner to carrying neither). The app's lists are the product order's,
// and only a ROM provider's place in it is behaviour: the providers run in order, and the first to match a ROM fixes
// the feature roles' order. A role's kind, a kit format's id and a
// tracker integration's marker role are looked up, not sequenced (one package's roles interleave another's without
// consequence), so the generated module refuses a duplicate rather than let one silently replace another.
//
// A @retroplug dependency the list does not carry is refused only when it contributes (its manifest has an app, a
// consoles or a protocol member), which would silently lose it; one that contributes nothing (a native library a
// package links, a pure TS library with no retroplug member) is left out of the ordering.
//
// It writes only a file whose content changed, so a build that changes nothing touches nothing (CMake and the test
// runners can call it before every bundle without churning mtimes). Run it by hand with:
//
//   node packages/retroplug/scripts/lib/packages.mjs [--root <dir>] [--product <name>]
//        [--build-dir <dir>] [--products-dir <dir>]
//
// --root names a tree laid out as this one is (a package.json with the same retroplug members, packages/<name>/
// package.json), which is how the generator's tests drive it over fixtures. --build-dir is where a non-desktop
// product's tree lands (<build-dir>/products/<name>/, the layout CMake's bundle targets read); --products-dir names
// that products directory directly, which is how the typecheck keeps its tree out of build/ (a build directory is
// build.sh's sentinel for a configured tree). The desktop writes into the shell's own tree and takes neither.
import { existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
// The profiler's span labels start at 16 (spec/08-profiling.md): 1 to 15 are the engine's own stages.
const FIRST_LABEL = 16;
// The shell, the app itself (see the header).
const SHELL = "retroplug";
// The product whose tree the shell is built from, and the one whose generated modules the shell's own sources import
// (src/generated, ui/generated, cli/generated). Every other product's tree is written under the build directory.
const DESKTOP = "desktop";

/** The first line of every generated module's header, naming the product list it was generated from. The desktop's
 *  files keep the sentence (and so the bytes) they had; a product's name their own list, so a module under
 *  build/products/plugin/ does not claim to be the desktop's. */
function generatedFrom(product) {
  return product === DESKTOP
    ? "// Generated by packages/retroplug/scripts/lib/packages.mjs from the desktop product's package list"
    : `// Generated by packages/retroplug/scripts/lib/packages.mjs from the ${product} product's package list`;
}

/** A package's example session name: a plain file stem, because it becomes `build/cli/<name>.js` and an entry of the
 *  CMake list that builds the session bundles. */
const EXAMPLE_NAME = /^[A-Za-z0-9_-][A-Za-z0-9._-]*$/;

/** The shell's own example sessions, each bundled to `build/cli/<name>.js`: no package may take one of their names
 *  (the two names would overwrite each other's bundle, and CMake's own refusal does not name the clash). */
const SHELL_EXAMPLES = new Map([
  ["render-rom", "cli/sessions/render-rom.ts"],
  ["render-song", "cli/sessions/render-song.ts"],
  ["rom-test", "cli/sessions/rom-test.ts"],
]);

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
const NODE_KEYS = ["type", "label", "midiIn", "writes", "launches", "serialOut", "midiOut", "capture", "out"];

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

/** A package's `ui` and `views` modules, which must sit under its own ts/ui/ tree: that directory is what rule 3's
 *  surface rule reads (a React host module is importable only from its files) and what the bundle check classifies a
 *  module as UI by, so a module outside it could import React with no declaration being checked for it. */
function uiMemberFile(pkg, member, path, why) {
  const file = entryFile(pkg, member, path);
  const rel = relative(join(pkg.dir, "ts", "ui"), file).split(/[\\/]/).join("/");
  if (rel === "" || rel.startsWith("..")) {
    throw new Error(`${pkg.name}: retroplug.${member} names ${path}, which is outside the package's ts/ui/ tree: ` +
      `${why}, and rule 3 admits a React host module only from there`);
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

/** Whether a package contributes to the app: its manifest has an `app`, a `consoles`, a `protocol` or an `embed`
 *  member (the last is bytes rather than code, and a product that dropped the package would bake nothing in). */
function contributes(pkg) {
  return pkg.retroplug.app !== undefined || pkg.retroplug.consoles !== undefined ||
    pkg.retroplug.protocol !== undefined || pkg.retroplug.embed !== undefined ||
    pkg.retroplug.ui !== undefined || pkg.retroplug.views !== undefined ||
    pkg.retroplug.cli !== undefined;
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
 *  and CMake orders would disagree). Both messages name the pair. The shell's own declarations are the same rule for
 *  the DESKTOP list, which is the union: a contributing package its list leaves out would be imported by name and
 *  generated by nobody. A product list that is not the desktop's may drop one deliberately (the plugin drops the
 *  Launchpad), so the shell's declarations are not checked against it. */
function order(root, names, product = DESKTOP) {
  const packages = names.map((name) => readPackage(root, name));
  // The shell closes the list: it is not a package a product carries, and no package may depend on it.
  packages.push(readPackage(root, SHELL));
  const listed = new Set(names);

  for (const pkg of packages) {
    // The shell declares every package a product MAY carry, not the ones this list does: what it imports is what the
    // generated modules of THIS product name, and a package the list drops gets no module importing it. For the
    // desktop, which is the union, its declarations are held to the list below.
    if (pkg.name === SHELL && product !== DESKTOP) continue;
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

/** What one package contributes, from its manifest: its app module, its consoles module and its protocol nodes, each
 *  checked for what the generated modules need. */
function contributions(pkg) {
  const out = {
    app: null, consoles: null, protocol: null, placer: null, ui: null, views: null, cli: null, examples: [], host: [],
    nodes: [], roles: {}, embeds: [], kinds: {}, kindEntries: [],
  };
  const retroplug = pkg.retroplug;
  // The files this package bakes into a product's binary (`retroplug.embed`), which the app module declares as
  // `embeds` entries (id, label, platform) for the shell to load them by. The generator emits both sides and the
  // generated module pairs them: neither side alone can be loaded.
  if (retroplug.embed !== undefined) {
    if (!Array.isArray(retroplug.embed)) throw new Error(`${pkg.name}: retroplug.embed is not a list`);
    for (const entry of retroplug.embed) {
      if (!isObject(entry) || typeof entry.id !== "string" || entry.id === "") {
        throw new Error(`${pkg.name}: every retroplug.embed entry needs an "id", the resource's name on the wire`);
      }
      if (typeof entry.file !== "string" || entry.file === "") {
        throw new Error(`${pkg.name}: the embedded ROM ${entry.id} needs a "file", the bytes to bake in`);
      }
      if (out.embeds.some((baked) => baked.id === entry.id)) {
        throw new Error(`${pkg.name}: its retroplug.embed lists the id ${entry.id} twice`);
      }
      out.embeds.push({ id: entry.id });
    }
  }
  if (retroplug.app !== undefined) {
    if (typeof retroplug.app !== "string" || retroplug.app === "") {
      throw new Error(`${pkg.name}: retroplug.app must be a module path, not ${JSON.stringify(retroplug.app)}`);
    }
    out.app = entryFile(pkg, "app", retroplug.app);
  }
  if (retroplug.consoles !== undefined) {
    if (typeof retroplug.consoles !== "string" || retroplug.consoles === "") {
      throw new Error(
        `${pkg.name}: retroplug.consoles must be a module path, not ${JSON.stringify(retroplug.consoles)}`,
      );
    }
    out.consoles = entryFile(pkg, "consoles", retroplug.consoles);
  }
  if (retroplug.ui !== undefined) {
    if (typeof retroplug.ui !== "string" || retroplug.ui === "") {
      throw new Error(`${pkg.name}: retroplug.ui must be a module path, not ${JSON.stringify(retroplug.ui)}`);
    }
    out.ui = uiMemberFile(pkg, "ui", retroplug.ui, "the ui module is the package's UI code");
  }
  if (retroplug.views !== undefined) {
    if (typeof retroplug.views !== "string" || retroplug.views === "") {
      throw new Error(`${pkg.name}: retroplug.views must be a module path, not ${JSON.stringify(retroplug.views)}`);
    }
    out.views = uiMemberFile(pkg, "views", retroplug.views, "a views module is UI code");
  }
  if (retroplug.cli !== undefined) {
    if (typeof retroplug.cli !== "string" || retroplug.cli === "") {
      throw new Error(`${pkg.name}: retroplug.cli must be a module path, not ${JSON.stringify(retroplug.cli)}`);
    }
    out.cli = entryFile(pkg, "cli", retroplug.cli);
  }
  // A package's example sessions (`{ "name", "entry" }`): the CMake target bundles each to build/cli/<name>.js, and
  // the entry must live under the package's examples/ tree, beside the TS the shell's program includes.
  if (retroplug.examples !== undefined) {
    if (!Array.isArray(retroplug.examples)) throw new Error(`${pkg.name}: retroplug.examples is not a list`);
    const examplesDir = join(pkg.dir, "examples");
    for (const example of retroplug.examples) {
      if (!isObject(example) || typeof example.name !== "string" || example.name === "") {
        throw new Error(`${pkg.name}: every retroplug.examples entry needs a "name", the session it is run as`);
      }
      // The name IS the bundle's file stem (`build/cli/<name>.js`) and an entry of the CMake list that builds them, so
      // it may not leave that directory or split that list.
      if (!EXAMPLE_NAME.test(example.name)) {
        throw new Error(`${pkg.name}: the example session name ${example.name} is not a file stem: a name must match ` +
          `${EXAMPLE_NAME.source}, because it becomes build/cli/<name>.js and an entry of the CMake list`);
      }
      if (typeof example.entry !== "string" || example.entry === "") {
        throw new Error(`${pkg.name}: the example ${example.name} needs an "entry", its module`);
      }
      const file = resolve(pkg.dir, example.entry);
      if (!file.startsWith(examplesDir + sep)) {
        throw new Error(`${pkg.name}: the example ${example.name}'s entry ${example.entry} is outside its examples/ ` +
          "tree, where the shell's program includes it");
      }
      if (!existsSync(file)) {
        throw new Error(`${pkg.name}: the example ${example.name} names ${example.entry}, which does not exist (${file})`);
      }
      if (out.examples.some((other) => other.name === example.name)) {
        throw new Error(`${pkg.name}: its retroplug.examples lists the name ${example.name} twice`);
      }
      out.examples.push({ name: example.name, entry: file });
    }
  }
  // A host module the package's code imports by bare specifier (the CLI SDK for a `cli` file, React's modules for a
  // `ui` one): the shell's host-modules.json is the list, and the generated typecheck paths come from it.
  if (retroplug.host !== undefined) {
    if (!Array.isArray(retroplug.host)) throw new Error(`${pkg.name}: retroplug.host is not a list`);
    for (const module of retroplug.host) {
      if (typeof module !== "string" || module === "") {
        throw new Error(`${pkg.name}: every retroplug.host entry must be a module specifier`);
      }
      if (out.host.includes(module)) throw new Error(`${pkg.name}: its retroplug.host lists ${module} twice`);
      out.host.push(module);
    }
  }
  // The contribution kinds this package DECLARES for others to fill (docs/modular-graph-phase-f5a-design.md section
  // 6.5): a control-plane module default-exporting defineKind, the runtime registry the script bundle calls, and the
  // keys a contributing manifest may carry as data.
  if (retroplug.kinds !== undefined) {
    if (!isObject(retroplug.kinds)) throw new Error(`${pkg.name}: retroplug.kinds is not an object`);
    for (const [kind, spec] of Object.entries(retroplug.kinds)) {
      if (typeof kind !== "string" || !/^[A-Za-z][A-Za-z0-9]*$/.test(kind)) {
        throw new Error(`${pkg.name}: the contribution kind ${JSON.stringify(kind)} is not a name`);
      }
      if (!isObject(spec)) throw new Error(`${pkg.name}: the contribution kind ${kind} is not an object`);
      if (typeof spec.control !== "string" || spec.control === "") {
        throw new Error(`${pkg.name}: the contribution kind ${kind} needs a "control": the module that ` +
          "default-exports defineKind");
      }
      const runtime = spec.runtime;
      if (!isObject(runtime)) {
        throw new Error(`${pkg.name}: the contribution kind ${kind} needs a "runtime" ({ module, register, type })`);
      }
      for (const key of ["module", "register", "type"]) {
        if (typeof runtime[key] !== "string" || runtime[key] === "") {
          throw new Error(`${pkg.name}: the contribution kind ${kind}'s runtime needs a ${JSON.stringify(key)}`);
        }
      }
      const data = spec.data ?? [];
      if (!Array.isArray(data)) throw new Error(`${pkg.name}: the contribution kind ${kind}'s data is not a list`);
      for (const key of data) {
        if (typeof key !== "string" || key === "" || key === "id" || key === "module") {
          throw new Error(`${pkg.name}: the contribution kind ${kind}'s data has ${JSON.stringify(key)}, which is no ` +
            "key a contributing entry may carry (id and module are the entry's own)");
        }
        if (data.indexOf(key) !== data.lastIndexOf(key)) {
          throw new Error(`${pkg.name}: the contribution kind ${kind} lists the data key ${key} twice`);
        }
      }
      out.kinds[kind] = {
        kind,
        control: entryFile(pkg, `the ${kind} kind's control`, spec.control),
        runtime: {
          module: entryFile(pkg, `the ${kind} kind's runtime`, runtime.module),
          register: runtime.register,
          type: runtime.type,
        },
        data,
      };
    }
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
  // The module that fills buildGraph's slots for this package's nodes (docs/modular-graph-phase-f5a-design.md section
  // 6.3): a module of its own and not `protocol.module`, because the planner's bundle imports the generated placers and
  // must carry no node code.
  if (protocol.placer !== undefined) {
    if (typeof protocol.placer !== "string" || protocol.placer === "") {
      throw new Error(`${pkg.name}: retroplug.protocol's placer must be a module path, not ` +
        JSON.stringify(protocol.placer));
    }
    out.placer = entryFile(pkg, "protocol's placer", protocol.placer);
  }
  // The entries this package CONTRIBUTES to the kinds other packages declare (`protocol.kinds.<kind>`): each item's id,
  // the module holding its runtime half, and the data its declaring package reads. The entries' data keys are checked
  // against the declaring manifests' declarations after every package is parsed, since the declaring package may sit
  // before or after this one in the product order.
  const kinds = protocol.kinds ?? {};
  if (!isObject(kinds)) throw new Error(`${pkg.name}: retroplug.protocol.kinds is not an object`);
  for (const [kind, entries] of Object.entries(kinds)) {
    if (!Array.isArray(entries)) throw new Error(`${pkg.name}: retroplug.protocol.kinds.${kind} is not a list`);
    for (const entry of entries) {
      if (!isObject(entry) || typeof entry.id !== "string" || entry.id === "") {
        throw new Error(`${pkg.name}: every retroplug.protocol.kinds.${kind} entry needs an "id", the item's ` +
          "stable id");
      }
      if (typeof entry.module !== "string" || entry.module === "") {
        throw new Error(`${pkg.name}: the ${kind} item ${entry.id} needs a "module", the file that ` +
          "default-exports its runtime half");
      }
      out.kindEntries.push({
        kind,
        id: entry.id,
        pkg: pkg.name,
        file: entryFile(pkg, `protocol's ${kind} item`, entry.module),
        raw: entry,
        data: {},
      });
    }
  }
  for (const node of nodeList) {
    if (!isObject(node) || typeof node.type !== "string" || node.type === "") {
      throw new Error(`${pkg.name}: every retroplug.protocol.nodes entry needs a "type", the node's id`);
    }
    if (!Number.isInteger(node.label)) {
      throw new Error(`${pkg.name}: the node ${node.type} needs a "label", a whole number (its span label)`);
    }
    if (node.capture !== undefined) {
      // The serial-out capture each of the node's modes asks its system's core for: data, so the planner reads it
      // with no cart in its bundle. An unknown capture would be a mode nothing acts on.
      if (!isObject(node.capture)) {
        throw new Error(`${pkg.name}: the node ${node.type}'s capture is not an object of mode: capture`);
      }
      for (const [mode, capture] of Object.entries(node.capture)) {
        if (!["off", "listen", "drive"].includes(capture)) {
          throw new Error(
            `${pkg.name}: the node ${node.type}'s capture for ${JSON.stringify(mode)} is ` +
              `${JSON.stringify(capture)}, not "off", "listen" or "drive"`,
          );
        }
      }
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
function nodeTable(packages, product = DESKTOP) {
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
  if (product === DESKTOP) {
    // The desktop is the union: its labels run from 16 with no gap, which is what spec/08 and the manifests speak.
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
  } else {
    // Any other product's list is the desktop's with packages dropped, so its nodes keep their relative order and gain
    // the positions they hold in THIS product: the host numbers the nodes it is handed by the order it registers them
    // in (ScriptHost::spanLabelFor), so the labels are positional at run time and a hole at a dropped package would be
    // nothing (design 7.1, E6). In the plugin rp.lsdj.serial-out is 22, not 23.
    labels.forEach((label, i) => {
      byType.get(byLabel.get(label).node.type).node.label = FIRST_LABEL + i;
    });
  }
  return { byType };
}

/** The members an app module contributes as lists (definePackage's PackageContributions and the augmentations that
 *  extend it: roles, romProviders and embeds from core, and the tracker's, dsp's and each declaring package's own). A
 *  contribution kind is read from the app module by ITS name, so a kind named one of these would read that member's
 *  list as its items. */
const APP_MEMBERS = [
  "roles",
  "romProviders",
  "embeds",
  "projectSections",
  "trackerIntegrations",
  "projectServices",
  "kitFormats",
];

/** The contribution kinds the product's packages declare, by kind, in product order, and each contributing entry's
 *  data checked against its declaring package's declaration (docs/modular-graph-phase-f5a-design.md section 6.5): a
 *  kind two packages declare would fill one registry twice, an entry naming a kind nothing declares would never be
 *  handed to anyone, and an entry key its declaring manifest does not list is data nothing reads. */
function validateKinds(packages) {
  const declared = new Map();
  for (const { pkg, kinds } of packages) {
    for (const [kind, decl] of Object.entries(kinds)) {
      if (APP_MEMBERS.includes(kind)) {
        throw new Error(`${pkg.name}: the contribution kind ${kind} is named after the app member ${kind}, which the ` +
          `generated registry would read as that member's list of items (an app's members are ` +
          `${APP_MEMBERS.join(", ")})`);
      }
      const at = declared.get(kind);
      if (at) {
        throw new Error(at.name === pkg.name
          ? `the package ${pkg.name} declares the contribution kind ${kind} twice`
          : `two packages declare the contribution kind ${kind}: ${at.name} and ${pkg.name}`);
      }
      declared.set(kind, { pkg, name: pkg.name, decl });
    }
  }
  const entryOwners = new Map();
  for (const { pkg, kindEntries } of packages) {
    for (const entry of kindEntries) {
      const owner = declared.get(entry.kind);
      if (!owner) {
        throw new Error(`${pkg.name}: retroplug.protocol.kinds names the contribution kind ${entry.kind}, which no ` +
          "listed package declares");
      }
      let owners = entryOwners.get(entry.kind);
      if (!owners) {
        owners = new Map();
        entryOwners.set(entry.kind, owners);
      }
      const at = owners.get(entry.id);
      if (at) {
        throw new Error(at === pkg.name
          ? `the package ${pkg.name} declares the ${entry.kind} item ${entry.id} twice`
          : `two packages declare the ${entry.kind} item ${entry.id}: ${at} and ${pkg.name}`);
      }
      owners.set(entry.id, pkg.name);
      for (const key of Object.keys(entry.raw)) {
        if (key === "id" || key === "module") continue;
        if (!owner.decl.data.includes(key)) {
          throw new Error(`${pkg.name}: the ${entry.kind} item ${entry.id} has ${JSON.stringify(key)}, which nothing ` +
            `reads (its keys are id, module and the kind's data: ${owner.decl.data.join(", ")})`);
        }
        entry.data[key] = entry.raw[key];
      }
    }
  }
  return declared;
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
  // `path.relative` across Windows drives returns an absolute path (`C:/...`), which `./` would turn into `./C:/...`.
  if (!spec.startsWith(".") && !isAbsolute(spec)) spec = `./${spec}`;
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

/** A generated local per declaring package and kind: the package name loses its punctuation, as every generated local
 *  does. A package declares each kind once (a duplicate is refused) and a package name is unique, so two locals cannot
 *  mangle to one name. */
function kindLocal(pkgName, kind) {
  return `kind_${kind}_${pkgName.replace(/[^A-Za-z0-9_]/g, "_")}`;
}

/** The generated packages module's block per declared contribution kind (docs/modular-graph-phase-f5a-design.md
 *  section 6.5): the app modules' items in product order, the manifests' runtime halves, the pairing refusals, and the
 *  declaring package's own check and accept. The mechanism knows no kind: everything about one comes from the manifests
 *  and the declaring package's module. */
function kindBlocks({ packages, kindDecls }) {
  const app = packages.filter((p) => p.app);
  const lines = [];
  for (const { pkg: owner, decl } of kindDecls) {
    const local = kindLocal(owner.pkg.name, decl.kind);
    const item = `KindItem_${decl.kind}`;
    const entries = packages.flatMap((p) => p.kindEntries.filter((entry) => entry.kind === decl.kind));
    lines.push(
      `/** The ${decl.kind} items the product's packages contribute, in product order: each app module's`,
      ` *  \`${decl.kind}\` list, which the declaring package accepts (its \`accept\`). */`,
      `type ${item} = Parameters<typeof ${local}.accept>[0][number];`,
      `const KIND_SOURCES_${decl.kind}: readonly { pkg: string; items: readonly ${item}[] }[] = [`,
      ...app.map((p) =>
        `  { pkg: ${JSON.stringify(p.pkg.name)}, items: ${identifier(p.pkg.name)}.${decl.kind} ?? [] },`),
      "];",
      `// The runtime halves the manifests declare (their protocol.kinds.${decl.kind}), which the script bundle`,
      "// registers.",
      `const KIND_RUNTIMES_${decl.kind}: readonly { pkg: string; id: string }[] = [`,
      ...entries.map((entry) => `  { pkg: ${JSON.stringify(entry.pkg)}, id: ${JSON.stringify(entry.id)} },`),
      "];",
      "// A duplicate item id is refused naming both packages, or the one package that listed it twice: a document",
      "// names an item by its id alone, so the second one would be unreachable without a word.",
      `const kindOwners_${decl.kind} = new Map<string, string>();`,
      `for (const source of KIND_SOURCES_${decl.kind}) {`,
      "  for (const item of source.items) {",
      "    const id = item.id;",
      `    const owner = kindOwners_${decl.kind}.get(id);`,
      "    if (owner !== undefined) {",
      "      throw new Error(owner === source.pkg",
      `        ? \`the package \${source.pkg} contributes the ${decl.kind} item \${id} twice\``,
      `        : \`two packages contribute the ${decl.kind} item \${id}: \${owner} and \${source.pkg}\`);`,
      "    }",
      `    kindOwners_${decl.kind}.set(id, source.pkg);`,
      "  }",
      "}",
      "// An item with one half and not the other is a document that names an item nothing runs, or a runtime no menu",
      "// offers.",
      `for (const runtime of KIND_RUNTIMES_${decl.kind}) {`,
      `  if (!kindOwners_${decl.kind}.has(runtime.id)) {`,
      `    throw new Error(\`the ${decl.kind} item \${runtime.id} has no control-plane half\`);`,
      "  }",
      "}",
      `for (const id of kindOwners_${decl.kind}.keys()) {`,
      `  if (!KIND_RUNTIMES_${decl.kind}.some((runtime) => runtime.id === id)) {`,
      `    throw new Error(\`the ${decl.kind} item \${id} has no runtime half\`);`,
      "  }",
      "}",
      `const KIND_ITEMS_${decl.kind}: readonly ${item}[] = ` +
        `KIND_SOURCES_${decl.kind}.flatMap((source) => source.items);`,
      "// The declaring package's own checks over the items and the manifests' data, then its registry.",
      `const KIND_DATA_${decl.kind} = KIND_DATA.${decl.kind};`,
      `${local}.check?.(KIND_ITEMS_${decl.kind}, KIND_DATA_${decl.kind}, {`,
      "  nodeTypes: Object.keys(PROTOCOL_NODES),",
      "});",
      `${local}.accept(KIND_ITEMS_${decl.kind}, KIND_DATA_${decl.kind});`,
      "",
    );
  }
  return lines;
}

function renderPackages({ packages, outDir, names, product }) {
  const app = packages.filter((p) => p.app);
  // Each declaring package's kind module: the control-plane half's `accept` and `check`, imported rather than
  // named by the generator, so a kind is data (docs/modular-graph-phase-f5a-design.md section 6.5).
  const kindDecls = packages.flatMap((p) => Object.values(p.kinds).map((decl) => ({ pkg: p, decl })));
  const lines = [];
  lines.push(
    `${generatedFrom(product)} (the root`,
    `// package.json's retroplug.products.${product}) and the packages' manifests. Do not edit; edit a manifest and`,
    "// rebuild.",
    "//",
    "// The app's registrations, in product order (the list's, which is a dependency order, then the shell): the roles",
    "// buildAppRegistry applies, the ROM providers it offers, the tracker integrations the Songs and Assets menus",
    "// search, the kit formats the audio driver dispatches to, the project sections and services each package owns,",
    "// and the contribution kinds' items. The consoles and the real carts are apart, in ./consoles.ts, the protocol",
    "// tables and the kinds' data in ./protocol.ts and the graph placers in ./placers.ts, so a module that needs one",
    "// of those imports no package's app.",
    'import type { RoleType, RomProvider } from "@retroplug/core";',
    'import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";',
    'import type { KitFormat } from "@retroplug/dsp/kitContract";',
    'import type { Embed, ProjectSection } from "@retroplug/core/definePackage";',
    'import type { ProjectServiceFactory } from "@retroplug/tracker/projectService";',
    'import { CONSOLES } from "./consoles";',
    // The placers are checked against the sections below (a placer's key is its section's), so this module imports
    // them: the control plane already carries them through buildGraph, so only the check is new here.
    'import { PLACERS } from "./placers";',
  );
  for (const p of app) lines.push(`import ${identifier(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.app)}";`);
  // The kind's data and the product's node types reach the declaring package's check: a mode naming a node type no
  // package declares would place nothing.
  if (kindDecls.length) lines.push('import { KIND_DATA, PROTOCOL_NODES } from "./protocol";');
  for (const { pkg: owner, decl } of kindDecls) {
    lines.push(
      `import ${kindLocal(owner.pkg.name, decl.kind)} from "${moduleSpecifier(outDir, owner.pkg, decl.control)}";`,
    );
  }
  lines.push(
    "",
    "/** The product's package ids, in product order, the shell excluded: what a contribution's `pkg` names, and the",
    " *  prefix a node type of the package's starts with (a few name a console instead: rp.sms.sync is smsggdj's). */",
    "export const PACKAGE_IDS: readonly string[] = [",
    ...names.map((name) => `  ${JSON.stringify(name)},`),
    "];",
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
    "// A console's core is the kind of its core-config role (the role is named after its core), so a descriptor",
    "// naming a core nothing contributes could be classified and never configured. The check is here rather than in",
    "// ./consoles.ts because the roles are this module's (that one imports no package's app).",
    "for (const consoleDescriptor of CONSOLES) {",
    "  if (!ROLES.some((role) => role.kind === consoleDescriptor.core && role.category === \"system\")) {",
    "    throw new Error(`the console ${consoleDescriptor.id} names the core ${consoleDescriptor.core}, whose system ` +",
    "      \"role no package contributes\");",
    "  }",
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
    "/** The project sections the product's packages contribute, in product order: a block under a .rplg's",
    " *  `settings` which its package parses, defaults and migrates (docs/modular-graph-phase-f5a-design.md",
    " *  section 6.1). A key the shell owns, and a key two packages declare, are refused below: the shell keeps",
    " *  every non-core key verbatim, so a duplicate would have one section silently replace the other's bytes in",
    " *  every project. */",
    "export const PROJECT_SECTIONS: readonly { pkg: string; section: ProjectSection }[] = [",
    ...app.map((p) => `  ...(${identifier(p.pkg.name)}.projectSections ?? []).map((section) => ({ pkg: ` +
      `${JSON.stringify(p.pkg.name)}, section })),`),
    "];",
    "// Keep in step with the shell's CORE_SETTING_KEYS (packages/retroplug/src/projectConfig.ts): the one list of the",
    "// settings keys a package may not contribute a section under.",
    "const SHELL_SECTION_KEYS = [\"layout\", \"midiRouting\", \"audioRouting\", \"zoom\"];",
    "const sectionOwners = new Map<string, string>();",
    "const sectionKeysByPkg = new Map<string, string[]>();",
    "for (const { pkg, section } of PROJECT_SECTIONS) {",
    "  if (SHELL_SECTION_KEYS.includes(section.key)) {",
    "    throw new Error(`the package ${pkg} declares the project section ${section.key}, which is one of the ` +",
    "      `shell's own settings (${SHELL_SECTION_KEYS.join(\", \")})`);",
    "  }",
    "  const owner = sectionOwners.get(section.key);",
    "  if (owner !== undefined) {",
    "    throw new Error(owner === pkg",
    "      ? `the package ${pkg} declares the project section ${section.key} twice`",
    "      : `two packages declare the project section ${section.key}: ${owner} and ${pkg}`);",
    "  }",
    "  sectionOwners.set(section.key, pkg);",
    "  sectionKeysByPkg.set(pkg, [...(sectionKeysByPkg.get(pkg) ?? []), section.key]);",
    "}",
    "",
    "/** The project services the product's packages contribute, in product order: a feature's control-plane",
    " *  half, which the shell builds one instance of per project store (docs/modular-graph-phase-f5a-design.md",
    " *  section 6.2). A key is a model's and a section's, so two services under one key would fight over the",
    " *  graph's placement. */",
    "export const PROJECT_SERVICES: readonly { pkg: string; service: ProjectServiceFactory }[] = [",
    ...app.map((p) => `  ...(${identifier(p.pkg.name)}.projectServices ?? []).map((service) => ({ pkg: ` +
      `${JSON.stringify(p.pkg.name)}, service })),`),
    "];",
    "const serviceKeys = new Map<string, string>();",
    "for (const { pkg, service } of PROJECT_SERVICES) {",
    "  const owner = serviceKeys.get(service.key);",
    "  if (owner !== undefined) {",
    "    throw new Error(owner === pkg",
    "      ? `the package ${pkg} declares the project service ${service.key} twice`",
    "      : `two packages declare the project service ${service.key}: ${owner} and ${pkg}`);",
    "  }",
    "  serviceKeys.set(service.key, pkg);",
    "}",
    "for (const { pkg, service } of PROJECT_SERVICES) {",
    "  // A service's key is the section it reads and the model its placer places: a key no package declares a section",
    "  // for would read undefined where the store asserts, and one of the shell's keys would fight the shell's field.",
    "  if (SHELL_SECTION_KEYS.includes(service.key)) {",
    "    throw new Error(`the package ${pkg} declares the project service ${service.key}, which is one of the shell's ` +",
    "      `own settings (${SHELL_SECTION_KEYS.join(\", \")})`);",
    "  }",
    "  // A package that declares no section at all is not held to one: a fixture tree may replace an app module, and a",
    "  // service key is judged against the sections the product actually declares.",
    "  if (!sectionOwners.has(service.key) && (sectionKeysByPkg.get(pkg) ?? []).length > 0) {",
    "    throw new Error(`the package ${pkg} declares the project service ${service.key}, which matches no project ` +",
    "      \"section (the service key is its section's key)\");",
    "  }",
    "}",
    "// A placer's key is the section whose model it places, so the same two rules hold where the placement is made.",
    "for (const { pkg, placer } of PLACERS) {",
    "  if (SHELL_SECTION_KEYS.includes(placer.key)) {",
    "    throw new Error(`the package ${pkg} places the graph's model ${placer.key}, which is one of the shell's own ` +",
    "      `settings (${SHELL_SECTION_KEYS.join(\", \")})`);",
    "  }",
    "  if (!sectionOwners.has(placer.key) && (sectionKeysByPkg.get(pkg) ?? []).length > 0) {",
    "    throw new Error(`the package ${pkg} places the graph's model ${placer.key}, which matches no project ` +",
    "      \"section (a placer's key is its section's key)\");",
    "  }",
    "}",
    "",
    "/** The config.json sections the product's packages contribute, in product order: a root key of the user config",
    " *  which its package parses, defaults and migrates (docs/modular-graph-phase-f5b-design.md section 6.1). The",
    " *  shell keeps every non-core root key verbatim, so the same two refusals hold: a key the shell owns, and a",
    " *  key two packages declare. */",
    "export const CONFIG_SECTIONS: readonly { pkg: string; section: ProjectSection }[] = [",
    ...app.map((p) => `  ...(${identifier(p.pkg.name)}.configSections ?? []).map((section) => ({ pkg: ` +
      `${JSON.stringify(p.pkg.name)}, section })),`),
    "];",
    "// The shell's own config.json keys (packages/retroplug/src/userConfig.ts's CORE_CONFIG_KEYS, which a test holds",
    "// this list to): the root keys a package may not contribute a section under.",
    "export const SHELL_CONFIG_KEYS = [\"schemaVersion\", \"activeKeyboardBindings\", \"activeGamepadBindings\",",
    "  \"defaultZoom\", \"sramAutoSave\", \"render\", \"useNativeFileDialogs\"];",
    "const configOwners = new Map<string, string>();",
    "for (const { pkg, section } of CONFIG_SECTIONS) {",
    "  if (SHELL_CONFIG_KEYS.includes(section.key)) {",
    "    throw new Error(`the package ${pkg} declares the config section ${section.key}, which is one of the ` +",
    "      `shell's own keys (${SHELL_CONFIG_KEYS.join(\", \")})`);",
    "  }",
    "  const owner = configOwners.get(section.key);",
    "  if (owner !== undefined) {",
    "    throw new Error(owner === pkg",
    "      ? `the package ${pkg} declares the config section ${section.key} twice`",
    "      : `two packages declare the config section ${section.key}: ${owner} and ${pkg}`);",
    "  }",
    "  configOwners.set(section.key, pkg);",
    "}",
    "",
    ...kindBlocks({ packages, kindDecls }),
    "/** The embedded ROMs the product's packages declare, in product order: the id native's resource table knows them",
    " *  by, the label a menu offers one under, and the console it runs on. A load takes its platform from here, so an",
    " *  embedded ROM is not assumed to be any console's. */",
    "export const EMBEDS: readonly Embed[] = [",
    ...contributionLines(app, "embeds"),
    "];",
    "// The other half of each declaration: the manifest entries (`retroplug.embed`) that put the bytes in the binary, by",
    "// the package that bakes them in.",
    "const EMBED_SOURCES: readonly { pkg: string; embeds: readonly Embed[] }[] = [",
    ...app.map((p) => `  { pkg: ${JSON.stringify(p.pkg.name)}, embeds: ${identifier(p.pkg.name)}.embeds ?? [] },`),
    "];",
    `const MANIFEST_EMBEDS: readonly { pkg: string; id: string }[] = [${manifestsWithEmbeds(packages)}];`,
    "// A declaration and a manifest entry are one embedded ROM's two halves: the shell loads it by the declaration,",
    "// native boots the bytes the manifest names, so neither can stand alone. The id belongs to the package whose bytes",
    "// carry it, an id is one ROM, and its platform is a console this product runs - the same rule the consoles' own",
    "// checks hold the descriptors to.",
    "for (const source of EMBED_SOURCES) {",
    "  for (const embed of source.embeds) {",
    "    const declarers = EMBED_SOURCES.flatMap((s) => s.embeds.filter((e) => e.id === embed.id).map(() => s.pkg));",
    "    if (declarers.length > 1) {",
    "      const [first, second] = declarers;",
    "      throw new Error(first === second",
    "        ? `the package ${first} declares the embedded ROM ${embed.id} twice: an id is one ROM`",
    "        : `two packages declare the embedded ROM ${embed.id}: ${first} and ${second}`);",
    "    }",
    "    const baked = MANIFEST_EMBEDS.filter((e) => e.id === embed.id);",
    "    if (!baked.length) {",
    "      throw new Error(`the package ${source.pkg} declares the embedded ROM ${embed.id}, which its manifest does ` +",
    '        "not bake in (its retroplug.embed)");',
    "    }",
    "    if (!baked.some((e) => e.pkg === source.pkg)) {",
    "      throw new Error(`the embedded ROM ${embed.id} is declared by ${source.pkg} and baked in by ${baked[0].pkg}: ` +",
    '        "the two halves belong to one package");',
    "    }",
    "    if (!CONSOLES.some((c) => c.platforms.some((p) => p.id === embed.platform))) {",
    "      throw new Error(`the embedded ROM ${embed.id} names the platform ${embed.platform}, which no console ` +",
    '        "descriptor registers");',
    "    }",
    "  }",
    "}",
    "for (const baked of MANIFEST_EMBEDS) {",
    "  if (!EMBEDS.some((e) => e.id === baked.id)) {",
    "    throw new Error(`the package ${baked.pkg} bakes in the embedded ROM ${baked.id}, which no app module ` +",
    '      "declares (an embeds entry with its label and platform)");',
    "  }",
    "}",
    "",
  );
  return lines.join("\n");
}

/** The generated graph placers module (`<shell>/src/generated/placers.ts`): each package's `protocol.placer` module, in
 *  product order, and the checks buildGraph's slots are held to. A placer's module is reachable from the planner's
 *  bundle, so this file imports a package that imports types and constants only (test/script/bundle-protocol.test.ts
 *  holds the bundle to carrying no node module and no zod). */
function renderPlacers({ packages, outDir, product }) {
  const contributors = packages.filter((p) => p.placer);
  const local = (name) => `placer_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's graph placers, in product order: each package's `protocol.placer` module, which says where",
    "// the nodes of a model the shell hands it go (docs/modular-graph-phase-f5a-design.md section 6.3). buildGraph",
    `// reads the slots in this order, so a package's place in \`retroplug.products.${product}\` is where its nodes land.`,
    "// Only the planner imports this module, and it carries types and constants alone: no package's node code and",
    "// no schema.",
    'import type { GraphPlacer } from "@retroplug/core/placer";',
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.placer)}";`);
  }
  lines.push("");
  lines.push("/** Each package's graph placer, in product order. */");
  lines.push("export const PLACERS: readonly { pkg: string; placer: GraphPlacer<any> }[] = [");
  for (const p of contributors) {
    lines.push(`  { pkg: ${JSON.stringify(p.pkg.name)}, placer: ${local(p.pkg.name)} },`);
  }
  lines.push("];");
  lines.push(
    "const placerKeys = new Map<string, string>();",
    "for (const { pkg, placer } of PLACERS) {",
    "  const owner = placerKeys.get(placer.key);",
    "  if (owner !== undefined) {",
    "    throw new Error(owner === pkg",
    "      ? `the package ${pkg} places the graph's model ${placer.key} twice`",
    "      : `two packages place the graph's model ${placer.key}: ${owner} and ${pkg}`);",
    "  }",
    "  placerKeys.set(placer.key, pkg);",
    "}",
    "",
  );
  return lines.join("\n");
}

/** The generated UI contributions module (`<shell>/ui/generated/menus.ts`): each package's `retroplug.ui` module, in
 *  product order, and the checks a contribution is held to at load. The order is the menus' order - a system-settings
 *  block's rows and a device entry land where the package sits in the product list - so it is the list's, not a sort. */
function renderMenus({ packages, outDir, generatedDir, shellSrc, product }) {
  const contributors = packages.filter((p) => p.ui);
  // A module identifier per package: the package name loses its punctuation, and a collision would be a tsc error
  // naming neither manifest.
  const local = (name) => `ui_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's UI contributions, in product order: each package's `retroplug.ui` module, which default-exports the",
    "// system-settings rows, menu builders, tracker specs and services it adds. The shell composes the menus from this",
    `// list, so a package's place in \`retroplug.products.${product}\` is where its rows and entries land. Only ui/ imports`,
    "// this module: the control plane, the render worker, the SDK and the planner carry no menu contribution.",
    'import { PLACEMENTS, type ContributedAction, type RoleRow, type UiAction, type UiContribution } ' +
      'from "@retroplug/ui/contribution";',
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.ui)}";`);
  }
  // The role kinds and tracker integrations the product registers, for the checks below: a row over a kind nothing
  // registers, or over a key its role's schema does not read, would render nowhere and do nothing, and a tracker UI no
  // integration matches would never be found - typos the compiler cannot see in a data literal.
  lines.push(`import { ROLES, TRACKER_INTEGRATIONS } from "${specifier(outDir, join(generatedDir, "packages.ts"))}";`);
  // The binding vocabulary an action's default is checked against (the key names the profiles resolve, SDL's pad
  // names, and the defaults the shell and the Game Boy buttons already take), all of it the shell's own modules: read
  // from wherever this module was written, so a product's tree reaches the shell's real files.
  lines.push(
    `import { GAMEPAD_BUTTON_NAMES, resolveKeyName } from "${specifier(outDir, join(shellSrc, "keyCodes.ts"))}";`,
  );
  lines.push(
    `import { DEFAULT_GAMEPAD_ACTIONS, DEFAULT_KEYBOARD_ACTIONS, defaultBindingMap } from ` +
      `"${specifier(outDir, join(shellSrc, "bindingMap.ts"))}";`,
  );
  lines.push("");
  lines.push("/** Each package's UI contribution, in product order. */");
  lines.push("export const UI_CONTRIBUTIONS: readonly { pkg: string; ui: UiContribution }[] = [");
  for (const p of contributors) {
    lines.push(`  { pkg: ${JSON.stringify(p.pkg.name)}, ui: ${local(p.pkg.name)} },`);
  }
  lines.push("];");
  lines.push("");
  lines.push("/** The product's app actions, in product order, with the package each came from: the bindings menus'");
  lines.push(" *  rows, the key and gamepad maps' known ids, and the dispatch's lookups. */");
  lines.push("export const ACTIONS: readonly ContributedAction[] = UI_CONTRIBUTIONS.flatMap(({ pkg, ui }) =>");
  lines.push("  (ui.actions ?? []).map((action) => ({ pkg, action })),");
  lines.push(");");
  lines.push("");
  lines.push("// A row's id has to be unique inside its placement: the menu is keyed by id, so a second row of one id would");
  lines.push("// take the first one's place (a system-settings row lands in the System submenu, a tracker's extras in its own");
  lines.push("// submenu).");
  lines.push("const rowOwners = new Map<string, string>();");
  lines.push("function claimRow(placement: string, id: string, pkg: string): void {");
  lines.push("  const owner = rowOwners.get(`${placement}:${id}`);");
  lines.push("  if (owner === undefined) {");
  lines.push("    rowOwners.set(`${placement}:${id}`, pkg);");
  lines.push("    return;");
  lines.push("  }");
  lines.push("  throw new Error(owner === pkg");
  lines.push("    ? `the package ${pkg} declares the row ${id} twice in the placement ${placement}`");
  lines.push("    : `two packages declare the row ${id} in the placement ${placement}: ${owner} and ${pkg}`);");
  lines.push("}");
  lines.push("");
  lines.push("const roleKinds = new Set(ROLES.map((role) => role.kind));");
  lines.push("/** The keys a role's schema declares, which are the only ones a row can read (or gate on). */");
  lines.push(
    "const roleKeys = new Map(ROLES.map((role) => [role.kind, new Set(Object.keys(role.schema.parse({})))]));",
  );
  lines.push("const trackerIds = new Set(TRACKER_INTEGRATIONS.map((integration) => integration.id));");
  lines.push("const trackerOwners = new Map<string, string>();");
  lines.push("// A service is looked up by its id: two of one id would run one of them twice or the other never, so a");
  lines.push("// duplicate is refused rather than let the second definition win.");
  lines.push("const serviceOwners = new Map<string, string>();");
  lines.push("const placements = new Set<string>(PLACEMENTS);");
  lines.push("// An action's id is what a binding profile stores, so it is unique across the product and names its");
  lines.push("// package. Its defaults are held to the names the shell binds and to the ones it already binds at a");
  lines.push("// default: a key on both would fire the action AND the Game Boy button (or the shell action) it is on.");
  lines.push("const actionOwners = new Map<string, string>();");
  lines.push("const padButtonNames = new Set<string>(GAMEPAD_BUTTON_NAMES);");
  lines.push("const defaultMap = defaultBindingMap();");
  lines.push("const takenKeyCodes = new Set<number>();");
  lines.push("for (const names of Object.values(DEFAULT_KEYBOARD_ACTIONS)) {");
  lines.push("  for (const name of names) takenKeyCodes.add(resolveKeyName(name)!);");
  lines.push("}");
  lines.push("for (const names of Object.values(defaultMap.keyboard)) {");
  lines.push("  for (const name of names) takenKeyCodes.add(resolveKeyName(name)!);");
  lines.push("}");
  lines.push("const takenPadNames = new Set<string>();");
  lines.push("for (const names of Object.values(DEFAULT_GAMEPAD_ACTIONS)) {");
  lines.push("  for (const name of names) takenPadNames.add(name);");
  lines.push("}");
  lines.push("for (const names of Object.values(defaultMap.gamepad)) {");
  lines.push("  for (const name of names) takenPadNames.add(name);");
  lines.push("}");
  lines.push("");
  lines.push("/** One contributed row: its role must be registered, its id unique in its placement, and its key (and its");
  lines.push(" *  gate's key) one its role's schema reads. */");
  lines.push("function checkRow(placement: string, pkg: string, row: RoleRow): void {");
  lines.push("  if (!roleKinds.has(row.role)) {");
  lines.push("    throw new Error(`the package ${pkg} declares the row ${row.id} over the role ${row.role}, which no ` +");
  lines.push('      "registered role type has");');
  lines.push("  }");
  lines.push("  claimRow(placement, row.id, pkg);");
  lines.push("  const keys = roleKeys.get(row.role)!;");
  lines.push("  if (!keys.has(row.key)) {");
  lines.push(
    "    throw new Error(`the package ${pkg} declares the row ${row.id} over the key ${row.key}, which the role ` +",
  );
  lines.push('      `${row.role} does not read`);');
  lines.push("  }");
  lines.push("  if (row.when && !keys.has(row.when.key)) {");
  lines.push(
    "    throw new Error(`the package ${pkg} declares the row ${row.id} gated on the key ${row.when.key}, which ` +",
  );
  lines.push('      `the role ${row.role} does not read`);');
  lines.push("  }");
  lines.push("}");
  lines.push("");
  lines.push("/** One contributed action: its id unique across the product and prefixed by its own package's name,");
  lines.push(" *  its default key and pad names ones the shell resolves, and no default the shell (or a Game Boy");
  lines.push(" *  button) already binds, since one press would fire both.");
  lines.push(" *  Two contributions on one key are allowed: the overlays' backtick. */");
  lines.push("function checkAction(pkg: string, action: UiAction): void {");
  lines.push("  const owner = actionOwners.get(action.id);");
  lines.push("  if (owner !== undefined) {");
  lines.push("    throw new Error(owner === pkg");
  lines.push("      ? `the package ${pkg} declares the action ${action.id} twice`");
  lines.push("      : `two packages declare the action ${action.id}: ${owner} and ${pkg}`);");
  lines.push("  }");
  lines.push("  actionOwners.set(action.id, pkg);");
  lines.push("  if (!action.id.startsWith(`${pkg}.`)) {");
  lines.push("    throw new Error(`the package ${pkg} declares the action ${action.id}, which is not prefixed ` +");
  lines.push('      `by its own name ("${pkg}.")`);');
  lines.push("  }");
  lines.push("  for (const name of action.keys ?? []) {");
  lines.push("    const code = resolveKeyName(name);");
  lines.push("    if (code === null) {");
  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +");
  lines.push('        `${name}, which is no key name`);');
  lines.push("    }");
  lines.push("    if (takenKeyCodes.has(code)) {");
  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +");
  lines.push('        `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);');
  lines.push("    }");
  lines.push("    if (/^[A-Z]$/.test(name)) {");
  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ${name} by ` +");
  lines.push('        `default, which no host sends (the hosts send letters unshifted: bind "${name.toLowerCase()}")`);');
  lines.push("    }");
  lines.push("  }");
  lines.push("  for (const name of action.pad ?? []) {");
  lines.push("    if (!padButtonNames.has(name)) {");
  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +");
  lines.push('        `${name}, which is no SDL button name (the names are GAMEPAD_BUTTON_NAMES)`);');
  lines.push("    }");
  lines.push("    if (takenPadNames.has(name)) {");
  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +");
  lines.push('        `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);');
  lines.push("    }");
  lines.push("  }");
  lines.push("}");
  lines.push("");
  lines.push("for (const { pkg, ui } of UI_CONTRIBUTIONS) {");
  lines.push("  for (const row of ui.rows ?? []) checkRow(\"system.settings\", pkg, row);");
  lines.push("  for (const action of ui.actions ?? []) checkAction(pkg, action);");
  lines.push("  for (const tracker of ui.trackers ?? []) {");
  lines.push("    if (!trackerIds.has(tracker.id)) {");
  lines.push(
    "      throw new Error(`the package ${pkg} declares a tracker UI for ${tracker.id}, which no registered ` +",
  );
  lines.push('        "tracker integration has");');
  lines.push("    }");
  lines.push("    const owner = trackerOwners.get(tracker.id);");
  lines.push("    if (owner !== undefined) {");
  lines.push("      throw new Error(owner === pkg");
  lines.push("        ? `the package ${pkg} declares the tracker UI ${tracker.id} twice`");
  lines.push("        : `two packages declare a tracker UI for ${tracker.id}: ${owner} and ${pkg}`);");
  lines.push("    }");
  lines.push("    trackerOwners.set(tracker.id, pkg);");
  lines.push(
    "    for (const row of tracker.extras?.rows ?? []) checkRow(`instance.tracker:${tracker.id}`, pkg, row);",
  );
  lines.push("  }");
  lines.push("  for (const service of ui.services ?? []) {");
  lines.push("    const owner = serviceOwners.get(service.id);");
  lines.push("    if (owner !== undefined) {");
  lines.push("      throw new Error(owner === pkg");
  lines.push("        ? `the package ${pkg} declares the UI service ${service.id} twice`");
  lines.push("        : `two packages declare a UI service ${service.id}: ${owner} and ${pkg}`);");
  lines.push("    }");
  lines.push("    serviceOwners.set(service.id, pkg);");
  lines.push("  }");
  lines.push("  for (const menu of ui.menus ?? []) {");
  lines.push("    if (!placements.has(menu.placement)) {");
  lines.push("      throw new Error(`the package ${pkg} declares a menu builder at the placement ${menu.placement}, ` +");
  lines.push('        `which is no placement (the placements are ${PLACEMENTS.join(", ")})`);');
  lines.push("    }");
  lines.push("  }");
  lines.push("}");
  lines.push("");
  return lines.join("\n");
}

/** The generated views module (`<shell>/ui/generated/views.ts`): each package's `retroplug.views` module, in product
 *  order, and the checks a view is held to at load. Apart from menus.ts, which every pure-tier test that reaches
 *  menuDefs.ts bundles with no alias: a component view imports React, and the UI bundle is the one that can resolve it
 *  (tools/build-ui.js's aliases and nodePaths). The order is the views' order: a window view is looked up by id (the
 *  first match, which the duplicate check makes unique) while the tile layers stack where the package sits in the
 *  product list, so it is the list's order, not a sort. */
function renderViews({ packages, outDir, product }) {
  const contributors = packages.filter((p) => p.views);
  const local = (name) => `views_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's views, in product order: each package's `retroplug.views` module, default-exporting the window",
    "// and tile views it draws. Only the shell's view slots import this module, and only the UI bundle resolves it: a",
    "// component view imports React, which the pure tier (the menu tests, the control plane, the CLI) has no path to.",
    'import type { ViewDef, ViewsContribution } from "@retroplug/ui/contribution";',
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.views)}";`);
  }
  lines.push("");
  lines.push("/** Each package's views, in product order. */");
  lines.push("export const VIEW_CONTRIBUTIONS: readonly { pkg: string; views: ViewsContribution }[] = [");
  for (const p of contributors) {
    lines.push(`  { pkg: ${JSON.stringify(p.pkg.name)}, views: ${local(p.pkg.name)} },`);
  }
  lines.push("];");
  lines.push("");
  lines.push("/** The product's views, in product order, with the package each came from: what host.views.open looks");
  lines.push(" *  a window view up by id, and the tile layers every tile mounts in turn. */");
  lines.push("export const VIEWS: readonly { pkg: string; view: ViewDef }[] = VIEW_CONTRIBUTIONS.flatMap(({ pkg, views }) =>");
  lines.push("  (views.views ?? []).map((view) => ({ pkg, view })),");
  lines.push(");");
  lines.push("");
  lines.push("// A view's id is its test id and the id host.views.open names, so it is unique across the product; its");
  lines.push("// placement and its body's kind are the two data fields the type cannot hold a package to at load.");
  lines.push("const viewOwners = new Map<string, string>();");
  lines.push('const viewPlacements = new Set<string>(["window", "tile"]);');
  lines.push('const bodyKinds = new Set<string>(["pixels", "readout", "component"]);');
  lines.push("");
  lines.push("/** One contributed view: its id unique across the product, its placement one the shell has, its body kind");
  lines.push(" *  one the shell renders with what that kind is opened by (a session's `open`, a component's `component`), and");
  lines.push(" *  no `visible` on a window view (the type refuses that; this backs the type). */");
  lines.push("function checkView(pkg: string, view: ViewDef): void {");
  lines.push("  const owner = viewOwners.get(view.id);");
  lines.push("  if (owner !== undefined) {");
  lines.push("    throw new Error(owner === pkg");
  lines.push("      ? `the package ${pkg} declares the view ${view.id} twice`");
  lines.push("      : `two packages declare the view ${view.id}: ${owner} and ${pkg}`);");
  lines.push("  }");
  lines.push("  viewOwners.set(view.id, pkg);");
  lines.push("  if (!viewPlacements.has(view.placement)) {");
  lines.push("    throw new Error(`the package ${pkg} declares the view ${view.id} at the placement ${view.placement}, ` +");
  lines.push('      `which is no placement (the placements are window, tile)`);');
  lines.push("  }");
  lines.push("  const kind = view.body?.kind;");
  lines.push("  if (!bodyKinds.has(kind)) {");
  lines.push("    throw new Error(`the package ${pkg} declares the view ${view.id} with the body kind ${kind}, ` +");
  lines.push('      `which is no body kind (the kinds are pixels, readout, component)`);');
  lines.push("  }");
  lines.push("  const body = view.body as { readonly open?: unknown; readonly component?: unknown };");
  lines.push('  if (kind === "component") {');
  lines.push('    if (typeof body.component !== "function") {');
  lines.push("      throw new Error(`the package ${pkg} declares the view ${view.id} with a component body and no ` +");
  lines.push('        "component (reactView builds the one a component body carries)");');
  lines.push("    }");
  lines.push('  } else if (typeof body.open !== "function") {');
  lines.push("    throw new Error(`the package ${pkg} declares the view ${view.id} with a ${kind} body and no open ` +");
  lines.push('      `(a ${kind} body\'s open is what the shell calls)`);');
  lines.push("  }");
  lines.push('  if (view.placement !== "tile" && (view as { readonly visible?: unknown }).visible !== undefined) {');
  lines.push("    throw new Error(`the package ${pkg} declares the window view ${view.id} with a visible flag, which only ` +");
  lines.push('      `a tile view has`);');
  lines.push("  }");
  lines.push("}");
  lines.push("");
  lines.push("for (const { pkg, views } of VIEW_CONTRIBUTIONS) {");
  lines.push("  for (const view of views.views ?? []) checkView(pkg, view);");
  lines.push("}");
  lines.push("");
  return lines.join("\n");
}

/** Every workspace package's published module, as a `paths` entry resolved from `fromDir`: a product's tree is not
 *  under the shell, so it has no node_modules of its own to resolve `@retroplug/*` through, and a symlink to the
 *  shell's would be a platform's business (Windows needs a privilege for one). The map is complete for the tree
 *  because it is built from the manifests: every export key a package publishes, mapped to the file the manifest
 *  publishes there. */
function workspacePathEntries(root, fromDir) {
  const entries = [];
  const dir = join(root, "packages");
  for (const name of readdirSync(dir).sort()) {
    const manifestPath = join(dir, name, "package.json");
    if (!existsSync(manifestPath)) continue;
    const manifest = readJson(manifestPath);
    if (typeof manifest.name !== "string" || !manifest.name.startsWith(`@retroplug/`)) continue;
    for (const { key, target } of exportEntries(manifest)) {
      if (typeof target !== "string" || !target.endsWith(".ts")) continue;
      const specifierName = key === "." ? manifest.name : `${manifest.name}/${key.replace(/^\.\//, "")}`;
      const file = relative(fromDir, join(dir, name, target)).split(/[\\/]/).join("/");
      entries.push({
        specifier: specifierName,
        target: file.startsWith(".") || isAbsolute(file) ? file : `./${file}`,
      });
    }
  }
  return entries;
}

/** The shell's host modules and aliases as `paths` entries resolved from `fromDir`, so a product's tree resolves them
 *  to the same files the shell's program does. */
function shellPathEntries(shellDir, hostList, fromDir) {
  const entries = [];
  for (const entry of hostList) {
    if (typeof entry.types !== "string" || entry.types === "") continue;
    const file = relative(fromDir, join(shellDir, entry.types)).split(/[\\/]/).join("/");
    entries.push({
      specifier: entry.module,
      target: file.startsWith(".") || isAbsolute(file) ? file : `./${file}`,
    });
  }
  for (const [specifier, targets] of Object.entries(shellAliases(shellDir))) {
    entries.push({
      specifier,
      target: targets
        .map((target) => relative(fromDir, join(shellDir, target)).split(/[\\/]/).join("/"))
        .map((target) => (target.startsWith(".") || isAbsolute(target) ? target : `./${target}`)),
    });
  }
  return entries;
}

/** A non-desktop product's typecheck program, written beside its tree: it extends the shell's tsconfig (the compiler
 *  options), includes the product's generated modules, and takes its paths from a file of its own (TypeScript
 *  replaces a child's paths with the extending config's, so the shell's could not be extended and added to). The
 *  shell's own sources come in through the generated modules' relative imports, which is what checks them against
 *  this product's registries: the shell program itself still resolves generated/* to the desktop's tree, whose shapes
 *  are the same. */
function renderProductTsconfig(shell, outDir, pathsFile) {
  const productDir = resolve(outDir, "..", "..", "..", "..");
  const rel = (path) => relative(productDir, path).split(/[\\/]/).join("/");
  return (
    JSON.stringify(
      {
        extends: [rel(join(shell.dir, "tsconfig.json")).replace(/\.json$/, ""), `./${pathsFile}`],
        include: ["./packages"],
        // The base config's ambient globals are reached through ${configDir}, which is THIS config's directory: the
        // product's tree is not under the shell, so the file is named again from here.
        files: [rel(join(shell.dir, "..", "core", "ts", "runtime.d.ts"))],
      },
      null,
      2,
    ) + "\n"
  );
}

/** A product's `paths` file: the workspace's published modules and the shell's host modules and aliases. */
function renderProductPaths(entries) {
  const unique = [...new Map(entries.map((entry) => [entry.specifier, entry])).values()];
  return (
    JSON.stringify(
      {
        compilerOptions: {
          paths: Object.fromEntries(
            unique.map(({ specifier: spec, target }) => [spec, Array.isArray(target) ? target : [target]]),
          ),
        },
      },
      null,
      2,
    ) + "\n"
  );
}

/** The generated CLI tools module (`<shell>/cli/generated/tools.ts`): each package's `retroplug.cli` module, in product
 *  order, with the load-time refusal of a name two packages declare (the shell's own five are refused by the dispatcher,
 *  which is where they are). Imported only by cli/, so no other bundle carries a tool. */
function renderCliTools({ packages, outDir, product }) {
  const contributors = packages.filter((p) => p.cli);
  const local = (name) => `cli_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's CLI tools, in product order: each package's `retroplug.cli` module, which default-exports the",
    "// subcommands it adds. The dispatcher appends them after the shell's own five, so a package's place in",
    `// \`retroplug.products.${product}\` is where its commands land in the index. Imported only by cli/, so the control`,
    "// plane, the render worker, the SDK and the UI carry no tool.",
    'import type { CliTool } from "@retroplug/core/cliTool";',
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.cli)}";`);
  }
  lines.push("");
  lines.push("/** Each package's CLI tools, in product order, and the package they came from. */");
  lines.push("export const CLI_CONTRIBUTIONS: readonly { pkg: string; tools: readonly CliTool[] }[] = [");
  for (const p of contributors) {
    lines.push(`  { pkg: ${JSON.stringify(p.pkg.name)}, tools: ${local(p.pkg.name)} },`);
  }
  lines.push("];");
  lines.push("");
  lines.push("// One subcommand, one tool: the index is what a user types, so two packages may not declare a name (the");
  lines.push("// shell's five are checked where they are listed).");
  lines.push("const toolOwners = new Map<string, string>();");
  lines.push("for (const { pkg, tools } of CLI_CONTRIBUTIONS) {");
  lines.push("  for (const tool of tools) {");
  lines.push("    const owner = toolOwners.get(tool.name);");
  lines.push("    if (owner !== undefined) {");
  lines.push("      throw new Error(owner === pkg");
  lines.push("        ? `the package ${pkg} declares the CLI tool ${tool.name} twice`");
  lines.push("        : `two packages declare the CLI tool ${tool.name}: ${owner} and ${pkg}`);");
  lines.push("    }");
  lines.push("    toolOwners.set(tool.name, pkg);");
  lines.push("  }");
  lines.push("}");
  lines.push("");
  lines.push("/** Every contributed tool, in product order. */");
  lines.push("export const CLI_TOOLS: readonly CliTool[] = CLI_CONTRIBUTIONS.flatMap((entry) => entry.tools);");
  lines.push("");
  return lines.join("\n");
}

/** The files a tsconfig extends, as it writes them: `extends` takes one specifier or a list, and a tsconfig may carry
 *  comments, so the text is read as JSON with its comments removed rather than searched for a name. */
function tsconfigExtends(path) {
  const text = readFileSync(path, "utf8")
    .replace(/\/\*[\s\S]*?\*\//g, "")
    .replace(/(^|[^:])\/\/.*$/gm, "$1");
  let parsed;
  try {
    parsed = JSON.parse(text);
  } catch (error) {
    throw new Error(`${path}: it does not parse as a tsconfig (${error.message}), so its extends cannot be read`);
  }
  const value = parsed?.extends;
  if (typeof value === "string") return [value];
  return Array.isArray(value) ? value.filter((entry) => typeof entry === "string") : [];
}

/** The shell's host-module list (`host-modules.json`): the bare specifiers a package may import through its surface's
 *  host, and the declaration file a typecheck resolves each to. */
function hostModules(shellDir) {
  const path = join(shellDir, "host-modules.json");
  if (!existsSync(path)) throw new Error(`the shell has no ${path}: its host-module list names every host module`);
  const list = readJson(path);
  if (!Array.isArray(list)) throw new Error(`${path} is not a list`);
  for (const entry of list) {
    if (!isObject(entry) || typeof entry.module !== "string" || entry.module === "") {
      throw new Error(`${path} has an entry with no "module"`);
    }
  }
  return list;
}

/** The shell's own typecheck aliases (`tsconfig.aliases.json`): the paths only its program needs, which must ride in
 *  the file it extends because a child's `paths` replace the parent's rather than merging with it. `ui-harness` is the
 *  test harness alias scripts/run-ui-tests.mjs gives esbuild. */
function shellAliases(shellDir) {
  const path = join(shellDir, "tsconfig.aliases.json");
  if (!existsSync(path)) throw new Error(`the shell has no ${path}: it names the aliases its own program adds`);
  const aliases = readJson(path);
  if (!isObject(aliases)) throw new Error(`${path} is not an object of specifier: [path]`);
  for (const [specifier, targets] of Object.entries(aliases)) {
    if (!Array.isArray(targets) || targets.some((target) => typeof target !== "string" || target === "")) {
      throw new Error(`${path}: ${specifier} is not a list of paths`);
    }
  }
  return aliases;
}

/** One `paths` entry, as the generated tsconfigs spell it. */
function pathEntry(specifier, targets) {
  return `      ${JSON.stringify(specifier)}: [${targets.map((target) => JSON.stringify(target)).join(", ")}]`;
}

/** The generated typecheck paths: each host module's `types` file, resolved from the shell (where the generated config
 *  lives), so a package's tsconfig can extend it and `tsc` reads the same declaration the binary serves. */
function renderHostModules(list) {
  const typed = list.filter((entry) => typeof entry.types === "string" && entry.types !== "");
  const entries = typed.map((entry) => pathEntry(entry.module, [`./${entry.types.replaceAll("\\", "/")}`]));
  return [
    "// Generated by packages/retroplug/scripts/lib/packages.mjs from the shell's host-modules.json. Do not edit; edit",
    "// the list and rebuild. A package whose tsconfig extends this resolves a host module to the declaration file a",
    "// consumer reads, exactly as their tsconfig does.",
    "{",
    '  "compilerOptions": {',
    '    "paths": {',
    ...entries.map((entry, i) => `${entry}${i === entries.length - 1 ? "" : ","}`),
    "    }",
    "  }",
    "}",
    "",
  ].join("\n");
}

/** The shell program's own generated config: the same host modules, plus the shell's aliases. It exists because
 *  TypeScript REPLACES a child's `paths` with the extending config's rather than merging them, so the shell cannot
 *  extend the package-facing file and add its aliases beside it. */
function renderShellPaths(list, aliases) {
  const typed = list.filter((entry) => typeof entry.types === "string" && entry.types !== "");
  const entries = [
    ...typed.map((entry) => pathEntry(entry.module, [`./${entry.types.replaceAll("\\", "/")}`])),
    ...Object.entries(aliases).map(([specifier, targets]) => pathEntry(specifier, targets)),
  ];
  return [
    "// Generated by packages/retroplug/scripts/lib/packages.mjs from the shell's host-modules.json and its",
    "// tsconfig.aliases.json. Do not edit; edit those and rebuild. This is the shell program's paths: the host modules",
    "// every package resolves, and the aliases only the shell's own tests use.",
    "{",
    '  "compilerOptions": {',
    '    "paths": {',
    ...entries.map((entry, i) => `${entry}${i === entries.length - 1 ? "" : ","}`),
    "    }",
    "  }",
    "}",
    "",
  ].join("\n");
}

/** The manifest embed entries the generated module pairs with the app modules' declarations, as source text. */
function manifestsWithEmbeds(packages) {
  return packages
    .flatMap((p) => p.embeds.map((baked) => ({ pkg: p.pkg.name, id: baked.id })))
    .map((baked) => `{ pkg: ${JSON.stringify(baked.pkg)}, id: ${JSON.stringify(baked.id)} }`)
    .join(", ");
}

function renderConsoles({ packages, outDir, product }) {
  const contributors = packages.filter((p) => p.consoles);
  // A module identifier per package: the package name loses its punctuation, as the app imports' do, and a collision
  // would be a tsc error naming neither manifest.
  const local = (name) => `consoles_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The consoles the product's packages contribute, in product order, and the physical carts a project can drive.",
    "// The descriptors say how a ROM is classified, which node type places a system, what that node's knobs and stream",
    "// layouts are and how a render of it splits and shapes its output; a real cart says which platform's systems",
    "// reach it. Nothing here imports an app module or zod: the planner reads this list, and a bundle of the planner",
    "// must carry no role's schema (test/script/bundle-protocol.test.ts).",
    'import type { ConsoleDescriptor, RealCart } from "@retroplug/core/consoles";',
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.consoles)}";`);
  }
  lines.push(
    "",
    "/** The consoles, in product order: each package's descriptors, which the classifier, the node table, the battery",
    " *  rules and the render are read from. */",
    "export const CONSOLES: readonly ConsoleDescriptor[] = [",
    ...contributors.map((p) => `  ...(${local(p.pkg.name)}.consoles ?? []),`),
    "];",
    "// A platform id is looked up by the classifier, a stored project's validation and the menus, so one declared",
    "// twice would leave one of the two consoles unreachable; a node type places a system, so two descriptors sharing",
    "// one would make the node table depend on the order. (A descriptor's core must also be a system role, which",
    "// ./packages.ts checks: the roles are its.)",
    "const consolePlatforms = new Map<string, string>();",
    "const consoleNodeTypes = new Map<string, string>();",
    "for (const consoleDescriptor of CONSOLES) {",
    "  for (const consolePlatform of consoleDescriptor.platforms) {",
    "    const owner = consolePlatforms.get(consolePlatform.id);",
    "    if (owner !== undefined) {",
    "      throw new Error(`two console descriptors declare the platform ${consolePlatform.id}: ${owner} and ` +",
    "        consoleDescriptor.id);",
    "    }",
    "    consolePlatforms.set(consolePlatform.id, consoleDescriptor.id);",
    "  }",
    "  const nodeOwner = consoleNodeTypes.get(consoleDescriptor.nodeType);",
    "  if (nodeOwner !== undefined) {",
    "    throw new Error(`two console descriptors declare the node type ${consoleDescriptor.nodeType}: ` +",
    "      `${nodeOwner} and ${consoleDescriptor.id}`);",
    "  }",
    "  consoleNodeTypes.set(consoleDescriptor.nodeType, consoleDescriptor.id);",
    "}",
    "",
    "/** The physical carts a project can drive, in product order: each names the platform whose systems it receives",
    " *  and the node that owns its link. */",
    "export const REAL_CARTS: readonly RealCart[] = [",
    ...contributors.map((p) => `  ...(${local(p.pkg.name)}.realCarts ?? []),`),
    "];",
    "// One cart per platform and one per node, because a document places at most one: the store's lookup,",
    "// buildGraph's placement and the menu's jobs all resolve a platform's cart, and with two registered the answer",
    "// would be whichever package the product listed last.",
    "const cartPlatforms = new Map<string, string>();",
    "const cartNodes = new Map<string, string>();",
    "for (const realCart of REAL_CARTS) {",
    "  if (!consolePlatforms.has(realCart.platform)) {",
    "    throw new Error(`the real cart ${realCart.cartNode} names the platform ${realCart.platform}, which no ` +",
    "      \"console descriptor registers\");",
    "  }",
    "  const platformOwner = cartPlatforms.get(realCart.platform);",
    "  if (platformOwner !== undefined) {",
    "    throw new Error(`two real carts name the platform ${realCart.platform}: ${platformOwner} and ` +",
    "      realCart.cartNode);",
    "  }",
    "  cartPlatforms.set(realCart.platform, realCart.cartNode);",
    "  const nodeOwner = cartNodes.get(realCart.cartNode);",
    "  if (nodeOwner !== undefined) {",
    "    throw new Error(`two real carts name the cart node ${realCart.cartNode}: ${nodeOwner} and ` +",
    "      realCart.platform);",
    "  }",
    "  cartNodes.set(realCart.cartNode, realCart.platform);",
    "}",
    "",
  );
  return lines.join("\n");
}

/** A node field as the generated table spells it: a list inline, an object (a capture table) as spaced keys, so its
 *  line reads like its neighbours'. */
function literal(value) {
  if (Array.isArray(value)) return `[${value.map((v) => JSON.stringify(v)).join(", ")}]`;
  if (isObject(value)) {
    const entries = Object.entries(value).map(([k, v]) => `${JSON.stringify(k)}: ${JSON.stringify(v)}`);
    return `{ ${entries.join(", ")} }`;
  }
  return JSON.stringify(value);
}

function renderProtocol({ byType, roles, kindDecls, packages, outDir, shellSrc, product }) {
  const lines = [];
  lines.push(
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The protocol tables buildGraph, the stores and a DirectGraph wire a system's roles through, as the manifests",
    "// spell them. Nothing but a type is imported here: the data is apart from the node modules it describes, so a",
    "// bundle that reads it carries no node's code, no role's schema and nothing of the app.",
    `import type { ProtocolNodeSpec } from "${specifier(outDir, join(shellSrc, "graph/protocolNodes.ts"))}";`,
    'import type { KindData } from "@retroplug/core/defineKind";',
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
      for (const key of ["midiIn", "writes", "launches", "serialOut", "midiOut", "capture", "out"]) {
        if (node[key] === undefined) continue;
        fields.push(`${key}: ${literal(node[key])}`);
      }
      lines.push(`  ${JSON.stringify(node.type)}: { ${fields.join(", ")} },`);
    }
    lines.push("} as const satisfies Record<string, ProtocolNodeSpec>;");
  }
  lines.push(
    "",
    "/** Every protocol node type the product's packages declare, wired or not, in label order (a package's own placer",
    " *  places the ones the graph does not wire). */",
    "export const DECLARED_NODE_TYPES: readonly string[] = [",
    ...specs.map(({ node }) => `  ${JSON.stringify(node.type)},`),
    "];",
    "",
    "/** The role kinds that have a protocol node, each with the type it is placed as (the ids PROTOCOL_NODES wires),",
    " *  in the order of the labels they are placed at. */",
    "export const ROLE_NODES: Readonly<Record<string, keyof typeof PROTOCOL_NODES>> = {",
  );
  for (const { kind, type } of roles) lines.push(`  ${JSON.stringify(kind)}: ${JSON.stringify(type)},`);
  lines.push(
    "};",
    "",
    "/** The data the contributing manifests declare for each contribution kind's items, by kind and then item id (a",
    " *  controller app's sync override, say): only the keys the declaring manifest lists as data, and nothing else.",
    " *  DATA, so a planner applies it with no item code and no schema - an app module has both - and what a key means",
    " *  is its declaring package's check's. */",
    "export const KIND_DATA: Readonly<Record<string, KindData>> = {",
  );
  for (const [kind, { decl }] of kindDecls) {
    lines.push(`  ${JSON.stringify(kind)}: {`);
    for (const { pkg, kindEntries } of packages) {
      for (const entry of kindEntries) {
        if (entry.kind !== kind) continue;
        if (Object.keys(entry.data).length === 0) continue;
        lines.push(`    ${JSON.stringify(entry.id)}: ${JSON.stringify(entry.data)},`);
      }
    }
    lines.push("  },");
  }
  lines.push("};", "");
  return lines.join("\n");
}

function renderScriptNodes({ packages, outDir, kindDecls, product }) {
  const spec = packages.filter((p) => p.protocol);
  // Each declared kind's runtime halves: the contributing manifests' entries, in product order, and the declaring
  // package's module that registers them. A kind nothing contributes registers nothing.
  const kindRuntimes = [...kindDecls].map(([kind, { decl }]) => ({
    kind,
    decl,
    entries: packages.flatMap((p) => p.kindEntries.filter((entry) => entry.kind === kind)),
  }));
  const withKinds = kindRuntimes.filter((runtime) => runtime.entries.length > 0);
  const head = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The script-node bundle's entry: each contribution kind's runtime halves (the manifests' protocol.kinds",
    "// entries, registered by the declaring package), then every package's protocol node definitions, in span-label",
    "// order (spec/08-profiling.md: one label a type, 16 up), which the manifests' retroplug.protocol.nodes fix",
    "// whatever order a package lists its exports in. esbuilt to a global-code IIFE, compiled to QuickJS bytecode",
    "// and embedded by each product (packages/native/cmake/bundles.cmake), so every Engine loads it into both of",
    "// its script contexts.",
  ];
  if (!spec.length && !withKinds.length) {
    return [
      ...head,
      'import { registerScriptNodes } from "@retroplug/core/register";',
      "",
      `// No package in the ${product} product contributes a protocol node or an item of a declared contribution kind.`,
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
  const nodeLocals = new Map();
  for (const p of spec) {
    const name = local(`nodes_${identifier(p.pkg.name).slice(4)}`, `the package ${p.pkg.name}'s nodes module`);
    nodeLocals.set(p.pkg.name, name);
    lines.push(`import ${name} from "${moduleSpecifier(outDir, p.pkg, p.protocol)}";`);
  }
  // Per kind: the declaring package's runtime registry, then each contributing manifest's module, in product order.
  const kindLocals = [];
  for (const { kind, decl, entries } of withKinds) {
    lines.push(`import { ${decl.runtime.register}, type ${decl.runtime.type} } from ` +
      `"${moduleSpecifier(outDir, kindDecls.get(kind).pkg, decl.runtime.module)}";`);
    const items = [];
    for (const entry of entries) {
      const name = local(
        `item_${kind}_${entry.pkg.replace(/[^A-Za-z0-9_]/g, "_")}_${entry.id.replace(/[^A-Za-z0-9_]/g, "_")}`,
        `the package ${entry.pkg}'s ${kind} item ${entry.id}`,
      );
      items.push({ entry, name });
      const owner = packages.find((p) => p.pkg.name === entry.pkg);
      lines.push(`import ${name} from "${moduleSpecifier(outDir, owner.pkg, entry.file)}";`);
    }
    kindLocals.push({ kind, decl, items });
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
  for (const { kind, decl, items } of kindLocals) {
    lines.push(
      `// The ${kind} items' runtimes, in product order: the manifests' protocol.kinds.${kind}. Each import is one`,
      "// manifest entry's module, so the check is per entry: a module that registers another id would leave a",
      "// document naming an item nothing runs, and the message names the module and both ids rather than a set",
      "// difference.",
      `const kindItems_${kind}: readonly ${decl.runtime.type}[] = [`,
    );
    for (const item of items) lines.push(`  ${item.name},`);
    lines.push("];", `const kindModules_${kind}: readonly string[] = [`);
    for (const item of items) lines.push(`  ${JSON.stringify(item.name)},`);
    lines.push("];", `const kindIds_${kind}: readonly string[] = [`);
    for (const item of items) lines.push(`  ${JSON.stringify(item.entry.id)},`);
    lines.push(
      "];",
      `for (let i = 0; i < kindItems_${kind}.length; i++) {`,
      `  if (kindItems_${kind}[i].id !== kindIds_${kind}[i]) {`,
      "    throw new Error(",
      `      \`the script bundle's ${kind} item \${kindModules_${kind}[i]} registers ` +
        `\${kindItems_${kind}[i].id}, but its manifest entry declares \${kindIds_${kind}[i]}\``,
      "    );",
      "  }",
      "}",
      `${decl.runtime.register}(kindItems_${kind});`,
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

/** Generate the product's modules, into the shell's own tree for the desktop and into `<buildDir>/products/<product>/`
 *  (or `<productsDir>/<product>/`) for any other product. Returns the ordered package names and, for each file, whether
 *  it was written (it is not, when its content is unchanged). */
export function generate({
  root = REPO,
  product = DESKTOP,
  buildDir = join(root, "build"),
  productsDir = join(buildDir, "products"),
} = {}) {
  const manifestPath = join(root, "package.json");
  const manifest = readJson(manifestPath);
  const retroplug = isObject(manifest.retroplug) ? manifest.retroplug : {};
  const products = isObject(retroplug.products) ? retroplug.products : {};
  const desktop = products[DESKTOP];
  if (!Array.isArray(desktop) || !desktop.length) {
    throw new Error(`${manifestPath} has no retroplug.products.${DESKTOP} list of package names`);
  }
  // Every list is validated as the desktop's is, and one that is not the desktop's must be a SUBSEQUENCE of it in the
  // same relative order: the registration order and the set CMake adds cannot then disagree, and this product's
  // script-node span labels keep the desktop's order (they are the product's own positions, see nodeTable).
  const names = products[product];
  if (!Array.isArray(names)) {
    throw new Error(`${manifestPath} has no retroplug.products.${product} list of package names (the products are ` +
      Object.keys(products).join(", ") + ")");
  }
  const validateList = (list, name) => {
    if (!list.length) throw new Error(`${manifestPath} has an empty retroplug.products.${name} list`);
    if (list.some((entry) => typeof entry !== "string" || entry === "")) {
      throw new Error(`${manifestPath} lists a package name that is not a name in retroplug.products.${name}`);
    }
    if (new Set(list).size !== list.length) {
      throw new Error(`${manifestPath} lists a package twice in retroplug.products.${name}`);
    }
    if (list.includes(SHELL)) {
      throw new Error(`${manifestPath} lists the shell (${SHELL}) in retroplug.products.${name}`);
    }
  };
  validateList(desktop, DESKTOP);
  if (product !== DESKTOP) {
    validateList(names, product);
    let at = 0;
    for (const name of names) {
      const found = desktop.indexOf(name, at);
      if (found === -1) {
        throw new Error(`retroplug.products.${product} is not a subsequence of retroplug.products.${DESKTOP}: ` +
          `${name} is in no order there, or out of it`);
      }
      at = found + 1;
    }
  }

  const ordered = order(root, names, product);
  const shell = ordered[ordered.length - 1];
  // Where this product's generated modules land: the shell's tree for the desktop (so the typecheck, the pure tier,
  // the shell's tests and every existing path are unchanged), and a parallel tree under the build directory for any
  // other product, laid out as a shell's is so the same relative specifiers hold. A product that is not the desktop
  // ships no CLI, so its tree has no cli/generated.
  const productTree = product === DESKTOP ? shell.dir : join(productsDir, product, "packages", shell.name);
  const outDir = join(productTree, GENERATED);
  const uiOutDir = join(productTree, "ui/generated");
  const cliOutDir = product === DESKTOP ? join(shell.dir, "cli/generated") : null;
  const contributionsOf = ordered.map((pkg) => ({ pkg, ...contributions(pkg) }));
  const { byType } = nodeTable(contributionsOf, product);
  const kinds = roleTable(contributionsOf);
  const kindDecls = validateKinds(contributionsOf);

  // A capture is for the node whose package DECODES the bytes: the core is clocked and its serial out captured, and
  // the decoder is the second node the capture-carrier names as its `out` (rp.lsdj.serial-out for rp.lsdj.sync). Each
  // misuse here would arm a core whose bytes nothing reads: an unwired node is dropped out of PROTOCOL_NODES whole, and
  // an `out` that is no serial-out node decodes nothing, so the core would be clocked under the bytes the placed role
  // writes.
  for (const { pkg, nodes } of contributionsOf) {
    for (const node of nodes) {
      if (node.capture === undefined) continue;
      if (node.midiIn === undefined && node.writes === undefined) {
        throw new Error(`${pkg.name}: the node ${node.type} declares a capture and is not wired (no midiIn and ` +
          "writes), so PROTOCOL_NODES would drop it whole and nothing would decode the bytes it clocked");
      }
      if (node.out === undefined) {
        throw new Error(`${pkg.name}: the node ${node.type} declares a capture and no out: a capture is for the ` +
          "serial-out node that decodes the bytes");
      }
      const decoder = byType.get(node.out);
      if (decoder === undefined) {
        throw new Error(`${pkg.name}: the node ${node.type} declares a capture decoded at ${node.out}, which no ` +
          "package declares");
      }
      if (decoder.node.serialOut !== true) {
        throw new Error(`${pkg.name}: the node ${node.type} declares a capture decoded at ${node.out}, which declares ` +
          "no serialOut");
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

  // A package's declared host modules are the shell's list, and a name it does not have is refused here rather than at
  // a bundle: the generated typecheck paths come from the same list.
  const hostList = hostModules(shell.dir);
  for (const { pkg, host } of contributionsOf) {
    for (const module of host) {
      if (!hostList.some((entry) => entry.module === module)) {
        throw new Error(`${pkg.name}: retroplug.host names ${module}, which the shell's host-modules.json does not list`);
      }
    }
  }

  // A package's TypeScript that resolves a host module through the generated paths must have a tsconfig extending them:
  // an example session (the shell's program includes `../*/examples`), and a package declaring a host module the UI
  // owns (`surface: "ui"`), because a `.tsx` reaches `react` and `react/jsx-runtime` only through those paths. A
  // tsconfig that includes the files without the extend fails its own `tsc` (an unresolved host specifier), which is
  // the visible half; one that leaves them out of `include` typechecks clean and hands the files to the shell's program
  // alone, which is the half nothing else catches. The parsed `extends` decides, never the text: naming the file in a
  // comment resolves nothing.
  const hostConfigName = "tsconfig.host-modules.json";
  const hostConfig = join(shell.dir, hostConfigName);
  // Reading a host-modules entry's surfaces exactly as check-boundaries.mjs's surfacesOf does, non-string entries
  // dropped: the generator and the checker judge one manifest shape and must agree on it.
  const surfacesOf = (entry) =>
    typeof entry.surface === "string"
      ? [entry.surface]
      : Array.isArray(entry.surface)
        ? entry.surface.filter((surface) => typeof surface === "string")
        : [];
  for (const { pkg, examples, host } of contributionsOf) {
    const uiHost = host.some((module) =>
      surfacesOf(hostList.find((entry) => entry.module === module) ?? {}).includes("ui"));
    const why = examples.length ? "it lists examples" : uiHost ? "it declares a UI host module" : null;
    if (why === null) continue;
    // What the extend is for, in the two messages' own phrasing (they predate the React half and read as they did).
    const must = examples.length ? "an example resolves its host module" : "a .tsx resolves React";
    const where = examples.length ? "an example resolves a host module" : "a .tsx resolves React";
    const tsconfigPath = join(pkg.dir, "tsconfig.json");
    if (!existsSync(tsconfigPath)) {
      throw new Error(`${pkg.name}: ${why} but has no tsconfig.json, which must extend ` +
        `../${SHELL}/${hostConfigName} so ${must}`);
    }
    const extensions = tsconfigExtends(tsconfigPath).map((entry) => resolve(pkg.dir, entry));
    if (!extensions.includes(hostConfig)) {
      throw new Error(`${pkg.name}: ${why} but its tsconfig.json does not extend ` +
        `../${SHELL}/${hostConfigName}, where ${where}`);
    }
  }

  // One build/cli/<name>.js per session: the shell's own three sessions are bundled to their own names, and two
  // packages' examples (or one package's twice) writing the same stem would silently overwrite each other's bundle.
  const exampleOwners = new Map([...SHELL_EXAMPLES].map(([name, entry]) => [name, `${SHELL} (${entry})`]));
  for (const { pkg, examples } of contributionsOf) {
    for (const example of examples) {
      const owner = exampleOwners.get(example.name);
      if (owner !== undefined) {
        const shellOwnsIt = SHELL_EXAMPLES.has(example.name);
        throw new Error(shellOwnsIt
          ? `${pkg.name}: the example session ${example.name} is the shell's own (${SHELL_EXAMPLES.get(example.name)}), ` +
            `which is bundled to the same build/cli/${example.name}.js`
          : `the example session ${example.name} is declared twice: ${owner} and ${pkg.name}`);
      }
      exampleOwners.set(example.name, pkg.name);
    }
  }

  const shellSrc = join(shell.dir, "src");
  const files = [
    {
      path: join(outDir, "packages.ts"),
      content: renderPackages({ packages: contributionsOf, outDir, names, product }),
    },
    {
      path: join(outDir, "consoles.ts"),
      content: renderConsoles({ packages: contributionsOf, outDir, product }),
    },
    {
      path: join(outDir, "protocol.ts"),
      content: renderProtocol({ byType, roles, kindDecls, packages: contributionsOf, outDir, shellSrc, product }),
    },
    {
      path: join(outDir, "scriptNodes.ts"),
      content: renderScriptNodes({ packages: contributionsOf, outDir, kindDecls, product }),
    },
    {
      path: join(outDir, "placers.ts"),
      content: renderPlacers({ packages: contributionsOf, outDir, product }),
    },
    {
      path: join(uiOutDir, "menus.ts"),
      content: renderMenus({ packages: contributionsOf, outDir: uiOutDir, generatedDir: outDir, shellSrc, product }),
    },
    {
      path: join(uiOutDir, "views.ts"),
      content: renderViews({ packages: contributionsOf, outDir: uiOutDir, product }),
    },
  ];
  if (cliOutDir) {
    files.push({
      path: join(cliOutDir, "tools.ts"),
      content: renderCliTools({ packages: contributionsOf, outDir: cliOutDir, product }),
    });
  }
  if (product === DESKTOP) {
    // The desktop's typecheck paths are the shell program's: a product's tree typechecks through its own tsconfig.
    files.push(
      { path: join(shell.dir, hostConfigName), content: renderHostModules(hostList) },
      {
        path: join(shell.dir, "tsconfig.paths.json"),
        content: renderShellPaths(hostList, shellAliases(shell.dir)),
      },
    );
  } else {
    const productDir = join(productsDir, product);
    files.push(
      {
        path: join(productDir, "tsconfig.json"),
        content: renderProductTsconfig(shell, outDir, "tsconfig.paths.json"),
      },
      {
        path: join(productDir, "tsconfig.paths.json"),
        content: renderProductPaths([
          ...shellPathEntries(shell.dir, hostList, productDir),
          ...workspacePathEntries(root, productDir),
        ]),
      },
    );
  }
  mkdirSync(outDir, { recursive: true });
  mkdirSync(uiOutDir, { recursive: true });
  if (cliOutDir) mkdirSync(cliOutDir, { recursive: true });
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
  const flag = (name) => {
    const at = process.argv.indexOf(name);
    return at === -1 ? undefined : resolve(process.argv[at + 1] ?? "");
  };
  const productAt = process.argv.indexOf("--product");
  const product = productAt === -1 ? DESKTOP : process.argv[productAt + 1];
  try {
    // The root's build directory by default, as generate() itself defaults: a fixture run with --root and no
    // --build-dir must not write into the real repository's build/.
    const root = flag("--root") ?? REPO;
    const buildDir = flag("--build-dir") ?? join(root, "build");
    generate({
      root,
      product,
      buildDir,
      productsDir: flag("--products-dir") ?? join(buildDir, "products"),
    });
  } catch (e) {
    console.error(`packages.mjs: ${e.message}`);
    process.exit(1);
  }
}
