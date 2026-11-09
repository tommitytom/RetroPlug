// LSDj's render hooks (packages/tracker/ts/trackerRender.ts) over a fake emulator host: what the shell's render library
// asks of the cart, without a core, an engine or a WAV. The library itself is held to its own suites; what is pinned
// here is that the cart's hook answers what the pipeline needs - LSDj promotes a slot into the SRAM it boots from,
// probes NR52 for the end of a song, and spells its song listings its own way.
import { test, expect } from "@retroplug/testing";
import type { EmulatorHost, Host } from "@retroplug/core/host";
import type { RenderFacts } from "@retroplug/tracker/trackerRender";
import { lsdjIntegration } from "../lsdjIntegration";
import { lsdjSongCatalog } from "../lsdjSongCatalog";
import { savBytes as lsdjSav } from "../fixtures";

const LSDJ = lsdjIntegration.render!;

/** A host over one work-RAM buffer, so a write is what the next read sees: the pitch a cart's load loop verifies
 *  against. Buttons and audio time are recorded, and the CPU answers what a case sets. */
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

test("LSDj's song listing keeps its own spelling", () => {
  // The selection errors and --list-songs print the cart's own line, which is why these are data on the hooks rather
  // than one neutral form: LSDj says "(no named projects)".
  expect(LSDJ.songList!.listing!([])).toBe("(no named projects)");
  expect(LSDJ.songList!.empty).toBe("(no named projects \u2014 only the working song)"); // the CLI's line, byte for byte
  const lsdj = LSDJ.listSongs(lsdjSav("lsdj499"));
  expect(LSDJ.songList!.listing!(lsdj)).toBe(lsdj.map((s) => `${s.index}: ${s.name}`).join(", "));
});
