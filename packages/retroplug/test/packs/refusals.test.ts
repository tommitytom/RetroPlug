// A pack's refusal reaches what a project shows (docs/downloadable-packs-m3-design.md section 5): the native table's
// (__rp_packRefusals: API level, no library, discovery), the TS loader's, and an Engine's (each setGraph answer's
// refusedPacks), so a need whose pack is refused reads `refused` with the reason, the "needs updating" card's data.
import { test, expect } from "@retroplug/testing";
import { needOf, statusOf } from "../../src/packs/needs";
import { forgetNativeRefusals, noteEngineRefusals, noteTsRefusal } from "../../src/packs/refusals";
import { packStatus } from "../../src/packs/loadPacks";
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
