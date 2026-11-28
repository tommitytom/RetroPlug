// The live load on a real host (docs/downloadable-packs-m3-design.md section 7): run by `run-native-tests.mjs --packs`
// on retroplug-host-packs with no pack on the path (the pack-less folder). The render fixture pack gives every ROM its
// tsfix-render role, so a system booted after the load carries it, and one booted before does not.
import { test, expect } from "@retroplug/testing";
import { bootSession } from "../../src/bootSession";
import { loadPackNow } from "../../src/packs/loadPackNow";
import { packStatus } from "../../src/packs/loadPacks";

declare const __MGB_ROM__: string;
declare const __PACKS_BUILD_DIR__: string;

test("a pack loaded live reaches the next system, and a second load of its folder answers the same", async () => {
  const s = bootSession();
  const kinds = (): string[] => {
    const loaded = s.project.systems.loadRom(__MGB_ROM__);
    if (loaded === null || !("system" in loaded)) throw new Error(`mGB did not load: ${JSON.stringify(loaded)}`);
    const view = s.project.systems.view().find((v) => v.id === loaded.system);
    s.project.systems.removeSystem(loaded.system);
    return view?.roles.map((r) => r.kind) ?? [];
  };
  expect(kinds().includes("tsfix-render")).toBe(false);
  const dir = `${__PACKS_BUILD_DIR__}/tsfix-render/1.0.0`;
  expect(await loadPackNow(dir)).toEqual({ loaded: "tsfix-render" });
  expect(packStatus().loaded["tsfix-render"]).toEqual(["app"]);
  expect(kinds().includes("tsfix-render")).toBe(true);
  expect(await loadPackNow(dir)).toEqual({ loaded: "tsfix-render" });
  const nowhere = await loadPackNow(`${__PACKS_BUILD_DIR__}/no-such-pack/1.0.0`);
  expect("refused" in nowhere).toBe(true);
  s.project.newProject();
});
