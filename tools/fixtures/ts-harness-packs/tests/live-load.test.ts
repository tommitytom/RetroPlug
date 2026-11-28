// A session loads a pack live: `loadPackNow` from the SDK loads the render fixture pack into this session, and the next
// system it boots carries the pack's role. Run by tools/run-cli-ts-tests.sh section 8 with RP_LIVE_PACK naming the
// pack's folder and RP_GB_ROM naming mGB.
import { test, expect, bootSession, loadPackNow } from "retroplug-cli";

declare const tjs: { env: Record<string, string | undefined> };

test("a pack loaded live reaches the next system a session boots", async () => {
  const s = bootSession();
  const kinds = (): string[] => {
    const loaded = s.project.systems.loadRom(tjs.env.RP_GB_ROM ?? "");
    if (loaded === null || !("system" in loaded)) throw new Error(`the ROM did not load: ${JSON.stringify(loaded)}`);
    const view = s.project.systems.view().find((v) => v.id === loaded.system);
    s.project.systems.removeSystem(loaded.system);
    return view?.roles.map((r) => r.kind) ?? [];
  };
  expect(kinds().includes("tsfix-render")).toBe(false);
  expect(await loadPackNow(tjs.env.RP_LIVE_PACK ?? "")).toEqual({ loaded: "tsfix-render" });
  expect(kinds().includes("tsfix-render")).toBe(true);
});
