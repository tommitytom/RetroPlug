// Author an smsggdj `.rplg.zip` fixture (export = PKZIP, carrying the ROM + the authored battery AND a
// savestate) for the real-Reaper host-sync render. The Master System counterpart of
// author-risa-rplg.ts, and it needs one step the others do not.
//
// smsggdj deliberately does NOT autoload a save (`song_new` boots blank; main.asm:238 - "a first
// power-on should make sound"), so a `.rplg` carrying only the battery would restore a core with an
// EMPTY working song and render silence. The export therefore has to happen with the core already in
// the state we want, which `ProjectStore.export` supports because it captures `readState` (the
// savestate) alongside `readSram`. So this script:
//
//   1. boots the cart with the metronome battery,
//   2. pokes the song into the RUNNING core's working song (the same seam the headless guards use -
//      far more robust than driving the ROM's file browser),
//   3. taps Play, which in a slave sync mode ARMS the ROM (sync_wait = 1, screen reads WAIT) rather
//      than starting it,
//   4. exports.
//
// The render then needs no input at all: the restored core is already armed, and the DAW transport's
// first 24-PPQN clock starts it. That is the SMS equivalent of lsdj-sync's `autoStart`.
//
// Serves BOTH machines - the Master System and Game Gear builds of smsggdj differ only in which pins
// carry the sync counter, so one script authors either fixture.
//
// Injected at bundle time by tools/author-sms-rplg.js.
//   __SMS_ROM__       absolute ROM path (smsggdj_v0_45.sms / .gg)
//   __SMS_MACHINE__   "sms" or "gg" - the sync role's wire format
//   __RPLG_OUT__      absolute output .rplg.zip path
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import { buildSmsMetronomeSav, pokeMetronomeIntoWram, SMS_SYNC_IN24 } from "./smsSyncSong";

declare const __SMS_ROM__: string;
declare const __SMS_MACHINE__: "sms" | "gg";
declare const __RPLG_OUT__: string;

// smsggdj's Play/Stop. Mesen puts Buttons::B on $DC bit 4 (SMS button 1) and Buttons::A on bit 5
// (button 2); PAUSE is its own line and rides the shared wire byte 7.
const PAUSE = 7;

const be = createRealBackend();
const audio = createAudioDriver();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());

// `enableFm: false` is no longer REQUIRED - the vendored SmsFmAudio change makes FM sum with the PSG
// instead of muting it (see sms-fm.test.ts) - but it is kept, for a different reason: with FM off the
// YM2413 provider contributes nothing to the mix at all, so the drift render measures the PSG
// metronome alone. The battery's OPTIONS block sets the ROM's own fm_on = 0 to match. Passed as a role
// config so it lands at CONSTRUCT: configureSms runs before LoadRom, so a later applyRoleConfig would
// not take.
const id = project.systems.adopt(
  {
    romPath: __SMS_ROM__,
    roles: [
      { kind: "mesen", config: { enableFm: false } },
      { kind: "sms-sync", config: { machine: __SMS_MACHINE__ } },
    ],
  },
  { sramBytes: buildSmsMetronomeSav(SMS_SYNC_IN24) },
);
if (id == null) throw new Error(`adopt failed for ${__SMS_ROM__}`);

audio.renderAudio(5000); // boot: splash, config_load (takes IN24 + fm off), song_new

const writes = pokeMetronomeIntoWram(be, id);
if (writes === 0) throw new Error("poked no bytes - the working song layout moved?");

// Arm. In IN24 this latches the idle line state into sync_last, sets sync_wait and shows WAIT; it does
// NOT start playback. The first host clock does that.
audio.pressButton(id, PAUSE, true);
audio.renderAudio(150);
audio.pressButton(id, PAUSE, false);
audio.renderAudio(600);

const ok = project.export(__RPLG_OUT__);
console.log(`[author-sms-rplg] ${__SMS_MACHINE__}: ${ok ? "wrote" : "FAILED"} ${__RPLG_OUT__} (poked ${writes} bytes)`);
(globalThis as { tjs?: { exit(code: number): void } }).tjs?.exit(ok ? 0 : 1);
