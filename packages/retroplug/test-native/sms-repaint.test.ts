// A host-side song load has to REPAINT the cart, not just change what it plays.
//
// smsggdj redraws only the text rows flagged in `dirty_rows`, and every one of its own load paths ends
// in `mark_all_dirty` (editor.asm fpx_close, cont_load_fire). `liveLoad` reproduced `load_rebase` and
// the directory-entry metadata but not that, so a menu Load put the right song in memory and left the
// PREVIOUS one on screen - the notes changed, the display did not, until the user nudged a control.
// Reported from real use, and it is exactly the kind of defect the RAM-level tests cannot see: the
// bytes were all correct.
//
// Measuring it needs care, because the cart animates parts of its screen unprompted (a blinking glyph
// in the header, and a text row in the grid that redraws itself). Two idle samples 800 ms apart differ
// by hundreds of pixels, and that swamped the signal - a first attempt reported 702 differing bytes for
// an idle interval and 36 for a load that had done nothing at all. So build an ANIMATION MASK from
// several idle samples with no input, and compare only the pixels outside it. That is reproducible to
// the pixel across runs.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { buildSav } from "../src/smsggdj/codec/sav";
import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_SONG } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const BTN_UP = 2;
const BTN_DOWN = 3;

type Snap = { px: Uint8Array; w: number; h: number };

function snap(be: ReturnType<typeof createRealBackend>, id: number): Snap {
  const f = be.getFrame(id)!;
  expect(f.published).toBeTruthy();
  return { px: f.pixels.slice(), w: f.width, h: f.height };
}

/** Add every pixel that differs between `a` and `b` to `mask` (XRGB8888, the X byte ignored). */
function markDiff(a: Snap, b: Snap, mask: Uint8Array): void {
  for (let p = 0; p < mask.length; p++) {
    const i = p * 4;
    if (a.px[i] !== b.px[i] || a.px[i + 1] !== b.px[i + 1] || a.px[i + 2] !== b.px[i + 2]) mask[p] = 1;
  }
}

function diffOutside(a: Snap, b: Snap, mask: Uint8Array): number {
  let n = 0;
  for (let p = 0; p < mask.length; p++) {
    if (mask[p]) continue;
    const i = p * 4;
    if (a.px[i] !== b.px[i] || a.px[i + 1] !== b.px[i + 1] || a.px[i + 2] !== b.px[i + 2]) n++;
  }
  return n;
}

function nudge(audio: ReturnType<typeof createAudioDriver>, id: number, btn: number): void {
  audio.pressButton(id, btn, true);
  audio.renderAudio(100);
  audio.pressButton(id, btn, false);
  audio.renderAudio(400);
}

test("a live song load repaints the cart's screen, without the user touching anything", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-repaint: missing ${ROM}`);
    return;
  }
  const id = 90;
  // A song with a populated SONG grid, against the blank one the cart boots into (song_new), so there
  // is a large and unambiguous visual difference to look for.
  const block = buildMetronomeBlock();
  for (let row = 0; row < 8; row++) block.set([0x00, 0x01, 0x02, 0x03], P_SONG + row * 4);
  const sav = buildSav([{ block, name: "LOADED" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;

  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(5000); // boot, and let the splash-to-editor transition settle

  const before = snap(be, id);
  const mask = new Uint8Array(before.w * before.h);
  for (let i = 0; i < 6; i++) {
    audio.renderAudio(400);
    markDiff(before, snap(be, id), mask);
  }
  let animating = 0;
  for (let p = 0; p < mask.length; p++) if (mask[p]) animating++;
  const stable = snap(be, id);
  // ~250 of 49,152. If this ever approaches the whole screen the measurement below is meaningless, so
  // fail loudly rather than passing on a mask that hides everything.
  expect(animating < mask.length / 10).toBeTruthy();

  // The load, with NO input at all afterwards - the user is in the RetroPlug menu, not on the pad.
  const writes = smsggdjIntegration.liveLoad!(be.readFile(ROM)!, sav, 0)!;
  const layout = resolveSmsggdjLayout("0.45")!;
  expect(writes.some((w) => w.offset === layout.dirtyRows && w.bytes.every((b) => b === 1))).toBeTruthy();
  for (const w of writes) expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();
  audio.renderAudio(800);
  const afterLoad = snap(be, id);

  // Then force a repaint the way the user had to, returning the cursor to where it started so a load
  // that painted correctly is already this frame.
  nudge(audio, id, BTN_DOWN);
  nudge(audio, id, BTN_UP);
  const settled = snap(be, id);

  const byLoad = diffOutside(stable, afterLoad, mask);
  const byNudge = diffOutside(afterLoad, settled, mask);
  console.log(`[sms-repaint] ${animating} px animate on their own; load repainted ${byLoad} px, a forced repaint then added ${byNudge}`);

  // Measured 1072 and 2-4. Before the fix these were 0 and 271: the load moved nothing, and the nudge
  // was doing all the work - which is the whole bug, so both halves are asserted.
  expect(byLoad > 500).toBeTruthy();
  expect(byNudge < 100).toBeTruthy();
  expect(be.removeSystem(id)).toBeTruthy();
});
