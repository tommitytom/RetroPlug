// An awaited render on Node lets Node's event loop run while it renders. In a host with no UI tick an offline job's
// promise settles through a poll that waits in native between drains (packages/retroplug/src/offline.ts); as a pure
// microtask chain that poll held Node's timers and I/O until the job ended, so the audio driver makes it yield to the
// loop after each waited step on Node, as the CLI's render does on txiki.
//
//   node --test packages/native/node/test/offline-yield.test.mjs
//
// Requires the addon and the built SDK (run.mjs builds both).

import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, mkdtempSync, mkdirSync, copyFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { createRequire } from "node:module";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const ADDON = join(REPO, "build/node/retroplug.node");
const SDK = join(REPO, "build/cli-sdk/retroplug-cli.js");
const MGB = join(REPO, "resources/roms/mGB.gb");

const require = createRequire(import.meta.url);

test("an awaited render lets Node's timers run while it renders", async () => {
  for (const p of [ADDON, SDK, MGB]) assert.ok(existsSync(p), `missing ${p}`);
  const scratch = mkdtempSync(join(tmpdir(), "rp-offline-yield-"));
  try {
    const cfg = join(scratch, "cfg");
    mkdirSync(cfg, { recursive: true });
    process.env.RETROPLUG_USER_CONFIG_DIR = cfg;
    const addon = require(ADDON);
    globalThis[Symbol.for("plugin")] = { __rpcSend: addon.rpcSend, args: [] };
    globalThis.tjs = { exit: (c) => { process.exitCode = c; } };
    // The SDK bundle is ESM named .js under a commonjs package root, so it is imported as .mjs.
    const sdkMjs = join(scratch, "retroplug-cli.mjs");
    copyFileSync(SDK, sdkMjs);
    const sdk = await import(pathToFileURL(sdkMjs).href);
    const s = sdk.bootSession();

    // Long enough to take several native waits (each up to 100 ms) whatever the machine.
    const spec = { rom: MGB, out: join(scratch, "yield.wav"), durationMs: 120000 };
    let ticks = 0;
    const timer = setInterval(() => ticks++, 1);
    let ticksWhileRendering = 0;
    const result = await s.backend.engineRun("render", spec, {
      onProgress: () => (ticksWhileRendering = ticks),
    });
    clearInterval(timer);
    assert.equal(result.status, "done", result.message);
    assert.ok(ticksWhileRendering > 0, `the interval fired ${ticksWhileRendering} times by the render's last progress`);
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
});
