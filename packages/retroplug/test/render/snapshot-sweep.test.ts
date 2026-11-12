// The startup sweep for what a crash leaves of a render's snapshots (src/renderSnapshotSweep.ts,
// docs/known-bugs-batch-1-design.md section 4.2). A live job's snapshot is its own and the engine's pool deletes it
// (EngineRunOptions.discardFiles), so the sweep may only take what no job can own any more: a file of the snapshot
// name a day old, past any render's wait behind the engine's worker budget. Everything else in the config directory
// is left alone, and so is a snapshot it cannot age or cannot delete.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { RENDER_SNAPSHOT_MAX_AGE_MS, sweepRenderSnapshots } from "../../src/renderSnapshotSweep";

const DAY = 24 * 60 * 60 * 1000;

/** A config directory with one file of every kind the sweep meets, and the backend that holds them. */
function configDir(): MockBackend {
  const be = new MockBackend("/config");
  be.seed("/config/.render-src-crashed.sav", "x"); // a process that died with a render queued: gone
  be.setFileAge("/config/.render-src-crashed.sav", DAY + 1);
  be.seed("/config/.render-src-live.sav", "x"); // a render submitted a minute ago: kept
  be.setFileAge("/config/.render-src-live.sav", 60 * 1000);
  be.seed("/config/.render-src-edge.ss0", "x"); // exactly a day old: the bound is "older than", so kept
  be.setFileAge("/config/.render-src-edge.ss0", DAY);
  be.seed("/config/config.json", "{}"); // not a snapshot, however old
  be.setFileAge("/config/config.json", 30 * DAY);
  be.seed("/config/.render-src-future.ss0", "x"); // a mtime from the future (a clock that went back): kept
  be.setFileAge("/config/.render-src-future.ss0", -60 * 1000);
  be.seed("/config/.render-src-dir/inside.sav", "x"); // a directory wears the name (listDir marks it ".../"): kept
  return be;
}

test("the sweep deletes a render snapshot a day old, and nothing a live render could still own", () => {
  const be = configDir();
  expect(sweepRenderSnapshots(be, "/config")).toBe(1);
  expect(be.fileExists("/config/.render-src-crashed.sav")).toBe(false);
  expect(be.fileExists("/config/.render-src-live.sav")).toBe(true);
  expect(be.fileExists("/config/.render-src-edge.ss0")).toBe(true);
  expect(be.fileExists("/config/.render-src-future.ss0")).toBe(true);
  expect(be.fileExists("/config/config.json")).toBe(true);
  expect(be.fileExists("/config/.render-src-dir/inside.sav")).toBe(true);
  // A second sweep has nothing left to do, and its bound is the day the first one used.
  expect(sweepRenderSnapshots(be, "/config")).toBe(0);
  expect(RENDER_SNAPSHOT_MAX_AGE_MS).toBe(DAY);
});

test("the sweep passes over a snapshot it cannot age and one it cannot delete", () => {
  class Awkward extends MockBackend {
    override fileAgeMs(path: string): number | null {
      return path.endsWith("-unreadable.sav") ? null : super.fileAgeMs(path);
    }
    override deleteFile(path: string): boolean {
      return path.endsWith("-stuck.sav") ? false : super.deleteFile(path);
    }
  }
  const be = new Awkward("/config");
  be.seed("/config/.render-src-unreadable.sav", "x");
  be.setFileAge("/config/.render-src-unreadable.sav", 9 * DAY);
  be.seed("/config/.render-src-stuck.sav", "x");
  be.setFileAge("/config/.render-src-stuck.sav", 9 * DAY);
  // One that can be deleted beside them, so a sweep that gave up at the first refusal shows.
  be.seed("/config/.render-src-gone.sav", "x");
  be.setFileAge("/config/.render-src-gone.sav", 9 * DAY);
  expect(sweepRenderSnapshots(be, "/config")).toBe(1);
  expect(be.fileExists("/config/.render-src-unreadable.sav")).toBe(true);
  expect(be.fileExists("/config/.render-src-stuck.sav")).toBe(true);
  expect(be.fileExists("/config/.render-src-gone.sav")).toBe(false);
});

test("the sweep never ages a directory: listDir marks one, and the mock has no age for it", () => {
  // The sweep skips a subdirectory by the trailing '/' listDir puts on its name, not by asking its age first: native's
  // fileAgeMs ages a directory like a file (test:plugin's [host] case), while this fake answers nothing for one, so a
  // sweep that aged before it looked would pass over a directory here and take one under a native host.
  const be = configDir();
  expect(be.listDir("/config").includes(".render-src-dir/")).toBe(true);
  expect(be.fileAgeMs("/config/.render-src-dir")).toBe(null);
});

test("composing the store graph is what sweeps, so every host that starts does it once", () => {
  const be = configDir();
  composeAppStores({ backend: be });
  expect(be.fileExists("/config/.render-src-crashed.sav")).toBe(false);
  expect(be.fileExists("/config/.render-src-live.sav")).toBe(true);
});

test("a sweep that cannot list the config directory warns and leaves the store graph standing", () => {
  // The sweep is a startup tidy, not a load: a config directory this filesystem refuses to list leaves its leftovers
  // for the next startup, and every screen the graph stands on still composes (the isolation rule a package's
  // contributions get). One warning says why, in the sweep's own name.
  class Unlistable extends MockBackend {
    override listDir(dir: string): string[] {
      if (this.canonicalize(dir) === this.configDir()) throw new Error("this directory cannot be listed");
      return super.listDir(dir);
    }
  }
  const be = new Unlistable("/config");
  be.seed("/config/.render-src-crashed.sav", "x");
  be.setFileAge("/config/.render-src-crashed.sav", 9 * DAY);
  const warnings: string[] = [];
  const original = console.warn;
  console.warn = (...args: unknown[]) => void warnings.push(args.map((a) => String(a)).join(" "));
  try {
    const stores = composeAppStores({ backend: be });
    expect(stores.project !== undefined).toBe(true);
  } finally {
    console.warn = original;
  }
  expect(warnings).toEqual(["render snapshot sweep: this directory cannot be listed"]);
  expect(be.fileExists("/config/.render-src-crashed.sav")).toBe(true); // nothing was taken
});
