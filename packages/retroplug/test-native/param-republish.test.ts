// C2's params in the document: a document carries a param's value only when it differs from the one the node was last
// given (docs/modular-graph-phase-c2-design.md section 5), and a live setParam is one of the ways a node is given one.
// So a document repeating the value it carried before still lands when a live edit moved the core away from it. Both
// routes TS has are here: the store's mixed edit (a setting field and a NES's expansionVolume in one setRoleConfig, so
// it publishes rather than setting the param live) and a DirectGraph's setCoreConfig repeating its value.
//
// The value is watched where only the core can show it: the expansion chip's own level, 200% against a live 100%.
import { test, expect, skip } from "@retroplug/testing";
import { bootSession } from "../src/bootSession";
import { createAudioDriver } from "../src/audioDriver";
import { DirectGraph } from "../src/graph/directGraph";

declare const __REPO_RESOURCES_DIR__: string;
const VRC6 = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc6.nes";

const db = (x: number) => 20 * Math.log10(Math.max(x, 1e-12));

function rms(a: Float32Array): number {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return Math.sqrt(s / a.length);
}

test("the store's mixed edit lands the document's expansion volume, though a live edit moved the core away", () => {
  const s = bootSession();
  if (!s.backend.fileExists(VRC6)) skip("no VRC6 rom");
  const id = s.project.systems.addSystem(VRC6);
  if (id == null) throw new Error("the VRC6 did not build");
  s.audio.renderAudio(1500); // boot
  // A fresh note on the cartridge chip's first pulse (channel 6), its sustain's level.
  const level = () => {
    s.audio.stageMidiIn([0xb5, 7, 127]);
    s.audio.stageMidiIn([0x95, 69, 100]);
    return db(rms(s.audio.renderAudio(600)));
  };

  const full = level(); // the role's default expansion volume, 100
  // Only a param moves, so the store sets it live (setParam) with no publish.
  expect(s.project.systems.setRoleConfig(id, "mesen", { expansionVolume: 50 })).toBeTruthy();
  s.audio.renderAudio(150);
  const half = level();
  // A setting moves too, so this edit publishes, and the document carries the expansion volume it now holds, the value
  // the last document carried. The region change resets the core, so the document lands on a fresh one.
  expect(s.project.systems.setRoleConfig(id, "mesen", { region: "pal", expansionVolume: 100 })).toBeTruthy();
  s.audio.renderAudio(1500); // the reset's boot
  const back = level();
  console.log(
    `[param-republish] store: 100% ${full.toFixed(2)} dBFS, live 50% ${half.toFixed(2)}, document 100% ${back.toFixed(2)}`,
  );

  expect(full, "the cartridge chip sounds at unity").toBeGreaterThan(-60);
  expect(half, "the live 50% is ~6 dB down").toBeCloseTo(full - 6, 0.5);
  expect(back - half, "the document's 100% landed over the live 50%").toBeGreaterThan(3);
  expect(Math.abs(back - full), "and it is the default 100% again").toBeLessThan(1.5);
  s.project.systems.removeSystem(id);
});

test("DirectGraph's setCoreConfig lands a param a live edit moved the core away from", () => {
  const s = bootSession();
  if (!s.backend.fileExists(VRC6)) skip("no VRC6 rom");
  // Structure for this file's directly built system, its graph rendering through its own audio driver.
  const graph = new DirectGraph(s.backend);
  const audio = createAudioDriver(graph.graph);
  const spec = {
    romPath: VRC6,
    platform: "nes" as const,
    core: "mesen" as const,
    embeddedRom: "",
    savPath: null,
    statePath: null,
  };
  expect(graph.add(spec, 77)).toBeTruthy();
  // A DirectGraph places no roles of its own: the NES needs its n8 MIDI-to-FIFO role to hear host MIDI at all.
  expect(graph.setPipeline(77, [{ kind: "nes-n8-midi", config: {} }])).toBeTruthy();
  expect(graph.setMidiRouting("sendToAll")).toBeTruthy(); // and it routes no MIDI until told to
  audio.renderAudio(1500); // boot, the node's first block giving the param its default 100
  const level = () => {
    audio.stageMidiIn([0xb5, 7, 127]);
    audio.stageMidiIn([0x95, 69, 100]);
    return db(rms(audio.renderAudio(600)));
  };

  expect(graph.setCoreConfig(77, { expansionVolume: 200 })).toBeTruthy();
  audio.renderAudio(150);
  const full = level();
  // A live edit moves the core away, keeping the value in DirectGraph's model.
  expect(graph.setParam(77, "expansionVolume", 100)).toBeTruthy();
  audio.renderAudio(150);
  const low = level();
  // The same value the last document carried, published again: the node no longer has it, so it is sent.
  expect(graph.setCoreConfig(77, { expansionVolume: 200 })).toBeTruthy();
  audio.renderAudio(150);
  const back = level();
  console.log(
    `[param-republish] DirectGraph: 200% ${full.toFixed(2)} dBFS, live 100% ${low.toFixed(2)}, document 200% ${back.toFixed(2)}`,
  );

  expect(full, "the cartridge chip sounds").toBeGreaterThan(-60);
  expect(full - low, "the live 100% is ~6 dB down").toBeGreaterThan(3);
  expect(back - low, "the document's 200% landed over the live 100%").toBeGreaterThan(3);
  expect(Math.abs(back - full), "and it is 200% again").toBeLessThan(1.5);
  graph.remove(77);
});
