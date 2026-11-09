// risa's render hooks over a fake RenderHost: what the shell's render library asks of the cart, without a core, an
// engine or a WAV. risa presses SELECT+START with two discarded prep chunks, reads its own sequencer flag for the end of
// a song, and says what it cannot do when the ROM predates a runtime layout.
import { test, expect } from "@retroplug/testing";
import type { RenderFacts, RenderHost, RenderPlaySink } from "@retroplug/tracker/trackerRender";
import { risaIntegration } from "../risaIntegration";
import { risaSongCatalog } from "../risaSongCatalog";
import { savBytes as risaSav } from "../fixtures";
import { loadSongToWorkingInSav } from "../risaSongOps";

const RISA = risaIntegration.render!;

/** A host over one work-RAM buffer, so a write is what the next read sees. Buttons and audio time are recorded, and
 *  the CPU answers what a case sets. */
function fakeHost(over: Partial<RenderHost> & { ram?: Uint8Array } = {}): {
  host: RenderHost;
  ram: Uint8Array;
  buttons: [number, boolean][];
  audioMs: number[];
} {
  const ram = over.ram ?? new Uint8Array(0x2000);
  const buttons: [number, boolean][] = [];
  const audioMs: number[] = [];
  const host: RenderHost = {
    renderAudio: (ms) => audioMs.push(ms),
    readCpu: () => null,
    readRam: () => ram,
    writeRam: (_id, offset, bytes) => {
      if (offset < 0 || offset + bytes.length > ram.length) return false;
      ram.set(bytes, offset);
      return true;
    },
    pressButton: (_id, button, down) => buttons.push([button, down]),
    ...over,
  };
  return { host, ram, buttons, audioMs };
}

/** A sink that answers one silent chunk per pull and counts the pulls: what a play gesture captures through. */
function fakeSink(): { sink: RenderPlaySink; pulls: number[] } {
  const pulls: number[] = [];
  return {
    sink: {
      renderChunk: (ms) => {
        pulls.push(ms);
        return [new Float32Array(Math.round((ms * 44100) / 1000) * 2)];
      },
    },
    pulls,
  };
}

/** The request facts a hook is handed: no ROM or battery read, nothing pinned, no song, no savestate, the 10-minute
 *  cap. */
const facts = (over: Partial<RenderFacts> = {}): RenderFacts => ({
  rom: null,
  sav: null,
  state: false,
  song: false,
  durationMs: undefined,
  maxDurationMs: 600000,
  ...over,
});

test("risa lists and seeds its catalog, and begins a song on SELECT+START", () => {
  const sav = risaSav("v2_blumarbl");
  const songs = RISA.listSongs(sav);
  expect(songs.length > 0).toBe(true);
  expect(songs.map((s) => ({ index: s.index, name: s.name }))).toEqual(risaSongCatalog.list(sav));
  const seed = RISA.seedSong!(sav, songs[0].index);
  expect(seed).toEqual(loadSongToWorkingInSav(sav, songs[0].index));
  // A slot the catalog does not hold seeds nothing.
  expect(RISA.seedSong!(sav, 31)).toBe(null);
  // ... and says why, in its own words: a legacy-layout catalog lists slots this build cannot promote.
  expect(RISA.seedRefusal!(31)).toBe("render: could not load risa song 31 (needs a current-layout catalog)");

  // The gesture: two discarded prep chunks, then the SELECT+START chunk captured as the lead-in.
  const { host, buttons } = fakeHost();
  const { sink, pulls } = fakeSink();
  const leadIn = RISA.pressPlay!(host, 1, new Uint8Array(0), sink);
  expect(pulls.length).toBe(3);
  expect(leadIn.length).toBe(1);
  expect(leadIn[0][0].length / 2).toBe(pulls[2] * 44.1); // interleaved stereo: frames are halves
  // Start down, Start up (the nudge), then SELECT and START together, then both released.
  expect(buttons).toEqual([
    [7, true],
    [7, false],
    [6, true],
    [7, true],
    [7, false],
    [6, false],
  ]);
});

test("risa's probe reads its sequencer flag, and says why it cannot when the build has no layout", () => {
  const warnings: string[] = [];
  const rom = new Uint8Array(0x4000);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // iNES
  expect(RISA.playingProbe!(fakeHost().host, 1, rom, facts(), (m) => warnings.push(m))).toBe(null);
  expect(warnings.length).toBe(1);
  expect(warnings[0].includes("no runtime layout")).toBe(true);
  // It names the length the render falls back to, so the log says what the WAV will be.
  expect(warnings[0].includes("rendering a fixed 600000ms")).toBe(true);
});

test("risa's song listing keeps its own spelling", () => {
  expect(RISA.songList!.listing!([])).toBe("(no saved songs)");
  expect(RISA.songList!.empty).toBe("(no saved songs in the catalog)");
});
