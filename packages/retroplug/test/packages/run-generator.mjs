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
//   generate        { files: { "packages.ts": <text>, "protocol.ts": <text>, "scriptNodes.ts": <text> },
//                              packages: [...] }
//   twice           { mtimes: [<after one run>, <after a second>], changed: [<first run>, <second run>] }
//   run             { registered: [<the ids the generated entry registers, in order>] }
//   missing-node    { error: <the generated entry's own refusal> }
//   order | cycle | unlisted-contributes | unlisted-plain | depends-on-shell | duplicate | duplicate-type |
//   duplicate-role | gap | low-label | role-undeclared | role-unwired | missing-module | unexported-app | name-mismatch |
//   outside-ts | shell-undeclared | unknown-key | half-wired | extra-node | duplicate-role-kind |
//   duplicate-kit-format | duplicate-marker-role | duplicate-app | duplicate-app-control | app-unknown-key |
//   app-unpaired-runtime | app-unpaired-control | app-runtime-mismatch | app-module-empty | sync-unknown-key |
//   sync-unused-off | sync-node-undeclared | identifier-collision | apps-only
//                   { error: <the generator's message, or null when it did not refuse> }
//                   (unlisted-plain generates, and reports files like generate)
//
// The run, missing-node and extra-node modes bundle the generated entry with the same esbuild options the build uses
// and load it, so the entry's own checks are what is exercised, not a copy of them.
import { cpSync, mkdtempSync, readFileSync, renameSync, rmSync, statSync, utimesSync, writeFileSync } from "node:fs";
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
    "protocol.ts": readFileSync(join(generated, "protocol.ts"), "utf8"),
    "scriptNodes.ts": readFileSync(join(generated, "scriptNodes.ts"), "utf8"),
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
  const { buildSync } = require("esbuild");
  buildSync({
    ...buildOptions(join(generated, "packages.ts"), out, "esm"),
    alias: {
      "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
      "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
    },
  });
  const loaded = await import(pathToFileURL(out).href);
  return {
    roles: loaded.ROLES.map((role) => role.kind),
    romProviders: loaded.ROM_PROVIDERS.map((provider) => provider.id),
    trackerIntegrations: loaded.TRACKER_INTEGRATIONS.map((integration) => integration.id),
    kitFormats: loaded.KIT_FORMATS.map((format) => format.id),
  };
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
      const files = ["packages.ts", "protocol.ts", "scriptNodes.ts"].map((name) => join(generated, name));
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
    case "sync-node-undeclared":
      // The node type the mode is set on must be one a package declares: a misspelt one would plan, and do nothing.
      editManifest("alpha", (json) => {
        json.retroplug.protocol.apps[0].syncOverride.node = "rp.alpha.syncx";
      });
      result = refusal();
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
      writeTree("packages/a-b/ts/app.ts", "export default { controllerApps: [{ id: \"c\", label: \"C\", config: { parse: (v) => v } }] };\n");
      writeTree("packages/a-b/ts/appRuntime.ts", "export default { id: \"c\" };\n");
      editManifest("a", (json) => {
        json.name = "@retroplug/a";
        json.dependencies = { "@retroplug/core": "workspace:*" };
        json.exports["./appRuntime"] = "./ts/appRuntime.ts";
        json.retroplug.protocol = { apps: [{ id: "b-c", module: "ts/appRuntime.ts" }] };
      });
      writeTree("packages/a/ts/app.ts", "export default { controllerApps: [{ id: \"b-c\", label: \"BC\", config: { parse: (v) => v } }] };\n");
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
        "export default { id: \"not-alpha-app\", factory: () => () => {}, playbackModel: () => null, target: () => null };\n",
      );
      generate({ root });
      try {
        result = await loadEntry();
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
