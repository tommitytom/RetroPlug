#!/usr/bin/env node
// Build + run the TypeScript harness tests (TAP). Replaces the per-file
// cli-ts-test-<slug> / ui-ts-test-<slug> CMake targets and their CONFIGURE_DEPENDS
// glob (tests are now discovered at runtime, no reconfigure needed).
//
//   node scripts/run-ts-tests.js <cli|ui> [slugFilter]
//
//   cli  -> test/ts/**/*.test.ts  (excluding test/ts/ui/), run via
//           build/bin/retroplug-cli --test
//   ui   -> test/ts/ui/**/*.test.ts, run via build/bin/retroplug-ui-test --test
//
// slugFilter (optional): a path slug under the runner's test root with the
// .test.ts suffix stripped, in slash ("gb/mgb") or dash ("gb-mgb") form. Matches
// that test exactly, or a directory prefix ("gb/lsdj" runs every lsdj test).
// Each test bundles via tools/build-test.js then runs in its own QuickJS process
// (one runtime per file = isolation). Exits nonzero if any test fails.

const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "..");

const runner = process.argv[2];
const filter = process.argv[3];
if (runner !== "cli" && runner !== "ui") {
    console.error("usage: node scripts/run-ts-tests.js <cli|ui> [slugFilter]");
    process.exit(2);
}

const TEST_TS_ROOT = path.join(REPO_ROOT, "test/ts");
const testRoot = runner === "ui" ? path.join(TEST_TS_ROOT, "ui") : TEST_TS_ROOT;
const binary = path.join(REPO_ROOT, "build/bin", runner === "ui" ? "retroplug-ui-test" : "retroplug-cli")
    + (process.platform === "win32" ? ".exe" : "");
// cli bundles land in build/test-js/<slug>.js; ui in build/test-js/ui/<slug>.js
const outRoot = runner === "ui"
    ? path.join(REPO_ROOT, "build/test-js/ui")
    : path.join(REPO_ROOT, "build/test-js");

function walk(dir, out = []) {
    for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, ent.name);
        if (ent.isDirectory()) {
            // for the cli runner, test/ts/ui is handled by the ui runner
            if (runner === "cli" && p === path.join(TEST_TS_ROOT, "ui")) continue;
            walk(p, out);
        } else if (ent.name.endsWith(".test.ts")) {
            out.push(p);
        }
    }
    return out;
}

function matches(slugSlash, f) {
    if (!f) return true;
    const slugDash = slugSlash.replace(/\//g, "-");
    return slugSlash === f || slugDash === f
        || slugSlash.startsWith(f + "/") || slugDash.startsWith(f + "-");
}

if (!fs.existsSync(binary)) {
    console.error(`runner not built: ${path.relative(REPO_ROOT, binary)} (run \`pnpm configure\` with -DBUILD_TESTING=ON for ui, then build)`);
    process.exit(1);
}

const tests = walk(testRoot)
    .map((tsPath) => {
        const slug = path.relative(testRoot, tsPath).replace(/\.test\.ts$/, "").split(path.sep).join("/");
        return { tsPath, slug };
    })
    .filter((t) => matches(t.slug, filter))
    .sort((a, b) => a.slug.localeCompare(b.slug));

if (tests.length === 0) {
    console.error(filter ? `no tests match slug "${filter}"` : "no tests found");
    process.exit(1);
}

const failures = [];
for (const { tsPath, slug } of tests) {
    const outJs = path.join(outRoot, `${slug}.js`);
    fs.mkdirSync(path.dirname(outJs), { recursive: true });

    const build = spawnSync(process.execPath, [path.join(REPO_ROOT, "tools/build-test.js"), tsPath, outJs], {
        stdio: "inherit",
        cwd: REPO_ROOT,
    });
    if (build.status !== 0) {
        console.error(`# BUILD FAILED: ${slug}`);
        failures.push(slug);
        continue;
    }

    const run = spawnSync(binary, ["--test", outJs], { stdio: "inherit", cwd: REPO_ROOT });
    if (run.status !== 0) failures.push(slug);
}

if (failures.length) {
    console.error(`\n# ${failures.length}/${tests.length} test file(s) FAILED: ${failures.join(", ")}`);
    process.exit(1);
}
console.error(`\n# ${tests.length} test file(s) passed`);
