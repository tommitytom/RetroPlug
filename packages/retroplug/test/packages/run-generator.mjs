#!/usr/bin/env node
// Drives packages/retroplug/scripts/lib/packages.mjs over its fixture tree for
// test/packages/packages-generator.test.ts.
// The generator is Node's (it reads and writes the filesystem), so a test that runs under txiki spawns this instead of
// importing it, as bundle-no-zod.test.ts and device-pack.test.ts run their Node toolchains.
//
//   node test/packages/run-generator.mjs <mode>
//
// Each mode copies fixtures/tree to a fresh directory (so a refusal cannot leave the fixtures mutated and a mode can
// edit what it needs), edits it where the mode is a refusal, runs the generator and prints one JSON line:
//
//   product <name>  the same, from a product list the root manifest declares, read from its tree under
//                   build/products/<name>/ (and the desktop's tree too, for a case that checks the two moved apart);
//                   it also reports the product's own script-node labels
//   generate        { files: { "packages.ts": <text>, "consoles.ts": <text>, "protocol.ts": <text>,
//                              "scriptNodes.ts": <text>, "placers.ts": <text>, "menus.ts": <text>,
//                              "views.ts": <text> }, packages: [...] }
//   consoles        { consoles: [{ id, core, nodeType, platforms }], realCarts: [{ platform, cartNode }] }
//   twice           { mtimes: [<after one run>, <after a second>], changed: [<first run>, <second run>] }
//   run             { registered: [<the ids the generated entry registers, in order>] }
//   missing-node    { error: <the generated entry's own refusal> }
//   order | cycle | shell-unlisted | unlisted-contributes | unlisted-plain | depends-on-shell | duplicate | duplicate-type |
//   duplicate-role | gap | low-label | role-undeclared | role-unwired | missing-module | unexported-app |
//   name-mismatch | outside-ts | shell-undeclared | unknown-key | capture-bad | half-wired | extra-node |
//   duplicate-role-kind |
//   duplicate-kit-format | duplicate-marker-role | duplicate-app | duplicate-app-control | app-unknown-key |
//   duplicate-section | duplicate-service | duplicate-placer | shell-section | placer-missing |
//   kind-duplicate-item | kind-one-half | kind-check-unknown-node | kind-app-member |
//   app-unpaired-runtime | app-unpaired-control | app-runtime-mismatch | app-module-empty |
//   duplicate-app-one-package | identifier-collision | apps-only | capture-unwired | capture-no-decoder |
//   capture-not-object | capture-out-undeclared | capture-out-not-serial | capture-form | embed-form |
//   embed-unpaired-manifest | embed-unpaired-declaration | embed-unregistered-platform | embed-duplicate |
//   embed-declared-twice | embed-manifest-not-list | embed-manifest-no-id | embed-manifest-no-file |
//   embed-manifest-id-twice | embed-mismatched-package | duplicate-console-platform |
//   console-core-norole | duplicate-console-type | realcart-unregistered | realcart-platform-twice |
//   realcart-node-twice | ui-missing-module
//                   { error: <the generator's message, or null when it did not refuse> }
//   ui | ui-duplicate-row | ui-unknown-placement | ui-role-undeclared | ui-unknown-key | ui-unknown-when-key |
//   ui-unknown-tracker | ui-duplicate-tracker | ui-duplicate-service | views | views-duplicate-id |
//   views-duplicate-across | views-bad-placement | views-bad-kind | views-no-body | views-no-open |
//   views-no-component | views-window-visible | views-outside-ui | ui-outside-ui
//   cli | cli-duplicate-tool | cli-missing-module | examples-outside | examples-tsconfig-missing |
//   examples-tsconfig-unextended | examples-tsconfig-extended | examples-duplicate-name | examples-shell-name |
//   examples-unsafe-name | host-unknown | host-known | react-tsconfig-missing | react-tsconfig-unextended |
//   react-tsconfig-extended | actions | actions-duplicate-id | actions-duplicate-across | actions-bad-prefix |
//   actions-unknown-key | actions-uppercase-key | actions-unknown-pad | actions-shell-key | actions-gb-key |
//   actions-return-key | actions-gb-pad
//                   the menus module: ui reports the contributing packages in product order, the others load it into
//                   its own refusal ({ error })
//                   (unlisted-plain generates, and reports files like generate)
//
// The run, missing-node and extra-node modes bundle the generated entry with the same esbuild options the build uses
// and load it, so the entry's own checks are what is exercised, not a copy of them.
import {
  cpSync, mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, statSync, utimesSync, writeFileSync,
} from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { generate } from "../../scripts/lib/packages.mjs";
import { fixtureAliases } from "./fixtureAliases.mjs";

const require = createRequire(import.meta.url);
const { buildOptions } = require("../../../../tools/build-session.js");

const HERE = dirname(fileURLToPath(import.meta.url));
const FIXTURES = join(HERE, "fixtures");
const MODE = process.argv[2];

const work = mkdtempSync(join(tmpdir(), "rp-packages-"));
const root = join(work, "tree");
cpSync(join(FIXTURES, "tree"), root, { recursive: true });

const generated = join(root, "packages/retroplug/src/generated");

function editManifest(name, edit) {
  const path = join(root, "packages", name, "package.json");
  const json = JSON.parse(readFileSync(path, "utf8"));
  edit(json);
  writeFileSync(path, `${JSON.stringify(json, null, 2)}\n`);
}

function editList(...names) {
  const path = join(root, "package.json");
  const json = JSON.parse(readFileSync(path, "utf8"));
  json.retroplug.products.desktop = names;
  writeFileSync(path, `${JSON.stringify(json, null, 2)}\n`);
}

/** A product's generated tree, as a product writes it: under the build directory, in the shell's shape. */
function productOutputs(work, name) {
  const tree = join(work, "build/products", name, "packages/retroplug");
  const read = (...parts) => readFileSync(join(tree, ...parts), "utf8");
  return {
    "packages.ts": read("src/generated/packages.ts"),
    "consoles.ts": read("src/generated/consoles.ts"),
    "protocol.ts": read("src/generated/protocol.ts"),
    "scriptNodes.ts": read("src/generated/scriptNodes.ts"),
    "placers.ts": read("src/generated/placers.ts"),
    "menus.ts": read("ui/generated/menus.ts"),
    "views.ts": read("ui/generated/views.ts"),
    tsconfig: readFileSync(join(work, "build/products", name, "tsconfig.json"), "utf8"),
  };
}

function outputs() {
  return {
    "packages.ts": readFileSync(join(generated, "packages.ts"), "utf8"),
    "consoles.ts": readFileSync(join(generated, "consoles.ts"), "utf8"),
    "protocol.ts": readFileSync(join(generated, "protocol.ts"), "utf8"),
    "scriptNodes.ts": readFileSync(join(generated, "scriptNodes.ts"), "utf8"),
    "placers.ts": readFileSync(join(generated, "placers.ts"), "utf8"),
    "menus.ts": readFileSync(join(root, "packages/retroplug/ui/generated/menus.ts"), "utf8"),
    "views.ts": readFileSync(join(root, "packages/retroplug/ui/generated/views.ts"), "utf8"),
  };
}

function refusal() {
  try {
    generate({ root });
  } catch (e) {
    return { error: e.message };
  }
  return { error: null };
}

/** The fixture apps, each contributing a role and one entry of every other list: every entry carries the package's
 *  name, and the integration its marker role, since those are the keys the generated module checks for a duplicate. */
function contributionApp(name) {
  const role = name === "retroplug" ? "shell-role" : `${name}-role`;
  return (
    "export default {\n" +
    `  roles: [{ kind: "${role}" }],\n` +
    `  romProviders: [{ id: "${name}" }],\n` +
    `  trackerIntegrations: [{ id: "${name}", markerRole: "${name}-marker" }],\n` +
    `  kitFormats: [{ id: "${name}" }],\n` +
    "};\n"
  );
}

/** A fixture app whose four lists the mode writes: a shared key in one list is what the generated module refuses. */
function appWith({ kind, provider, markerRole, kitId }) {
  return (
    "export default {\n" +
    `  roles: [{ kind: "${kind}" }],\n` +
    `  romProviders: [{ id: "${provider}" }],\n` +
    `  trackerIntegrations: [{ id: "${markerRole}", markerRole: "${markerRole}" }],\n` +
    `  kitFormats: [{ id: "${kitId}" }],\n` +
    "};\n"
  );
}

/** The generated placers module, bundled and loaded: its checks run at load, as a build's do (two packages placing one
 *  graph model). The fixture packages' placers are plain objects, so nothing else is aliased. */
async function loadPlacers() {
  const out = join(work, "placers.mjs");
  const { buildSync } = require("esbuild");
  buildSync({
    ...buildOptions(join(generated, "placers.ts"), out, "esm"),
    alias: {
      "@retroplug/alpha/placer": join(root, "packages/alpha/ts/placer.ts"),
      "@retroplug/beta/placer": join(root, "packages/beta/ts/placer.ts"),
    },
  });
  return import(pathToFileURL(out).href);
}

/** A fixture app contributing one project section and one service, each under its own key: the generated registry
 *  collects them (packages.ts's PROJECT_SECTIONS and PROJECT_SERVICES) and refuses a key two packages share. */
function sectionApp(sectionKey, serviceKey = sectionKey) {
  return (
    "export default {\n" +
    // alpha's manifest declares a controllerApps runtime, so the app keeps its control-plane half: a mode testing a
    // section or service rule must not fail the kind pairing first.
    '  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v) => v ?? {} } }],\n' +
    `  projectSections: [{ key: ${JSON.stringify(sectionKey)}, schema: { parse: (v) => v }, ` +
    `defaults: () => ({}) }],\n` +
    `  projectServices: [{ key: ${JSON.stringify(serviceKey)}, create: () => ({}) }],\n` +
    "};\n"
  );
}

// The generated packages.ts, bundled and loaded: the lists are plain data (the apps it imports are plain objects), so
// loading reports exactly what a build's registries would see.
async function loadPackages() {
  const out = join(work, "packages.mjs");
  const consolesOut = join(work, "consoles.mjs");
  const { buildSync } = require("esbuild");
  const alias = {
    "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
    "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
    "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
    "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
    // The two declaring packages' control-plane halves (a kind's `accept` and `check`, run at load).
    "@retroplug/beta/kind": join(root, "packages/beta/ts/kind.ts"),
    "@retroplug/alpha/kind": join(root, "packages/alpha/ts/widgetKind.ts"),
    // packages.ts imports the generated placers for their keys' section check, so the fixture's placer resolves too.
    "@retroplug/alpha/placer": join(root, "packages/alpha/ts/placer.ts"),
    "@retroplug/beta/placer": join(root, "packages/beta/ts/placer.ts"),
  };
  buildSync({ ...buildOptions(join(generated, "packages.ts"), out, "esm"), alias });
  // The consoles are a module of their own, so each generated list is loaded as the build loads it: the app's
  // registrations from packages.ts (which reads the console list for its core-role check) and the descriptors from
  // consoles.ts.
  buildSync({ ...buildOptions(join(generated, "consoles.ts"), consolesOut, "esm"), alias });
  const loaded = await import(pathToFileURL(out).href);
  const consoles = await import(pathToFileURL(consolesOut).href);
  return {
    roles: loaded.ROLES.map((role) => role.kind),
    romProviders: loaded.ROM_PROVIDERS.map((provider) => provider.id),
    trackerIntegrations: loaded.TRACKER_INTEGRATIONS.map((integration) => integration.id),
    kitFormats: loaded.KIT_FORMATS.map((format) => format.id),
    projectSections: loaded.PROJECT_SECTIONS.map((entry) => `${entry.pkg}:${entry.section.key}`),
    projectServices: loaded.PROJECT_SERVICES.map((entry) => entry.service.key),
    embeds: loaded.EMBEDS.map((embed) => ({ id: embed.id, label: embed.label, platform: embed.platform })),
    consoles: consoles.CONSOLES.map((descriptor) => ({
      id: descriptor.id,
      core: descriptor.core,
      nodeType: descriptor.nodeType,
      platforms: descriptor.platforms.map((platform) => platform.id),
    })),
    realCarts: consoles.REAL_CARTS.map((cart) => ({ platform: cart.platform, cartNode: cart.cartNode })),
  };
}

/** A fixture app for the menus modes: the role a ui row names, and alpha's controller-app control-plane half, which
 *  its manifest's protocol.kinds.controllerApps entry pairs with (the generated module refuses one half
 *  alone). */
function uiApp(name) {
  return (
    "export default {\n" +
    // The role carries a schema, because a row's key (and its gate's) is held to the keys its role declares; the
    // tracker integration is what a tracker UI's id is held to.
    `  roles: [{ kind: "${name}-role", schema: { parse: () => ({ model: "a" }) } }],\n` +
    `  trackerIntegrations: [{ id: "${name}-tracks" }],\n` +
    (name === "alpha"
      ? '  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v) => v ?? {} } }],\n'
      : "") +
    "};\n"
  );
}

/** The aliases every menu-module loader needs: the fixture packages' ui and app modules the way a pnpm link resolves
 *  them, the real @retroplug/ui contract, and the fixture shell's src/*.ts shims of the real shell's binding
 *  vocabulary. One map, shared with test/script/bundle-views.mjs. */
function menusAlias() {
  return fixtureAliases(root);
}

/** Bundle one of the shell's generated modules over the fixture tree and load it, running its own checks: the menus
 *  module holds a row's role kind and id, a builder's placement and an action's id and defaults, and the views module a
 *  view's id, placement and body. */
async function importGenerated(module, name) {
  const out = join(work, name);
  const { buildSync } = require("esbuild");
  buildSync({
    ...buildOptions(join(root, `packages/retroplug/ui/generated/${module}.ts`), out, "esm"),
    alias: menusAlias(),
  });
  return import(pathToFileURL(out).href);
}

async function loadMenus() {
  return (await importGenerated("menus", "menus.mjs")).UI_CONTRIBUTIONS.map((entry) => entry.pkg);
}

/** The package ui modules the generated menus module imports, in the order it imports them: the filter's answer
 *  without loading it, which a mode whose fixture rows name no registered role could not do. */
function menusImports() {
  const source = readFileSync(join(root, "packages/retroplug/ui/generated/menus.ts"), "utf8");
  return [...source.matchAll(/^import ui_\w+ from "@retroplug\/([^/]+)\/ui";$/gm)].map((match) => match[1]);
}

/** The generated views module's VIEWS: product order, and each view's id, placement and body kind as the registry
 *  reports them. */
async function loadViews() {
  const loaded = await importGenerated("views", "views.mjs");
  return loaded.VIEWS.map(({ pkg, view }) => ({ pkg, id: view.id, placement: view.placement, kind: view.body.kind }));
}

/** The generated menus module's ACTIONS: product order, and each action's id, label and default key and pad names as
 *  the registry reports them. */
async function loadActions() {
  const loaded = await importGenerated("menus", "actions.mjs");
  return loaded.ACTIONS.map(({ pkg, action }) => ({
    pkg,
    id: action.id,
    label: action.label,
    keys: action.keys ?? [],
    pad: action.pad ?? [],
  }));
}

/** Load the menus module with alpha contributing `ui`, and report the registry's own refusal (or null): the action
 *  checks run as the module loads, so a bad default is a load error. */
async function actionRefusal(ui) {
  writeTree("packages/alpha/ts/ui/index.ts", ui);
  editManifest("alpha", (m) => {
    m.retroplug.ui = "ts/ui/index.ts";
    m.exports["./ui"] = "./ts/ui/index.ts";
  });
  generate({ root });
  try {
    await loadActions();
    return { error: null };
  } catch (e) {
    return { error: e.message };
  }
}

/** The generated CLI tools module, bundled and loaded as the dispatcher loads it: loading it runs its own refusal of a
 *  name two packages declare. */
async function loadCli() {
  const out = join(work, "tools.mjs");
  const { buildSync } = require("esbuild");
  buildSync({
    ...buildOptions(join(root, "packages/retroplug/cli/generated/tools.ts"), out, "esm"),
    alias: {
      "@retroplug/core/cliTool": join(HERE, "../../../core/ts/cliTool.ts"),
      "@retroplug/alpha/cli": join(root, "packages/alpha/ts/cli.ts"),
      "@retroplug/beta/cli": join(root, "packages/beta/ts/cli.ts"),
      "@retroplug/zeta/cli": join(root, "packages/zeta/ts/cli.ts"),
    },
  });
  const loaded = await import(pathToFileURL(out).href);
  return loaded.CLI_CONTRIBUTIONS.map((entry) => ({ pkg: entry.pkg, tools: entry.tools.map((tool) => tool.name) }));
}

// The generated entry, bundled and loaded as the build loads it. Loading it runs its checks and registers.
async function loadEntry() {
  const out = join(work, "scriptNodes.mjs");
  const { buildSync } = require("esbuild");
  // The fixture tree is not a workspace: alias core's modules the way a pnpm link would resolve them.
  buildSync({
    ...buildOptions(join(generated, "scriptNodes.ts"), out, "esm"),
    alias: {
      "@retroplug/core/defineNode": join(root, "packages/core/ts/defineNode.ts"),
      "@retroplug/core/register": join(root, "packages/core/ts/register.ts"),
      "@retroplug/alpha/nodes": join(root, "packages/alpha/ts/nodes.ts"),
      "@retroplug/alpha/appRuntime": join(root, "packages/alpha/ts/appRuntime.ts"),
      // Each declaring package's runtime registry and each contributing manifest's module, in product order.
      "@retroplug/beta/apps": join(root, "packages/beta/ts/apps.ts"),
      "@retroplug/alpha/widgets": join(root, "packages/alpha/ts/widgets.ts"),
      "@retroplug/beta/widgetRuntime": join(root, "packages/beta/ts/widgetRuntime.ts"),
    },
  });
  await import(pathToFileURL(out).href);
  return { registered: globalThis.__rp_registered };
}

/** A fixture app contributing a system-category role (the core-config role a console's core is named for): the app
 *  half of a package that runs a console. */
function consoleApp({ role, keepsApp = false }) {
  return (
    "export default {\n" +
    `  roles: [{ kind: "${role}", category: "system" }],\n` +
    // The manifest still declares alpha's controller app runtime, so its control-plane half has to stay contributed.
    (keepsApp
      ? '  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v) => v ?? {} } }],\n'
      : "") +
    "};\n"
  );
}

/** A fixture app module declaring embedded ROMs (the shell loads by the declaration; the manifest bakes the bytes). It
 *  keeps the role the modes' console descriptors name as their core, and alpha's control-plane half, since the fixture
 *  manifests declare both. */
function embedApp(name, embeds) {
  const entry = (e) =>
    `{ id: ${JSON.stringify(e.id)}, label: ${JSON.stringify(e.label ?? e.id)}, ` +
    `platform: ${JSON.stringify(e.platform)} }`;
  const app =
    name === "alpha"
      ? '  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v) => v ?? {} } }],\n'
      : "";
  return (
    "export default {\n" +
    `  roles: [{ kind: "${name}-core", category: "system" }],\n` +
    app +
    `  embeds: [${embeds.map(entry).join(", ")}],\n` +
    "};\n"
  );
}

/** A fixture package's retroplug.consoles module: the descriptors its cores run and the physical carts one of them can
 *  drive. It is apart from the app module because the planner reads it and must carry no schema, so a mode that
 *  registers a console writes this file. */
function consoleModule({ descriptors = [], realCarts = [] }) {
  const descriptor = (d) =>
    `{ id: "${d.id}", core: "${d.core}", nodeType: "${d.nodeType}", platforms: [{ id: "${d.platform}", label: ` +
    `"${d.platform}", extensions: [".${d.platform}"] }] }`;
  const cart = (c) => `{ platform: "${c.platform}", cartNode: "${c.cartNode}" }`;
  return (
    "export default {\n" +
    `  consoles: [${descriptors.map(descriptor).join(", ")}],\n` +
    (realCarts.length ? `  realCarts: [${realCarts.map(cart).join(", ")}],\n` : "") +
    "};\n"
  );
}

// A file in the fixture tree, rewritten for a mode.
function writeTree(relative, text) {
  mkdirSync(dirname(join(root, relative)), { recursive: true });
  writeFileSync(join(root, relative), text);
}

/** Give a fixture package a views module at the path its manifest names: a package may declare `views` and no `ui`
 *  (the `views` mode deletes zeta's `ui` member, so it is one) or both. */
function viewsModule(name, source) {
  writeTree(`packages/${name}/ts/ui/views.ts`, source);
  editManifest(name, (m) => {
    m.retroplug.views = "ts/ui/views.ts";
    m.exports["./views"] = "./ts/ui/views.ts";
  });
}

let result;
try {
  switch (MODE) {
    case "generate": {
      const ran = generate({ root });
      // The collections are read from the generated modules too: each list's product order is what the shell consumes.
      const loaded = await loadPackages();
      result = {
        files: outputs(),
        packages: ran.packages,
        projectSections: loaded.projectSections,
        projectServices: loaded.projectServices,
      };
      break;
    }
    case "product": {
      // A product list that is a subsequence of the desktop's: its own tree under the build directory, laid out as the
      // shell's is, and the desktop's tree unchanged beside it.
      const name = process.argv[3];
      generate({ root });
      generate({ root, product: name, buildDir: join(work, "build") });
      result = { files: productOutputs(work, name), desktop: outputs() };
      break;
    }
    case "product-refused": {
      // Every list is validated as the desktop's is, and one that is not the desktop's is a subsequence of it.
      const name = process.argv[3];
      try {
        generate({ root, product: name, buildDir: join(work, "build") });
        result = { error: null };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    }
    case "twice": {
      const first = generate({ root });
      const files = [
        ...["packages.ts", "consoles.ts", "protocol.ts", "scriptNodes.ts", "placers.ts"].map((name) =>
          join(generated, name)),
        join(root, "packages/retroplug/ui/generated/menus.ts"),
        join(root, "packages/retroplug/ui/generated/views.ts"),
        join(root, "packages/retroplug/cli/generated/tools.ts"),
        join(root, "packages/retroplug/tsconfig.host-modules.json"),
        join(root, "packages/retroplug/tsconfig.paths.json"),
      ];
      // An old, fixed mtime makes "the second run wrote it again" visible without depending on clock granularity: a
      // write replaces it with the run's time, a run that leaves the file alone leaves this.
      const old = new Date(Date.now() - 60000);
      for (const file of files) utimesSync(file, old, old);
      const before = files.map((file) => statSync(file).mtimeMs);
      const second = generate({ root });
      const after = files.map((file) => statSync(file).mtimeMs);
      result = {
        mtimes: [before, after],
        changed: [first.files.map((f) => f.changed), second.files.map((f) => f.changed)],
      };
      break;
    }
    case "run":
      generate({ root });
      result = await loadEntry();
      break;
    case "missing-node":
      // A manifest node its module does not export: the entry must refuse rather than let every later type take the
      // label below its own.
      generate({ root });
      writeTree("packages/alpha/ts/nodes.ts", "export default [];\n");
      try {
        result = await loadEntry();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui":
      // The positive side: alpha's app contributes the role its ui module's row names, and beta contributes a builder,
      // so the menus module loads and its contributions are reported in product order. zeta contributes without an app
      // of its own and is named FIRST in the product list, so the reported order (zeta, alpha, beta) is the list's and
      // not a sort of the three names.
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      writeTree(
        "packages/beta/ts/ui/index.ts",
        'export default { menus: [{ placement: "start", build: () => [] }] };\n',
      );
      editManifest("beta", (m) => {
        m.retroplug.ui = "ts/ui/index.ts";
        m.exports["./ui"] = "./ts/ui/index.ts";
      });
      generate({ root });
      result = { packages: await loadMenus() };
      break;
    case "ui-duplicate-row":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default {\n  rows: [\n' +
          '    { id: "alpha-model", key: "model", label: "Model", role: "alpha-role", toggle: true },\n' +
          '    { id: "alpha-model", key: "other", label: "Other", role: "alpha-role", toggle: true },\n' +
          "  ],\n};\n",
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-unknown-placement":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default { menus: [{ placement: "instance.elsewhere", build: () => [] }] };\n',
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-role-undeclared":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default {\n  rows: [{ id: "alpha-model", key: "model", label: "Model", role: "nowhere", ' +
          'toggle: true }],\n};\n',
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-unknown-key":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default {\n  rows: [{ id: "alpha-model", key: "nonsense", label: "Model", role: "alpha-role", ' +
          'toggle: true }],\n};\n',
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-unknown-when-key":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default {\n  rows: [{ id: "alpha-model", key: "model", label: "Model", role: "alpha-role", ' +
          'toggle: true, when: { key: "nonsense", in: ["a"] } }],\n};\n',
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-unknown-tracker":
      writeTree("packages/alpha/ts/ui/index.ts", 'export default { trackers: [{ id: "nowhere" }] };\n');
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-duplicate-tracker":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default { trackers: [{ id: "alpha-tracks" }, { id: "alpha-tracks" }] };\n',
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "ui-duplicate-service":
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default { services: [{ id: "alpha-watch" }, { id: "alpha-watch" }] };\n',
      );
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      generate({ root });
      try {
        result = { packages: await loadMenus() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views": {
      // Three contributors, so the reported order is the product order (zeta is listed first) rather than an
      // alphabetical sort. A view is data: its id, its placement and a body of one kind, which the generated module
      // names in VIEWS. zeta declares a views module and no ui one, which a package may: its ui member is deleted here,
      // so this mode runs a views-only package through contributes(), the menus module's filter and the views module.
      editManifest("zeta", (m) => {
        delete m.retroplug.ui;
        delete m.exports["./ui"];
      });
      viewsModule(
        "zeta",
        'export default { views: [{ id: "zeta-view", label: "Zeta View", placement: "tile", ' +
          'body: { kind: "readout", open: () => null } }] };\n',
      );
      viewsModule(
        "alpha",
        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile", ' +
          'body: { kind: "readout", open: () => null } }] };\n',
      );
      viewsModule(
        "beta",
        'export default { views: [{ id: "beta-view", label: "Beta View", placement: "window", ' +
          'body: { kind: "pixels", open: () => null } }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews(), menus: menusImports() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    }
    case "views-no-body":
      // A contribution whose view has no body at all: the data check backs the type with a message that names the
      // package, rather than a bare TypeError from reading `kind` off nothing.
      viewsModule(
        "alpha",
        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile" }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-no-open":
      // A session body with nothing to open: the shell calls `body.open`, so this is a view that could never draw. The
      // fixture is plain data (no typecheck), which is why the generated module's check is what catches it.
      viewsModule(
        "alpha",
        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile", ' +
          'body: { kind: "readout" } }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-no-component":
      // A component body with no component: the shell mounts what `component` names, so an empty one draws nothing and
      // reports nothing.
      viewsModule(
        "alpha",
        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile", ' +
          'body: { kind: "component" } }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-duplicate-id":
      viewsModule(
        "alpha",
        'export default { views: [\n' +
          '  { id: "same-view", label: "First", placement: "tile", body: { kind: "readout", open: () => null } },\n' +
          '  { id: "same-view", label: "Second", placement: "window", body: { kind: "pixels", open: () => null } },\n' +
          '] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-duplicate-across":
      for (const name of ["alpha", "beta"]) {
        viewsModule(
          name,
          `export default { views: [{ id: "same-view", label: "${name}", placement: "tile", ` +
            'body: { kind: "readout", open: () => null } }] };\n',
        );
      }
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-bad-placement":
      viewsModule(
        "alpha",
        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "float", ' +
          'body: { kind: "readout", open: () => null } }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-bad-kind":
      viewsModule(
        "alpha",
        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile", ' +
          'body: { kind: "hologram", open: () => null } }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-window-visible":
      viewsModule(
        "alpha",
        'const visible = { get: () => true, subscribe: () => () => {} };\n' +
          'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "window", visible, ' +
          'body: { kind: "readout", open: () => null } }] };\n',
      );
      generate({ root });
      try {
        result = { views: await loadViews() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "views-outside-ui":
      // A views module is UI code and lives in the package's ts/ui/ tree: a path beside it would put a React import in
      // a file no surface rule reads.
      writeTree("packages/alpha/ts/views.ts", "export default { views: [] };\n");
      editManifest("alpha", (m) => {
        m.retroplug.views = "ts/views.ts";
        m.exports["./views"] = "./ts/views.ts";
      });
      result = refusal();
      break;
    case "ui-outside-ui":
      // The same placement rule for the ui module, which every package names as ts/ui/index.ts.
      writeTree("packages/alpha/ts/ui.ts", "export default {};\n");
      editManifest("alpha", (m) => {
        m.retroplug.ui = "ts/ui.ts";
        m.exports["./ui"] = "./ts/ui.ts";
      });
      result = refusal();
      break;
    case "actions": {
      // The positive side: one action per package, two of them on the same backtick (which two CONTRIBUTIONS may
      // share), so the report is the product order and the defaults as declared.
      writeTree(
        "packages/zeta/ts/ui/index.ts",
        'export default { actions: [{ id: "zeta.first", label: "Zeta First", keys: ["`"], run: () => {} }] };\n',
      );
      writeTree(
        "packages/alpha/ts/ui/index.ts",
        'export default { actions: [{ id: "alpha.second", label: "Alpha Second", keys: ["`"], run: () => {} }] };\n',
      );
      writeTree(
        "packages/beta/ts/ui/index.ts",
        'export default { actions: [{ id: "beta.third", label: "Beta Third", pad: ["guide"], run: () => {} }] };\n',
      );
      for (const name of ["alpha", "beta"]) {
        editManifest(name, (m) => {
          m.retroplug.ui = "ts/ui/index.ts";
          m.exports["./ui"] = "./ts/ui/index.ts";
        });
      }
      generate({ root });
      try {
        result = { actions: await loadActions() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    }
    case "actions-duplicate-id":
      result = await actionRefusal(
        'export default { actions: [\n' +
          '  { id: "alpha.same", label: "First", keys: ["9"], run: () => {} },\n' +
          '  { id: "alpha.same", label: "Second", keys: ["0"], run: () => {} },\n' +
          '] };\n',
      );
      break;
    case "actions-duplicate-across": {
      // Both packages in one load: the check runs as the module loads, and a module already loaded is not checked
      // again.
      for (const name of ["alpha", "beta"]) {
        writeTree(
          `packages/${name}/ts/ui/index.ts`,
          `export default { actions: [{ id: "alpha.same", label: "${name}", run: () => {} }] };\n`,
        );
        editManifest(name, (m) => {
          m.retroplug.ui = "ts/ui/index.ts";
          m.exports["./ui"] = "./ts/ui/index.ts";
        });
      }
      generate({ root });
      try {
        await loadActions();
        result = { error: null };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    }
    case "actions-bad-prefix":
      result = await actionRefusal(
        'export default { actions: [{ id: "zeta.stolen", label: "Stolen", keys: ["9"], run: () => {} }] };\n',
      );
      break;
    case "actions-unknown-key":
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.key", label: "Key", keys: ["Nonsense"], run: () => {} }] };\n',
      );
      break;
    case "actions-uppercase-key":
      // A default the hosts can never deliver: DPF sends a letter unshifted ('f' for the F key), so a bare "F" would
      // show in the bindings menu and never fire.
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.key", label: "Key", keys: ["F"], run: () => {} }] };\n',
      );
      break;
    case "actions-unknown-pad":
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.pad", label: "Pad", pad: ["nonsense"], run: () => {} }] };\n',
      );
      break;
    case "actions-shell-key":
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.menu", label: "Menu", keys: ["Escape"], run: () => {} }] };\n',
      );
      break;
    case "actions-gb-key":
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.jump", label: "Jump", keys: ["Z"], run: () => {} }] };\n',
      );
      break;
    case "actions-return-key":
      // "Return" and the Game Boy Start default "Enter" are one DPF code (0x0d), so the collision is by code, not by
      // name: a check that compared names would let this through.
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.enter", label: "Enter", keys: ["Return"], run: () => {} }] };\n',
      );
      break;
    case "actions-gb-pad":
      result = await actionRefusal(
        'export default { actions: [{ id: "alpha.jump", label: "Jump", pad: ["a"], run: () => {} }] };\n',
      );
      break;
    case "cli": {
      // The positive side: beta contributes one tool and alpha another, and zeta a third; zeta is listed first, so the
      // reported order is the product order rather than an alphabetical sort of the three names.
      writeTree(
        "packages/zeta/ts/cli.ts",
        'export default [{ name: "zeta-tool", summary: "", help: "", run: () => {} }];\n',
      );
      editManifest("zeta", (m) => {
        m.retroplug.cli = "ts/cli.ts";
        m.exports["./cli"] = "./ts/cli.ts";
      });
      writeTree(
        "packages/alpha/ts/cli.ts",
        'export default [{ name: "alpha-tool", summary: "", help: "", run: () => {} }];\n',
      );
      writeTree(
        "packages/beta/ts/cli.ts",
        'export default [{ name: "beta-tool", summary: "", help: "", run: () => {} }];\n',
      );
      for (const name of ["alpha", "beta"]) {
        editManifest(name, (m) => {
          m.retroplug.cli = "ts/cli.ts";
          m.exports["./cli"] = "./ts/cli.ts";
        });
      }
      generate({ root });
      result = { tools: await loadCli() };
      break;
    }
    case "cli-duplicate-tool": {
      writeTree(
        "packages/alpha/ts/cli.ts",
        'export default [{ name: "same", summary: "", help: "", run: () => {} }];\n',
      );
      writeTree(
        "packages/beta/ts/cli.ts",
        'export default [{ name: "same", summary: "", help: "", run: () => {} }];\n',
      );
      for (const name of ["alpha", "beta"]) {
        editManifest(name, (m) => {
          m.retroplug.cli = "ts/cli.ts";
          m.exports["./cli"] = "./ts/cli.ts";
        });
      }
      generate({ root });
      try {
        result = { tools: await loadCli() };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    }
    case "cli-missing-module": {
      editManifest("alpha", (m) => {
        m.retroplug.cli = "ts/nothing.ts";
      });
      result = refusal();
      break;
    }
    case "examples-outside": {
      editManifest("alpha", (m) => {
        m.retroplug.examples = [{ name: "away", entry: "ts/away.ts" }];
      });
      result = refusal();
      break;
    }
    case "examples-tsconfig-missing":
    case "examples-tsconfig-unextended":
    case "examples-tsconfig-extended": {
      // A package with examples must have a tsconfig extending the generated host-module paths: the shell's program
      // compiles ../*/examples, and only that file tells `tsc` what `retroplug-cli` resolves to there.
      editManifest("alpha", (m) => {
        m.retroplug.examples = [{ name: "alpha-session", entry: "examples/alpha-session.ts" }];
      });
      mkdirSync(join(root, "packages/alpha/examples"), { recursive: true });
      writeTree("packages/alpha/examples/alpha-session.ts", "export {};\n");
      if (MODE !== "examples-tsconfig-missing") {
        // The unextended case NAMES the generated file in a comment: only the parsed `extends` admits it, so a check
        // that searched the text would pass a tsconfig which resolves nothing.
        const extendsLine =
          MODE === "examples-tsconfig-extended"
            ? '"extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"]'
            : '// extend tsconfig.host-modules.json (not really)\n  "extends": ["../../tsconfig.base.json"]';
        writeTree("packages/alpha/tsconfig.json", `{\n  ${extendsLine},\n  "include": ["ts", "examples"]\n}\n`);
      }
      result = MODE === "examples-tsconfig-extended" ? { error: refusal().error } : refusal();
      break;
    }
    case "examples-shell-name": {
      // The shell's own three example sessions (cli/sessions/render-rom.ts, render-song.ts, rom-test.ts) are bundled to
      // the same build/cli/<name>.js, so a package's example of one of those names would overwrite a shell session.
      editManifest("alpha", (m) => {
        m.retroplug.examples = [{ name: "render-rom", entry: "examples/render-rom.ts" }];
      });
      mkdirSync(join(root, "packages/alpha/examples"), { recursive: true });
      writeTree("packages/alpha/examples/render-rom.ts", "export {};\n");
      writeTree("packages/alpha/tsconfig.json",
        '{\n  "extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"],\n' +
        '  "include": ["ts", "examples"]\n}\n');
      result = refusal();
      break;
    }
    case "examples-unsafe-name": {
      // A name is a FILE STEM under build/cli/: `..` would escape it, and `;` would split the CMake list it lands in.
      editManifest("alpha", (m) => {
        m.retroplug.examples = [{ name: "../escape", entry: "examples/escape.ts" }];
      });
      mkdirSync(join(root, "packages/alpha/examples"), { recursive: true });
      writeTree("packages/alpha/examples/escape.ts", "export {};\n");
      writeTree("packages/alpha/tsconfig.json",
        '{\n  "extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"],\n' +
        '  "include": ["ts", "examples"]\n}\n');
      result = refusal();
      break;
    }
    case "examples-duplicate-name": {
      for (const name of ["alpha", "beta"]) {
        editManifest(name, (m) => {
          m.retroplug.examples = [{ name: "same-session", entry: "examples/same-session.ts" }];
        });
        mkdirSync(join(root, `packages/${name}/examples`), { recursive: true });
        writeTree(`packages/${name}/examples/same-session.ts`, "export {};\n");
        writeTree(`packages/${name}/tsconfig.json`,
          '{\n  "extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"],\n' +
          '  "include": ["ts", "examples"]\n}\n');
      }
      result = refusal();
      break;
    }
    case "host-unknown": {
      editManifest("alpha", (m) => {
        m.retroplug.host = ["not-a-host-module"];
      });
      result = refusal();
      break;
    }
    case "host-known": {
      editManifest("alpha", (m) => {
        m.retroplug.host = ["fixture-lib"];
      });
      generate({ root });
      result = { error: null };
      break;
    }
    case "react-tsconfig-missing":
    case "react-tsconfig-unextended":
    case "react-tsconfig-extended": {
      // A package that declares a host module the UI owns resolves React through the generated host-module paths, so
      // its tsconfig must extend them even without an example session.
      editManifest("alpha", (m) => {
        m.retroplug.host = ["fixture-react"];
      });
      if (MODE !== "react-tsconfig-missing") {
        const extendsLine =
          MODE === "react-tsconfig-extended"
            ? '"extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"]'
            : '// extend tsconfig.host-modules.json (not really)\n  "extends": ["../../tsconfig.base.json"]';
        writeTree("packages/alpha/tsconfig.json", `{\n  ${extendsLine},\n  "include": ["ts"]\n}\n`);
      }
      result = MODE === "react-tsconfig-extended" ? { error: refusal().error } : refusal();
      break;
    }
    case "ui-missing-module": {
      editManifest("alpha", (m) => {
        m.retroplug.ui = "ts/nothing.ts";
      });
      result = refusal();
      break;
    }
    case "shell-unlisted": {
      // The DESKTOP list is the union, so a package the shell declares and that contributes must be in it or the
      // shell's generated modules name a package nothing registered. A product list that is not the desktop's may
      // drop one deliberately (the `product` mode's "solo" drops two), so the rule is the desktop's alone.
      editList("alpha", "beta");
      result = refusal();
      break;
    }
    case "order":
      // beta depends on alpha, and the list puts beta first: the order is the list's, so this is refused rather than
      // re-sorted.
      editList("beta", "alpha");
      result = refusal();
      break;
    case "cycle":
      // beta already depends on alpha; alpha depending on beta closes the loop, which the order rule sees as alpha
      // preceding a dependency.
      editManifest("alpha", (json) => {
        json.dependencies = { "@retroplug/beta": "workspace:*" };
      });
      result = refusal();
      break;
    case "unlisted-contributes":
      // gamma is not the product's and would contribute a list, so carrying beta without it would silently drop it.
      editManifest("beta", (json) => {
        json.dependencies = { ...json.dependencies, "@retroplug/gamma": "workspace:*" };
      });
      result = refusal();
      break;
    case "unlisted-plain":
      // delta contributes nothing (a native library to link) and plain has no retroplug member at all (a pure TS
      // library), so neither is a product's business: both are left out of the ordering rather than refused. The
      // rewritten manifest keeps beta's kind declaration, so alpha's contribution still has its declaring package.
      writeTree(
        "packages/beta/package.json",
        `${JSON.stringify(
          {
            name: "@retroplug/beta",
            private: true,
            exports: { "./app": "./ts/app.ts", "./kind": "./ts/kind.ts", "./apps": "./ts/apps.ts" },
            dependencies: {
              "@retroplug/alpha": "workspace:*",
              "@retroplug/delta": "workspace:*",
              "@retroplug/plain": "workspace:*",
            },
            retroplug: {
              app: "ts/app.ts",
              kinds: {
                controllerApps: {
                  control: "ts/kind.ts",
                  runtime: { module: "ts/apps.ts", register: "registerControllerApps", type: "ControllerAppRuntime" },
                  data: ["syncOverride"],
                },
              },
            },
          },
          null,
          2,
        )}\n`,
      );
      {
        const ran = generate({ root });
        result = { files: outputs(), packages: ran.packages };
      }
      break;
    case "depends-on-shell":
      // The shell is the app itself: nothing may depend on it, whatever it contributes.
      editManifest("beta", (json) => {
        json.dependencies = { ...json.dependencies, "@retroplug/retroplug": "workspace:*" };
      });
      result = refusal();
      break;
    case "unknown-key":
      // A misspelt wiring knob (here `launchs` for `launches`) would drop out of PROTOCOL_NODES without a word.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].launchs = true;
      });
      result = refusal();
      break;
    case "capture-bad":
      // A capture value the planner's type does not have: the mode it is for would capture nothing, silently.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = { midiMap: "sideways" };
      });
      result = refusal();
      break;
    case "half-wired":
      // A node with midiIn and no writes is placed into PROTOCOL_NODES with half its wiring.
      editManifest("alpha", (json) => {
        delete json.retroplug.protocol.nodes[0].writes;
      });
      result = refusal();
      break;
    case "extra-node":
      // No manifest declares a node, and a module exports one: the entry must name it rather than complain about a
      // node defined twice.
      writeTree(
        "packages/retroplug/src/script/nodes.ts",
        "export default [{ id: \"rp.fixture.shell\" }, { id: \"rp.fixture.extra\" }];\n",
      );
      generate({ root });
      try {
        result = await loadEntry();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate":
      // The shell's only node takes the label alpha's node has.
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.nodes[0].label = 16;
      });
      result = refusal();
      break;
    case "duplicate-type":
      // Two packages declare one node type.
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.nodes[0].type = "rp.alpha.midi";
      });
      result = refusal();
      break;
    case "duplicate-role":
      // Two packages declare one role kind (through a type both declare, so only the kind is wrong).
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.roles = { "alpha-midi": "rp.fixture.shell" };
      });
      result = refusal();
      break;
    case "gap":
      // 17 is the shell's, so moving it to 18 leaves 17 unclaimed.
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.nodes[0].label = 18;
      });
      result = refusal();
      break;
    case "low-label":
      // The first protocol label is 16 (spec/08-profiling.md): 15 is the engine's own.
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.nodes[0].label = 15;
      });
      result = refusal();
      break;
    case "role-undeclared":
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.roles = { "shell-role": "rp.nope" };
      });
      result = refusal();
      break;
    case "role-unwired":
      // A node with no midiIn and no writes is not in PROTOCOL_NODES, so a role cannot be placed as it.
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.roles = { "shell-role": "rp.fixture.shell" };
      });
      result = refusal();
      break;
    case "missing-module":
      editManifest("alpha", (json) => {
        json.retroplug.protocol.module = "ts/nope.ts";
      });
      result = refusal();
      break;
    case "unexported-app":
      // The manifest names an app module its exports do not publish: a generated import of it would resolve to nothing
      // (or, worse, to another file if the key pointed elsewhere).
      editManifest("alpha", (json) => {
        delete json.exports["./app"];
      });
      result = refusal();
      break;
    case "name-mismatch":
      // A package whose manifest names another package: the generator would import that name, and the checker would
      // judge the directory's.
      editManifest("alpha", (json) => {
        json.name = "@retroplug/alfa";
      });
      result = refusal();
      break;
    case "outside-ts":
      // A contribution beside ts/, which the package's exports may not publish (the boundary check refuses it): the
      // generated import cannot name it.
      writeTree("packages/alpha/app.ts", "export default { roles: [] };\n");
      editManifest("alpha", (json) => {
        json.retroplug.app = "app.ts";
      });
      result = refusal();
      break;
    case "shell-undeclared":
      // A contributing package the shell does not declare: pnpm would not link it, and the generated module's import
      // would not resolve.
      editManifest("retroplug", (json) => {
        delete json.dependencies["@retroplug/alpha"];
      });
      result = refusal();
      break;
    case "duplicate-role-kind":
      // Two apps register one role kind: the registry holds roles by kind, so the second would replace the first
      // without a word.
      writeTree(
        "packages/alpha/ts/app.ts",
        appWith({ kind: "shared-role", provider: "alpha", markerRole: "alpha", kitId: "alpha" }),
      );
      writeTree(
        "packages/beta/ts/app.ts",
        appWith({ kind: "shared-role", provider: "beta", markerRole: "beta", kitId: "beta" }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-kit-format":
      // Two formats with one id: the audio driver dispatches by id, so one would be unreachable.
      writeTree(
        "packages/alpha/ts/app.ts",
        appWith({ kind: "alpha-role", provider: "alpha", markerRole: "alpha", kitId: "shared-kit" }),
      );
      writeTree(
        "packages/beta/ts/app.ts",
        appWith({ kind: "beta-role", provider: "beta", markerRole: "beta", kitId: "shared-kit" }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-marker-role":
      // Two integrations with one marker role: resolveTracker takes the first whose marker a system has, and the
      // markers are exclusive by design, so the second could never be reached.
      writeTree(
        "packages/alpha/ts/app.ts",
        appWith({ kind: "alpha-role", provider: "alpha", markerRole: "shared-marker", kitId: "alpha" }),
      );
      writeTree(
        "packages/beta/ts/app.ts",
        appWith({ kind: "beta-role", provider: "beta", markerRole: "shared-marker", kitId: "beta" }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-section":
      // Two packages own one block of a .rplg's settings: the shell writes a section back under its key, so one
      // package's bytes would replace the other's in every project.
      writeTree("packages/alpha/ts/app.ts", sectionApp("shared", "alpha"));
      writeTree("packages/beta/ts/app.ts", sectionApp("shared", "beta"));
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-service":
      // Two services under one key: the key is a model's and a section's, so they would fight over the graph's
      // placement.
      writeTree("packages/alpha/ts/app.ts", sectionApp("alpha", "shared"));
      writeTree("packages/beta/ts/app.ts", sectionApp("beta", "shared"));
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "shell-section":
      // A section under a key the shell owns: `layout` is the shell's own setting, so its schema would be read as the
      // section's and its bytes written back as one.
      writeTree("packages/alpha/ts/app.ts", sectionApp("layout"));
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "service-no-section":
      // A service's key is its section's: one naming no section would read undefined where the store asserts, so the
      // generated registry refuses it before the store ever builds an instance.
      writeTree("packages/alpha/ts/app.ts", sectionApp("alpha", "nowhere"));
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "service-shell-key":
      // A service under one of the shell's own settings would read the shell's field as its section.
      writeTree("packages/alpha/ts/app.ts", sectionApp("alpha", "layout"));
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "placer-no-section":
      // A placer's key is the section whose model it places, so one naming no section would place a model no reader
      // can produce.
      writeTree("packages/alpha/ts/placer.ts", 'export default { key: "nowhere", place: () => ({}) };\n');
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "placer-shell-key":
      // A placer under one of the shell's own settings would be handed the shell's field as a model.
      writeTree("packages/alpha/ts/placer.ts", 'export default { key: "layout", place: () => ({}) };\n');
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-placer":
      // Two packages placing one graph model: buildGraph would run both placers' slots, so the two node sets would land
      // in one document and collide.
      editManifest("beta", (json) => {
        json.exports["./placer"] = "./ts/placer.ts";
        json.retroplug.protocol = { placer: "ts/placer.ts" };
      });
      writeTree("packages/beta/ts/placer.ts", 'export default { key: "alpha", place: () => ({}) };\n');
      generate({ root });
      try {
        result = await loadPlacers();
        result = { placers: result.PLACERS.map((entry) => `${entry.pkg}:${entry.placer.key}`) };
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "placer-missing":
      // A placer module named and absent: the generated import would fail at the bundle, so the manifest is refused.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.placer = "ts/nowhere.ts";
      });
      result = refusal();
      break;
    case "duplicate-app":
      // Two packages declare one controller app id: a project stores an id and the script bundle looks one up, so one
      // would silently replace the other.
      editManifest("beta", (json) => {
        json.exports["./appRuntime"] = "./ts/appRuntime.ts";
        json.retroplug.protocol = { kinds: { controllerApps: [{ id: "alpha-app", module: "ts/appRuntime.ts" }] } };
      });
      writeTree("packages/beta/ts/appRuntime.ts", "export default { id: \"alpha-app\" };\n");
      result = refusal();
      break;
    case "apps-only":
      // A package contributing an app and NO node names no nodes module: the generated entry still registers the app.
      editManifest("alpha", (json) => {
        const apps = json.retroplug.protocol.kinds.controllerApps;
        delete apps[0].syncOverride;
        json.retroplug.protocol = { kinds: { controllerApps: apps } };
      });
      editManifest("retroplug", (json) => {
        delete json.retroplug.protocol;
      });
      result = { files: null, packages: null };
      {
        const ran = generate({ root });
        result = { files: outputs(), packages: ran.packages };
      }
      break;
    case "app-module-empty":
      // A module that default-exports no nodes has nothing to export: the manifest should not name it.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes = [];
      });
      result = refusal();
      break;
    case "duplicate-app-one-package":
      // ONE app module contributes the same id twice: the generated registry names the package once rather than
      // "alpha and alpha".
      writeTree(
        "packages/alpha/ts/app.ts",
        "export default { controllerApps: [\n" +
          "  { id: \"same-app\", label: \"First\", config: { parse: (v) => v } },\n" +
          "  { id: \"same-app\", label: \"Second\", config: { parse: (v) => v } },\n" +
          "] };\n",
      );
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-app-control":
      // Two app modules contribute one control-plane id: the generated registry refuses it, naming both packages.
      writeTree(
        "packages/alpha/ts/app.ts",
        "export default { controllerApps: [{ id: \"same-app\", label: \"Alpha\", config: { parse: (v) => v } }] };\n",
      );
      writeTree(
        "packages/beta/ts/app.ts",
        "export default { controllerApps: [{ id: \"same-app\", label: \"Beta\", config: { parse: (v) => v } }] };\n",
      );
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "identifier-collision":
      // Package "a-b" with app "c" and package "a" with app "b-c" mangle to the same generated identifier: tsc would
      // refuse the duplicate import and say nothing about which manifests collided.
      renameSync(join(root, "packages/alpha"), join(root, "packages/a-b"));
      renameSync(join(root, "packages/beta"), join(root, "packages/a"));
      editList("a-b", "a");
      editManifest("retroplug", (json) => {
        // The renamed pair alone: zeta leaves the product list here, so the shell must not depend on it any more.
        json.dependencies = { "@retroplug/a-b": "workspace:*", "@retroplug/a": "workspace:*" };
      });
      editManifest("a-b", (json) => {
        json.name = "@retroplug/a-b";
        json.retroplug.protocol.kinds.controllerApps = [{ id: "c", module: "ts/appRuntime.ts" }];
      });
      writeTree(
        "packages/a-b/ts/app.ts",
        "export default { controllerApps: [{ id: \"c\", label: \"C\", config: { parse: (v) => v } }] };\n",
      );
      writeTree("packages/a-b/ts/appRuntime.ts", "export default { id: \"c\" };\n");
      editManifest("a", (json) => {
        json.name = "@retroplug/a";
        json.dependencies = { "@retroplug/core": "workspace:*" };
        json.exports["./appRuntime"] = "./ts/appRuntime.ts";
        json.retroplug.protocol = { kinds: { controllerApps: [{ id: "b-c", module: "ts/appRuntime.ts" }] } };
      });
      writeTree(
        "packages/a/ts/app.ts",
        "export default { controllerApps: [{ id: \"b-c\", label: \"BC\", config: { parse: (v) => v } }] };\n",
      );
      writeTree("packages/a/ts/appRuntime.ts", "export default { id: \"b-c\" };\n");
      result = refusal();
      break;
    case "kind-app-member":
      // A kind named one of the app's own members would read that member's list as its items: the registry cannot tell
      // "the roles" from "the items of a kind called roles".
      editManifest("alpha", (json) => {
        json.retroplug.kinds = {
          roles: { control: "ts/widgetKind.ts", runtime: { module: "ts/widgets.ts", register: "registerWidgets",
            type: "WidgetRuntime" }, data: [] },
        };
      });
      result = refusal();
      break;
    case "kind-duplicate-item":
      // ONE app module lists one widget id twice: the declaring package's kind names the package once, not "alpha and
      // alpha", because a project stores only the id and the second item would be unreachable.
      writeTree(
        "packages/alpha/ts/app.ts",
        "export default { widgets: [{ id: \"w\", label: \"First\" }, { id: \"w\", label: \"Second\" }] };\n",
      );
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "kind-one-half":
      // A contributing manifest declares a widget's runtime and no app module lists the item: a document naming it
      // would run nothing, and a menu would offer nothing.
      editManifest("beta", (json) => {
        json.retroplug.protocol = {
          kinds: { widgets: [{ id: "w", module: "ts/widgetRuntime.ts", mode: "rp.alpha.midi" }] },
        };
      });
      writeTree("packages/beta/ts/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "kind-check-unknown-node":
      // The declaring package's own check over the data: a mode naming a node type no package declares would place
      // nothing, and no generic validation can know what a kind's data means.
      editManifest("beta", (json) => {
        json.retroplug.protocol = {
          kinds: { widgets: [{ id: "w", module: "ts/widgetRuntime.ts", mode: "rp.nowhere" }] },
        };
      });
      writeTree("packages/alpha/ts/app.ts", "export default { widgets: [{ id: \"w\", label: \"W\" }] };\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "app-unknown-key":
      // A misspelt knob in an app entry: nothing reads it, and a runtime would be built without what it meant.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.kinds.controllerApps[0].factorY = "ts/appRuntime.ts";
      });
      result = refusal();
      break;
    case "app-unpaired-runtime":
      // A manifest declares a runtime no app module's control-plane half contributes: the generated registry refuses it
      // rather than leaving a document to name an app no menu offers.
      writeTree("packages/alpha/ts/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "app-unpaired-control":
      // An app module contributes a control-plane half no manifest declares a runtime for: the id would reach a menu
      // and run nothing.
      writeTree(
        "packages/alpha/ts/app.ts",
        "export default { controllerApps: [\n" +
          "  { id: \"alpha-app\", label: \"Alpha App\", config: { parse: (v) => v } },\n" +
          "  { id: \"orphan-app\", label: \"Orphan\", config: { parse: (v) => v } },\n" +
          "] };\n",
      );
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "app-runtime-mismatch":
      // The runtime module registers an id the manifest does not declare: the script bundle refuses the set, as it does
      // the nodes' types.
      writeTree(
        "packages/alpha/ts/appRuntime.ts",
        "export default { id: \"not-alpha-app\", factory: () => () => {}, playbackModel: () => null, " +
          "target: () => null };\n",
      );
      generate({ root });
      try {
        result = await loadEntry();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "consoles":
      // A product whose packages register consoles and a real cart: the lists are the generated ones', in product
      // order. The cart node is a type of its own: a real cart is never the system node it drives.
      writeTree("packages/alpha/ts/app.ts", consoleApp({ role: "alpha-core", keepsApp: true }));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
          realCarts: [{ platform: "nes", cartNode: "rp.alpha.cart" }],
        }),
      );
      writeTree("packages/beta/ts/app.ts", consoleApp({ role: "beta-core" }));
      writeTree(
        "packages/beta/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "beta.gb", core: "beta-core", nodeType: "rp.beta.gb", platform: "gb" }],
        }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      result = await loadPackages();
      break;
    case "duplicate-console-platform":
      // Two descriptors declaring one platform id: the classifier and a project's validation look one up, so one of the
      // two consoles would be unreachable.
      writeTree("packages/alpha/ts/app.ts", consoleApp({ role: "alpha-core", keepsApp: true }));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
        }),
      );
      writeTree("packages/beta/ts/app.ts", consoleApp({ role: "beta-core" }));
      writeTree(
        "packages/beta/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "beta.nes", core: "beta-core", nodeType: "rp.beta.nes", platform: "nes" }],
        }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "console-core-norole":
      // A console whose core names no system-category role: the core-config role is named after its core, so its
      // systems could be classified and never configured.
      writeTree(
        "packages/alpha/ts/app.ts",
        "export default {\n" +
          '  roles: [{ kind: "alpha-core" }],\n' +
          '  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v) => v ?? {} } }],\n' +
          "};\n",
      );
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
        }),
      );
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "duplicate-console-type":
      // Two descriptors declaring one node type: a system is placed by the node type, so the node table would depend on
      // the order.
      writeTree("packages/alpha/ts/app.ts", consoleApp({ role: "alpha-core", keepsApp: true }));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
        }),
      );
      writeTree("packages/beta/ts/app.ts", consoleApp({ role: "beta-core" }));
      writeTree(
        "packages/beta/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "beta.gb", core: "beta-core", nodeType: "rp.alpha.nes", platform: "gb" }],
        }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "realcart-unregistered":
      // A real cart naming a platform no descriptor registers: nothing would ever receive what it sends.
      writeTree("packages/alpha/ts/app.ts", consoleApp({ role: "alpha-core", keepsApp: true }));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
          realCarts: [{ platform: "n64", cartNode: "rp.alpha.cart" }],
        }),
      );
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "realcart-platform-twice":
      // Two packs each register a cart for one platform: a project has one cart and one link, so which package's cart a
      // NES drove would be whichever the product listed last.
      writeTree("packages/alpha/ts/app.ts", consoleApp({ role: "alpha-core", keepsApp: true }));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
          realCarts: [{ platform: "nes", cartNode: "rp.alpha.cart" }],
        }),
      );
      writeTree("packages/beta/ts/app.ts", consoleApp({ role: "beta-core" }));
      writeTree(
        "packages/beta/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "beta.gba", core: "beta-core", nodeType: "rp.beta.gba", platform: "gba" }],
          realCarts: [{ platform: "nes", cartNode: "rp.beta.cart" }],
        }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "realcart-node-twice":
      // Two packs naming one cart node: the node a document places for a cart would be claimed twice, and the second
      // platform's systems would drive the first's link.
      writeTree("packages/alpha/ts/app.ts", consoleApp({ role: "alpha-core", keepsApp: true }));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
          realCarts: [{ platform: "nes", cartNode: "rp.alpha.cart" }],
        }),
      );
      writeTree("packages/beta/ts/app.ts", consoleApp({ role: "beta-core" }));
      writeTree(
        "packages/beta/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "beta.gba", core: "beta-core", nodeType: "rp.beta.gba", platform: "gba" }],
          realCarts: [{ platform: "gba", cartNode: "rp.alpha.cart" }],
        }),
      );
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "capture-unwired":
      // A capture on a node that is not wired: PROTOCOL_NODES drops the node whole, so the core would be clocked for
      // bytes no decoder sees.
      editManifest("alpha", (json) => {
        const node = json.retroplug.protocol.nodes[0];
        delete node.midiIn;
        delete node.writes;
        node.capture = { midiMap: "drive" };
        node.out = "rp.alpha.serial";
        json.retroplug.protocol.nodes.push({
          type: "rp.alpha.serial", label: 18, midiIn: null, writes: [], serialOut: true,
        });
      });
      writeTree("packages/alpha/ts/nodes.ts", 'export default [{ id: "rp.alpha.midi" }, { id: "rp.alpha.serial" }];\n');
      result = refusal();
      break;
    case "capture-no-decoder":
      // A wired capture with no `out`: nothing declares the serial-out node that decodes what the core clocks out.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = { midiMap: "drive" };
      });
      result = refusal();
      break;
    case "capture-not-object":
      // A capture that is not an object of mode: capture is data no rule can read.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = "drive";
      });
      result = refusal();
      break;
    case "capture-out-undeclared":
      // A decoder type no package declares: the bytes would have nowhere to go.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = { midiMap: "drive" };
        json.retroplug.protocol.nodes[0].out = "rp.alpha.nope";
      });
      result = refusal();
      break;
    case "capture-out-not-serial":
      // A decoder that declares no serialOut: the node placed after the system would take no bytes.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = { midiMap: "drive" };
        json.retroplug.protocol.nodes[0].out = "rp.alpha.plainer";
        json.retroplug.protocol.nodes.push({ type: "rp.alpha.plainer", label: 18 });
      });
      writeTree(
        "packages/alpha/ts/nodes.ts",
        'export default [{ id: "rp.alpha.midi" }, { id: "rp.alpha.plainer" }];\n',
      );
      result = refusal();
      break;
    case "capture-form":
      // The emitted table's spelling of a capture table: spaced like the fields around it.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = { midiMap: "drive", off: "listen" };
        json.retroplug.protocol.nodes[0].out = "rp.alpha.serial";
        json.retroplug.protocol.nodes.push({
          type: "rp.alpha.serial", label: 18, midiIn: null, writes: [], serialOut: true,
        });
      });
      writeTree("packages/alpha/ts/nodes.ts", 'export default [{ id: "rp.alpha.midi" }, { id: "rp.alpha.serial" }];\n');
      generate({ root });
      result = { files: outputs() };
      break;
    case "embed-form":
      // The declaration and the manifest entry of one embedded ROM, and what the shell reads of it: the load takes its
      // platform from the declaration, so it comes through with the label and the console.
      writeTree(
        "packages/alpha/ts/app.ts",
        embedApp("alpha", [{ id: "alpha-rom", label: "Alpha ROM", platform: "nes" }]),
      );
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
        }),
      );
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ id: "alpha-rom", file: "rom/alpha.bin" }];
      });
      writeTree("packages/retroplug/src/app.ts", "export default {};\n");
      generate({ root });
      result = await loadPackages();
      break;
    case "embed-unpaired-manifest":
      // The bytes baked in and nothing declaring them: the shell would have no label and no console to load by.
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ id: "alpha-rom", file: "rom/alpha.bin" }];
      });
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "embed-unpaired-declaration":
      // A declaration whose bytes no manifest bakes in: the load would name a resource native has not got.
      writeTree("packages/alpha/ts/app.ts", embedApp("alpha", [{ id: "alpha-rom", platform: "nes" }]));
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "embed-unregistered-platform":
      // A console this product does not run: the load would have no core to build.
      writeTree("packages/alpha/ts/app.ts", embedApp("alpha", [{ id: "alpha-rom", platform: "n64" }]));
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ id: "alpha-rom", file: "rom/alpha.bin" }];
      });
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "embed-duplicate":
      // One id, two packages: a project stores the id alone, so one of the two ROMs would be unreachable.
      writeTree("packages/alpha/ts/app.ts", embedApp("alpha", [{ id: "shared-rom", platform: "nes" }]));
      writeTree("packages/beta/ts/app.ts", embedApp("beta", [{ id: "shared-rom", platform: "nes" }]));
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ id: "shared-rom", file: "rom/alpha.bin" }];
      });
      editManifest("beta", (json) => {
        json.retroplug.embed = [{ id: "shared-rom", file: "rom/beta.bin" }];
      });
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "embed-declared-twice":
      // One package declaring one id twice: the menu would offer two rows for one ROM and the store would take the
      // first, so the generated module counts declarations, not declaring packages.
      writeTree(
        "packages/alpha/ts/app.ts",
        embedApp("alpha", [
          { id: "alpha-rom", platform: "nes" },
          { id: "alpha-rom", platform: "nes" },
        ]),
      );
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({
          descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }],
        }),
      );
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ id: "alpha-rom", file: "rom/alpha.bin" }];
      });
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    case "embed-manifest-not-list":
      // The manifest half is a list of resources; anything else names no bytes to bake in.
      editManifest("alpha", (json) => {
        json.retroplug.embed = { id: "alpha-rom", file: "rom/alpha.bin" };
      });
      result = refusal();
      break;
    case "embed-manifest-no-id":
      // A resource native's table cannot name: it is looked up by the id an app module declares.
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ file: "rom/alpha.bin" }];
      });
      result = refusal();
      break;
    case "embed-manifest-no-file":
      // An id with nothing baked in under it.
      editManifest("alpha", (json) => {
        json.retroplug.embed = [{ id: "alpha-rom" }];
      });
      result = refusal();
      break;
    case "embed-manifest-id-twice":
      // Two files under one id: which resource native bakes in would be whichever the table listed last.
      editManifest("alpha", (json) => {
        json.retroplug.embed = [
          { id: "alpha-rom", file: "rom/alpha.bin" },
          { id: "alpha-rom", file: "rom/other.bin" },
        ];
      });
      result = refusal();
      break;
    case "embed-mismatched-package":
      // Declared by one package and baked in by another: the two halves of an embedded ROM are one package's.
      writeTree("packages/alpha/ts/app.ts", embedApp("alpha", [{ id: "beta-rom", platform: "nes" }]));
      editManifest("beta", (json) => {
        json.retroplug.embed = [{ id: "beta-rom", file: "rom/beta.bin" }];
      });
      generate({ root });
      try {
        result = await loadPackages();
      } catch (e) {
        result = { error: e.message };
      }
      break;
    default:
      console.error(`run-generator.mjs: unknown mode "${MODE}"`);
      process.exit(2);
  }
} finally {
  rmSync(work, { recursive: true, force: true });
}

console.log(JSON.stringify(result));
