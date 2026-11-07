// smsggdj's working song, saved and carried on a REAL Mesen core with the shipped v0.45 ROMs - the native
// half of test/tracker/working-song-save (which proves the same logic over the mock).
//
// The working song is work RAM, in neither the `.sav` nor the project, so two paths used to lose it with no
// prompt: closing the window and a reset. Proven here: `writeSram` puts bytes in the LIVE battery and
// readSram sees them; "Save" lands the song in the battery, in the `.sav`, and SURVIVES the teardown flush
// that used to be the thing that would overwrite it; and a reset carries an unsaved song through the boot
// that would otherwise blank it, still marked unsaved.
import { test, expect, skip } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { buildAppRegistry } from "../src/appHost";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import type { Platform } from "../src/platform";
import { listSongs, readSongBlock, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { smsggdjSongCatalog } from "../src/tracker/smsggdjSongCatalog";
import { saveWorkingSong } from "../src/tracker/liveSav";
import { flushDirtySram } from "../src/sramAutoSave";
import { hasUnsavedChanges, unsavedChanges } from "../src/unsavedChanges";
import { buildMetronomeBlock } from "./smsSyncSong";
import { directGraph } from "../src/graph/directGraph";

declare const __REPO_RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

// This host never fires a timer while a test awaits, so saveWorkingSong must not wait on one: here every
// battery write applies inline (no audio thread), and its outcome is decided by the first synchronous look.
(globalThis as { __rp_timersPumped?: boolean }).__rp_timersPumped = false;

const ROMS: { path: string; platform: Platform }[] = [
  { path: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms", platform: "sms" },
  { path: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg", platform: "gg" },
];
const layout = resolveSmsggdjLayout("0.45")!;
const TICK_MS = 50;
const BOOT_LIMIT_MS = 8000;
const NAME = "MYSONG  ";

/** A cart of `rom` with NO battery file beside it - a new user's cart, whose battery has no directory. */
function freshCart(rom: { path: string; platform: Platform }, stem: string) {
  const be = createRealBackend();
  if (!be.fileExists(rom.path)) skip(`sms-working-song-save: missing ${rom.path}`);
  const romCopy = `${__CONFIG_DIR__}/${stem}.${rom.platform}`;
  const savPath = `${__CONFIG_DIR__}/${stem}.sav`;
  expect(be.writeFile(romCopy, be.readFile(rom.path)!)).toBeTruthy();
  if (be.fileExists(savPath)) be.deleteFile(savPath);
  const audio = createAudioDriver();
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  project.systems.adopt({
    romPath: romCopy,
    roles: [{ kind: "mesen", config: { enableFm: false } }, { kind: "sms-sync", config: { machine: rom.platform } }],
  });
  const id = () => project.systems.primary()!.id;
  const ready = () => smsggdjSongCatalog.workingSongReady!(be.readRam(id()) ?? undefined);
  const bootUp = (): void => {
    for (let t = 0; t < BOOT_LIMIT_MS && !ready(); t += TICK_MS) audio.renderAudio(TICK_MS);
    expect(ready(), "the cart never finished booting").toBeTruthy();
  };
  /** Compose a song the way the user would end up with one: the block, a name, and the cart's own flag. */
  const compose = (block: Uint8Array): void => {
    expect(be.writeRam(id(), layout.song, block)).toBeTruthy();
    expect(be.writeRam(id(), layout.name, Uint8Array.from(NAME, (c) => c.charCodeAt(0)))).toBeTruthy();
    expect(be.writeRam(id(), layout.edited, Uint8Array.of(1))).toBeTruthy();
    audio.renderAudio(TICK_MS);
  };
  const working = () => be.readRam(id())!.subarray(layout.song, layout.song + SMDJ4_BLOCK_LEN);
  return { be, audio, project, id, bootUp, compose, working, savPath };
}

test("writeSram lands in the live battery, and readSram sees it the block it lands", () => {
  const { be, audio, project, id, bootUp } = freshCart(ROMS[0], "sms-writesram");
  bootUp();
  const size = be.readSram(id())!.length;
  expect(size).toBe(32 * 1024);
  const payload = Uint8Array.from([0x53, 0x4d, 0x44, 0x4a, 0x34, 0x99]);
  expect(be.writeSram(id(), 0x100, payload)).toBeTruthy();
  audio.renderAudio(TICK_MS);
  // The SRAM snapshot is otherwise republished every 0.5 s; a stale one here is the auto-save mirror
  // flushing the PRE-write battery straight back over the file the write was made for.
  expect(be.readSram(id())!.subarray(0x100, 0x106)).toEqual(payload);
  expect(be.writeSram(id(), size - 2, Uint8Array.of(1, 2, 3))).toBeFalsy(); // overruns by one: refused whole
  expect(be.writeSram(id(), 0, new Uint8Array(0))).toBeFalsy();
  expect(be.writeSram(9999, 0, Uint8Array.of(1))).toBeFalsy(); // no such system

  // Conditional: applied only while the battery still holds `expect`. A stale `expect` - the cart wrote
  // the battery after the caller's snapshot - leaves it untouched, and readSram shows what it holds now.
  const fresh = Uint8Array.of(7, 7, 7, 7, 7, 7);
  expect(be.writeSram(id(), 0x100, fresh, Uint8Array.of(0, 0, 0, 0, 0, 0))).toBeTruthy(); // accepted, judged at apply
  audio.renderAudio(TICK_MS);
  expect(be.readSram(id())!.subarray(0x100, 0x106)).toEqual(payload); // refused: the battery held `payload`
  expect(be.writeSram(id(), 0x100, fresh, payload)).toBeTruthy();
  audio.renderAudio(TICK_MS);
  expect(be.readSram(id())!.subarray(0x100, 0x106)).toEqual(fresh); // matched: applied
  expect(be.writeSram(id(), 0x100, fresh, Uint8Array.of(1))).toBeFalsy(); // `expect` of the wrong length
  project.systems.clear(); // a live core here is emulated by every later test in the file
});

test("writeSram on a Game Boy (SameBoy) battery is readable at once, not half a second later", () => {
  // SameBoy publishes its battery sliced out of a savestate the core refreshes every ~0.5 s, so a write
  // used to stay invisible to readSram that long - forever, with no block running - and the teardown flush
  // could write the pre-write battery back to disk. Nothing writes a GB battery this way yet; this keeps it
  // honest for whoever does.
  const be = createRealBackend();
  const rom = __REPO_RESOURCES_DIR__ + "/roms/mGB.gb"; // MBC1 + 32 KB cart RAM, shipped in the repo
  if (!be.fileExists(rom)) skip(`sms-working-song-save: missing ${rom}`);
  const audio = createAudioDriver();
  const id = 9001;
  // Placed directly (docs/modular-graph-phase-b2-design.md section 2.4): no store, no battery file.
  const graph = directGraph();
  expect(
    graph.add({ romPath: rom, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, id),
  ).toBeTruthy();
  // Past mGB's own boot, which initialises its settings at the start of cart RAM; the write goes at the
  // far end, which mGB never touches, so anything that moves it is the publish path and not the cart.
  audio.renderAudio(1500);
  const at = 0x7ff0;
  const payload = Uint8Array.of(0xc0, 0xff, 0xee);
  expect(be.readSram(id)!.subarray(at, at + 3)).toEqual(Uint8Array.of(0xff, 0xff, 0xff));
  expect(be.writeSram(id, at, payload)).toBeTruthy();
  expect(be.readSram(id)!.subarray(at, at + 3)).toEqual(payload); // inline: no block has run
  audio.renderAudio(TICK_MS);
  expect(be.readSram(id)!.subarray(at, at + 3)).toEqual(payload); // the next coarse publish keeps it
  audio.renderAudio(1000);
  expect(be.readSram(id)!.subarray(at, at + 3)).toEqual(payload);
  expect(graph.remove(id)).toBeTruthy();
});

for (const rom of ROMS) {
  const tag = rom.platform;

  test(`[${tag}] Save puts the working song in the battery and the .sav, and the teardown keeps it there`, async () => {
    const { be, audio, project, id, bootUp, compose, savPath } = freshCart(rom, `sms-save-${tag}`);
    try {
    bootUp();
    const song = buildMetronomeBlock();
    compose(song);

    // The unsaved-changes prompt sees it - on a battery with no directory yet, which it used to call clean.
    expect(hasUnsavedChanges(be, project)).toBeTruthy();
    expect(unsavedChanges(be, project).some((i) => i.kind === "song" && i.name === "MYSONG")).toBeTruthy();

    const sys = project.systems.systems()[0];
    const pending = saveWorkingSong(be, project.systems, sys);
    audio.renderAudio(TICK_MS); // the battery write is queued: it lands on the next block
    expect(await pending).toEqual({ kind: "saved", index: 0, replaced: false });

    const sram = be.readSram(id())!;
    expect(listSongs(sram)).toEqual([{ index: 0, name: "MYSONG" }]);
    expect(readSongBlock(sram, 0)).toEqual(song);
    audio.renderAudio(TICK_MS);
    expect(be.readRam(id())![layout.edited]).toBe(0); // the cart's own UNSAVED flag, cleared

    flushDirtySram(be, project.systems.systems()); // what Save does next: battery -> .sav
    expect(readSongBlock(be.readFile(savPath)!, 0)).toEqual(song);

    // The close itself. Its teardown flush writes readSram to disk - a save made only to the FILE would be
    // overwritten here by the stale live battery. It must still hold the song afterwards.
    project.systems.clear();
    expect(readSongBlock(be.readFile(savPath)!, 0)).toEqual(song);
    expect(hasUnsavedChanges(be, project)).toBeFalsy();
    } finally {
      project.systems.clear();
    }
  });

  test(`[${tag}] a reset carries an unsaved song through the boot that would blank it, still unsaved`, () => {
    const { be, audio, project, id, bootUp, compose, working } = freshCart(rom, `sms-carry-${tag}`);
    try {
    bootUp();
    const song = buildMetronomeBlock();
    compose(song);
    const before = id();

    project.systems.reset(before);
    expect(id() === before).toBe(false);
    expect(project.systems.hasCarries()).toBeTruthy();
    // Mid-boot the cart holds nothing of it, but the close guard must still see an unsaved song.
    expect(hasUnsavedChanges(be, project)).toBeTruthy();

    for (let t = 0; t < BOOT_LIMIT_MS && project.systems.hasCarries(); t += TICK_MS) {
      project.systems.settleCarries();
      audio.renderAudio(TICK_MS);
    }
    expect(project.systems.hasCarries(), "the carry never landed").toBeFalsy();
    audio.renderAudio(1000); // and it HOLDS: nothing later in the boot erases it
    expect(working()).toEqual(song);
    const ram = be.readRam(id())!;
    expect(String.fromCharCode(...ram.subarray(layout.name, layout.name + 8))).toBe(NAME);
    expect(ram[layout.edited]).toBe(1); // a reset is not a save
    expect(hasUnsavedChanges(be, project)).toBeTruthy();
    } finally {
      project.systems.clear();
    }
  });
}
