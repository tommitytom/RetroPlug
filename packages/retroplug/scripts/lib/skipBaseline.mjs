// The skip ratchet. Once a skipped case reports itself honestly (see testing/harness.ts), the next
// question is whether the set of skipping files is allowed to GROW — because the way this codebase lost
// coverage was never a deleted test, it was a fixture path quietly going stale while the suite stayed
// green. A tracked baseline is what turns that from invisible into a failed build.
//
// The asymmetry is the whole design: a file may skip only if it is listed, and only up to its listed
// count, but skipping FEWER is never an error. That is what lets one baseline serve two very different
// environments. Skips here are driven entirely by fixture availability, and CI checks out none of the
// sibling trees (/workspaces/resources, /workspaces/bliptoaster, the risa source) that a dev box has —
// so CI is the worst case, the baseline records it, and a machine that owns the fixtures simply runs
// more of the suite and passes.
//
// The cost of that asymmetry is that the dev box is held to nothing, so it cannot notice a path typo
// that flips a case from pass to skip on a machine that HAS the fixture. RP_FAIL_ON_SKIP=1 is the other
// half: it demands zero skips, for a box with a complete fixture set.
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const FILE = join(dirname(dirname(fileURLToPath(import.meta.url))), "skip-baseline.json");

function load() {
  try {
    const data = JSON.parse(readFileSync(FILE, "utf8"));
    return data && typeof data === "object" ? data : {};
  } catch {
    return {};
  }
}

/** Sorted keys + the same formatting saveTimings uses, so regenerating produces a reviewable diff
 *  rather than a reordered file. */
function save(all) {
  const sorted = {};
  for (const suite of Object.keys(all).sort()) {
    sorted[suite] = {};
    for (const slug of Object.keys(all[suite]).sort()) sorted[suite][slug] = all[suite][slug];
  }
  writeFileSync(FILE, JSON.stringify(sorted, null, 1) + "\n");
}

/**
 * `entries` is [{ slug, skip, total }] for every file that RAN.
 *
 * Returns { ok, lines }. Callers print the lines and exit 1 when !ok.
 */
export function checkSkipBaseline(suite, entries, { filtered, update, strict }) {
  const lines = [];

  if (strict) {
    const skipping = entries.filter((e) => e.skip > 0);
    if (!skipping.length) return { ok: true, lines };
    lines.push(`# RP_FAIL_ON_SKIP=1: ${skipping.reduce((a, e) => a + e.skip, 0)} case(s) skipped`);
    for (const e of skipping) lines.push(`#   ${e.slug}: ${e.skip}/${e.total}`);
    return { ok: false, lines };
  }

  if (update) {
    // A filtered run measures a handful of files. runPool's timings MERGE in that case, which is right
    // for a scheduling hint and wrong for a contract: merging here would let `test:native risa --update`
    // ratchet seven files up with nothing in the diff to review. Refuse instead.
    if (filtered) {
      lines.push("# --update-skip-baseline needs a full run: it records every file, and a filtered run");
      lines.push("#   would silently keep the stale entries for everything it did not execute.");
      return { ok: false, lines };
    }
    const all = load();
    all[suite] = {};
    for (const e of entries.filter((x) => x.skip > 0).sort((a, b) => a.slug.localeCompare(b.slug))) {
      all[suite][e.slug] = { skipped: e.skip, total: e.total };
    }
    save(all);
    const n = Object.keys(all[suite]).length;
    lines.push(`# skip-baseline updated: ${suite} now records ${n} skipping file(s)`);
    return { ok: true, lines };
  }

  // A filtered run cannot judge the baseline: it has no reading for the files it did not execute.
  if (filtered) return { ok: true, lines };

  const base = load()[suite] ?? {};
  const seen = new Map(entries.map((e) => [e.slug, e]));
  const problems = [];

  for (const e of entries) {
    const want = base[e.slug];
    if (e.skip > 0 && !want) {
      problems.push(`${e.slug}: skipped ${e.skip}/${e.total} case(s) but is not in the baseline`);
    } else if (want && e.skip > want.skipped) {
      problems.push(`${e.slug}: skipped ${e.skip} case(s), baseline allows ${want.skipped}`);
    }
    // Deleted coverage must not read as improvement: dropping cases from a skipping file lowers its skip
    // count, which the "fewer is fine" rule would otherwise wave through.
    if (want && e.total < want.total) {
      problems.push(`${e.slug}: has ${e.total} case(s), baseline recorded ${want.total} — cases were removed`);
    }
  }
  for (const slug of Object.keys(base)) {
    if (!seen.has(slug)) problems.push(`${slug}: in the baseline but did not run (renamed or deleted?)`);
  }

  if (!problems.length) return { ok: true, lines };
  lines.push(`# skip-baseline FAILED for ${suite}:`);
  for (const p of problems) lines.push(`#   ${p}`);
  lines.push("# If these skips are expected, re-run with --update-skip-baseline and review the diff.");
  return { ok: false, lines };
}
