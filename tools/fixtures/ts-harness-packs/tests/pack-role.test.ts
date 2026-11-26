// A session file sees the launch packs (docs/downloadable-packs-cli-plan.md): the SDK commits their app halves before
// the body runs. The render fixture pack (packages/native/test/packs/ts-fixture/render) gives every ROM its
// tsfix-render role, so a system booted here carries it only if this session loaded the pack. Run by
// tools/run-cli-ts-tests.sh section 8 with that pack on RETROPLUG_PACK_PATH and RP_GB_ROM naming mGB.
import { test, expect, bootSession } from "retroplug-cli";

declare const tjs: { env: Record<string, string | undefined> };

test("a session's system carries the role a launch pack gives every ROM", () => {
  const s = bootSession();
  const loaded = s.project.systems.loadRom(tjs.env.RP_GB_ROM ?? "");
  if (loaded === null || !("system" in loaded)) throw new Error(`the ROM did not load: ${JSON.stringify(loaded)}`);
  const view = s.project.systems.view().find((v) => v.id === loaded.system);
  expect(view?.roles.map((r) => r.kind).includes("tsfix-render")).toBe(true);
  s.project.systems.removeSystem(loaded.system);
});
