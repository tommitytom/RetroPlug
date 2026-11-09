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
//   generate        { files: { "packages.ts": <text>, "consoles.ts": <text>, "protocol.ts": <text>,
//                              "scriptNodes.ts": <text> }, packages: [...] }
//   consoles        { consoles: [{ id, core, nodeType, platforms }], realCarts: [{ platform, cartNode }] }
//   twice           { mtimes: [<after one run>, <after a second>], changed: [<first run>, <second run>] }
//   run             { registered: [<the ids the generated entry registers, in order>] }
//   missing-node    { error: <the generated entry's own refusal> }
//   order | cycle | unlisted-contributes | unlisted-plain | depends-on-shell | duplicate | duplicate-type |
//   duplicate-role | gap | low-label | role-undeclared | role-unwired | missing-module | unexported-app | name-mismatch |
//   outside-ts | shell-undeclared | unknown-key | capture-bad | half-wired | extra-node | duplicate-role-kind |
//   duplicate-kit-format | duplicate-marker-role | duplicate-app | duplicate-app-control | app-unknown-key |
//   app-unpaired-runtime | app-unpaired-control | app-runtime-mismatch | app-module-empty | sync-unknown-key |
//   sync-unused-off | sync-node-undeclared | sync-not-object | sync-no-node | sync-no-mode | sync-off-is-mode |
//   duplicate-app-one-package | identifier-collision | apps-only | capture-unwired | capture-no-decoder |
//   capture-not-object | capture-out-undeclared | capture-out-not-serial | capture-form | embed-form |
//   embed-unpaired-manifest | embed-unpaired-declaration | embed-unregistered-platform | embed-duplicate |
//   embed-declared-twice | embed-manifest-not-list | embed-manifest-no-id | embed-manifest-no-file |
//   embed-manifest-id-twice | embed-mismatched-package | duplicate-console-platform |
//   console-core-norole | duplicate-console-type | realcart-unregistered | realcart-platform-twice |
//   realcart-node-twice | ui-missing-module
//                   { error: <the generator's message, or null when it did not refuse> }
//   ui | ui-duplicate-row | ui-unknown-placement | ui-role-undeclared | ui-unknown-key | ui-unknown-when-key |
//   ui-unknown-tracker | ui-duplicate-tracker | ui-duplicate-service
//   cli | cli-duplicate-tool | cli-missing-module | examples-outside | examples-tsconfig-missing |
//   examples-tsconfig-unextended | examples-tsconfig-extended | examples-duplicate-name | host-unknown | host-known
//                   the menus module: ui reports the contributing packages in product order, the others load it into
//                   its own refusal ({ error })
//                   (unlisted-plain generates, and reports files like generate)
//
// The run, missing-node and extra-node modes bundle the generated entry with the same esbuild options the build uses
// and load it, so the entry's own checks are what is exercised, not a copy of them.
import { cpSync, mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, statSync, utimesSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { generate } from "../../scripts/lib/packages.mjs";

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

function outputs() {
  return {
    "packages.ts": readFileSync(join(generated, "packages.ts"), "utf8"),
    "consoles.ts": readFileSync(join(generated, "consoles.ts"), "utf8"),
    "protocol.ts": readFileSync(join(generated, "protocol.ts"), "utf8"),
    "scriptNodes.ts": readFileSync(join(generated, "scriptNodes.ts"), "utf8"),
    "menus.ts": readFileSync(join(root, "packages/retroplug/ui/generated/menus.ts"), "utf8"),
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
 *  its manifest's protocol.apps pair with (the generated module refuses one half alone). */
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

/** The generated menus module, bundled and loaded as the UI loads it: its imports of the packages' ui modules are
 *  aliased the way a pnpm link resolves them, and loading it runs its checks (a row's role kind, a row id twice, a
 *  builder's placement). */
async function loadMenus() {
  const out = join(work, "menus.mjs");
  const { buildSync } = require("esbuild");
  buildSync({
    ...buildOptions(join(root, "packages/retroplug/ui/generated/menus.ts"), out, "esm"),
    alias: {
      "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
      "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
      "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
      "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
      "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui.ts"),
      "@retroplug/beta/ui": join(root, "packages/beta/ts/ui.ts"),
      // The ui package itself is a workspace dependency of the shell, not a fixture package: a real build resolves it
      // through the pnpm link, so the driver hands the bundle the tree's own copy.
      "@retroplug/ui/contribution": join(HERE, "../../../ui/ts/contribution.ts"),
    },
  });
  const loaded = await import(pathToFileURL(out).href);
  return loaded.UI_CONTRIBUTIONS.map((entry) => entry.pkg);
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
      "@retroplug/launchpad/apps": join(root, "packages/launchpad/ts/apps.ts"),
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
    `{ id: ${JSON.stringify(e.id)}, label: ${JSON.stringify(e.label ?? e.id)}, platform: ${JSON.stringify(e.platform)} }`;
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
  writeFileSync(join(root, relative), text);
}

let result;
try {
  switch (MODE) {
    case "generate": {
      const ran = generate({ root });
      result = { files: outputs(), packages: ran.packages };
      break;
    }
    case "twice": {
      const first = generate({ root });
      const files = [
        ...["packages.ts", "consoles.ts", "protocol.ts", "scriptNodes.ts"].map((name) => join(generated, name)),
        join(root, "packages/retroplug/ui/generated/menus.ts"),
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
      // so the menus module loads and its contributions are reported in product order (two contributors, so the order
      // is what is reported rather than a list of one).
      writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
      writeTree("packages/beta/ts/ui.ts", 'export default { menus: [{ placement: "start", build: () => [] }] };\n');
      editManifest("beta", (m) => {
        m.retroplug.ui = "ts/ui.ts";
        m.exports["./ui"] = "./ts/ui.ts";
      });
      generate({ root });
      result = { packages: await loadMenus() };
      break;
    case "ui-duplicate-row":
      writeTree(
        "packages/alpha/ts/ui.ts",
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
        "packages/alpha/ts/ui.ts",
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
        "packages/alpha/ts/ui.ts",
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
        "packages/alpha/ts/ui.ts",
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
        "packages/alpha/ts/ui.ts",
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
      writeTree("packages/alpha/ts/ui.ts", 'export default { trackers: [{ id: "nowhere" }] };\n');
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
        "packages/alpha/ts/ui.ts",
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
        "packages/alpha/ts/ui.ts",
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
    case "cli": {
      // The positive side: beta contributes one tool and alpha another, so the reported order is the product order.
      writeTree("packages/alpha/ts/cli.ts", 'export default [{ name: "alpha-tool", summary: "", help: "", run: () => {} }];\n');
      writeTree("packages/beta/ts/cli.ts", 'export default [{ name: "beta-tool", summary: "", help: "", run: () => {} }];\n');
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
      writeTree("packages/alpha/ts/cli.ts", 'export default [{ name: "same", summary: "", help: "", run: () => {} }];\n');
      writeTree("packages/beta/ts/cli.ts", 'export default [{ name: "same", summary: "", help: "", run: () => {} }];\n');
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
    case "ui-missing-module": {
      editManifest("alpha", (m) => {
        m.retroplug.ui = "ts/nothing.ts";
      });
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
      // library), so neither is a product's business: both are left out of the ordering rather than refused.
      writeTree(
        "packages/beta/package.json",
        `${JSON.stringify(
          {
            name: "@retroplug/beta",
            private: true,
            exports: { "./app": "./ts/app.ts" },
            dependencies: {
              "@retroplug/alpha": "workspace:*",
              "@retroplug/delta": "workspace:*",
              "@retroplug/plain": "workspace:*",
            },
            retroplug: { app: "ts/app.ts" },
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
    case "duplicate-app":
      // Two packages declare one controller app id: a project stores an id and the script bundle looks one up, so one
      // would silently replace the other.
      editManifest("beta", (json) => {
        json.exports["./appRuntime"] = "./ts/appRuntime.ts";
        json.retroplug.protocol = { apps: [{ id: "alpha-app", module: "ts/appRuntime.ts" }] };
      });
      writeTree("packages/beta/ts/appRuntime.ts", "export default { id: \"alpha-app\" };\n");
      result = refusal();
      break;
    case "apps-only":
      // A package contributing an app and NO node names no nodes module: the generated entry still registers the app.
      editManifest("alpha", (json) => {
        const apps = json.retroplug.protocol.apps;
        delete apps[0].syncOverride;
        json.retroplug.protocol = { apps };
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
    case "sync-unknown-key":
      // A misspelt field of a sync override is data nothing reads: the mode it meant would silently not apply.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.apps[0].syncOverride.listing = "MidiMap";
      });
      result = refusal();
      break;
    case "sync-unused-off":
      // An `off` with no `listening` value: the mode it is for is never reached.
      editManifest("alpha", (json) => {
        delete json.retroplug.protocol.apps[0].syncOverride.listening;
      });
      result = refusal();
      break;
    case "sync-not-object":
      // An override that is not an object is data no rule can read at all.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.apps[0].syncOverride = "MidiMap";
      });
      result = refusal();
      break;
    case "sync-no-node":
      // No node type to set the mode on.
      editManifest("alpha", (json) => {
        delete json.retroplug.protocol.apps[0].syncOverride.node;
      });
      result = refusal();
      break;
    case "sync-no-mode":
      // No mode to put it in.
      editManifest("alpha", (json) => {
        delete json.retroplug.protocol.apps[0].syncOverride.mode;
      });
      result = refusal();
      break;
    case "sync-off-is-mode":
      // The "not listening" mode is the listening one: the pair changes nothing, so one of the two is a mistake.
      editManifest("alpha", (json) => {
        const sync = json.retroplug.protocol.apps[0].syncOverride;
        sync.off = sync.mode;
      });
      result = refusal();
      break;
    case "sync-node-undeclared":
      // The node type the mode is set on must be one a package declares: a misspelt one would plan, and do nothing.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.apps[0].syncOverride.node = "rp.alpha.syncx";
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
        json.dependencies = { ...json.dependencies, "@retroplug/a-b": "workspace:*", "@retroplug/a": "workspace:*" };
      });
      editManifest("a-b", (json) => {
        json.name = "@retroplug/a-b";
        json.retroplug.protocol.apps = [{ id: "c", module: "ts/appRuntime.ts" }];
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
        json.retroplug.protocol = { apps: [{ id: "b-c", module: "ts/appRuntime.ts" }] };
      });
      writeTree(
        "packages/a/ts/app.ts",
        "export default { controllerApps: [{ id: \"b-c\", label: \"BC\", config: { parse: (v) => v } }] };\n",
      );
      writeTree("packages/a/ts/appRuntime.ts", "export default { id: \"b-c\" };\n");
      result = refusal();
      break;
    case "app-unknown-key":
      // A misspelt knob in an app entry: nothing reads it, and a runtime would be built without what it meant.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.apps[0].factorY = "ts/appRuntime.ts";
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
        json.retroplug.protocol.nodes.push({ type: "rp.alpha.serial", label: 18, midiIn: null, writes: [], serialOut: true });
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
      writeTree("packages/alpha/ts/nodes.ts", 'export default [{ id: "rp.alpha.midi" }, { id: "rp.alpha.plainer" }];\n');
      result = refusal();
      break;
    case "capture-form":
      // The emitted table's spelling of a capture table: spaced like the fields around it.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes[0].capture = { midiMap: "drive", off: "listen" };
        json.retroplug.protocol.nodes[0].out = "rp.alpha.serial";
        json.retroplug.protocol.nodes.push({ type: "rp.alpha.serial", label: 18, midiIn: null, writes: [], serialOut: true });
      });
      writeTree("packages/alpha/ts/nodes.ts", 'export default [{ id: "rp.alpha.midi" }, { id: "rp.alpha.serial" }];\n');
      generate({ root });
      result = { files: outputs() };
      break;
    case "embed-form":
      // The declaration and the manifest entry of one embedded ROM, and what the shell reads of it: the load takes its
      // platform from the declaration, so it comes through with the label and the console.
      writeTree("packages/alpha/ts/app.ts", embedApp("alpha", [{ id: "alpha-rom", label: "Alpha ROM", platform: "nes" }]));
      writeTree(
        "packages/alpha/ts/consoles.ts",
        consoleModule({ descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }] }),
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
        consoleModule({ descriptors: [{ id: "alpha.nes", core: "alpha-core", nodeType: "rp.alpha.nes", platform: "nes" }] }),
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
