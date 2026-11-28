// The live load (docs/downloadable-packs-m3-design.md section 7): one development pack folder loaded now. Native loads
// it into the process's table (__rp_packLoadNow) and answers; on a load this module set's loader runs again with the
// builders it has loaded with, which loads only the new pack's halves; either way the status listeners hear of it.
import { test, expect } from "@retroplug/testing";
import { loadPackNow } from "../../src/packs/loadPackNow";
import { APP_BUILDERS, loadedBuilders, loadPacks, packStatus } from "../../src/packs/loadPacks";
import { subscribePackStatus } from "../../src/packs/refusals";
import type { PackHost, PackInfo } from "../../src/packs/packHost";

const g = globalThis as Record<string, unknown>;

test("a host with no live load answers so, and tells nobody", async () => {
  let told = 0;
  const off = subscribePackStatus(() => told++);
  expect(await loadPackNow("/packs/x/1.0.0")).toEqual({ refused: "this host loads no pack live" });
  expect(told).toBe(0);
  off();
});

test("a refused live load answers the refusal and tells the status listeners", async () => {
  g.__rp_packLoadNow = (dir: string) => JSON.stringify({ refused: `pack x is refused: nothing at ${dir}` });
  let told = 0;
  const off = subscribePackStatus(() => told++);
  expect(await loadPackNow("/packs/x/1.0.0")).toEqual({ refused: "pack x is refused: nothing at /packs/x/1.0.0" });
  expect(told).toBe(1);
  off();
  delete g.__rp_packLoadNow;
});

/** A pack whose only half is `protocol`, a data half: the loader commits it without importing anything. */
const dataPack = (id: string): PackInfo => ({
  id,
  version: "1.0.0",
  halves: ["protocol"],
  provides: { platforms: [], nodeTypes: [], roles: [] },
  requires: { packs: [], platforms: [] },
  protocol: { nodes: [], roles: {}, descriptors: {} },
});
const hostOf = (...packs: PackInfo[]): PackHost => ({
  list: () => packs,
  importHalf: async () => ({}),
  refuse: () => {},
});

test("a live load retries a pack TS refused, with the builder sets this module set has loaded with", async () => {
  const empty = hostOf();
  expect(loadedBuilders()).toBe(undefined);
  const ui = { ui: () => [] };
  await loadPacks(empty, APP_BUILDERS);
  await loadPacks(empty, ui);
  const union = loadedBuilders()!;
  expect(Object.keys(union).sort()).toEqual([...Object.keys(APP_BUILDERS), "ui"].sort());

  // TS refuses the pack at launch; a later builder set replaces the protocol builder in the union.
  const pack = dataPack("lpn-a");
  await loadPacks(hostOf(pack), {
    protocol: () => {
      throw new Error("a launch refusal");
    },
  });
  expect(packStatus().refused["lpn-a"]).toBe("a launch refusal");
  let seen = 0;
  await loadPacks(empty, { protocol: () => (seen++, []) });

  g.__rp_packLoadNow = () => JSON.stringify({ loaded: "lpn-a" });
  g.__rp_packList = () => JSON.stringify([pack]);
  let told = 0;
  const off = subscribePackStatus(() => told++);
  expect(await loadPackNow("/packs/lpn-a/1.0.0")).toEqual({ loaded: "lpn-a" });
  // The spy ran: the union's builders were used and the old refusal was forgotten. The commit schedules the
  // re-resolve, so the status listeners were not told a second time.
  expect(seen).toBe(1);
  expect(packStatus().loaded["lpn-a"]).toEqual(["protocol"]);
  expect(packStatus().refused["lpn-a"]).toBe(undefined);
  expect(told).toBe(0);
  off();
  delete g.__rp_packLoadNow;
  delete g.__rp_packList;
});

test("a pack the host loaded that this module set refuses answers the refusal", async () => {
  g.__rp_packLoadNow = () => JSON.stringify({ loaded: "lpn-b" });
  g.__rp_packList = () => JSON.stringify([dataPack("lpn-b")]);
  let told = 0;
  const off = subscribePackStatus(() => told++);
  const answer = await loadPackNow("/packs/lpn-b/1.0.0", {
    protocol: () => {
      throw new Error("its protocol half is not right");
    },
  });
  expect(answer).toEqual({ refused: "its protocol half is not right" });
  expect(told).toBe(1);
  off();
  delete g.__rp_packLoadNow;
  delete g.__rp_packList;
});
