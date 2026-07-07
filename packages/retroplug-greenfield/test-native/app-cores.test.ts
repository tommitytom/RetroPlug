// Phase 1b: the NES + GBA backends construct and boot a live core through the stores. The milestone
// signal is a PUBLISHED FRAMEBUFFER, not audio — Engine::screenshot returns true only once the core
// has rendered ≥1 frame (FrameBufferTriple.readInto fails while seq==0), so screenshot()==true after
// a render is a genuine "the emulation + render loop advanced" check that needs no input. (NES is
// silent until MIDI-driven and the host has no NES-MIDI path yet; Nanoloop GBA doesn't auto-play —
// so audio can't prove liveness here.) Each ROM is guarded with fileExists so a resource-less CI skips.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __RESOURCES_DIR__: string;
declare const __REPO_RESOURCES_DIR__: string;

// n8-midi.nes is committed in-repo; the GBA ROM lives only in the sibling resources tree.
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const GBA = __RESOURCES_DIR__ + "/roms/nanoloop287d.gba";

// Boot `rom` through the stores, advance the core, and assert its framebuffer published (the core is
// live and rendering). No DSP kernel needed — this is a construct/boot/render proof, not a role test.
function bootsAndRenders(rom: string, expectPlatform: string, warmupMs: number, pngName: string): void {
  const be = createRealBackend();
  if (!be.fileExists(rom)) {
    console.log(`# SKIP app-cores: ROM not found at ${rom}`);
    return;
  }
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  const res = project.systems.loadRom(rom);
  expect(res != null && "system" in res).toBeTruthy(); // a real system id, not a deferred project
  const id = (res as { system: number }).system;
  expect(project.systems.view()[0].platform).toBe(expectPlatform); // classified + routed to the right core

  audio.renderAudio(warmupMs); // advance the CPU + PPU/renderer
  const published = audio.screenshot(id, `/tmp/app-cores_${pngName}.png`);
  console.log(`[app-cores] ${expectPlatform} published=${published}`);
  expect(published).toBeTruthy(); // ≥1 frame rendered → the backend built a live, running core
}

test("the NES backend boots a live Mesen core through the stores (framebuffer liveness)", () => {
  bootsAndRenders(NES, "nes", 500, "nes"); // NES has no boot screen — renders immediately
});

test("the GBA backend boots a live Mesen core (HLE BIOS) through the stores (framebuffer liveness)", () => {
  bootsAndRenders(GBA, "gba", 1000, "gba"); // HLE boot; a beat to settle into the ROM
});
