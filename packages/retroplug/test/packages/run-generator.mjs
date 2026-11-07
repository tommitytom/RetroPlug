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
//                     packages: [...] }
//   twice           { mtimes: [<after one run>, <after a second>], changed: [<first run>, <second run>] }
//   run             { registered: [<the ids the generated entry registers, in order>] }
//   missing-node    { error: <the generated entry's own refusal> }
//   order | cycle | unlisted-contributes | unlisted-plain | depends-on-shell | duplicate | duplicate-type |
//   duplicate-role | gap | low-label | role-undeclared | role-unwired | missing-module | unknown-key | half-wired |
//   extra-node
//                   { error: <the generator's message, or null when it did not refuse> }
//                   (unlisted-plain generates, and reports files like generate)
//
// The run, missing-node and extra-node modes bundle the generated entry with the same esbuild options the build uses
// and load it, so the entry's own checks are what is exercised, not a copy of them.
import { cpSync, mkdtempSync, readFileSync, rmSync, statSync, utimesSync, writeFileSync } from "node:fs";
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

// The generated entry, bundled and loaded as the build loads it. Loading it runs its checks and registers.
async function loadEntry() {
  const out = join(work, "scriptNodes.mjs");
  const { buildSync } = require("esbuild");
  buildSync(buildOptions(join(generated, "scriptNodes.ts"), out, "esm"));
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
      editManifest("alpha", (json) => {
        json.retroplug.protocol.nodes = [];
        json.retroplug.protocol.roles = {};
      });
      editManifest("retroplug", (json) => {
        json.retroplug.protocol.nodes = [];
      });
      writeTree("packages/retroplug/src/script/nodes.ts", "export default [];\n");
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
    default:
      console.error(`run-generator.mjs: unknown mode "${MODE}"`);
      process.exit(2);
  }
} finally {
  rmSync(work, { recursive: true, force: true });
}

console.log(JSON.stringify(result));
