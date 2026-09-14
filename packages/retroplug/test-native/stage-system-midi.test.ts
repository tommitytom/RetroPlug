// backend.stageSystemMidi — control MIDI aimed at ONE system, delivered straight to its ingress and NEVER
// through the routing kernel.
//
// The bypass IS the feature, so that is what this proves rather than mere delivery. Two identical BlipToaster
// carts boot side by side; a theme CC goes to the first only; the first's screen changes and the second's does
// not. Through the musical path neither outcome would hold: the project's default routing is `sendToAll` (both
// carts would change), and CC 16 is global on the cart (any channel), so no channel trick could have separated
// them either. That leaves the direct ingress as the only way a UI knob can address the cart whose menu it is.
//
// Runs on the real Mesen core through the real backend, and SKIPS when the BlipToaster ROM isn't built.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";

const ROM = "/workspaces/bliptoaster/build/bliptoaster.nes";
const CC_UI_THEME = 16; // global on the cart: any channel, so the two instances cannot be told apart by channel

/** The cart polls its FIFO from the main loop, and boots for ~1s before it gets there. */
const WARMUP_MS = 1300;

function framePixels(be: ReturnType<typeof createRealBackend>, id: number): Uint8Array | null {
  const f = be.getFrame(id);
  return f ? new Uint8Array(f.pixels) : null;
}
const same = (a: Uint8Array, b: Uint8Array): boolean => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};

test("stageSystemMidi reaches the system it names, and only that one", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP stageSystemMidi: no ROM at ${ROM}`);
    return;
  }
  const spec = { romPath: ROM, platform: "nes" as const, core: "mesen" as const, embeddedRom: "", savPath: null, statePath: null };
  expect(be.constructSystem(spec, 1)).toBeTruthy();
  expect(be.constructSystem(spec, 2)).toBeTruthy();

  // Same ROM, same start, no input: the two cores run in lockstep, so any later difference is the CC's doing.
  audio.renderAudio(WARMUP_MS);
  const before1 = framePixels(be, 1)!;
  const before2 = framePixels(be, 2)!;
  expect(before1.length > 0).toBeTruthy();
  expect(same(before1, before2)).toBeTruthy();

  // Theme 9 (MONO) on instance 1 only. Accepted optimistically - it is queued to the audio thread.
  expect(be.stageSystemMidi(1, [0xb0, CC_UI_THEME, 9])).toBeTruthy();
  audio.renderAudio(400);

  const after1 = framePixels(be, 1)!;
  const after2 = framePixels(be, 2)!;
  expect(same(after1, before1)).toBe(false); // the CC landed: instance 1 repainted in the new theme
  expect(same(after2, before2)).toBeTruthy(); // and stayed off instance 2 entirely
  console.log(`[stageSystemMidi] CC ${CC_UI_THEME}=9 changed instance 1's screen, left instance 2 byte-identical`);

  // The guards, on the same live backend: nothing to deliver, and more than one channel message.
  expect(be.stageSystemMidi(1, [])).toBe(false);
  expect(be.stageSystemMidi(1, [0xb0, CC_UI_THEME, 9, 0, 0])).toBe(false);

  be.removeSystem(1);
  be.removeSystem(2);
});
