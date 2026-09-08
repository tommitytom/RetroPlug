// The NES→host back-channel, end to end: a 6502 instruction writing to $40F0 comes out of
// `backend.drainCoreBytes(id)`, and a file placed on the emulated SD card comes back through
// `CMD_F_FRD`. Both halves of what a ROM needs to stream data off the cartridge.
//
// It uses ROMs assembled here rather than a fixture, because the chain being proved is long - CPU write
// → Mesen's memory manager → the Edio command parser → the FIFO role → the system → the debug RPC → TS -
// and every link of it is RetroPlug's. A ROM that only sits in resources/ would prove the same thing but
// couldn't be read alongside the assertion. There is deliberately no toolchain here: the programs are a
// handful of LDA/STA pairs, so they are emitted directly.
//
// (The FIFO's own protocol semantics - framing, ack handshake, resp bytes - are covered without a CPU at
// all in packages/native/test/audio/NesEverdriveFifo.test.cpp.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import type { Session } from "../cli/session";

const FIFO_DATA = 0x40f0;
const REG_MSTAT = 0x40ff;
const MSTAT_MCU_PEND = 0x02;
const CMD_STATUS = 0x10;
const CMD_USB_WR = 0x22;
const CMD_F_FOPN = 0xc9;
const CMD_F_FRD = 0xca;
const CMD_F_FRD_MEM = 0xcb;
const FA_READ = 0x01;
const MEM_VRAM = 3; // rp::MemoryType::Vram, CHR-RAM on a cart that has it

/** `LDA #value` then `STA $40F0` — one byte pushed into the cartridge FIFO. */
function pushByte(value: number): number[] {
  return [0xa9, value & 0xff, 0x8d, FIFO_DATA & 0xff, FIFO_DATA >> 8];
}

/** The 4-byte Edio frame ed_cmd_tx sends: '+', '+'^0xFF, cmd, cmd^0xFF. */
function edioCmd(cmd: number): number[] {
  return [...pushByte(0x2b), ...pushByte(0x2b ^ 0xff), ...pushByte(cmd), ...pushByte(cmd ^ 0xff)];
}

/** Wrap 6502 code as a bootable NROM (mapper 0) cartridge: 16 KB PRG at $C000 + 8 KB CHR. */
function nromRom(code: number[]): Uint8Array {
  const PRG = 16 * 1024;
  const CHR = 8 * 1024;
  const rom = new Uint8Array(16 + PRG + CHR);
  rom.set([0x4e, 0x45, 0x53, 0x1a, 1, 1], 0); // "NES\x1A", 1x16K PRG, 1x8K CHR, mapper 0
  if (code.length > PRG - 6) throw new Error("program does not fit in one PRG bank");
  rom.set(code, 16);
  // Vectors live in the last 6 bytes of the bank; RESET points at the start of it ($C000).
  rom.set([0x00, 0xc0, 0x00, 0xc0, 0x00, 0xc0], 16 + PRG - 6);
  return rom;
}

/** The same cartridge with NO CHR bank, so Mesen gives the mapper 8 KB of CHR-RAM instead. That RAM
 *  is what a `CMD_F_FRD_MEM` DMA writes into, and what `readMemory(id, MEM_VRAM)` reads back. */
function chrRamRom(code: number[]): Uint8Array {
  const PRG = 16 * 1024;
  const rom = new Uint8Array(16 + PRG);
  rom.set([0x4e, 0x45, 0x53, 0x1a, 1, 0], 0); // "NES\x1A", 1x16K PRG, 0 CHR banks, mapper 0
  if (code.length > PRG - 6) throw new Error("program does not fit in one PRG bank");
  rom.set(code, 16);
  rom.set([0x00, 0xc0, 0x00, 0xc0, 0x00, 0xc0], 16 + PRG - 6);
  return rom;
}

/** The u32 little-endian parameter form `ed_cmd_file_read_mem` sends. */
function pushU32(value: number): number[] {
  return [0, 8, 16, 24].flatMap((shift) => pushByte((value >>> shift) & 0xff));
}

/** `LDA #value` then `STA $40FF`: arm the mapper status bits the wait loop will watch. */
function armMstat(value: number): number[] {
  return [0xa9, value & 0xff, 0x8d, REG_MSTAT & 0xff, REG_MSTAT >> 8];
}

/** The SDK's wait loop, at its simplest: spin until the MCU-pending bit clears. `dma.s` also checks
 *  the strobe and the high nibble (both covered without a CPU in NesEverdriveFifo.test.cpp); what
 *  this proves is that the bit CLEARS in emulated time, so a real ROM gets out of its loop. */
const WAIT_FOR_MCU: number[] = [
  0xad, REG_MSTAT & 0xff, REG_MSTAT >> 8, // LDA $40FF
  0x29, MSTAT_MCU_PEND,                   // AND #$02
  0xd0, 0xf9,                             // BNE -7 (back to the LDA)
];

/** Relay one byte of the FIFO's reply to the host: `LDA $40F0` then `STA $40F0` inside a USB_WR. */
const RELAY_ONE: number[] = [
  0xad, FIFO_DATA & 0xff, FIFO_DATA >> 8,
  0x8d, FIFO_DATA & 0xff, FIFO_DATA >> 8,
];

/** Ask for the last command's result and hand both status bytes to the host. */
const REPORT_STATUS: number[] = [
  ...edioCmd(CMD_STATUS),
  ...edioCmd(CMD_USB_WR),
  ...pushByte(0x02), // u16 payload length
  ...pushByte(0x00),
  ...RELAY_ONE,
  ...RELAY_ONE,
];

/** `code`, then a `jmp` to itself — the ROM does its one job and parks. */
function parkAfter(code: number[]): number[] {
  const here = 0xc000 + code.length;
  return [...code, 0x4c, here & 0xff, here >> 8];
}

/** Build `rom` at `path` and boot it with the emulated SD card pointed at `sdRoot`. */
function bootWith(rom: Uint8Array, path: string, sdRoot?: string): { s: Session; id: number } {
  const s = bootSession();
  expect(s.backend.writeFile(path, rom)).toBeTruthy();
  let id = s.project.systems.addSystem(path);
  if (id == null) throw new Error(`addSystem failed for ${path}`);
  if (sdRoot != null) {
    // sdRoot is baked in when the core is constructed (the FIFO is built at activate), so it only takes
    // effect after a reset — which hands back a NEW id, exactly like the `region` knob.
    expect(s.project.systems.setRoleConfig(id, "mesen", { sdRoot })).toBeTruthy();
    const next = s.project.systems.reset(id);
    if (next == null) throw new Error("reset after setRoleConfig failed");
    id = next;
  }
  return { s, id };
}

test("a ROM's CMD_USB_WR reaches the host through drainCoreBytes", () => {
  const payload = [0xde, 0xad, 0xbe, 0xef];
  const code = parkAfter([
    ...edioCmd(CMD_USB_WR),
    ...pushByte(payload.length), // u16 length, little-endian
    ...pushByte(0x00),
    ...payload.flatMap(pushByte),
  ]);
  const { s, id } = bootWith(nromRom(code), "/tmp/rp-corebytes-usbwr.nes");

  // Nothing has run yet, so there is nothing to drain — and the call must still be safe.
  expect(Array.from(s.backend.drainCoreBytes(id))).toEqual([]);

  s.audio.renderAudio(50); // the ROM sends its message within the first few hundred cycles
  expect(Array.from(s.backend.drainCoreBytes(id))).toEqual(payload);

  // A drain takes rather than peeks, and the parked ROM sends nothing more.
  s.audio.renderAudio(50);
  expect(Array.from(s.backend.drainCoreBytes(id))).toEqual([]);

  s.project.systems.removeSystem(id);
});

test("drainCoreBytes is empty, not an error, on a core with no back-channel", () => {
  const s = bootSession();
  const id = s.project.systems.loadMgb();
  if (id == null) throw new Error("loadMgb failed");
  s.audio.renderAudio(50);
  expect(Array.from(s.backend.drainCoreBytes(id))).toEqual([]);
  // A dead handle answers the same way, so a polling caller never has to guard.
  expect(Array.from(s.backend.drainCoreBytes(9999))).toEqual([]);
  s.project.systems.removeSystem(id);
});

test("a ROM reads a file off the emulated SD card and reports it over the back-channel", () => {
  // The round trip a data-streaming ROM actually makes: open a file on the card, read a block, and hand
  // what it got back to the host. Proves `sdRoot` reaches the FIFO and that CMD_F_FRD's framing is what a
  // 6502 program driving it expects (one resp byte, then the data).
  const sdRoot = "/tmp/rp-corebytes-card";
  const fileBytes = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88];
  const path = "/stream/clip.bin";

  const s0 = bootSession();
  expect(s0.backend.writeFile(sdRoot + path, new Uint8Array(fileBytes))).toBeTruthy();

  // Open for read, request the file's 8 bytes, then relay the resp byte + payload to the host. The ROM
  // can't branch on $40F1 without a poll loop, and it doesn't need to: the emulated device answers a
  // command before the next instruction runs, so the reply is already waiting.
  const readByteToUsb = [
    0xad, FIFO_DATA & 0xff, FIFO_DATA >> 8, // LDA $40F0  — pop one reply byte
    0x8d, FIFO_DATA & 0xff, FIFO_DATA >> 8, // STA $40F0  — and push it straight back
  ];
  const code = parkAfter([
    ...edioCmd(CMD_F_FOPN),
    ...pushByte(FA_READ),
    ...pushByte(path.length), // u16 path length
    ...pushByte(0x00),
    ...Array.from(path, (c) => pushByte(c.charCodeAt(0))).flat(),
    // FOPN stores its result for a separate CMD_STATUS query, which this ROM skips: the read below
    // fails loudly (resp != 0) if the open didn't take.
    ...edioCmd(CMD_F_FRD),
    ...pushByte(fileBytes.length), // u32 length
    ...pushByte(0x00),
    ...pushByte(0x00),
    ...pushByte(0x00),
    // Relay resp + the 8 data bytes. Each relay is inside one CMD_USB_WR of length 1, so the host sees
    // them as a stream rather than needing the ROM to buffer.
    ...Array.from({ length: fileBytes.length + 1 }, () => [
      ...edioCmd(CMD_USB_WR),
      ...pushByte(0x01),
      ...pushByte(0x00),
      ...readByteToUsb,
    ]).flat(),
  ]);

  const { s, id } = bootWith(nromRom(code), "/tmp/rp-corebytes-sdread.nes", sdRoot);
  s.audio.renderAudio(50);

  const got = Array.from(s.backend.drainCoreBytes(id));
  expect(got.length).toEqual(fileBytes.length + 1);
  expect(got[0]).toEqual(0x00); // CMD_F_FRD success — the open found the file under sdRoot
  expect(got.slice(1)).toEqual(fileBytes);

  s.project.systems.removeSystem(id);
  s.backend.deleteFile(sdRoot + path);
});

test("without sdRoot the same ROM finds nothing, wherever the process was started", () => {
  // The old behaviour resolved "/" against the CWD, so a file a test wrote could silently become the
  // next run's card. An unset root is a per-process scratch dir, so this open must fail.
  const fileBytes = [0x11, 0x22];
  const path = "/rp-corebytes-cwd.bin";
  const s0 = bootSession();
  // Written relative to the process's working directory — what "/" used to mean.
  expect(s0.backend.writeFile(path.slice(1), new Uint8Array(fileBytes))).toBeTruthy();

  const code = parkAfter([
    ...edioCmd(CMD_F_FOPN),
    ...pushByte(FA_READ),
    ...pushByte(path.length),
    ...pushByte(0x00),
    ...Array.from(path, (c) => pushByte(c.charCodeAt(0))).flat(),
    ...edioCmd(CMD_F_FRD),
    ...pushByte(fileBytes.length),
    ...pushByte(0x00),
    ...pushByte(0x00),
    ...pushByte(0x00),
    ...edioCmd(CMD_USB_WR),
    ...pushByte(0x01),
    ...pushByte(0x00),
    0xad, FIFO_DATA & 0xff, FIFO_DATA >> 8,
    0x8d, FIFO_DATA & 0xff, FIFO_DATA >> 8,
  ]);

  const { s, id } = bootWith(nromRom(code), "/tmp/rp-corebytes-nosd.nes");
  s.audio.renderAudio(50);

  // FAT_NO_FILE from the read, not the file's first byte.
  expect(Array.from(s.backend.drainCoreBytes(id))).toEqual([0x04]);

  s.project.systems.removeSystem(id);
  s.backend.deleteFile(path.slice(1));
});

// --- CMD_F_FRD_MEM: the MCU writes cartridge memory itself -------------------------------------
//
// The other way a cartridge gets data off the card, and the one the emulator could not check at all
// until now: instead of replying through the FIFO, the MCU reads the file straight into cartridge
// memory over the PI bus while the 6502 waits on $40FF. Proven on a real N8 Pro from a running game,
// where the only available check was to power-cycle the console and diff a CHR dump.

/** Index of the first differing byte, or -1 when equal. Keeps a 4 KB mismatch to one number. */
function firstDiff(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; ++i) if (a[i] !== b[i]) return i;
  return a.length === b.length ? -1 : n;
}

/** Open `path`, DMA `len` bytes of it to `piAddr`, wait for the MCU, and report the status. */
function dmaProgram(path: string, piAddr: number, len: number): number[] {
  return parkAfter([
    ...edioCmd(CMD_F_FOPN),
    ...pushByte(FA_READ),
    ...pushByte(path.length), // u16 path length
    ...pushByte(0x00),
    ...Array.from(path, (c) => pushByte(c.charCodeAt(0))).flat(),
    // The parameters STAGE the transfer; the arm and the exec write below are what run it. Sent in
    // this order because that is the order the device's own SDK sends them (ed_run_dma).
    ...edioCmd(CMD_F_FRD_MEM),
    ...pushU32(piAddr),
    ...pushU32(len),
    ...armMstat(MSTAT_MCU_PEND),
    ...pushByte(0x00), // _ed_halt_exec: the write that actually starts the MCU routine
    ...WAIT_FOR_MCU,
    ...REPORT_STATUS,
  ]);
}

// CHR-RAM's base on the PI bus: the upper 4 MB of the CHR chip, not ADDR_CHR (everdrive.sv forces
// bit 22 for a chr_ram game). The same address `n8-load --dump-chr` reads a CHR-RAM cart at.
const PI_CHR_RAM = 0x00c00000;

test("a ROM DMAs a file off the card straight into CHR-RAM", () => {
  // The whole chain, none of it mocked: a 6502 STA reaches Mesen's memory manager, the Edio parser
  // stages and runs the transfer, the cart writer resolves an N8 PI address onto Mesen's CHR-RAM,
  // and readMemory hands the bytes back. A frame of nesvj's video is exactly this, 4096 bytes of it.
  const sdRoot = "/tmp/rp-corebytes-dma-card";
  const path = "/clip.bin";
  const payload = new Uint8Array(4096);
  for (let i = 0; i < payload.length; ++i) payload[i] = (i * 7 + 3) & 0xff;

  const s0 = bootSession();
  expect(s0.backend.writeFile(sdRoot + path, payload)).toBeTruthy();

  const { s, id } = bootWith(chrRamRom(dmaProgram(path, PI_CHR_RAM, payload.length)),
                             "/tmp/rp-corebytes-dma.nes", sdRoot);
  s.audio.renderAudio(50);

  // An empty drain here means the ROM is still in its wait loop: the pending bit never cleared, so
  // a real console would have spun to its timeout and given up on the DMA.
  expect(Array.from(s.backend.drainCoreBytes(id))).toEqual([0x00, 0xa5]);

  const chr = s.backend.readMemory(id, MEM_VRAM);
  expect(chr).toBeTruthy();
  expect(chr!.length).toBeGreaterThanOrEqual(payload.length);
  expect(firstDiff(chr!.slice(0, payload.length), payload)).toEqual(-1);

  s.project.systems.removeSystem(id);
  s.backend.deleteFile(sdRoot + path);
});

test("a DMA into a cart with no CHR-RAM is refused, not silently dropped", () => {
  // nromRom ships CHR-ROM, so PI $C00000 is not backed on this cart. The MCU still ANSWERS - the
  // wait loop has to exit, or the ROM could never learn anything - and CMD_STATUS is where it finds
  // out, which is what lets it fall back to the byte-at-a-time path that always works.
  //
  // A silent drop would be the worst outcome available: indistinguishable from a working DMA, so the
  // ROM would display whatever was already in CHR-RAM and report success.
  const sdRoot = "/tmp/rp-corebytes-dma-nochr-card";
  const path = "/clip.bin";
  const payload = new Uint8Array([0x11, 0x22, 0x33, 0x44]);

  const s0 = bootSession();
  expect(s0.backend.writeFile(sdRoot + path, payload)).toBeTruthy();

  const { s, id } = bootWith(nromRom(dmaProgram(path, PI_CHR_RAM, payload.length)),
                             "/tmp/rp-corebytes-dma-nochr.nes", sdRoot);
  s.audio.renderAudio(50);

  const got = Array.from(s.backend.drainCoreBytes(id));
  expect(got.length).toEqual(2);
  expect(got[0]).toBeGreaterThan(0); // a real Edio error, reported through the usual status query
  expect(got[1]).toEqual(0xa5);

  s.project.systems.removeSystem(id);
  s.backend.deleteFile(sdRoot + path);
});
