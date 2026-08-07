// Phase 1b: the NES + GBA + SMS/GG backends construct and boot a live core through the stores. The
// milestone signal is a PUBLISHED FRAMEBUFFER, not audio - getFrame(id).published flips true only once
// the core has rendered ≥1 frame (the FrameBufferTriple stays unpublished while seq==0), so
// published==true after a render is a genuine "the emulation + render loop advanced" check that needs
// no input. (NES is silent until MIDI-driven and the host has no NES-MIDI path yet; Nanoloop GBA
// doesn't auto-play; an idle smsggdj plays nothing - so audio can't prove liveness here.) Each ROM is
// guarded with fileExists so a resource-less CI skips.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __RESOURCES_DIR__: string;
declare const __REPO_RESOURCES_DIR__: string;

// n8-midi.nes + the smsggdj pair are committed in-repo; the GBA ROM lives only in the sibling
// resources tree.
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const GBA = __RESOURCES_DIR__ + "/roms/nanoloop287d.gba";
const SMS = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const GG = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg";

// Boot `rom` through the stores, advance the core, and assert its framebuffer published (the core is
// live and rendering). Liveness is the published frame itself (getFrame, in-memory) — no screenshot /
// file write, so it can't be confused with a failed PNG write. No DSP kernel needed — this is a
// construct/boot/render proof, not a role test.
//
// `size` additionally pins the VISIBLE resolution. Worth asserting because Mesen's real per-console
// overscan defaults live in the .NET UI this build doesn't compile, so each system sets its own - and
// MesenVideoDevice min-clamps rather than failing, which would turn a drifted constant into a silently
// cropped picture instead of a test failure.
function bootsAndRenders(
  rom: string,
  expectPlatform: string,
  warmupMs: number,
  size?: { width: number; height: number },
): void {
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
  const frame = be.getFrame(id);
  const published = frame?.published ?? false; // ≥1 published frame == the core rendered
  console.log(`[app-cores] ${expectPlatform} published=${published} ${frame?.width}x${frame?.height}`);
  expect(published).toBeTruthy(); // ≥1 frame rendered → the backend built a live, running core
  if (size) {
    expect(frame?.width).toBe(size.width);
    expect(frame?.height).toBe(size.height);
  }
}

test("the NES backend boots a live Mesen core through the stores (framebuffer liveness)", () => {
  bootsAndRenders(NES, "nes", 500); // NES has no boot screen — renders immediately
});

test("the GBA backend boots a live Mesen core (HLE BIOS) through the stores (framebuffer liveness)", () => {
  bootsAndRenders(GBA, "gba", 1000); // HLE boot; a beat to settle into the ROM
});

test("the SMS backend boots a live Mesen core through the stores (framebuffer liveness)", () => {
  bootsAndRenders(SMS, "sms", 1000, { width: 256, height: 192 });
});

test("the Game Gear backend picks the GG machine, not SMS (geometry is the proof)", () => {
  // The ONLY end-to-end check that the .gg path doesn't quietly fall through to Master System. Both
  // machines share one system class and one Z80, so a wrong choice runs perfectly and sounds fine - the
  // visible resolution is what gives it away. The machine reaches Mesen as the STAGED FILE'S EXTENSION
  // (SmsConsole::LoadRom reads nothing else), so this covers the whole platform → gameGear → stageRom
  // chain in one assertion.
  bootsAndRenders(GG, "gg", 1000, { width: 160, height: 144 });
});
