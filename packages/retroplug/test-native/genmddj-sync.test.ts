// genmddj, the real ROM, clocked by the DAW through controller port 2 (design section 5). Fixture-gated: the ROM is not
// vendored (tools/fetch-genmddj-fixture.sh builds it from a checkout), so CI skips this case and the skip baseline
// counts it.
//
// This is the one case that runs the whole path at once: the Mega Drive console, its port-2 device, the genmddj-sync
// role's node, and the cart's own `sync_in_delta` recovering clocks from the two lines. Nothing here speaks to the core
// directly except the two option pokes and the pad's Start, so what the ROM ends up doing is its own doing.
//
// The cart's SYNC option is set by writing its OPTIONS byte in work RAM rather than by seeding a battery: genmddj's save
// format is spec 2's (the design defers its SRAM codec), so a battery the cart would accept cannot be written here. The
// option is read by its engine every tick (`opt_sync`), so a poke is what a user's menu pick ends up as.
//
// The counter's residue is deliberately NOT an assertion: `(sync_last - armed) & 3` is the clock count mod 4, and 48
// clocks is 0, so a residue check would compare 0 with 0. What the case asserts instead is the ROWS the clocks advanced
// to, which a stalled or mis-rateed feed cannot produce by accident.
import { test, expect, skip } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string; // the temp dir run-native-tests.mjs gives each file (this case's own ROM copy)

const GENMDDJ_ROM = __RESOURCES_DIR__ + "/roms/genmddj_v0_27.bin";

// The cart's own work-RAM variables, by offset from $FF0000: `getMemory(Ram)` hands back the work RAM in 68k address
// order (BlastemMemory.cpp), and the store's readRam/writeRam offsets are the same, so these are the equs' addresses
// with the $FF prefix dropped.
//
//   sync_last   (main.asm:219) the 2-bit counter the ROM read on its last frame's poll
//   sync_wait   (main.asm:220) 1 from the arm until the first external clock, which is the WAIT the cart shows on screen
//   live_bar    (main.asm:288) the master 16-row bar counter, one step per ROW ADVANCE (main.asm:9586)
//   playing     (main.asm:115) 0 stopped, 1 playing: the cart's own transport
//   opt_sync    (main.asm:245) the OPTIONS sync mode; 5 is IN24
//   proj_tmpo   (main.asm:253) the project tempo in BPM
//
// live_bar rather than `play_row` ($FFE20E) for the rows: `play_row` is the playhead of the SCREEN (main.asm:5256), and
// the cart boots on SONG, whose grid marks every track, so get_playrow answers $FF ("none") there and on every other
// screen that is not PHRASE/CHAIN/TABLE. live_bar is the sequencer's own count of row advances, which is what a clock
// stream has to move.
const SYNC_LAST = 0xd425;
const SYNC_WAIT = 0xd426;
const LIVE_BAR = 0xd3cb;
const PLAYING = 0xe20a;
const OPT_SYNC = 0xe3e5;
const PROJ_TEMPO = 0xe3e7;
const SYNC_IN24 = 5;
const BUTTON_START = 7;
/** IN24 divides the clock stream by 6 per row (main.asm:9574), so 24 PPQN puts 4 rows in a beat. */
const CLOCKS_PER_ROW = 6;
/** The DAW tempo this case runs the transport at, and its pulses per quarter note (genmddjSyncNode.ts). */
const BPM = 120;
const PPQN = 24;

test("the DAW clocks genmddj through port 2", () => {
  const be = createRealBackend();
  if (!be.fileExists(GENMDDJ_ROM)) skip(`genmddj-sync: no ROM at ${GENMDDJ_ROM}`);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();

  // The ROM is copied into the case's own directory first, so its .sav and its battery slot are this case's and the
  // resources tree stays read-only. (There is no be.copyFile; read and write, as Rig.rom does.)
  const local = __CONFIG_DIR__ + "/genmddj_v0_27.bin";
  expect(be.writeFile(local, be.readFile(GENMDDJ_ROM)!)).toBeTruthy();

  const id = (project.systems.loadRom(local) as { system: number }).system;
  expect(project.systems.view()[0].platform).toBe("md");
  // The role the ROM provider attached is what makes port 2 the host's device at all (the descriptor's claimedBytesIn,
  // BlastemNode's coreBytesIn knob): unplaced, the cart would read a pad's idle lines and no poke could change that.
  expect(project.systems.view()[0].roles.map((r) => r.kind).includes("genmddj-sync")).toBeTruthy();

  audio.renderAudio(2000); // boot: the cart clears RAM, sets up the VDP and draws its UI
  const ram = () => project.systems.readRam(id)!;

  // IN24, and a tempo whose 24-PPQN clocks are well inside the cart's 3-per-frame poll cap.
  expect(project.systems.writeRam(id, OPT_SYNC, Uint8Array.of(SYNC_IN24))).toBe(true);
  expect(project.systems.writeRam(id, PROJ_TEMPO, Uint8Array.of(BPM))).toBe(true);

  // Play: the pad's Start, so the cart's own play-start runs and arms the sync (a RAM poke of `playing` would skip the
  // arm, which is the state this case is about).
  audio.pressButton(id, BUTTON_START, true);
  audio.renderAudio(100);
  audio.pressButton(id, BUTTON_START, false);
  audio.renderAudio(500);

  // Armed and waiting. The pokes really reached the cart, and the cart's own play-start really ran: without both, the
  // stillness below would be a stopped cart rather than an unclocked one. main.asm:8806 then latches the idle lines
  // (both counter bits high, the pull-ups an unplugged pad leaves) and sets the WAIT flag.
  expect(ram()[OPT_SYNC]).toBe(SYNC_IN24);
  expect(ram()[PLAYING]).toBe(1);
  expect(ram()[SYNC_WAIT]).toBe(1);
  expect(ram()[SYNC_LAST]).toBe(3);
  // Nothing has advanced, and nothing could have: the transport is stopped, so the node sent no level, and IN24 gives
  // the cart no clock of its own.
  expect(ram()[LIVE_BAR]).toBe(0);

  audio.setTransport(true);
  audio.setBpm(BPM);
  const renderedMs = 1000;
  audio.renderAudio(renderedMs);

  // The clocks are derived from the TRANSPORT, not from the ROM's counter: BPM at 24 PPQN for one second of audio
  // (renderAudio takes milliseconds) is 48 clocks, and the ROM counts `(read - last) & 3` per frame, so its counter
  // value cannot carry the count at all (48 is 0 mod 4). The rows can: the head start plays row 0 on the first clock
  // and one row follows per 6 clocks, so 48 clocks are 1 + floor(47 / 6) = 8 advances, plus or minus the frames at the
  // transport's two edges. Asserting the count is what makes this a tempo check as well: a node clocking at the wrong
  // PPQN would still move the playhead, just not by this much.
  const ticks = Math.round((BPM / 60) * PPQN * (renderedMs / 1000));
  const expectedRows = 1 + Math.floor((ticks - 1) / CLOCKS_PER_ROW);
  expect(ram()[SYNC_WAIT]).toBe(0); // WAIT cleared by the first clock, so the cart is running
  expect(Math.abs(ram()[LIVE_BAR] - expectedRows)).toBeLessThanOrEqual(1);

  // Stopping the clocks stops the song: IN24 carries no stop message, the delta simply goes to 0, and with it every
  // row advance. The last block's queued levels still land, so the cart settles by one row before it freezes.
  audio.setTransport(false);
  audio.renderAudio(500);
  const frozenLast = ram()[SYNC_LAST];
  const frozenBar = ram()[LIVE_BAR];
  audio.renderAudio(500);
  expect(ram()[SYNC_LAST]).toBe(frozenLast); // no clock was counted, so the counter froze
  expect(ram()[LIVE_BAR]).toBe(frozenBar); // and so did the sequencer

  project.systems.removeSystem(id);
});
