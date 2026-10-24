// RETROPLUG_DETERMINISTIC=1 (set by the test runner for every parity file) powers each core on in a
// fixed state: SameBoy's GB_random() returns 0, Mesen NES RAM powers on as all zeros, and the SMS VDP
// starts at scanline 0. Without it two boots of the same ROM in one process disagree about work RAM,
// because SameBoy's generator advances between them and Mesen draws fresh random bytes; and about when an
// SMS song plays, because the VDP's random start shifts every frame against the audio. A boot is silent,
// so that last one only shows once something sounds.
import { test, expect } from "../../testing/harness";
import { bootSession } from "../../src/bootSession";
import { Rig, even } from "./rig";
import { compareAudio, laneSounds, type CaseDigest } from "./digest";
import { frames, smsggdj, smsStart } from "./scenes";

declare const __REPO_RESOURCES_DIR__: string;

const session = bootSession();

// ms is how long the core runs before RAM is read: the NES ROM clears most of its RAM within a few ms, so the
// NES case reads almost at once, while the randomised power-on image is still mostly there.
function bootAndReadRam(load: () => number | null, ms = 100): Uint8Array {
  const id = load();
  expect(typeof id, "the system builds").toBe("number");
  session.audio.renderAudio(ms);
  const ram = session.project.systems.readRam(id as number);
  expect(ram != null, "work RAM is published").toBeTruthy();
  session.project.systems.removeSystem(id as number);
  return ram as Uint8Array;
}

// Counted rather than compared whole, so a failure says how far apart the boots are instead of printing
// both RAM images.
function bytesDiffering(a: Uint8Array, b: Uint8Array): number {
  let n = Math.abs(a.length - b.length);
  for (let i = 0; i < Math.min(a.length, b.length); i++) if (a[i] !== b[i]) n++;
  return n;
}

test("two SameBoy boots power on with identical work RAM", () => {
  const a = bootAndReadRam(() => session.project.systems.loadMgb());
  const b = bootAndReadRam(() => session.project.systems.loadMgb());
  expect(a.length).toBeGreaterThan(0);
  expect(bytesDiffering(a, b), "needs RETROPLUG_DETERMINISTIC=1 (the runner sets it for parity/ files)").toBe(0);
});

test("two Mesen NES boots power on with identical work RAM", () => {
  const nes = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
  const a = bootAndReadRam(() => session.project.systems.addSystem(nes), 1);
  const b = bootAndReadRam(() => session.project.systems.addSystem(nes), 1);
  expect(a.length).toBeGreaterThan(0);
  expect(bytesDiffering(a, b), "needs RETROPLUG_DETERMINISTIC=1 (the runner sets it for parity/ files)").toBe(0);
});

test("two smsggdj plays render identical audio", () => {
  session.project.newProject(); // the Rig below runs its own control plane over the same engine
  const rig = new Rig();
  const play = (): CaseDigest => {
    rig.reset(44100);
    smsggdj(rig, "sms", false);
    const blocks = (n: number) => even(n, 1024);
    const segments = [];
    for (const seg of [...smsStart(44100, blocks), { blocks: blocks(frames(44100, 1000)) }]) {
      segments.push(rig.render(2, seg).digest);
      seg.after?.(rig);
    }
    return { segments, midiOut: { count: 0, fnv: "" }, sram: {}, ram: {} };
  };
  const a = play();
  expect(laneSounds(a, 0), "the song plays").toBeTruthy();
  expect(compareAudio(a, play())).toEqual([]);
});
