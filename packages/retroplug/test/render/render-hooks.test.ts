// Each cart's render hooks (packages/tracker/ts/trackerRender.ts) over a fake RenderHost: what the shell's render
// library asks of a cart, without a core, an engine or a WAV. The library itself is held to its own suites
// (test/render/*, test-native/render-*); what is pinned here is that a hook answers what the pipeline needs, and that
// each cart's oddity is the cart's: LSDj promotes a slot into the SRAM it boots from, risa presses SELECT+START and
// reads its sequencer flag, smsggdj writes into work RAM after the boot and knows the two things a render of it cannot
// do.
import { test, expect } from "@retroplug/testing";
import type { RenderFacts, RenderHost, RenderPlaySink } from "@retroplug/tracker/trackerRender";
import { lsdjIntegration } from "@retroplug/lsdj/lsdjIntegration";
import { lsdjSongCatalog } from "@retroplug/lsdj/lsdjSongCatalog";
import { savBytes as lsdjSav } from "@retroplug/lsdj/testFixtures";
import { risaIntegration } from "@retroplug/risa/risaIntegration";
import { risaSongCatalog } from "@retroplug/risa/risaSongCatalog";
import { savBytes as risaSav } from "@retroplug/risa/testFixtures";
import { loadSongToWorkingInSav } from "@retroplug/risa/risaSongOps";
import { smsggdjIntegration } from "@retroplug/smsggdj/smsggdjIntegration";
import { SMDJ4_BLOCK_LEN, buildSav, readSongBlock } from "@retroplug/smsggdj/codec/sav";
import { resolveSmsggdjLayout } from "@retroplug/smsggdj/runtime/layout";

const LSDJ = lsdjIntegration.render!;
const RISA = risaIntegration.render!;
const SMSGGDJ = smsggdjIntegration.render!;

/** A host over one work-RAM buffer, so a write is what the next read sees: the pitch a cart's load loop verifies
 *  against. Buttons and audio time are recorded, and the CPU answers what a case sets. */
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

// ---- LSDj ---------------------------------------------------------------------------------------------------------

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

test("LSDj lists the battery's songs and seeds a chosen one as the working song", () => {
  const sav = lsdjSav("lsdj499");
  const songs = LSDJ.listSongs(sav);
  expect(songs.length > 0).toBe(true);
  expect(songs.map((s) => ({ index: s.index, name: s.name }))).toEqual(lsdjSongCatalog.list(sav));
  const pick = songs[0];
  const seed = LSDJ.seedSong!(sav, pick.index);
  expect(seed !== null).toBe(true);
  // The seed is the image a fresh system boots from, and the slot the caller picked is what it plays.
  expect(lsdjSongCatalog.workingName(seed!)).toBe(pick.name);
  expect(lsdjSongCatalog.list(seed!)).toEqual(songs.map((s) => ({ index: s.index, name: s.name })));
  // A slot the battery does not hold seeds nothing, rather than a silent cart.
  expect(songs.some((s) => s.index === 31)).toBe(false);
  expect(LSDJ.seedSong!(sav, 31)).toBe(null);
});

test("LSDj's end-of-song probe is NR52's sound-enable bit", () => {
  const rom = new Uint8Array(0x150); // the probe is the ROM's cart's, so what it is handed is the ROM
  const on = fakeHost({ readCpu: () => 0x80 });
  const off = fakeHost({ readCpu: () => 0x00 });
  const unreadable = fakeHost({ readCpu: () => null });
  expect(LSDJ.playingProbe!(on.host, 1, rom, facts(), () => {})!()).toBe(true);
  expect(LSDJ.playingProbe!(off.host, 1, rom, facts(), () => {})!()).toBe(false);
  // A core whose register cannot be read says nothing, which reads as stopped: the cap bounds the render.
  expect(LSDJ.playingProbe!(unreadable.host, 1, rom, facts(), () => {})!()).toBe(false);
});

// ---- risa ---------------------------------------------------------------------------------------------------------

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

// ---- smsggdj ------------------------------------------------------------------------------------------------------

/** A ROM that identifies as an smsggdj build with a committed layout. */
function smsggdjRom(): Uint8Array {
  const rom = new Uint8Array(0x8000);
  const marker = "SMSGGDJ";
  for (let i = 0; i < marker.length; i++) rom[0x100 + i] = marker.charCodeAt(i);
  const version = "V0.45";
  for (let i = 0; i < version.length; i++) rom[0x180 + i] = version.charCodeAt(i);
  return rom;
}

/** A one-song battery this build can decode: the block's checksum is the image's, as buildSav writes it. */
function smsggdjSav(): Uint8Array {
  const block = new Uint8Array(SMDJ4_BLOCK_LEN);
  for (let i = 0; i < block.length; i += 16) block.set([0x11, 0xff, 0x00, 0x00], i);
  return buildSav([{ block, name: "TEST" }], 32 * 1024)!;
}

test("smsggdj loads a song into work RAM after the boot, and verifies it held", () => {
  const rom = smsggdjRom();
  const sav = smsggdjSav();
  const layout = resolveSmsggdjLayout("0.45")!;
  const { host, ram, audioMs } = fakeHost();
  SMSGGDJ.loadSong!(host, 1, rom, sav, 0);
  // The block the battery holds is what the cart's work RAM holds now, whole.
  const block = readSongBlock(sav, 0)!;
  expect(Array.from(ram.subarray(layout.song, layout.song + block.length))).toEqual(Array.from(block));
  // The load waited for the boot (up to 6 s of audio), then let the queued writes land: both are audio time.
  expect(audioMs.length > 0).toBe(true);
  // A slot with no song is a refusal, not a silent cart.
  expect(() => SMSGGDJ.loadSong!(host, 1, rom, sav, 9)).toThrow("could not load smsggdj song 9");
});

test("smsggdj presses Pause only on a cart it recognises and while the transport is stopped", () => {
  const rom = smsggdjRom();
  const layout = resolveSmsggdjLayout("0.45")!;
  const stopped = fakeHost();
  const { sink, pulls } = fakeSink();
  const leadIn = SMSGGDJ.pressPlay!(stopped.host, 1, rom, sink);
  expect(leadIn.length).toBe(1);
  expect(pulls).toEqual([100]);
  expect(stopped.buttons).toEqual([[7, true], [7, false]]);

  // A ROM carrying the build marker but no version string is not a build whose transport this knows: pressing Pause on
  // an ordinary Master System game pauses it.
  const unversioned = rom.slice();
  unversioned.fill(0, 0x180, 0x1a0);
  const other = fakeHost();
  expect(SMSGGDJ.pressPlay!(other.host, 1, unversioned, fakeSink().sink).length).toBe(0);
  expect(other.buttons).toEqual([]);

  // Already running (a render restored from a savestate, which carries work RAM): pressing would STOP it.
  const running = fakeHost();
  running.ram[layout.playState] = 1;
  const quiet = fakeSink();
  expect(SMSGGDJ.pressPlay!(running.host, 1, rom, quiet.sink).length).toBe(0);
  expect(running.buttons).toEqual([]);
  expect(quiet.pulls).toEqual([]);
});

test("smsggdj says what a render of it cannot do: no end marker, and a blank boot without a song", () => {
  const rom = smsggdjRom();
  const smsFacts = (over: Partial<RenderFacts> = {}): RenderFacts => facts({ rom, sav: smsggdjSav(), ...over });

  // Where a probe that can find the end would have answered: no pinned length, so the cap is what the WAV gets.
  const probeSays = (f: RenderFacts): string[] => {
    const out: string[] = [];
    SMSGGDJ.playingProbe!(fakeHost().host, 1, rom, f, (m) => out.push(m));
    return out;
  };
  const noEnd = probeSays(smsFacts());
  expect(noEnd.length).toBe(1);
  expect(noEnd[0].includes("loop forever")).toBe(true);
  expect(noEnd[0].includes("600000ms")).toBe(true);
  // A pinned duration answers it, and a ROM that is not this cart is left alone.
  expect(probeSays(smsFacts({ durationMs: 5000 }))).toEqual([]);
  expect(probeSays(facts({ rom: new Uint8Array(0x100) }))).toEqual([]);

  // The boot state: a blank boot is silence unless a song was selected or a savestate carries the working song.
  const said = (f: RenderFacts): string[] => {
    const out: string[] = [];
    SMSGGDJ.warnings!(f, (m) => out.push(m));
    return out;
  };
  const blank = said(smsFacts());
  expect(blank.length).toBe(1);
  expect(blank[0].includes("BLANK song")).toBe(true);
  expect(blank[0].includes("0:TEST")).toBe(true);
  expect(said(smsFacts({ song: true }))).toEqual([]);
  expect(said(smsFacts({ state: true }))).toEqual([]);
  expect(said(facts({ rom: new Uint8Array(0x100) }))).toEqual([]);
});

test("each cart's song listing keeps its own spelling", () => {
  // The selection errors and --list-songs print the cart's own line, which is why these are data on the hooks rather
  // than one neutral form: LSDj says "(no named projects)", smsggdj writes "0:METRO" with no space after the colon.
  expect(LSDJ.songList!.listing!([])).toBe("(no named projects)");
  expect(LSDJ.songList!.empty).toBe("(no named projects \u2014 only the working song)"); // the CLI's line, byte for byte
  const lsdj = LSDJ.listSongs(lsdjSav("lsdj499"));
  expect(LSDJ.songList!.listing!(lsdj)).toBe(lsdj.map((s) => `${s.index}: ${s.name}`).join(", "));
  expect(RISA.songList!.listing!([])).toBe("(no saved songs)");
  expect(RISA.songList!.empty).toBe("(no saved songs in the catalog)");
  expect(SMSGGDJ.songList!.listing!([{ index: 0, name: "METRO" }])).toBe("0:METRO");
  expect(SMSGGDJ.songList!.listing!([])).toBe("(none)");
  expect(SMSGGDJ.songList!.empty).toBe("(no saved songs in the catalog)");
});
