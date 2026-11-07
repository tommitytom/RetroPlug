// backend.stageNodeMidi - control MIDI aimed at ONE node of the document, delivered straight to its core's
// ingress and NEVER through the routing node.
//
// The bypass IS the feature, so that is what this proves rather than mere delivery. Two identical BlipToaster
// carts boot side by side; a theme CC goes to the first only; the first's screen changes and the second's does
// not. Through the musical path neither outcome would hold: the project's default routing is `sendToAll` (both
// carts would change), and CC 16 is global on the cart (any channel), so no channel trick could have separated
// them either. That leaves the direct ingress as the only way a UI knob can address the cart whose menu it is.
//
// Runs on the real Mesen core through the real backend, and SKIPS when the BlipToaster ROM isn't built.
import { test, expect, skip } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { directGraph } from "../src/graph/directGraph";

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

declare const __REPO_RESOURCES_DIR__: string;
const ROM = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
// The cart's rig message (F0 7D 42 03 …): baseCh, ppu, curve, theme, font. Channel-less BY DESIGN, so no
// channel trick could have separated two instances - only the per-system inject can.
const sxSettings = (theme: number): number[] => [0xf0, 0x7d, 0x42, 0x03, 0, 1, 0, theme, 0, 0xf7];

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

test("stageNodeMidi reaches the system node it names, and only that one", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(ROM)) skip(`stageNodeMidi: no ROM at ${ROM}`);
  const spec = { romPath: ROM, platform: "nes" as const, core: "mesen" as const, embeddedRom: "", savPath: null, statePath: null };
  expect(graph.add(spec, 1)).toBeTruthy();
  expect(graph.add(spec, 2)).toBeTruthy();

  // Same ROM, same start, no input: the two cores run in lockstep, so any later difference is the message's doing.
  audio.renderAudio(WARMUP_MS);
  const before1 = framePixels(be, 1)!;
  const before2 = framePixels(be, 2)!;
  expect(before1.length > 0).toBeTruthy();
  expect(same(before1, before2)).toBeTruthy();

  // Theme 9 (MONO) on instance 1 only. Accepted optimistically - it is queued to the audio thread. Ten bytes,
  // so this also covers the OWNING-payload half of the command: a rig block does not fit inline.
  expect(be.stageNodeMidi("sys-1", sxSettings(9))).toBeTruthy();
  audio.renderAudio(400);

  const after1 = framePixels(be, 1)!;
  const after2 = framePixels(be, 2)!;
  expect(same(after1, before1)).toBe(false); // it landed: instance 1 repainted in the new theme
  expect(same(after2, before2)).toBeTruthy(); // and stayed off instance 2 entirely
  console.log(`[stageNodeMidi] rig SysEx (theme 9) changed instance 1's screen, left instance 2 byte-identical`);

  // The one guard left: nothing to deliver. There is no upper bound any more - the raw ingress has no cap,
  // which is the whole reason a SysEx can get through at all.
  expect(be.stageNodeMidi("sys-1", [])).toBe(false);
  expect(be.stageNodeMidi("sys-4242", sxSettings(3))).toBeTruthy(); // queued optimistically; no such node
  // A non-empty message to an id the plan places returns true whatever the node then does with it, so this is a smoke
  // call, not an expectation: the cart node the document places for system 1 (its "drive the real cart" setting
  // defaults on) takes the same door. With no cart attached in this host the link drops it, a device's business.
  be.stageNodeMidi("cart-1", sxSettings(3));

  graph.remove(1);
  graph.remove(2);
});
