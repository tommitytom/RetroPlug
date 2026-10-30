// A saved system's backend "system"-role config reaches its node's settings, in the labels the node reads
// (docs/modular-graph-phase-c3-design.md section 6): the construct-time fields among them, which are what the node
// builds its core with, so a loaded non-default model/highpass is applied AT the boot (not via a post-construct restart
// that would nuke a just-restored savestate). The store stays generic: it forwards whichever role's kind === the core.
// A fresh add sends the same defaults, which are the core's own.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { RoleRegistry } from "../../src/systemRoles";
import { registerCoreRoles } from "../../src/coreRoles";
import { gbRom, nesRom } from "../systems/fixtures";
import { canonicalJson } from "../../src/graph/canonicalJson";

// `v` as a document carries it: canonical JSON, its keys sorted.
const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));

function makeStore() {
  const be = new MockBackend("/cfg");
  const reg = new RoleRegistry();
  registerCoreRoles(reg); // the sameboy system role (model/highpass/linkGroupId/fastBoot)
  return { be, store: new SystemsStore(be, () => {}, reg) };
}

test("adopt gives the node the saved sameboy role config as its settings", () => {
  const { be, store } = makeStore();
  be.seed("/roms/a.gb", gbRom());

  // A saved system whose sameboy role carries a NON-default model (DmgB) + highpass (RemoveDcOffset).
  const roles = [{ kind: "sameboy", config: { model: "dmgB", highpass: "removeDcOffset", linkGroupId: 0, fastBoot: true } }];
  store.adopt({ romPath: "/roms/a.gb", roles });

  const id = store.view()[0].id;
  const boot = be.bootOf(id);
  expect(boot?.type).toBe("rp.sameboy.gb");
  // The settings are the stored config in the labels the node reads: the fields the file carried, and only those (a
  // field it omits leaves the node's own default, which is the core's).
  expect(canon(boot?.settings)).toEqual(canon({
    system: id,
    rom: "res:1",
    romName: "a",
    model: "dmgB",
    highpass: "removeDcOffset",
    fastBoot: true,
    serialOutCapture: "off",
  }));
});

test("adopt gives the node the saved mesen (NES) role config, its construct-time fields included", () => {
  const { be, store } = makeStore();
  be.seed("/roms/a.nes", nesRom());

  // A saved NES system whose mesen role carries a non-default region (pal) and a construct-time FIFO profile. The store
  // finds the role via r.kind === core, and a NES system's core is "mesen".
  const roles = [{ kind: "mesen", config: { region: "pal", removeSpriteLimit: true, fifo: "hardware", sdRoot: "/sd" } }];
  store.adopt({ romPath: "/roms/a.nes", roles });

  const id = store.view()[0].id;
  const boot = be.bootOf(id);
  expect(boot?.type).toBe("rp.mesen.nes");
  // The stored config's own fields, the construct-time ones among them: they are what its node builds the core with.
  expect(canon(boot?.settings)).toEqual(canon({
    system: id,
    rom: "res:1",
    romName: "a",
    region: "pal",
    removeSpriteLimit: true,
    sdRoot: "/sd",
    fifo: "hardware",
  }));
  expect("gameGear" in (boot?.settings ?? {})).toBe(false); // an SMS field: a NES node does not name it
});

test("a fresh add gives its node the role schema's defaults, which are the core's own", () => {
  const { be, store } = makeStore();
  be.seed("/roms/b.gb", gbRom());

  store.addSystem("/roms/b.gb");
  const settings = be.bootOf(store.view()[0].id)?.settings ?? {};
  // A fresh system's role is its schema's parse, whose defaults are the core's own.
  expect(settings.model).toBe("cgbC");
  expect(settings.highpass).toBe("accurate");
  expect(settings.fastBoot).toBe(true);
  expect(settings.colorCorrection).toBe("disabled");
});
