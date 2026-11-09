// smsggdj's render hooks over a fake emulator host: what the shell's render library asks of the cart, without a core,
// an engine or a WAV. smsggdj writes a song into work RAM after the boot and verifies it held, presses Pause only on a
// build it recognises and while the transport is stopped, and says what a render of it cannot do.
import { test, expect } from "@retroplug/testing";
import type { EmulatorHost, Host } from "@retroplug/core/host";
import type { RenderFacts, RenderPlaySink } from "@retroplug/tracker/trackerRender";
import { smsggdjIntegration } from "../smsggdjIntegration";
import { SMDJ4_BLOCK_LEN, buildSav, readSongBlock } from "../codec/sav";
import { resolveSmsggdjLayout } from "../runtime/layout";

const SMSGGDJ = smsggdjIntegration.render!;

/** A host over one work-RAM buffer, so a write is what the next read sees. Buttons and audio time are recorded, and
 *  the CPU answers what a case sets. */
function fakeHost(over: Partial<EmulatorHost> & { ram?: Uint8Array } = {}): {
  host: Host<"emulator">;
  ram: Uint8Array;
  buttons: [number, boolean][];
  audioMs: number[];
} {
  const ram = over.ram ?? new Uint8Array(0x2000);
  const buttons: [number, boolean][] = [];
  const audioMs: number[] = [];
  const host: Host<"emulator"> = {
    emulator: {
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
    },
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

test("smsggdj's song listing keeps its own spelling", () => {
  expect(SMSGGDJ.songList!.listing!([{ index: 0, name: "METRO" }])).toBe("0:METRO");
  expect(SMSGGDJ.songList!.listing!([])).toBe("(none)");
  expect(SMSGGDJ.songList!.empty).toBe("(no saved songs in the catalog)");
});
