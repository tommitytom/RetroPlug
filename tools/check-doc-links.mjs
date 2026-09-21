#!/usr/bin/env node
// Verify every relative markdown link in the tracked docs still points at something.
//
// The 2026-09-20 audit found ~40 dead links and a `file:line` anchor citing a block in a file
// hundreds of lines shorter than the number quoted. That class of rot is invisible in review (a
// link renders the same whether or not it resolves) and only ever gets found by someone reading
// the doc and being misled, so it is worth a gate rather than a periodic sweep.
//
//   node tools/check-doc-links.mjs            # paths + EOF-overrunning #L anchors; nonzero on any
//   node tools/check-doc-links.mjs --anchors  # also WARN on #L anchors that look mis-aimed
//
// Two checks are exact and gate the build:
//   1. the link target exists on disk
//   2. a `#Lnnn` anchor is within the target file
//
// The third is a heuristic and only ever warns. A `#Lnnn` anchor is "aimed" if any backticked
// identifier in the link text or the two lines before it appears within +/-25 lines of the cited
// line. It cannot prove an anchor is right, only that nothing near the target resembles what the
// sentence is talking about - so it is advisory input for a human, never a failure.
//
// A target that EXISTS but is not tracked is an error too, and that is not pedantry: CI builds from
// a fresh clone, so a link to a derived or gitignored file (the `examples/**/*.rpp` Reaper fixtures
// are the live example) resolves on the machine that wrote it and nowhere else. Checking existence
// alone makes this script pass locally and fail in CI, which is the worst of both. Paths inside
// submodules are exempt - they are tracked by their own repo, not this one.
//
// Scope: tracked `*.md` outside deps/ (vendored docs are not ours to fix). http(s)/mailto and
// bare `#fragment` links are skipped: resolving those needs the network or a heading parser.

import { readFileSync, existsSync, statSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import { execFileSync } from "node:child_process";

const WINDOW = 25;
const wantAnchors = process.argv.includes("--anchors");

// execFileSync, not a shell: `git ls-files -- '*.md'` needs the quotes to survive, and cmd.exe
// does not strip them, so the shell form silently matches nothing on Windows.
const files = execFileSync("git", ["ls-files", "--", "*.md"], { encoding: "utf8" })
    .split("\n")
    .filter((f) => f && !f.startsWith("deps/"));

// Everything this repo tracks, to tell "present in a clone" from "present on this machine".
const tracked = new Set(
    execFileSync("git", ["ls-files"], { encoding: "utf8" }).split("\n").filter(Boolean),
);
const submodules = new Set(
    execFileSync("git", ["ls-files", "--stage"], { encoding: "utf8" })
        .split("\n")
        .filter((l) => l.startsWith("160000"))
        .map((l) => l.split("\t")[1]),
);

const errors = [];
const warnings = [];

for (const file of files) {
    const lines = readFileSync(file, "utf8").split("\n");

    lines.forEach((line, i) => {
        for (const m of line.matchAll(/\[([^\]]*)\]\(([^)\s]+)\)/g)) {
            const [, text, href] = m;
            if (/^(https?:|mailto:|#)/.test(href)) continue;

            const hash = href.indexOf("#");
            const relPath = hash >= 0 ? href.slice(0, hash) : href;
            const frag = hash >= 0 ? href.slice(hash + 1) : "";
            if (!relPath) continue;

            const where = `${file}:${i + 1}`;
            const target = resolve(dirname(file), decodeURI(relPath));
            if (!existsSync(target)) {
                errors.push(`${where}  dead link  ${href}`);
                continue;
            }

            // Present here, but would it be present in a fresh clone?
            const rel = relative(process.cwd(), target);
            const inSubmodule = [...submodules].some((m) => rel === m || rel.startsWith(`${m}/`));
            if (!inSubmodule && !statSync(target).isDirectory() && !tracked.has(rel)) {
                errors.push(`${where}  target is untracked, so it will not exist in a fresh clone  ${href}`);
                continue;
            }

            const anchor = /^L(\d+)/.exec(frag);
            if (!anchor || !statSync(target).isFile()) continue;

            const body = readFileSync(target, "utf8").split("\n");
            const n = Number(anchor[1]);
            if (n > body.length) {
                errors.push(`${where}  anchor past EOF  ${href}  (${relPath} has ${body.length} lines)`);
                continue;
            }
            if (!wantAnchors) continue;

            // Heuristic: does anything near the cited line resemble what the sentence names?
            const context = lines.slice(Math.max(0, i - 2), i + 1).join(" ");
            const ids = new Set();
            for (const t of `${text} ${context}`.matchAll(/`([A-Za-z_][A-Za-z0-9_:.>-]{3,})`/g)) {
                // Take the last path/namespace segment: `Engine::applyConfigField` -> applyConfigField.
                const id = t[1].replace(/^.*[:.]/, "").replace(/[^A-Za-z0-9_]/g, "");
                if (id.length >= 4) ids.add(id);
            }
            const bare = text.replace(/[`[\]]/g, "").split(/[:\s(]/)[0];
            if (/^[A-Za-z_][A-Za-z0-9_]{3,}$/.test(bare)) ids.add(bare);
            if (ids.size === 0) continue;

            const near = body.slice(Math.max(0, n - 1 - WINDOW), n - 1 + WINDOW).join("\n");
            if (![...ids].some((id) => near.includes(id))) {
                warnings.push(`${where}  anchor may be stale  ${href}  (looked for ${[...ids].slice(0, 4).join(", ")})`);
            }
        }
    });
}

for (const w of warnings) console.log(`WARN  ${w}`);
for (const e of errors) console.error(`ERROR ${e}`);

const scanned = files.length;
if (errors.length === 0) {
    console.log(`\ncheck-doc-links: ${scanned} file(s) clean` + (warnings.length ? `, ${warnings.length} anchor warning(s)` : ""));
    process.exit(0);
}
console.error(`\ncheck-doc-links: ${errors.length} error(s) across ${scanned} file(s)`);
process.exit(1);
