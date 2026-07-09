// C2: the "mesen" system-role knobs (NES Region + Remove Sprite Limit) reach a REAL Mesen core.
// Two native paths: (1) a non-default region seeded via the construct settings blob boots the core
// (configureNes consumes it before LoadRom); (2) live setRoleConfig edits cross applyRoleConfig →
// Engine::applyConfigField → the setters — the live sprite-limit toggle and the region change (which
// forces emu_->Reset()) both leave the core alive and rendering. (Honest scope: this proves the
// round-trip reaches the core and survives both apply modes; the knobs' visible/timing effect isn't
// asserted headlessly — the TS mock tests carry the semantics.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

test("a NES core boots with a non-default region seeded from its role config (construct blob)", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  // adopt a saved NES system whose mesen role carries region PAL (2) → constructSystem forwards it as
  // the settings blob → MesenBackend decodes it → configureNes applies it before LoadRom.
  project.systems.adopt({ romPath: NES, roles: [{ kind: "mesen", config: { region: 2, removeSpriteLimit: false } }] });
  const v = project.systems.view()[0];
  expect(v.platform).toBe("nes");
  expect(v.roles.find((r) => r.kind === "mesen")?.config.region).toBe(2);

  audio.renderAudio(500);
  expect(audio.screenshot(v.id, "/tmp/app-mesen-settings_construct.png")).toBeTruthy(); // booted + rendered
});

test("live NES knob edits reach the core: sprite-limit (live) + region (resets) both keep it rendering", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  const id = (project.systems.loadRom(NES) as { system: number }).system;
  audio.renderAudio(500);
  expect(audio.screenshot(id, "/tmp/app-mesen-settings_boot.png")).toBeTruthy();

  // Live: mutates GetNesConfig().RemoveSpriteLimit, no reset.
  expect(project.systems.setRoleConfig(id, "mesen", { removeSpriteLimit: true })).toBeTruthy();
  audio.renderAudio(200);
  expect(audio.screenshot(id, "/tmp/app-mesen-settings_sprite.png")).toBeTruthy();

  // Region change forces emu_->Reset() (RunFrame is bypassed). The core reboots and must render again.
  expect(project.systems.setRoleConfig(id, "mesen", { region: 2 })).toBeTruthy(); // PAL
  audio.renderAudio(500);
  expect(audio.screenshot(id, "/tmp/app-mesen-settings_region.png")).toBeTruthy(); // survived the reset

  // Re-sending the same region is a no-op (value-guarded — no spurious reset).
  expect(project.systems.setRoleConfig(id, "mesen", { region: 2 })).toBeTruthy();
  audio.renderAudio(200);
  expect(audio.screenshot(id, "/tmp/app-mesen-settings_region2.png")).toBeTruthy();
});
