// M3 end to end (docs/downloadable-packs-m3-design.md section 9), on retroplug-host-packs with no pack on the path, so
// the host links neither Mesen nor BlipToaster: a project with a BlipToaster NES system, a Game Boy system and a system
// on a pack refused for its API level opens with the Game Boy running and the other two placeholders; it saves to
// identical bytes; Mesen then BlipToaster loaded live bring the NES system up with its ROM and roles, and it renders;
// the API-level fixture gives the refused reason; and a role whose pack the Engine refuses is left out while its
// system runs.
import { test, expect } from "@retroplug/testing";
import { bootSession } from "../../src/bootSession";
import { loadPackNow } from "../../src/packs/loadPackNow";
import { packStatus } from "../../src/packs/loadPacks";

declare const __CONFIG_DIR__: string;
declare const __REPO_RESOURCES_DIR__: string;
declare const __MESEN_PACK__: string;
declare const __BLIPTOASTER_PACK__: string;
declare const __PACKS_BUILD_DIR__: string;

const versionOf = (dir: string): string => dir.slice(dir.lastIndexOf("/") + 1);

test("a project naming missing packs opens, saves back unchanged, and comes alive as the packs arrive", async () => {
  const s = bootSession();
  const be = s.backend;
  const MESEN = { id: "mesen", name: "Mesen", version: versionOf(__MESEN_PACK__) };
  const BLIP = { id: "bliptoaster", name: "BlipToaster", version: versionOf(__BLIPTOASTER_PACK__) };
  const PROTO_KIND = { id: "tsfix-proto-kind", name: "TS fixture variant tsfix-proto-kind", version: "1.0.0" };
  const OLD = { id: "packfixture-api-older", name: "Pack fixture (packfixture-api-older)", version: "1.0.0" };
  const nes = {
    platform: "nes",
    core: "mesen",
    romPath: `${__REPO_RESOURCES_DIR__}/roms/bliptoaster.nes`,
    roles: [
      { kind: "mesen", config: {} },
      { kind: "nes-n8-midi", config: {} },
      { kind: "bliptoaster", config: {} },
      { kind: "bliptoaster-assets", config: { overrides: [] } },
    ],
    provenance: { platform: MESEN, roles: { mesen: MESEN, bliptoaster: BLIP, "bliptoaster-assets": BLIP } },
  };
  const old = {
    platform: "m3-old",
    core: "m3-old",
    romPath: `${__REPO_RESOURCES_DIR__}/roms/bliptoaster.nes`,
    provenance: { platform: OLD },
  };
  const gb = {
    platform: "gb",
    core: "sameboy",
    embeddedRom: "mgb",
    roles: [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }, { kind: "tsfix-proto-kind", config: {} }],
    provenance: { roles: { "tsfix-proto-kind": PROTO_KIND } },
  };
  const path = `${__CONFIG_DIR__}/m3.rplg`;
  const doc = { schemaVersion: "4", settings: {}, systems: [gb, nes, old] };
  expect(be.writeFile(path, new TextEncoder().encode(JSON.stringify(doc)))).toBe(true);

  // It opens: the Game Boy runs, the other two are placeholders, and the outcome names every pack it needs.
  const outcome = s.project.load(path);
  expect(outcome.kind).toBe("loaded");
  if (outcome.kind !== "loaded") return;
  expect(outcome.systems).toBe(1);
  expect(outcome.missingPacks?.map((p) => p.id)).toEqual([
    "tsfix-proto-kind",
    "mesen",
    "bliptoaster",
    "packfixture-api-older",
  ]);
  expect(outcome.missingPacks?.map((p) => [p.id, p.status, p.systems, p.roles])).toEqual([
    ["tsfix-proto-kind", "missing", 0, 1],
    ["mesen", "missing", 1, 0],
    ["bliptoaster", "missing", 1, 0],
    ["packfixture-api-older", "missing", 1, 0],
  ]);
  const opened = s.project.systems.slotView();
  expect(opened.map((v) => v.placeholder !== undefined)).toEqual([false, true, true]);
  s.audio.renderAudio(200);
  expect(be.readState(opened[0].id) !== null).toBe(true);
  expect(s.project.isDirty()).toBe(false);

  // It saves to identical bytes, the placeholders as written.
  expect(s.project.save(`${__CONFIG_DIR__}/m3-one.rplg`)).toBe(true);
  expect(s.project.load(`${__CONFIG_DIR__}/m3-one.rplg`).kind).toBe("loaded");
  expect(s.project.save(`${__CONFIG_DIR__}/m3-two.rplg`)).toBe(true);
  const one = new TextDecoder().decode(be.readFile(`${__CONFIG_DIR__}/m3-one.rplg`)!);
  expect(new TextDecoder().decode(be.readFile(`${__CONFIG_DIR__}/m3-two.rplg`)!)).toBe(one);
  const saved = JSON.parse(one) as { systems: unknown[] };
  expect(saved.systems[1]).toEqual(nes);
  expect(saved.systems[2]).toEqual(old);
  const ids = s.project.systems.slotView().map((v) => v.id);

  // The API-level fixture is refused, and its placeholder says it needs updating.
  const refused = await loadPackNow(`${__PACKS_BUILD_DIR__}/packfixture-api-older/1.0.0`);
  expect("refused" in refused && refused.refused.includes("API level")).toBe(true);
  const oldNeed = s.project.systems.slotView()[2].placeholder?.needs[0];
  expect(oldNeed?.status).toBe("refused");
  expect(oldNeed?.needsUpdate).toBe(true);
  expect(oldNeed?.reason).toBe("refused" in refused ? refused.refused : undefined);

  // Mesen arrives: the NES system comes up in its slot, under its id, its roles as stored; BlipToaster's are missing.
  expect(await loadPackNow(__MESEN_PACK__)).toEqual({ loaded: "mesen" });
  let now = s.project.systems.slotView();
  expect(now[1].placeholder).toBe(undefined);
  expect(now[1].id).toBe(ids[1]);
  expect(now[1].platform).toBe("nes");
  expect(now[1].romPath).toBe(nes.romPath);
  expect(now[1].roles.map((r) => r.kind)).toEqual(["mesen", "nes-n8-midi", "bliptoaster", "bliptoaster-assets"]);
  expect(now[1].missingRoles?.map((n) => n.key)).toEqual(["bliptoaster", "bliptoaster-assets"]);
  expect(s.project.isDirty()).toBe(false);

  // BlipToaster arrives: nothing is missing on the NES system, and it renders.
  expect(await loadPackNow(__BLIPTOASTER_PACK__)).toEqual({ loaded: "bliptoaster" });
  now = s.project.systems.slotView();
  const nesNow = now[1].id;
  expect(now[1].missingRoles).toBe(undefined);
  expect(now[1].roles.map((r) => r.kind)).toEqual(["mesen", "nes-n8-midi", "bliptoaster", "bliptoaster-assets"]);
  s.audio.renderAudio(500);
  expect(be.readState(nesNow) !== null).toBe(true);
  expect(be.getFrame(nesNow)?.published).toBe(true);
  expect(s.project.isDirty()).toBe(false);

  // A pack every running Engine refuses (its kind items wait for a launch): its role stays missing, now refused, and
  // the Game Boy still runs.
  const gbBefore = be.readState(s.project.systems.slotView()[0].id);
  expect(gbBefore !== null).toBe(true);
  expect(await loadPackNow(`${__PACKS_BUILD_DIR__}/tsfix-proto-kind/1.0.0`)).toEqual({ loaded: "tsfix-proto-kind" });
  const gbNow = s.project.systems.slotView()[0];
  expect(gbNow.missingRoles?.map((n) => [n.key, n.status])).toEqual([["tsfix-proto-kind", "refused"]]);
  // The reason is the Engine's (its setGraph answer listed the pack), not only the planner's unknown-type.
  const engineWhy = packStatus().engine["tsfix-proto-kind"];
  expect(engineWhy !== undefined && engineWhy.length > 0).toBe(true);
  expect(gbNow.missingRoles?.[0].reason).toBe(engineWhy);
  s.audio.renderAudio(700);
  const gbAfter = be.readState(gbNow.id);
  expect(gbAfter !== null).toBe(true);
  expect(gbAfter!.join(",") !== gbBefore!.join(",")).toBe(true); // the same system kept running past the refusal
  expect(s.project.isDirty()).toBe(false);
  s.project.newProject();
});
