// Synthetic Mega Drive images for the TS tiers: the twins of `packages/blastem/native/test/SyntheticRom.hpp`, built
// from the same write tables and the same 68k encodings, so the parity group and the native cases boot the same bytes.
// A synthetic image is a console's own fixture, so it lives in `ts/fixtures.ts` and publishes as `./testFixtures`
// (spec/06), and the two files pin each other: `ts/test/fixtures.test.ts` hashes every image here, the native header's
// `SyntheticRom.test.cpp` pins the same numbers for its own, and a drift on either side fails one of the two.
//
// Each image is a 64 KiB cart (one Sega bank) with the "SEGA MEGA DRIVE " header at $100, no battery, a stack pointer
// of $00FFFE00 and a reset vector of $200 in its vectors, a table of `move.b #value,(address).L` register writes at
// $200, and a program after them ended by a `bra .` spin. The native header carries the encodings, the reason a program
// must start word-aligned, and what each image is for; both sides refuse an image that would overrun its spin.

/** One cart: the largest the ROM gate takes without a mapper, and the size every image here fills. */
export const MD_ROM_SIZE = 0x10000;

/** `move.b #value,(address).L`: one chip-register write, the layout's only write form. */
type Write = [address: number, value: number];

/** Vectors, header, the write table at $200, then `program` and its `bra .` spin. */
function image(program: number[], writes: Write[] = []): Uint8Array {
  const rom = new Uint8Array(MD_ROM_SIZE);
  rom.set([0x00, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x02, 0x00]); // SP $00FFFE00, reset PC $000200
  const magic = "SEGA MEGA DRIVE ";
  for (let i = 0; i < magic.length; i++) rom[0x100 + i] = magic.charCodeAt(i);
  let at = 0x200;
  for (const [address, value] of writes) {
    rom.set([0x13, 0xfc, 0x00, value, // move.b #value,(address).L
             (address >>> 24) & 0xff, (address >>> 16) & 0xff, (address >>> 8) & 0xff, address & 0xff], at);
    at += 8;
  }
  // An odd start is an address error and an overrun leaves no spin, so neither is an image worth booting.
  if (at % 2 !== 0 || at + program.length + 2 > rom.length)
    throw new Error("synthetic image: program must start word-aligned and fit before its spin");
  rom.set(program, at);
  at += program.length;
  rom[at] = 0x60; // bra.s *
  rom[at + 1] = 0xfe;
  return rom;
}

// The PSG: latch channel 0's tone (low nibble), its high six bits, then full volume. The same three writes the native
// header's images use, so the tone is the same tone.
const PSG_WRITES: Write[] = [
  [0xc00011, 0x8e],
  [0xc00011, 0x01],
  [0xc00011, 0x90],
];

/** The PSG tone plus a keyed YM2612 channel whose writes never land: the 68k reaches the YM only behind the Z80's bus
 *  request, which this image never asserts, so what sounds is the PSG. `fmOnlyRom` is the image that plays the YM. */
export function fmPsgRom(): Uint8Array {
  return image([0x60, 0xfe], [
    [0xa04000, 0x22], [0xa04001, 0x00], [0xa04000, 0x27], [0xa04001, 0x00],
    [0xa04000, 0x2b], [0xa04001, 0x80], [0xa04000, 0x30], [0xa04001, 0x01],
    [0xa04000, 0x40], [0xa04001, 0x00], [0xa04000, 0x50], [0xa04001, 0x1f],
    [0xa04000, 0x60], [0xa04001, 0x00], [0xa04000, 0x70], [0xa04001, 0x00],
    [0xa04000, 0x80], [0xa04001, 0x0f], [0xa04000, 0x90], [0xa04001, 0x00],
    [0xa04000, 0xa4], [0xa04001, 0x22], [0xa04000, 0xa0], [0xa04001, 0x69],
    [0xa04000, 0xb0], [0xa04001, 0x06], [0xa04000, 0xb4], [0xa04001, 0xc0],
    [0xa04000, 0x28], [0xa04001, 0xf0],
    ...PSG_WRITES,
  ]);
}

/** The same tone with the PSG alone: the FM half's own control in the audio tiers. */
export function psgOnlyRom(): Uint8Array {
  return image([0x60, 0xfe], PSG_WRITES);
}

/** The same patch as `fmPsgRom` with the two differences that make the YM2612 the only chip that sounds: the 68k
 *  asserts the Z80's bus request (so its YM writes are not dropped) and the algorithm is 7, all four operators
 *  carriers. No PSG write at all, so any output is the FM's. */
export function fmOnlyRom(): Uint8Array {
  return image([0x60, 0xfe], [
    [0xa11100, 0x01], [0xa11200, 0x01],
    [0xa04000, 0x22], [0xa04001, 0x00], [0xa04000, 0x27], [0xa04001, 0x00],
    [0xa04000, 0x2b], [0xa04001, 0x80], [0xa04000, 0x30], [0xa04001, 0x01],
    [0xa04000, 0x40], [0xa04001, 0x00], [0xa04000, 0x50], [0xa04001, 0x1f],
    [0xa04000, 0x60], [0xa04001, 0x00], [0xa04000, 0x70], [0xa04001, 0x00],
    [0xa04000, 0x80], [0xa04001, 0x0f], [0xa04000, 0x90], [0xa04001, 0x00],
    [0xa04000, 0xa4], [0xa04001, 0x22], [0xa04000, 0xa0], [0xa04001, 0x69],
    [0xa04000, 0xb0], [0xa04001, 0x07], [0xa04000, 0xb4], [0xa04001, 0xc0],
    [0xa04000, 0x28], [0xa04001, 0xf0],
  ]);
}

/** FNV-1a over an image: the number both files pin, so a byte that moves here and not in the header fails both. */
export function fnv1a(bytes: Uint8Array): number {
  let h = 2166136261 >>> 0;
  for (const b of bytes) {
    h ^= b;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

// A tiny assembler for the timing images: big-endian words at an absolute address, and only the encodings the native
// header lists. A branch's target is an absolute address, stamped by the same `at` the labels use, so a branch and its
// target cannot disagree.
class Asm {
  readonly rom = new Uint8Array(MD_ROM_SIZE);
  at = 0;

  private w(v: number): void {
    this.rom[this.at++] = (v >>> 8) & 0xff;
    this.rom[this.at++] = v & 0xff;
  }
  private l(v: number): void {
    this.w((v >>> 16) & 0xffff);
    this.w(v & 0xffff);
  }
  private moveWImmAbs(imm: number, addr: number): void {
    this.w(0x33fc);
    this.w(imm);
    this.l(addr);
  }
  private moveBImmAbs(imm: number, addr: number): void {
    this.w(0x13fc);
    this.w(imm);
    this.l(addr);
  }
  private moveLImmAbs(imm: number, addr: number): void {
    this.w(0x23fc);
    this.l(imm);
    this.l(addr);
  }
  // The displacement of a bra.w is measured from the extension word's address, two bytes past the opcode.
  private braW(target: number): void {
    const extension = this.at + 2;
    this.w(0x6000);
    this.w((target - extension) & 0xffff);
  }

  /** The image's own layout: vectors at $0, the HINT and VINT vectors at $70/$78, the header, the DMA source page, then
   *  the main program at $200, the HINT handler at $400 and the VINT handler at $420. `ints` enables VINT and HINT,
   *  `dma` also sets reg 1's DMA bit and starts a 68k-to-VDP transfer from the VINT handler, and `z80` loads a Z80
   *  program through the bus request window and reads Z80 RAM back in the main loop. The one entry the module's
   *  image functions reach for: the class itself stays inside this file. */
  timing(z80: boolean, ints: boolean, dma: boolean): Uint8Array {
    this.at = 0;
    this.l(0x00fffe00); // SP
    this.l(0x00000200); // reset PC
    this.at = 0x70;
    this.l(0x00000400); // HINT
    this.at = 0x78;
    this.l(0x00000420); // VINT
    const magic = "SEGA MEGA DRIVE ";
    for (let i = 0; i < magic.length; i++) this.rom[0x100 + i] = magic.charCodeAt(i);
    for (let i = 0; i < 0x200; i++) this.rom[0x1000 + i] = (i * 7 + 3) & 0xff; // the DMA source

    this.at = 0x200;
    this.moveWImmAbs(ints ? 0x8014 : 0x8004, 0xc00004); // reg 0: HINT enable
    this.moveWImmAbs(ints ? (dma ? 0x8174 : 0x8164) : dma ? 0x8154 : 0x8144, 0xc00004); // reg 1: display, VINT, DMA, mode 5
    this.moveWImmAbs(0x8a10, 0xc00004); // reg 10: HINT every 16 lines
    this.moveWImmAbs(0x8c81, 0xc00004); // reg 12: H40
    this.moveWImmAbs(0x8f02, 0xc00004); // reg 15: autoinc 2
    if (z80) {
      // Toggle a byte in Z80 RAM at $4000, read it back, store the program's state, and loop. The 68k asserts the bus
      // request, loads the program one byte at a time, releases reset, and then releases the bus.
      const prog = [0x3e, 0x2b, 0x32, 0x00, 0x40, 0x3e, 0x80, 0x32, 0x01, 0x40, 0x3e, 0x2a, 0x32, 0x00, 0x40, 0x21,
                    0x00, 0x10, 0x34, 0x7e, 0x32, 0x01, 0x40, 0x18, 0xf9];
      this.moveWImmAbs(0x0100, 0xa11100);
      this.moveWImmAbs(0x0000, 0xa11200);
      prog.forEach((b, i) => this.moveBImmAbs(b, 0xa00000 + i));
      this.moveWImmAbs(0x0100, 0xa11200);
      this.moveWImmAbs(0x0000, 0xa11100);
    }
    this.moveBImmAbs(0x8e, 0xc00011); // PSG channel 0: latch, the tone's high bits, then full volume
    this.moveBImmAbs(0x01, 0xc00011);
    this.moveBImmAbs(0x90, 0xc00011);
    if (ints) {
      this.w(0x46fc); // move.w #$2000,sr: supervisor, VINT (6) and HINT (4) pass
      this.w(0x2000);
    }
    const loop = this.at;
    this.w(0x3039);
    this.l(0xc00004); // move.w $C00004,d0 (status)
    this.w(0x3239);
    this.l(0xc00008); // move.w $C00008,d1 (HV)
    this.w(0xd441); // add.w d1,d2
    this.w(0x33c2);
    this.l(0xff0010); // move.w d2,$FF0010
    this.w(0xc6c1); // mulu.w d1,d3
    this.w(0x33c3);
    this.l(0xff0014); // move.w d3,$FF0014
    this.w(0x0242);
    this.w(0x000f); // andi.w #$F,d2
    this.w(0x0002);
    this.w(0x0090); // ori.b #$90,d2
    this.w(0x13c2);
    this.l(0xc00011); // move.b d2,$C00011 (PSG channel 0 volume)
    if (z80) {
      this.moveWImmAbs(0x0100, 0xa11100);
      this.w(0x1039);
      this.l(0xa01000); // move.b $A01000,d0 (Z80 RAM through the window)
      this.w(0x13c0);
      this.l(0xff0012); // move.b d0,$FF0012
      this.moveWImmAbs(0x0000, 0xa11100);
    }
    this.braW(loop);

    this.at = 0x400; // HINT
    this.w(0x5279);
    this.l(0xff0000); // addq.w #1,$FF0000
    this.w(0x4e73); // rte

    this.at = 0x420; // VINT
    this.w(0x5279);
    this.l(0xff0002); // addq.w #1,$FF0002
    this.w(0x33f9);
    this.l(0xc00008);
    this.l(0xff0004); // move.w $C00008,$FF0004
    if (dma) {
      // Source $0100 in the VDP's window, 256 words, then a VRAM write through the control port: a 68k-to-VDP DMA
      // (reg 23's high bit clear), which the VDP runs with the 68k stalled.
      this.moveWImmAbs(0x9300, 0xc00004);
      this.moveWImmAbs(0x9401, 0xc00004);
      this.moveWImmAbs(0x9500, 0xc00004);
      this.moveWImmAbs(0x9608, 0xc00004);
      this.moveWImmAbs(0x9700, 0xc00004);
      this.moveLImmAbs(0x40000080, 0xc00004);
    }
    this.w(0x4e73); // rte
    return this.rom;
  }
}

// The timing-observing images: a main loop that reads the VDP's status and HV counter (which sync the components at
// points the host did not choose), folds them into a PSG volume write, and spends a data-dependent number of clocks in
// `mulu`. The names say what else is on: interrupts, a Z80 running its own program behind a 68k bus request, and a
// 68k-to-VDP DMA started from the VINT handler.

/** VDP status/HV reads feeding the PSG, no interrupts: the components sync inside port handlers. */
export function hvPsgRom(): Uint8Array {
  return new Asm().timing(false, false, false);
}

/** The same loop with VINT and HINT enabled: interrupt entry and exit sync at points of the VDP's choosing. */
export function vintHintRom(): Uint8Array {
  return new Asm().timing(false, true, false);
}

/** And with the Z80 running its own program: what the 68k reads back from Z80 RAM depends on where the syncs fell. */
export function z80TimingRom(): Uint8Array {
  return new Asm().timing(true, true, false);
}

/** And with a 68k-to-VDP DMA from the VINT handler: the 68k stalls inside the control-port write for thousands of clocks. */
export function vintDmaRom(): Uint8Array {
  return new Asm().timing(true, true, true);
}
