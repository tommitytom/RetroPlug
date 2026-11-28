// A pack's refusal reaches what a project shows (docs/downloadable-packs-m3-design.md section 5): the native table's
// (__rp_packRefusals: API level, no library, discovery), the TS loader's, and an Engine's (each setGraph answer's
// refusedPacks), so a need whose pack is refused reads `refused` with the reason, the "needs updating" card's data.
import { test, expect } from "@retroplug/testing";
import { needOf, needText, statusOf } from "../../src/packs/needs";
import { forgetNativeRefusals, noteEngineRefusals, noteTsRefusal, tsRefusals } from "../../src/packs/refusals";
import { APP_BUILDERS, forgetRefused, loadPacks, packStatus } from "../../src/packs/loadPacks";
import type { PackHost } from "../../src/packs/packHost";
import { EngineGraph } from "../../src/engineGraph";

const g = globalThis as Record<string, unknown>;

test("a need whose pack the native table refused for its API level reads refused, needing an update", () => {
  g.__rp_packRefusals = () =>
    JSON.stringify([
      { id: "refix-old", reason: "pack refix-old is built for API level 2", version: "1.2.0", needsUpdate: true },
    ]);
  forgetNativeRefusals();
  const need = statusOf(needOf("platform", "refix-platform", { id: "refix-old", name: "Refix", version: "1.0.0" }));
  expect(need).toEqual({
    what: "platform",
    key: "refix-platform",
    pack: { id: "refix-old", name: "Refix", version: "1.2.0" },
    status: "refused",
    reason: "pack refix-old is built for API level 2",
    needsUpdate: true,
  });
  expect(packStatus().native["refix-old"]?.needsUpdate).toBe(true);
  delete g.__rp_packRefusals;
  forgetNativeRefusals();
  expect(statusOf(needOf("platform", "refix-platform", { id: "refix-old", name: "Refix" })).status).toBe("missing");
});

test("a TS loader's refusal reaches a need's status", () => {
  noteTsRefusal("refix-ts", "its app half threw");
  expect(statusOf(needOf("platform", "refix-ts-platform", { id: "refix-ts", name: "Refix TS" }))).toEqual({
    what: "platform",
    key: "refix-ts-platform",
    pack: { id: "refix-ts", name: "Refix TS" },
    status: "refused",
    reason: "its app half threw",
  });
});

test("a role the engine could not place reads the Engine's reason, not a stale native or TS refusal", () => {
  g.__rp_packRefusals = () => JSON.stringify([{ id: "refix-stale", reason: "loaded already from elsewhere" }]);
  forgetNativeRefusals();
  noteTsRefusal("refix-stale", "an old TS refusal");
  noteEngineRefusals([{ id: "refix-stale", reason: "a clash in this engine" }]);
  const need = needOf("role", "refix-stale-role", { id: "refix-stale", name: "Stale" });
  expect(statusOf(need, "the planner's", true).reason).toBe("a clash in this engine");
  expect(statusOf(need).reason).toBe("loaded already from elsewhere"); // the default order is unchanged
  delete g.__rp_packRefusals;
  forgetNativeRefusals();
  const none = needOf("role", "refix-planner-role", { id: "refix-none", name: "None" });
  expect(statusOf(none, "the planner's", true).reason).toBe("the planner's");
});

test("a rom need names no pack, so it stays as it is", () => {
  const need = needOf("rom", "game.gb");
  expect(statusOf(need, "a reason")).toBe(need);
});

test("an Engine's refusal rides its setGraph answer and reaches the status", () => {
  const graph = new EngineGraph({
    setGraph: () => ({
      ok: true,
      errors: [],
      refusedPacks: [{ id: "refix-engine", reason: "a clash in this engine" }],
    }),
  });
  graph.publish({ systems: [], routing: "stereo" });
  expect(packStatus().engine["refix-engine"]).toBe("a clash in this engine");
  expect(statusOf(needOf("role", "refix-role", { id: "refix-engine", name: "Refix Engine" }))).toEqual({
    what: "role",
    key: "refix-role",
    pack: { id: "refix-engine", name: "Refix Engine" },
    status: "refused",
    reason: "a clash in this engine",
  });
  noteEngineRefusals([]);
});

test("a pack refused in TS and tried again after forgetRefused loses its refusals once it loads", async () => {
  let broken = true;
  const host: PackHost = {
    list: () => [
      {
        id: "refix-later",
        version: "1.0.0",
        halves: ["app"],
        provides: { platforms: [], nodeTypes: [], roles: [] },
        requires: { packs: [], platforms: [] },
      },
    ],
    importHalf: async () => {
      if (broken) throw new Error("its app half threw");
      return { default: {} };
    },
    refuse: () => {},
  };
  const need = needOf("platform", "refix-later-platform", { id: "refix-later", name: "Refix Later" });
  await loadPacks(host, APP_BUILDERS);
  expect(packStatus().refused["refix-later"]).toBe("its app half threw");
  expect(statusOf(need).status).toBe("refused");
  // Without forgetting, the loader skips a refused id.
  broken = false;
  await loadPacks(host, APP_BUILDERS);
  expect(packStatus().loaded["refix-later"]).toBe(undefined);
  forgetRefused("refix-later");
  await loadPacks(host, APP_BUILDERS);
  expect(tsRefusals().has("refix-later")).toBe(false);
  expect(packStatus().refused["refix-later"]).toBe(undefined);
  expect(packStatus().loaded["refix-later"]).toEqual(["app"]);
  expect(statusOf(need).status).toBe("missing");
});

test("a pack that loaded ignores a native refusal of another folder of the same id", async () => {
  g.__rp_packRefusals = () => JSON.stringify([{ id: "refix-folder", reason: "an older folder was refused" }]);
  forgetNativeRefusals();
  const need = needOf("platform", "refix-folder-platform", { id: "refix-folder", name: "Refix Folder" });
  expect(statusOf(need).status).toBe("refused");
  const host: PackHost = {
    list: () => [
      {
        id: "refix-folder",
        version: "2.0.0",
        halves: ["app"],
        provides: { platforms: [], nodeTypes: [], roles: [] },
        requires: { packs: [], platforms: [] },
      },
    ],
    importHalf: async () => ({ default: {} }),
    refuse: () => {},
  };
  await loadPacks(host, APP_BUILDERS);
  expect(statusOf(need).status).toBe("missing");
  delete g.__rp_packRefusals;
  forgetNativeRefusals();
});

test("a pack that loaded and is also refused by the Engine still shows the Engine's refusal", async () => {
  const host: PackHost = {
    list: () => [
      {
        id: "refix-both",
        version: "1.0.0",
        halves: ["app"],
        provides: { platforms: [], nodeTypes: [], roles: [] },
        requires: { packs: [], platforms: [] },
      },
    ],
    importHalf: async () => ({ default: {} }),
    refuse: () => {},
  };
  await loadPacks(host, APP_BUILDERS);
  noteEngineRefusals([{ id: "refix-both", reason: "a clash in this engine" }]);
  const need = needOf("role", "refix-both-role", { id: "refix-both", name: "Refix Both" });
  expect(statusOf(need).reason).toBe("a clash in this engine");
  expect(statusOf(need).status).toBe("refused");
  noteEngineRefusals([]);
});

test("a build-failed need reads with and without a reason", () => {
  const base = needOf("rom", "song.rrone");
  expect(needText(base)).toBe("The ROM song.rrone is not one this build reads");
  expect(needText({ ...base, status: "refused" })).toBe("The ROM song.rrone could not be built");
  expect(needText({ ...base, status: "refused", reason: "the core refused it" })).toBe(
    "The ROM song.rrone could not be built: the core refused it",
  );
});
