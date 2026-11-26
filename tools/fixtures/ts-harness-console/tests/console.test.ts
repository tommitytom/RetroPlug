// A session builds systems on consoles a product links or loads as a pack (docs/downloadable-packs-cli-plan.md): the
// BlipToaster cart on the NES (Mesen's console, BlipToaster's roles) and smsggdj on the SMS. Run by
// tools/run-cli-ts-tests.sh section 9 against the desktop CLI and, in its pack mode, against one that links neither, so
// a session child that loaded no pack fails here. RP_NES_ROM and RP_SMS_ROM name the ROMs.
import { test, expect, bootSession } from "retroplug-cli";

declare const tjs: { env: Record<string, string | undefined> };

const s = bootSession();

type ConsoleView = { platform: string; core: string; kinds: string[] };

/** Boot `rom`, hand back its view, and remove the system once `check` has read it. */
function booted(rom: string | undefined, check: (v: ConsoleView) => void): void {
  const loaded = s.project.systems.loadRom(rom ?? "");
  if (loaded === null || !("system" in loaded)) throw new Error(`${rom} did not load: ${JSON.stringify(loaded)}`);
  try {
    const view = s.project.systems.view().find((v) => v.id === loaded.system);
    if (!view) throw new Error(`${rom}: no system ${loaded.system} in the view`);
    s.audio.renderAudio(200);
    check({ platform: view.platform, core: view.core, kinds: view.roles.map((r) => r.kind) });
  } finally {
    s.project.systems.removeSystem(loaded.system);
  }
}

test("the BlipToaster cart boots on the NES with BlipToaster's roles", () => {
  booted(tjs.env.RP_NES_ROM, (v) => {
    expect(v.platform).toBe("nes");
    expect(v.core).toBe("mesen");
    expect(v.kinds.includes("bliptoaster")).toBe(true);
    expect(v.kinds.includes("bliptoaster-assets")).toBe(true);
  });
});

test("smsggdj boots on the SMS", () => {
  booted(tjs.env.RP_SMS_ROM, (v) => {
    expect(v.platform).toBe("sms");
    expect(v.core).toBe("mesen");
    expect(v.kinds.includes("sms-sync")).toBe(true);
  });
});
