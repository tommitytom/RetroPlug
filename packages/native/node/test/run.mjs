#!/usr/bin/env node
// Build everything the Node-API host tests need, then run them.
//
//   pnpm test:node          (or: node packages/native/node/test/run.mjs)
//
// The addon sits behind -DRETROPLUG_NODE_ADDON=ON (off by default, since it needs Node's N-API
// headers), so unlike the other test:* scripts this one may have to RECONFIGURE before it can build
// its target. It only does so when the cache says the option is off, keeping the configured build/
// intact the rest of the time.

import { spawnSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";
import { cpus } from "node:os";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const BUILD = join(REPO, "build");
const CACHE = join(BUILD, "CMakeCache.txt");

function run(cmd, args, label) {
    console.log(`==> ${label}`);
    const r = spawnSync(cmd, args, { stdio: "inherit", cwd: REPO });
    if (r.error) {
        console.error(r.error.message);
        process.exit(1);
    }
    if (r.status !== 0) process.exit(r.status ?? 1);
}

const addonEnabled =
    existsSync(CACHE) && /^RETROPLUG_NODE_ADDON:BOOL=ON$/m.test(readFileSync(CACHE, "utf8"));

if (!addonEnabled) {
    run("cmake", ["-S", REPO, "-B", BUILD, "-DRETROPLUG_NODE_ADDON=ON"], "enabling the Node addon");
}

run(
    "cmake",
    ["--build", BUILD, "-j", String(cpus().length), "--target", "retroplug-node", "retroplug-cli"],
    "building retroplug-node + retroplug-cli",
);
run("node", [join(REPO, "tools/build-cli-sdk.mjs"), join(BUILD, "cli-sdk")], "building the CLI SDK");

run(
    "node",
    ["--test", join(HERE, "parity.test.mjs"), join(HERE, "emu-parity.test.mjs")],
    "running the Node-API host tests",
);
