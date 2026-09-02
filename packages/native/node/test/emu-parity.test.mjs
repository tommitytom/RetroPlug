// End-to-end host parity: the REAL TypeScript control plane (bootSession, the project/systems store,
// the DSP kernel, the audio driver) driven identically on both hosts, with the rendered PCM compared
// sample for sample.
//
//   node --test packages/native/node/test/emu-parity.test.mjs
//
// Requires:
//   ./build.sh -DRETROPLUG_NODE_ADDON=ON
//   cmake --build build --target retroplug-node -j$(nproc)
//   node tools/build-cli-sdk.mjs build/cli-sdk
//
// Where parity.test.mjs checks the codec in isolation, this checks the thing that actually matters:
// that packages/retroplug/src runs UNMODIFIED on Node and produces the same emulator output as the
// shipping QuickJS host. Both sides import the same built SDK bundle, so the only variables are the
// codec and the JS runtime.

import { test, before } from "node:test";
import assert from "node:assert/strict";
import { build } from "esbuild";
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, copyFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { createRequire } from "node:module";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const ADDON = join(REPO, "build/node/retroplug.node");
const CLI = join(REPO, "build/bin/retroplug-cli");
const SDK = join(REPO, "build/cli-sdk/retroplug-cli.js");
const NES_ROM = join(REPO, "resources/roms/bliptoaster.nes");

const require = createRequire(import.meta.url);

function requireArtifacts() {
  const missing = [];
  if (!existsSync(ADDON)) missing.push(`${ADDON} (./build.sh -DRETROPLUG_NODE_ADDON=ON)`);
  if (!existsSync(CLI)) missing.push(`${CLI} (cmake --build build --target retroplug-cli)`);
  if (!existsSync(SDK)) missing.push(`${SDK} (node tools/build-cli-sdk.mjs build/cli-sdk)`);
  if (!existsSync(NES_ROM)) missing.push(NES_ROM);
  if (missing.length) throw new Error(`missing build artifacts:\n  ${missing.join("\n  ")}`);
}

/** Boot the control plane in-process on the N-API addon and run the matrix. */
async function runNode(scratch) {
  const addon = require(ADDON);
  globalThis[Symbol.for("plugin")] = { __rpcSend: addon.rpcSend, args: [] };
  // cli/session.ts routes the process exit code through tjs.exit; it is the ONLY txiki coupling in
  // the whole TS layer, and this two-line shim is the entire cost of removing it.
  globalThis.tjs = { exit: (c) => { process.exitCode = c; } };

  // The SDK bundle is ESM but named .js, and the repo root package.json is commonjs, so Node would
  // load it as CJS. Copy it to .mjs for the import.
  const sdkMjs = join(scratch, "retroplug-cli.mjs");
  copyFileSync(SDK, sdkMjs);

  const sdk = await import(pathToFileURL(sdkMjs).href);
  const { runEmu } = await import("./emu-ops.mjs");
  return runEmu(sdk, NES_ROM);
}

/** Bundle the session and run the matrix out-of-process on the QuickJS host. */
async function runQuickJs(scratch) {
  const bundle = join(scratch, "emu-parity-session.js");
  await build({
    entryPoints: [join(HERE, "emu-session.mjs")],
    bundle: true,
    format: "esm",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    outfile: bundle,
    logLevel: "warning",
  });

  const res = spawnSync(CLI, [bundle, NES_ROM], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  const lines = (res.stdout || "").split("\n");
  const err = lines.find((l) => l.startsWith("__EMU_PARITY_ERROR__"));
  if (err) throw new Error(`session failed: ${err.slice("__EMU_PARITY_ERROR__".length)}`);
  const line = lines.find((l) => l.startsWith("__EMU_PARITY__"));
  if (!line) {
    throw new Error(
      `retroplug-cli produced no parity output (status ${res.status})\n` +
        `stdout: ${res.stdout}\nstderr: ${res.stderr}`,
    );
  }
  return JSON.parse(line.slice("__EMU_PARITY__".length));
}

let nodeResults;
let qjsResults;

before(async () => {
  requireArtifacts();
  const scratch = mkdtempSync(join(tmpdir(), "rp-emu-parity-"));
  // The QuickJS run first: the Node run installs globals into this process and boots an Engine, and
  // keeping the two sequential avoids any question of shared state.
  qjsResults = await runQuickJs(scratch);
  nodeResults = await runNode(scratch);
});

test("the real control plane boots and runs on Node", () => {
  const byLabel = Object.fromEntries(nodeResults);
  assert.equal(byLabel["sampleRate"], 44100, "bootSession did not wire the audio driver");
  assert.ok(byLabel["gb:id"] != null, "the embedded Game Boy core did not load");
  assert.ok(byLabel["nes:id"] != null, "the NES ROM did not load through Mesen");
});

test("both hosts ran the same matrix", () => {
  assert.deepStrictEqual(
    nodeResults.map(([label]) => label),
    qjsResults.map(([label]) => label),
  );
});

test("the emulators are actually producing sound", () => {
  // Guards the comparison below: two buffers of silence would match trivially.
  const byLabel = Object.fromEntries(nodeResults);
  assert.ok(byLabel["gb:peak"] > 0, "mGB rendered silence after note-on");
  assert.ok(byLabel["mix:peak"] > 0, "the NES rendered silence after note-on");
});

test("rendered PCM is identical between the two hosts", () => {
  const nodeBy = Object.fromEntries(nodeResults);
  const qjsBy = Object.fromEntries(qjsResults);
  for (const label of ["gb:pcmHash", "mix:pcmHash", "perSystem:hashes"]) {
    assert.equal(nodeBy[label], qjsBy[label], `PCM mismatch on ${label}`);
  }
  assert.equal(nodeBy["gb:pcmLen"], qjsBy["gb:pcmLen"]);
  assert.equal(nodeBy["mix:pcmLen"], qjsBy["mix:pcmLen"]);
});

test("emulator snapshots and debug reads agree", () => {
  const nodeBy = Object.fromEntries(nodeResults);
  const qjsBy = Object.fromEntries(qjsResults);
  assert.equal(nodeBy["gb:stateLen"], qjsBy["gb:stateLen"]);
  assert.equal(nodeBy["gb:stateHash"], qjsBy["gb:stateHash"]);
  assert.equal(nodeBy["nes:apuKeys"], qjsBy["nes:apuKeys"]);
  assert.equal(nodeBy["nes:pulse1Keys"], qjsBy["nes:pulse1Keys"]);
});

test("every recorded value agrees", () => {
  for (let i = 0; i < nodeResults.length; i++) {
    const [label, nodeValue] = nodeResults[i];
    const [, qjsValue] = qjsResults[i];
    assert.deepStrictEqual(nodeValue, qjsValue, `mismatch on "${label}"`);
  }
});
