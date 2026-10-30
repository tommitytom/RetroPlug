// A zero-byte `.sav` beside a ROM is a fresh cartridge, not a load failure. The store sends a seed by path whenever the
// file exists, and a file that opens and is empty is an empty payload, which is a resource (a blank battery) as a
// zero-length byte block is; only a file that cannot be opened is refused (docs/modular-graph-phase-c3-design.md
// sections 4 and 10). Before that rule, every verb that boots through the `.sav` path - add, a reset whose readSram
// answers nothing, a project load's adopt - failed on the ROM beside such a file.
//
// Run with: `pnpm test:native app-empty-sav`.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import type { Backend } from "../src/backend";
import { createAudioDriver, type AudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

function session(): { be: Backend; audio: AudioDriver; project: ProjectStore } {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  // A rebuild flushes the battery it replaces to the cart's sibling .sav: nothing here may write to the 0-byte file
  // this case is about (the offline render's own setting, setBatteryFlush).
  project.systems.setBatteryFlush(false);
  return { be, audio: createAudioDriver(project.graph), project };
}

const ROM = "/tmp/rp-empty-sav.nes";
const SAV = "/tmp/rp-empty-sav.sav";
const PROJECT = "/tmp/rp-empty-sav.rplg";

test("a zero-byte .sav beside a ROM boots a fresh cartridge: add, play and reset", () => {
  const { be, audio, project } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  be.writeFileAtomic(ROM, be.readFile(NES)!);
  be.writeFileAtomic(SAV, new Uint8Array(0));
  expect(be.fileExists(SAV)).toBeTruthy();
  const store = project.systems;
  // The store sends the battery by path because the file is there; the engine reads it, finds it empty and makes an
  // empty resource, so the cart boots at a blank battery instead of the boot failing and the add answering null.
  const id = store.addSystem(ROM)!;
  expect(typeof id).toBe("number");
  expect(store.systems().map((e) => e.id)).toEqual([id]);

  const play = (): number => {
    audio.renderAudio(1500); // the ROM's boot
    audio.stageMidiIn([0x90, 48, 127]);
    audio.renderAudio(200);
    audio.stageMidiIn([0x90, 48, 127]);
    return rms(audio.renderAudio(1000));
  };
  const level = play();
  console.log(`[empty-sav] a 0-byte .sav: rms ${level.toFixed(5)}`);
  expect(level > 0.001).toBeTruthy(); // the ROM plays, so it booted

  // A reset boots it again: its readSram answers nothing for a cart that has no battery, so the seed falls back to
  // the same 0-byte file, and the rebuild must boot rather than fail.
  const reset = store.reset(id)!;
  expect(typeof reset).toBe("number");
  expect(reset === id).toBe(false);
  expect(play() > 0.001).toBeTruthy();
  store.removeSystem(reset);
  be.deleteFile(ROM);
  be.deleteFile(SAV);
});

test("a project naming a ROM with a zero-byte .sav loads whole", () => {
  const { be } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  be.writeFileAtomic(ROM, be.readFile(NES)!);
  be.writeFileAtomic(SAV, new Uint8Array(0));
  // A thin project (paths, no blobs), so the load boots from the saved path and the 0-byte `.sav` beside it is the
  // seed it reads: the same put the add above makes.
  const systems = [{ platform: "nes", core: "mesen", romPath: ROM, roles: [{ kind: "mesen", config: {} }] }];
  be.writeFileAtomic(PROJECT, new TextEncoder().encode(JSON.stringify({ schemaVersion: "4", settings: {}, systems })));
  const fresh = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  fresh.systems.setBatteryFlush(false);
  const outcome = fresh.load(PROJECT);
  console.log(`[empty-sav] a project holding it: ${JSON.stringify(outcome)}`);
  expect(outcome).toEqual({ kind: "loaded", systems: 1 });
  expect(fresh.systems.systems().length).toBe(1);
  expect(be.readState(fresh.systems.systems()[0].id) !== null).toBe(true);
  be.deleteFile(ROM);
  be.deleteFile(SAV);
  be.deleteFile(PROJECT);
});
