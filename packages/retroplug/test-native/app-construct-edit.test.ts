// A construct-time edit lands with no manual Reset (docs/modular-graph-phase-c3-design.md section 2): those fields are
// read once, when a system's node builds its core, so SystemsStore.setRoleConfig rebuilds the system itself - a new id,
// the ROM read again from its source, the live battery carried - and the new value is in the document that replaces the
// old one. Each case watches a real core change behaviour, not just the document:
//
//  - a NES's FIFO profile is its RX queue's capacity, which the transport stats report (0 = unbounded);
//  - a NES's channel export mode decides how many streams its node produces, which the per-channel render returns;
//  - an SMS's FM switch decides whether the YM2413 is emulated at all, so an FM-instrument song goes silent.
// And what a rebuild CARRIES: a construct-time edit and a reset the battery, a duplicate the whole machine, a load-state
// the file it was given. A Game Boy has no construct-time field of its own (its model and the rest are applied live), so
// its rebuilds are the reset path itself. Every rebuild reads the ROM again, so a file a user rebuilt on disk is what
// the next boot runs, and one that has gone fails the rebuild instead of booting nothing. A stored core config written
// by an older build holds only the fields it knew, and every document names the rest at their defaults: an edit must
// stay live rather than move its node's rebuild key under a live id.
//
// Run with: `pnpm test:native app-construct-edit`.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { MemoryRegion, type Backend } from "../src/backend";
import { createAudioDriver, type AudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import {
  buildMetronomeBlock,
  buildConfigBlock,
  buildSmdj4Sav,
  BLOCK_LEN,
  WRAM_BASE,
  P_PHRASES,
  P_CHAINS,
  P_SONG,
  P_INSTR,
  STEPS_PER_PHRASE,
  CHAIN_ENTRIES,
  HIT_NOTE,
  SMS_ROWS_PER_BEAT,
  SMS_SYNC_OFF,
} from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;
declare const __RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
const SMS = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

function session(): { be: Backend; audio: AudioDriver; project: ProjectStore } {
  const be = createRealBackend();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  // A rebuild flushes the battery it replaces to the cart's sibling .sav; these are measurements against the fixtures
  // on disk, so nothing here may write to one (the offline render's own setting, setBatteryFlush).
  project.systems.setBatteryFlush(false);
  return { be, audio: createAudioDriver(project.graph), project };
}

// An FM-instrument song for smsggdj (sms-fm.test.ts's): channel 0 unused, channel 1 on an FM instrument, with the
// cart's own FM option on so it writes $F2 = 1. Audible only while RetroPlug emulates the YM2413.
const TYPE_FM = 4;
const FM_PROG_OFFSET = 12;
function fmSong(): Uint8Array {
  const b = buildMetronomeBlock();
  b[P_INSTR + 1 * 16] = TYPE_FM;
  b[P_INSTR + 1 * 16 + FM_PROG_OFFSET] = 1;
  for (let step = 0; step < STEPS_PER_PHRASE; step += SMS_ROWS_PER_BEAT)
    b.set([HIT_NOTE, 0x01, 0x00, 0x00], P_PHRASES + (STEPS_PER_PHRASE + step) * 4);
  for (let i = 0; i < CHAIN_ENTRIES; i++) b.set([0x01, 0x00], P_CHAINS + CHAIN_ENTRIES * 2 + i * 2);
  for (let row = 0; row < 8; row++) b.set([0xff, 0x01, 0xff, 0xff], P_SONG + row * 4);
  return b;
}

test("a NES FIFO profile edit reaches the core with no Reset: its RX queue's capacity is the new one", () => {
  const { be, project } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  const id = project.systems.addSystem(NES)!;
  expect(be.getCoreTransportStats(id).rxCapacity).toBe(0); // the default profile is unbounded

  expect(project.systems.setRoleConfig(id, "mesen", { fifo: "hardware" })).toBe(true);
  const rebuilt = project.systems.view()[0].id;
  expect(rebuilt === id).toBe(false); // a construct-time field: the node is rebuilt under a new id
  expect(be.getCoreTransportStats(rebuilt).rxCapacity).toBe(2048); // rp::FIFO_HW_DEPTH, with no Reset pressed

  // And the explicit depth override, again with no Reset.
  expect(project.systems.setRoleConfig(rebuilt, "mesen", { fifoDepth: 4096 })).toBe(true);
  const deeper = project.systems.view()[0].id;
  expect(be.getCoreTransportStats(deeper).rxCapacity).toBe(4096);
  project.systems.removeSystem(deeper);
});

test("the pre-C3 contract still holds: a construct-time edit then reset(old id) answers a live system", () => {
  const { be, audio, project } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  const store = project.systems;
  const id = store.addSystem(NES)!;
  // BlipToaster's helper does exactly this: apply the construct-time knob, then reset the id it was holding, which is
  // how code written before the store rebuilt inside setRoleConfig learned the id of the system that carried it.
  expect(store.setRoleConfig(id, "mesen", { n8Link: "off" })).toBe(true);
  const rebuilt = store.view()[0].id;
  expect(rebuilt === id).toBe(false);
  const next = store.reset(id);
  expect(next === null).toBe(false);
  expect(next === rebuilt).toBe(false);
  expect(store.view().map((e) => e.id)).toEqual([next as number]);
  expect(store.view()[0].roles.find((r) => r.kind === "mesen")!.config.n8Link).toBe("off");
  // The answer is a system the engine is running: its state is published, and a raw MIDI note plays it.
  expect(be.readState(next as number) !== null).toBe(true);
  audio.renderAudio(1500); // the ROM's boot, under the settings the reset gave it
  audio.stageMidiIn([0x90, 48, 127]);
  audio.renderAudio(200);
  audio.stageMidiIn([0x90, 48, 127]);
  const level = rms(audio.renderAudio(1000));
  console.log(`[construct-edit] reset of a replaced id: rms ${level.toFixed(5)}`);
  expect(level > 0.001).toBeTruthy();
  store.removeSystem(next as number);
});

test("a NES channel export mode edit reaches the core with no Reset: its per-channel streams appear", () => {
  const { be, audio, project } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  const id = project.systems.addSystem(NES)!;
  audio.renderAudio(500);
  expect(audio.renderAudioPerChannel(id, 100).length).toBe(1); // mix: one stereo stream

  expect(project.systems.setRoleConfig(id, "mesen", { channelExportMode: "individualMono" })).toBe(true);
  const rebuilt = project.systems.view()[0].id;
  expect(rebuilt === id).toBe(false);
  audio.renderAudio(500);
  // The five core channels as raw mono stems: the mode is what the node builds the core with, so the stems appear
  // without a Reset.
  expect(audio.renderAudioPerChannel(rebuilt, 100).length).toBe(5);
  project.systems.removeSystem(rebuilt);
});

test("an SMS FM edit reaches the core with no Reset: an FM song goes silent", () => {
  const { be, audio, project } = session();
  if (!be.fileExists(SMS)) skip(`no ROM at ${SMS}`);
  const block = fmSong();
  const sav = buildSmdj4Sav(block, buildConfigBlock(SMS_SYNC_OFF, 1)); // the cart's own FM option on
  const id = project.systems.adopt(
    { romPath: SMS, roles: [{ kind: "mesen", config: { enableFm: true } }, { kind: "sms-sync", config: { machine: "sms" } }] },
    { sramBytes: sav },
  )!;
  const play = (system: number): number => {
    audio.renderAudio(3000); // boot: splash, config_load (takes fm_on), song_new
    for (let i = 0; i < BLOCK_LEN; i++)
      if (!be.writeCpu(system, WRAM_BASE + i, block[i])) throw new Error(`writeCpu failed at +0x${i.toString(16)}`);
    audio.pressButton(system, 7, true); // PAUSE: leave edit mode for play
    audio.renderAudio(100);
    audio.pressButton(system, 7, false);
    audio.renderAudio(500);
    return rms(audio.renderAudio(2500));
  };
  const withFm = play(id);
  expect(withFm > 0.001).toBeTruthy(); // the YM2413 is emulated, so the FM instrument sounds

  const battery = new Uint8Array(be.readSram(id)!);
  expect(project.systems.setRoleConfig(id, "mesen", { enableFm: false })).toBe(true);
  const rebuilt = project.systems.view()[0].id;
  expect(rebuilt === id).toBe(false);
  // The battery came across with the rebuild: the seed is the old core's SRAM, not the blank cartridge a cold boot
  // would give (this system was adopted from bytes, so there is no .sav on disk to re-read).
  expect(new Uint8Array(be.readSram(rebuilt)!)).toEqual(battery);
  const withoutFm = play(rebuilt);
  console.log(`[construct-edit] SMS FM song: enableFm true ${withFm.toFixed(5)}, after the edit ${withoutFm.toFixed(5)}`);
  expect(withoutFm < 0.001).toBeTruthy(); // no FM chip, so the song is silent - and no Reset was pressed
  project.systems.removeSystem(rebuilt);
});

test("duplicate takes the whole machine, reset carries the battery, load-state takes the file", () => {
  const { be, project } = session();
  if (!be.fileExists(LSDJ)) skip(`no ROM at ${LSDJ}`);
  const store = project.systems;
  const id = store.addSystem(LSDJ)!;
  // A distinguishing byte in the live battery, read back by every verb below: load a copy with its last byte flipped,
  // so a rebuild that cold-boots the cart (a blank battery) cannot pass the comparisons.
  const seeded = new Uint8Array(be.readSram(id)!);
  expect(seeded.length).toBeGreaterThan(0x8000);
  seeded[seeded.length - 1] ^= 0xff;
  const batteryPath = "/tmp/construct-edit-battery.sav";
  expect(be.writeFileAtomic(batteryPath, seeded)).toBe(true);
  const source = store.loadSram(id, batteryPath)!;
  const battery = new Uint8Array(be.readSram(source)!);
  expect(battery).toEqual(seeded);

  // Duplicate: the savestate, which carries work RAM as well as the battery.
  const dup = store.duplicateSystem(source)!;
  expect(new Uint8Array(be.readState(dup)!)).toEqual(new Uint8Array(be.readState(source)!));

  // Reset: the battery the core was running, not a cold one. (A Game Boy's config has no construct-time field - its
  // model and the rest are applied live - so this console's rebuilds are the reset path itself.)
  const reset = store.reset(source)!;
  expect(new Uint8Array(be.readSram(reset)!)).toEqual(battery);

  // Load state: the file's bytes, not the live machine.
  const statePath = "/tmp/construct-edit-state.ss0";
  const saved = new Uint8Array(be.readState(reset)!);
  expect(store.saveState(reset, statePath)).toBe(true);
  const loaded = store.loadState(reset, statePath)!;
  expect(loaded !== reset).toBe(true);
  expect(new Uint8Array(be.readState(loaded)!)).toEqual(saved);
  store.removeSystem(loaded);
  store.removeSystem(dup);
});

test("a sparse stored NES config keeps a live edit, a param and a later live edit live", () => {
  const { be, project } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  const store = project.systems;
  // A project saved before the mesen role schema grew its construct-time fields: its core config holds only what that
  // build knew. Every document names every construct-time field at the node's default, so the document the running
  // node was planned from and the one an edit sends agree on what its core was built with.
  const id = store.adopt({ romPath: NES, core: "mesen", roles: [{ kind: "mesen", config: { region: "pal" } }] })!;
  expect(store.setRoleConfig(id, "mesen", { removeSpriteLimit: true })).toBe(true);
  expect(store.view()[0].id).toBe(id); // a live setting: a document under the id the running node holds
  expect(store.setRoleConfig(id, "mesen", { expansionVolume: 40 })).toBe(true);
  expect(store.view()[0].id).toBe(id); // a param: set live, no document
  // The publish after the param-only edit names the whole config again, construct-time fields included: still the same
  // node, so it plans rather than being refused for a new core under a live id.
  expect(store.setRoleConfig(id, "mesen", { region: "ntsc" })).toBe(true);
  expect(store.view()[0].id).toBe(id);
  const second = store.addSystem(NES)!;
  expect(store.view().map((e) => e.id)).toEqual([id, second]);
  // The one edit that is a rebuild still rebuilds, and the new core has the new value.
  expect(store.setRoleConfig(id, "mesen", { fifo: "hardware" })).toBe(true);
  const rebuilt = store.view()[0].id;
  expect(rebuilt === id).toBe(false);
  expect(store.view()[0].id).toBe(rebuilt);
  expect(be.getCoreTransportStats(rebuilt).rxCapacity).toBe(2048);
  store.removeSystem(rebuilt);
  store.removeSystem(second);
});

test("a ROM rewritten on disk is what reset and a construct-time edit boot; a deleted one fails its rebuild", () => {
  const { be, project } = session();
  const first = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
  const second = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc6.nes";
  if (!be.fileExists(first) || !be.fileExists(second)) skip(`no ROMs at ${first}`);
  const store = project.systems;
  const temp = "/tmp/rp-construct-edit-rom.nes";
  // The PRG image a file holds (iNES: 16-byte header, then PRG banks), which readMemory(Rom) answers for a live core.
  const prgOf = (bytes: Uint8Array): Uint8Array => bytes.slice(16, 16 + bytes[4] * 0x4000);
  const prg = (id: number): Uint8Array => new Uint8Array(be.readMemory(id, MemoryRegion.Rom)!);
  const a = prgOf(be.readFile(first)!);
  const b = prgOf(be.readFile(second)!);
  expect(a.slice(0, 64).some((v, i) => v !== b[i])).toBe(true);
  be.writeFileAtomic(temp, be.readFile(first)!);
  const id = store.addSystem(temp)!;
  expect(prg(id).slice(0, 64)).toEqual(a.slice(0, 64));
  // Rebuild the ROM on disk, then Reset: the reset boots the bytes that are there now, not the resource the old node
  // read (the pre-C3 behaviour, when every in-place rebuild handed the build the ROM's path).
  be.writeFileAtomic(temp, be.readFile(second)!);
  const reset = store.reset(id)!;
  expect(prg(reset).slice(0, 64)).toEqual(b.slice(0, 64));
  // A construct-time edit is an in-place rebuild too, so it reads the file again (here back to the first image).
  be.writeFileAtomic(temp, be.readFile(first)!);
  expect(store.setRoleConfig(reset, "mesen", { fifo: "hardware" })).toBe(true);
  const rebuilt = store.view()[0].id;
  expect(rebuilt === reset).toBe(false);
  const rebuiltResources = store.systems()[0].resources;
  // A ROM deleted since it was booted fails the rebuild rather than silently succeeding on the old bytes: the verb
  // answers null, the system is the one that was, and its core still runs the image it booted. This is what every
  // in-place verb did before C3 parked a core, and what a node's re-read costs: the file is read again, so a gone
  // file is a gone boot.
  be.deleteFile(temp);
  expect(store.reset(rebuilt)).toBe(null);
  expect(store.view().map((e) => e.id)).toEqual([rebuilt]);
  expect(store.systems()[0].resources).toEqual(rebuiltResources);
  expect(prg(rebuilt).slice(0, 64)).toEqual(a.slice(0, 64));
  store.removeSystem(rebuilt);
  be.deleteFile(temp);
});

test("a project with one unreadable ROM loads the rest, with a note", () => {
  const { be } = session();
  if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
  // A ROM this build cannot boot: an iNES header with no PRG banks, which Mesen refuses. It classifies as NES, so a load
  // reaches the file and the engine's BUILD is what fails - the case a saved project has to survive, since a node whose
  // build fails refuses its document and the load drops that system.
  const badPath = "/tmp/rp-construct-edit-bad.nes";
  const bad = new Uint8Array(16);
  bad.set([0x4e, 0x45, 0x53, 0x1a], 0); // "NES\x1A", 0 PRG banks, 0 CHR banks
  be.writeFileAtomic(badPath, bad);
  const projectPath = "/tmp/rp-construct-edit-bad.rplg";
  const systems = [
    { platform: "nes", core: "mesen", romPath: NES, roles: [{ kind: "mesen", config: {} }] },
    { platform: "nes", core: "mesen", romPath: badPath, roles: [{ kind: "mesen", config: {} }] },
    { platform: "gb", core: "sameboy", embeddedRom: "mgb", roles: [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }] },
  ];
  be.writeFileAtomic(projectPath, new TextEncoder().encode(JSON.stringify({ schemaVersion: "4", settings: {}, systems })));

  const fresh = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  fresh.systems.setBatteryFlush(false);
  const notes: string[] = [];
  const warn = console.warn;
  console.warn = (...args: unknown[]): void => void notes.push(args.map(String).join(" "));
  let outcome: unknown;
  try {
    outcome = fresh.load(projectPath);
  } finally {
    console.warn = warn;
  }
  console.log(`[construct-edit] a load with one unreadable ROM: ${JSON.stringify(outcome)}; notes: ${notes.join(" | ")}`);
  expect(outcome).toEqual({ kind: "loaded", systems: 2 }); // the two good systems, not the whole project lost
  expect(fresh.systems.systems().length).toBe(2);
  // Both are placed on the engine: a placed system's savestate snapshot answers a read.
  for (const e of fresh.systems.systems()) expect(be.readState(e.id) !== null).toBe(true);
  expect(notes.some((n: string) => n.includes("loads without it"))).toBe(true); // the note names what the load skipped
  be.deleteFile(badPath);
});
