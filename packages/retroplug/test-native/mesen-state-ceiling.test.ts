// Regression: a Mesen core's published savestate must keep tracking the machine as it runs, instead of
// freezing on the snapshot taken at boot.
//
// Mesen savestates are zlib-deflated, so their size follows the machine's ENTROPY, not its shape. Every
// Mesen backend used to size its snapshot slot as `measured + measured/2 + 8192` against a state captured
// at construct - a console that has booted but not yet run, whose RAM is nearly all zeros. On a GBA that
// boot state compresses to ~27 KB while normal play reaches ~98 KB, so the slot came out ~19.5 KB and
// every publish after that was skipped: readState/readSram froze on the boot snapshot for the life of the
// system. (NES and SMS escaped only because their states barely compress, so the 1.5x factor covered them.)
//
// The ceiling is now measured from an UNCOMPRESSED serialize, which is entropy-independent - see
// mesenStateCeiling. These tests assert the observable consequence: the published state changes as the
// core runs.
//
// COVERAGE LIMIT: the GBA leg is the one that actually reproduces the bug, and it needs a GBA ROM that is
// not in this repo, so it SKIPS on a clean checkout and in CI. The NES leg runs everywhere but would have
// passed before the fix too - it guards against a future regression on the core that is always available.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";

declare const __RESOURCES_DIR__: string;
declare const __REPO_RESOURCES_DIR__: string;

const GBA = __RESOURCES_DIR__ + "/roms/nanoloop287d.gba";
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

const eq = (a: Uint8Array, b: Uint8Array): boolean => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};

test("GBA: the published savestate tracks the running core, never freezing on the boot snapshot", () => {
  const be = createRealBackend();
  if (!be.fileExists(GBA)) {
    console.log(`# SKIP mesen-state-ceiling GBA leg: no GBA ROM at ${GBA}`);
    return;
  }
  const audio = createAudioDriver();
  const id = 801;
  expect(be.constructSystem({
    romPath: GBA, platform: "gba", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null,
  }, id)).toBeTruthy();

  audio.renderAudio(600);
  const boot = be.readState(id)!;
  expect(boot.length > 0).toBeTruthy();

  // Long enough for the game to fill RAM/VRAM, which is what makes the state compress worse and outgrow
  // the old ceiling. Before the fix every publish from here on was skipped.
  audio.renderAudio(20000);
  const warm = be.readState(id)!;

  console.log(`[mesen-state-ceiling] gba boot=${boot.length} warm=${warm.length}`);
  expect(eq(warm, boot)).toBeFalsy();            // a frozen slot hands back the identical blob
  expect(warm.length > boot.length).toBeTruthy(); // ...and a populated console deflates worse than a blank one

  be.removeSystem(id);
});

test("NES: the published savestate tracks the running core", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const id = 802;
  expect(be.constructSystem({
    romPath: NES, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null,
  }, id)).toBeTruthy();

  audio.renderAudio(600);
  const boot = be.readState(id)!;
  expect(boot.length > 0).toBeTruthy();

  audio.renderAudio(4000);
  const warm = be.readState(id)!;
  console.log(`[mesen-state-ceiling] nes boot=${boot.length} warm=${warm.length}`);
  expect(eq(warm, boot)).toBeFalsy();

  be.removeSystem(id);
});
