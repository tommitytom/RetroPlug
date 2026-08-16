// `writeRam`: the control-plane WRITE counterpart of `readRam`.
//
// The asymmetry it closes: reading a core's work RAM has always been a control-plane operation
// (`readRam` serves a per-block published snapshot, race-free by construction), but the only write was
// `writeCpu` - a debug-facet poke straight into the live core from the calling thread, documented as
// valid only while the audio thread is stopped. So the plugin could read a running cart's RAM and never
// write it, which was an artifact of what had been built rather than a boundary anyone chose.
//
// This one is queued through the same invoker as `pressButton`, so it lands BETWEEN blocks and is safe
// while the core plays. It is deliberately unguarded beyond bounds - poking a running program can
// confuse it, because the emulated code has invariants over those bytes that nothing here knows about.
// That is the feature, not a defect. Bounds are refused because a write past the region corrupts the
// host, which is a crash rather than a confused ROM.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const WRAM_LEN = 8192; // SMS work RAM, based at CPU $C000

let nextId = 1;

function boot(): { be: ReturnType<typeof createRealBackend>; audio: ReturnType<typeof createAudioDriver>; id: number } {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const id = nextId++;
  expect(
    be.constructSystem(
      {
        romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "",
        savPath: null, statePath: null, settings: JSON.stringify({ enableFm: false }),
      },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(1000); // let the cart boot so its RAM is real content, not power-on fill
  return { be, audio, id };
}

test("a poke lands in the same region readRam serves, at the same offset", () => {
  const be0 = createRealBackend();
  if (!be0.fileExists(ROM)) {
    console.log(`# SKIP write-ram: missing ${ROM}`);
    return;
  }
  const { be, audio, id } = boot();
  const before = be.readRam(id)!;
  expect(before.length).toBe(WRAM_LEN);

  // A high offset the running ROM does not immediately overwrite. Coordinates are shared with readRam
  // by contract, so this is a plain index - no CPU-address conversion anywhere.
  const offset = 0x1f00;
  const payload = Uint8Array.from([0xde, 0xad, 0xbe, 0xef, 0x55]);
  expect(be.writeRam(id, offset, payload)).toBeTruthy();
  audio.renderAudio(20); // the write is QUEUED: it applies on the next block, not inside the call

  const after = be.readRam(id)!;
  expect(after.subarray(offset, offset + payload.length)).toEqual(payload);
  // ...and nothing either side moved, so the offset is exact rather than approximately right.
  expect(after[offset - 1]).toBe(before[offset - 1]);
  expect(after[offset + payload.length]).toBe(before[offset + payload.length]);
  expect(be.removeSystem(id)).toBeTruthy();
});

test("a bulk poke the size of a song block lands whole", () => {
  // The case the union-of-PODs command ring cannot carry inline, and the reason this rides an owning
  // heap payload like setSystems and loadKernel do. 6,912 bytes is an smsggdj song.
  const be0 = createRealBackend();
  if (!be0.fileExists(ROM)) return;
  const { be, audio, id } = boot();
  const block = new Uint8Array(6912);
  for (let i = 0; i < block.length; i++) block[i] = (i * 7 + 13) & 0xff;

  expect(be.writeRam(id, 0, block)).toBeTruthy();
  audio.renderAudio(20);
  const ram = be.readRam(id)!;
  // Compare the whole span, not a spot check: a transport that truncated at some chunk boundary would
  // pass a first-and-last-byte test.
  let firstBad = -1;
  for (let i = 0; i < block.length; i++) {
    if (ram[i] !== block[i]) { firstBad = i; break; }
  }
  console.log(`[write-ram] 6912-byte poke: firstBad=${firstBad}`);
  expect(firstBad).toBe(-1);
  expect(be.removeSystem(id)).toBeTruthy();
});

test("it works while the core is RUNNING - which writeCpu cannot promise", () => {
  // The whole point. The write is packed into a DspCommand and drained by the audio loop between
  // blocks, so there is no window where the control thread touches the live core.
  const be0 = createRealBackend();
  if (!be0.fileExists(ROM)) return;
  const { be, audio, id } = boot();

  // Interleave pokes with rendering, each landing while the emulation advances.
  let landed = 0;
  for (let n = 0; n < 8; n++) {
    const off = 0x1e00 + n * 4;
    expect(be.writeRam(id, off, Uint8Array.from([n, n + 1, n + 2, n + 3]))).toBeTruthy();
    audio.renderAudio(50); // ~2200 frames of emulation between pokes
    const ram = be.readRam(id)!;
    if (ram[off] === n && ram[off + 3] === n + 3) landed++;
  }
  console.log(`[write-ram] ${landed}/8 pokes observed while running`);
  expect(landed).toBe(8);
  expect(be.removeSystem(id)).toBeTruthy();
});

test("bounds are the one thing it refuses", () => {
  const be0 = createRealBackend();
  if (!be0.fileExists(ROM)) return;
  const { be, audio, id } = boot();

  // Answered SYNCHRONOUSLY, unlike pressButton's fire-and-forget. A dropped key edge is a lost
  // keypress; a dropped 6,912-byte write is a caller that thinks it loaded a song.
  expect(be.writeRam(id, WRAM_LEN, Uint8Array.from([1]))).toBeFalsy(); // starts past the end
  expect(be.writeRam(id, WRAM_LEN - 2, Uint8Array.from([1, 2, 3]))).toBeFalsy(); // overruns by one
  expect(be.writeRam(id, 0, new Uint8Array(0))).toBeFalsy(); // nothing to write
  expect(be.writeRam(id, WRAM_LEN - 1, Uint8Array.from([0x99]))).toBeTruthy(); // the last byte IS writable

  // A partial write is refused rather than clipped: half a song block is not a song, and a caller that
  // asked for 6,912 bytes has no use for the first 300.
  audio.renderAudio(20);
  expect(be.readRam(id)![WRAM_LEN - 1]).toBe(0x99);
  expect(be.removeSystem(id)).toBeTruthy();
});

test("a poke to a system that does not exist is refused, and says so", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) return;
  // Answerable synchronously from the published snapshot (no RAM published for an unknown id), so the
  // caller gets false rather than a queued write that quietly evaporates.
  expect(be.writeRam(9999, 0, Uint8Array.from([1, 2, 3]))).toBeFalsy();
  createAudioDriver().renderAudio(20);
});
