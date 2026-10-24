// RETROPLUG_DETERMINISTIC=1 (set by the test runner for every parity file) powers each core on in a
// fixed state: SameBoy's GB_random() returns 0, and Mesen NES RAM powers on as all zeros. Without it
// two boots of the same ROM in one process disagree about work RAM, because SameBoy's generator advances
// between them and Mesen draws fresh random bytes.
import { test, expect } from "../../testing/harness";
import { bootSession } from "../../src/bootSession";

declare const __REPO_RESOURCES_DIR__: string;

const session = bootSession();

function bootAndReadRam(load: () => number | null): Uint8Array {
  const id = load();
  expect(typeof id, "the system builds").toBe("number");
  session.audio.renderAudio(100);
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
  expect(bytesDiffering(a, b)).toBe(0);
});

test("two Mesen NES boots power on with identical work RAM", () => {
  const nes = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
  const a = bootAndReadRam(() => session.project.systems.addSystem(nes));
  const b = bootAndReadRam(() => session.project.systems.addSystem(nes));
  expect(a.length).toBeGreaterThan(0);
  expect(bytesDiffering(a, b)).toBe(0);
});
