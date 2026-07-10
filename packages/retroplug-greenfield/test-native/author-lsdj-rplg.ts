// Author a greenfield LSDj `.rplg` fixture for the real-Reaper DAW-timing renders (the greenfield twin of
// the legacy test/ts `lsdj_*_metro|drift` bootstraps). Runs on native-greenfield-host: compose a store
// over the real backend, adopt the LSDj ROM with an authored sav + lsdj-sync mode, boot it to the song
// screen (and, for the MidiSync scenarios, press START to ARM "wait for MIDI clock" so the fixture's
// savestate captures the armed state — the Reaper host transport then clocks it from t=0), and export.
//
// Scenario + paths are injected at bundle time by tools/author-lsdj-rplg.js.
//   __SCENARIO__  "midi-metro" | "arduinoboy-metro" | "midi-drift"
//   __LSDJ_ROM__  absolute ROM path
//   __RPLG_OUT__  absolute output .rplg path
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import { savFromJson } from "../src/lsdjSav";

declare const __SCENARIO__: string;
declare const __LSDJ_ROM__: string;
declare const __RPLG_OUT__: string;

// A hard-panned pulse note per scenario. midi-* wait for the host's MIDI clock (SYNC=Midi, role MidiSync);
// arduinoboy waits for note 24 to arm play (SYNC=Lsdj, role MidiSyncArduinoboy). The drift song is
// percussive with one note per beat so every beat is a distinct transient the drift analyzer can pair.
const SONGS: Record<string, { syncMode: string; mode: number; autoStart: boolean; song: unknown }> = {
  "midi-metro": {
    syncMode: "Midi", mode: 1, autoStart: true,
    song: {
      rows: [{ chains: [0] }],
      chains: [{ phrases: [0] }],
      phrases: [{ notes: [1], instruments: [0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
    },
  },
  "arduinoboy-metro": {
    syncMode: "Lsdj", mode: 2, autoStart: false, // note 24 in the .rpp arms play at render t=0
    song: {
      rows: [{ chains: [0] }],
      chains: [{ phrases: [0] }],
      phrases: [{ notes: [1], instruments: [0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
    },
  },
  "midi-drift": {
    syncMode: "Midi", mode: 1, autoStart: true,
    song: {
      rows: [{ chains: [0] }],
      chains: [{ phrases: [0] }],
      // One note per beat (steps 0/4/8/12) on a short, percussive instrument so each beat retriggers a
      // fresh transient (LENGTH-limited, fast decay) — the drift analyzer pairs each to a click beat.
      phrases: [{ notes: [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0], instruments: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", length: 4, adsr: { initialLevel: 15, decaySpeed: 6 } }],
    },
  },
};

const scenario = SONGS[__SCENARIO__];
if (!scenario) throw new Error(`unknown scenario: ${__SCENARIO__}`);

const sav = savFromJson(JSON.stringify({ workingSong: { formatVersion: 22, settings: { syncMode: scenario.syncMode }, ...(scenario.song as object) } }));

const be = createRealBackend();
const audio = createAudioDriver();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());

// Adopt the LSDj ROM with the authored sav (auto-attaches sameboy + lsdj-sync via romProviders), then
// set the lsdj-sync mode + autoStart for this scenario. The armed state can't be captured in a savestate
// (it doesn't survive restore), so the fixture is just LSDj cold at the SONG SCREEN — the render arms it:
// MidiSync via autoStart (taps START on transport rise), Arduinoboy via the note-24 MIDI item in the .rpp.
const id = project.systems.adopt({ romPath: __LSDJ_ROM__ }, { sramBytes: sav });
if (id == null) throw new Error(`adopt failed for ${__LSDJ_ROM__}`);
project.systems.setRoleConfig(id, "lsdj-sync", { mode: scenario.mode, autoStart: scenario.autoStart });

audio.renderAudio(6000); // boot to the song screen from the sav (the savestate skips this in the render)

const ok = project.export(__RPLG_OUT__);
console.log(`[author-lsdj-rplg] ${__SCENARIO__}: ${ok ? "wrote" : "FAILED"} ${__RPLG_OUT__}`);
(globalThis as { tjs: { exit(code: number): void } }).tjs.exit(ok ? 0 : 1);
