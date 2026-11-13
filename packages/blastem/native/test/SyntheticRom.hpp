#pragma once

// Synthetic Mega Drive images, hand-assembled: no assembler, so CI needs none. Each is a 64 KiB cart (one Sega bank)
// with the "SEGA MEGA DRIVE " header at $100, no battery, a stack pointer of $00FFFE00 and a reset vector of $000200
// in its vectors, a table of `move.b #value,(address).L` register writes at $200, and a program after them, ended by a
// `bra .` spin. A program must start on an even address and leave room for the two spin bytes: `image()` refuses
// anything else, because an odd start is an address error and an overrun is no image at all, and either proves nothing.
//
// The 68k encodings used, and nothing else, so a reader can check a byte against a manual:
//   13FC 00vv aaaaaaaa   move.b #vv,(aaaaaaaa).L
//   23FC vvvvvvvv aaaaaaaa move.l #vvvvvvvv,(aaaaaaaa).L
//   33FC vvvv aaaaaaaa   move.w #vvvv,(aaaaaaaa).L
//   1039 aaaaaaaa       move.b (aaaaaaaa).L,d0
//   3039 aaaaaaaa       move.w (aaaaaaaa).L,d0
//   3239 aaaaaaaa       move.w (aaaaaaaa).L,d1
//   13C0 aaaaaaaa       move.b d0,(aaaaaaaa).L
//   13C2 aaaaaaaa       move.b d2,(aaaaaaaa).L
//   33C1 aaaaaaaa       move.w d1,(aaaaaaaa).L
//   33C2 aaaaaaaa       move.w d2,(aaaaaaaa).L
//   33F9 aaaaaaaa aaaaaaaa move.w (aaaaaaaa).L,(aaaaaaaa).L
//   5279 aaaaaaaa       addq.w #1,(aaaaaaaa).L
//   7200                moveq #0,d1
//   0200 00mm           andi.b #mm,d0
//   0242 vvvv           andi.w #vvvv,d2
//   0000 00mm           ori.b #mm,d0
//   0002 00mm           ori.b #mm,d2
//   E808                lsr.b #4,d0
//   0C00 00mm           cmpi.b #mm,d0
//   5241                addq.w #1,d1
//   D441                add.w d1,d2
//   C6C1                mulu.w d1,d3 (an unsigned multiply: its cost depends on the operands)
//   46FC vvvv           move.w #vvvv,sr
//   67dd / 66dd         beq.s / bne.s (dd is signed, from the address two bytes past the opcode)
//   6000 dddd           bra.w  (dd is signed, from the address of dddd)
//   4E73                rte
//   60dd                bra.s
//   60FE                bra.s  -2 (a spin)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace rp::blastem::test {

struct Write {
    std::uint32_t address;
    std::uint8_t  value;
};

// `= {}` on `writes`, and the includes above, are not style: without the default, every no-write image (portPollRom,
// syncEdgeRom) fails to compile, without <cstring> the memcpy below has no declaration, and without <stdexcept> the
// refusal below has no type.
inline std::vector<std::uint8_t> image(const std::vector<std::uint8_t>& program,
                                       const std::vector<Write>& writes = {}) {
    std::vector<std::uint8_t> rom(0x10000, 0x00);
    const std::uint8_t vectors[8] = {0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x02, 0x00};  // SP $00FFFE00, PC $000200
    std::memcpy(rom.data(), vectors, sizeof(vectors));
    std::memcpy(rom.data() + 0x100, "SEGA MEGA DRIVE ", 16);
    std::size_t at = 0x200;
    for (const Write& w : writes) {
        const std::uint8_t ins[8] = {0x13, 0xFC, 0x00, w.value,
                                     std::uint8_t(w.address >> 24), std::uint8_t(w.address >> 16),
                                     std::uint8_t(w.address >> 8), std::uint8_t(w.address)};
        std::memcpy(rom.data() + at, ins, sizeof(ins));
        at += sizeof(ins);
    }
    if (at % 2 != 0 || at + program.size() + 2 > rom.size()) {
        throw std::length_error("synthetic image: program must start word-aligned and fit before its spin");
    }
    std::memcpy(rom.data() + at, program.data(), program.size());
    at += program.size();
    rom[at] = 0x60;  // bra.s *
    rom[at + 1] = 0xFE;
    return rom;
}

inline std::uint32_t fnv1a(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t h = 2166136261u;
    for (std::uint8_t b : bytes) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}
// The PSG: latch channel 0's tone (low nibble), its high six bits, then full volume. The SN76489 register layout, so
// the same bytes sound on the Master System: Mesen's SmsAudio case uses this exact sequence.
inline std::vector<std::uint8_t> psgOnlyRom() {
    return image({0x60, 0xFE}, {{0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

// A YM2612 channel 1 patch plus key-on, then the PSG's tone: only the PSG sounds, because the 68k reaches the YM only
// with the Z80's bus (genesis.c's io_write requires `!z80_enabled || z80_get_busack`) and this image never asserts the
// bus request. It stays as it is because a case pins its hash; fmOnlyRom is the image that plays the YM.
inline std::vector<std::uint8_t> fmPsgRom() {
    return image({0x60, 0xFE},
                 {{0xA04000, 0x22}, {0xA04001, 0x00}, {0xA04000, 0x27}, {0xA04001, 0x00},
                  {0xA04000, 0x2B}, {0xA04001, 0x80}, {0xA04000, 0x30}, {0xA04001, 0x01},
                  {0xA04000, 0x40}, {0xA04001, 0x00}, {0xA04000, 0x50}, {0xA04001, 0x1F},
                  {0xA04000, 0x60}, {0xA04001, 0x00}, {0xA04000, 0x70}, {0xA04001, 0x00},
                  {0xA04000, 0x80}, {0xA04001, 0x0F}, {0xA04000, 0x90}, {0xA04001, 0x00},
                  {0xA04000, 0xA4}, {0xA04001, 0x22}, {0xA04000, 0xA0}, {0xA04001, 0x69},
                  {0xA04000, 0xB0}, {0xA04001, 0x06}, {0xA04000, 0xB4}, {0xA04001, 0xC0},
                  {0xA04000, 0x28}, {0xA04001, 0xF0},
                  {0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

// The same patch as fmPsgRom, with two differences that make the YM the ONLY chip that sounds: the 68k asserts the Z80's
// bus request (so its YM writes are not dropped) and the algorithm is 7, all four operators carriers, because with
// algorithm 6 operator 1 is a modulator and the patch would be silent. No PSG write at all, so any output is the YM's.
// The registers are the minimum for a note: LFO off, DAC on, operator 1's multiplier/level/attack/decay/sustain, channel
// 1's frequency and algorithm, stereo both, all operators keyed on.
inline std::vector<std::uint8_t> fmOnlyRom() {
    return image({0x60, 0xFE},
                 {{0xA11100, 0x01}, {0xA11200, 0x01},
                  {0xA04000, 0x22}, {0xA04001, 0x00}, {0xA04000, 0x27}, {0xA04001, 0x00},
                  {0xA04000, 0x2B}, {0xA04001, 0x80}, {0xA04000, 0x30}, {0xA04001, 0x01},
                  {0xA04000, 0x40}, {0xA04001, 0x00}, {0xA04000, 0x50}, {0xA04001, 0x1F},
                  {0xA04000, 0x60}, {0xA04001, 0x00}, {0xA04000, 0x70}, {0xA04001, 0x00},
                  {0xA04000, 0x80}, {0xA04001, 0x0F}, {0xA04000, 0x90}, {0xA04001, 0x00},
                  {0xA04000, 0xA4}, {0xA04001, 0x22}, {0xA04000, 0xA0}, {0xA04001, 0x69},
                  {0xA04000, 0xB0}, {0xA04001, 0x07}, {0xA04000, 0xB4}, {0xA04001, 0xC0},
                  {0xA04000, 0x28}, {0xA04001, 0xF0}});
}

// `move.b $A10005,d0; move.b d0,$FF0000; bra .`: what the ROM reads off controller port 2, in work RAM's first byte.
// ONE read, then the spin: this is the image for a case that reads the byte once (the memory cases).
inline std::vector<std::uint8_t> portPollRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05, 0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00});
}

// The same read and store, FOREVER: the port device's cases push a level one block and read the byte the next, which
// one read cannot show. The loop is 12 bytes of instruction plus the branch, so the `bra.s` at loop + 12 has its PC at
// loop + 14 and the displacement is -14 = 0xF2.
inline std::vector<std::uint8_t> portPollLoopRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00,             // move.b d0,$FF0000
                  0x60, 0xF2});                                   // bra.s  loop
}

// Reads port 2 in a loop and puts the two sync lines straight onto the PSG's channel 0 volume: bits 5 and 6 become
// attenuation bits 0 and 1, so each of the four levels the host can drive is a different amplitude, and every level a
// case stages is audible at its own sample (which a one-edge image cannot show). The tone is set up by the write table
// and the loop rewrites only the volume.
//
//   $200  the write table: the PSG's tone latch and its high bits (16 bytes, so the program starts at $210)
//   $210  move.b $A10005,d0
//   $216  andi.b #$60,d0
//   $21A  lsr.b #4,d0           (bits 5 and 6 become bits 0 and 1)
//   $21C  ori.b #$90,d0         (channel 0's volume latch, attenuation 0 to 3)
//   $220  move.b d0,$C00011
//   $226  bra.s  loop           ($E8: $210 - $228 is -24)
inline std::vector<std::uint8_t> portLevelRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                         // andi.b #$60,d0
                  0xE8, 0x08,                                     // lsr.b #4,d0
                  0x00, 0x00, 0x00, 0x90,                         // ori.b #$90,d0
                  0x13, 0xC0, 0x00, 0xC0, 0x00, 0x11,             // move.b d0,$C00011 (channel 0 volume)
                  0x60, 0xE8},                                    // bra.s  loop
                 {{0xC00011, 0x8E}, {0xC00011, 0x01}});
}

// Polls port 2 in a tight loop, counting its polls, and leaves the loop the first time TR/TH differ from their idle
// level ($60): it stores the count at $FF0000 (a word) and the masked level it read at $FF0002, which is therefore one
// of $00, $20, $40, $60. The count is the poll loop's own clock, so a case reads WHEN a level arrived in units the ROM
// can see, not in host samples.
//
//   $200  moveq #0,d1                 ($202 is the loop's head; the moveq runs once)
//   $202  move.b $A10005,d0           <- loop
//   $208  andi.b #$60,d0
//   $20C  cmpi.b #$60,d0
//   $210  bne.s  store                ($04: the level changed, so leave)
//   $212  addq.w #1,d1                (one poll counted)
//   $214  bra.s  loop                 ($EC: $202 - $216)
//   $216  move.w d1,$FF0000           store:
//   $21C  move.b d0,$FF0002
inline std::vector<std::uint8_t> portTimestampRom() {
    return image({0x72, 0x00,                                     // moveq #0,d1
                  0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                         // andi.b #$60,d0
                  0x0C, 0x00, 0x00, 0x60,                         // cmpi.b #$60,d0
                  0x66, 0x04,                                     // bne.s  store
                  0x52, 0x41,                                     // addq.w #1,d1
                  0x60, 0xEC,                                     // bra.s  loop
                  0x33, 0xC1, 0x00, 0xFF, 0x00, 0x00,             // move.w d1,$FF0000
                  0x13, 0xC0, 0x00, 0xFF, 0x00, 0x02});           // move.b d0,$FF0002
}

// A VDP status/HV poll in a tight loop, and NO port read: the image for a case that stages levels on a port nobody
// polls. `move.w $C00004,d0; bra.s .`. Every real game's vblank wait is this loop, and
// the control port's read handler syncs the components (vdp_port_read -> sync_components), so this is the image a case
// needs when a stop or a rebase falls on a handler's sync rather than on an instruction boundary. 8 bytes: the read
// and the branch, whose PC is loop + 8, so its displacement is -8 = 0xF8.
inline std::vector<std::uint8_t> vdpPollRom() {
    return image({0x30, 0x39, 0x00, 0xC0, 0x00, 0x04,  // move.w $C00004,d0  <- loop
                  0x60, 0xF8});                       // bra.s  loop
}

// Holds PSG channel 0 at full volume, then silences it the first time TR/TH leave their idle level, and records that
// it happened at $FF0001. An audio edge a case can time: the level's sample is the sample the volume drops on. Its
// program starts at $218 (three PSG writes precede it), its loop is 14 bytes, and its branch is 0xF0 ($218 - $228).
//
//   $200  $C00011 <- $8E, $01, $90    the three writes, from the table below
//   $218  move.b $A10005,d0           <- loop
//   $21E  andi.b #$60,d0
//   $222  cmpi.b #$60,d0
//   $226  beq.s  loop                 ($F0: the branch at $226 has its PC at $228, and $218 - $228 is -16)
//   $228  move.b #$9F,$C00011         channel 0, attenuation 15 (silence)
//   $230  move.b #$01,$FF0001         the flag
inline std::vector<std::uint8_t> syncEdgeRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,              // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                          // andi.b #$60,d0
                  0x0C, 0x00, 0x00, 0x60,                          // cmpi.b #$60,d0
                  0x67, 0xF0,                                      // beq.s  loop
                  0x13, 0xFC, 0x00, 0x9F, 0x00, 0xC0, 0x00, 0x11,  // move.b #$9F,$C00011 (channel 0 level 15)
                  0x13, 0xFC, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x01}, // move.b #$01,$FF0001 (the flag)
                  {{0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

// A tiny assembler for the timing images below: big-endian words, an absolute label (`at`), and only the encodings the
// list above names. A program's labels are stamped with `at` so a branch and its target cannot disagree.
struct Asm {
    std::vector<std::uint8_t> rom = std::vector<std::uint8_t>(0x10000, 0x00);
    std::uint32_t             at = 0;

    void w(std::uint16_t v) {
        rom[at++] = std::uint8_t(v >> 8);
        rom[at++] = std::uint8_t(v);
    }
    void l(std::uint32_t v) {
        w(std::uint16_t(v >> 16));
        w(std::uint16_t(v));
    }
    void moveWImmAbs(std::uint16_t imm, std::uint32_t addr) { w(0x33FC); w(imm); l(addr); }
    void moveBImmAbs(std::uint8_t imm, std::uint32_t addr) { w(0x13FC); w(imm); l(addr); }
    void moveLImmAbs(std::uint32_t imm, std::uint32_t addr) { w(0x23FC); l(imm); l(addr); }
    // The displacement of a bra.w is measured from the extension word's address, two bytes past the opcode.
    void braW(std::uint32_t target) {
        const std::uint32_t extension = at + 2;
        w(0x6000);
        w(static_cast<std::uint16_t>(static_cast<std::int32_t>(target) - static_cast<std::int32_t>(extension)));
    }
};

// A ROM that keeps the 68k inside one 64 KiB 68k-to-VRAM DMA after another: the VDP runs the transfer with the 68k
// stalled, so the longest stall the bus can produce is how far a run's stop is overshot. Measured at 53,693,175 Hz, one
// 64K-word transfer costs the 68k 11,980,603 master clocks with the display on in H32 (a fifth of a second, and the
// slowest grant rate there is), against 2,192,160 with the display off. `h40` is the wider, faster mode the audio cases
// use; H32 is the worst case the ring's size is derived from.
//
//   $200  VDP regs 0, 1, 15, 12: no HINT, display on or off, autoincrement 2, H32 or H40
//   $218  loop: regs 19-23 = length $FFFF, source $000000, then a VRAM-write command word that starts the DMA
inline std::vector<std::uint8_t> dmaLoopRom(bool display, bool h40 = true) {
    Asm a;
    a.at = 0;
    a.l(0x00FFFE00);  // SP
    a.l(0x00000200);  // reset PC
    std::memcpy(a.rom.data() + 0x100, "SEGA MEGA DRIVE ", 16);
    a.at = 0x200;
    a.moveWImmAbs(0x8004, 0xC00004);                         // reg 0: no HINT
    a.moveWImmAbs(display ? 0x8154 : 0x8114, 0xC00004);     // reg 1: display on/off, VINT, DMA, mode 5
    a.moveWImmAbs(0x8F02, 0xC00004);                         // reg 15: autoincrement 2
    a.moveWImmAbs(h40 ? 0x8C81 : 0x8C00, 0xC00004);         // reg 12: H40 or H32
    a.moveBImmAbs(0x8E, 0xC00011);                          // PSG channel 0: latch, high bits, full volume, so the ROM's
    a.moveBImmAbs(0x01, 0xC00011);                          // output is a tone a case can see and not just the DMA
    a.moveBImmAbs(0x90, 0xC00011);
    const std::uint32_t loop = a.at;
    a.moveWImmAbs(0x93FF, 0xC00004);                         // DMA length, low
    a.moveWImmAbs(0x94FF, 0xC00004);                         // DMA length, high ($FFFF = 65,535 words)
    a.moveWImmAbs(0x9500, 0xC00004);                         // DMA source, low
    a.moveWImmAbs(0x9600, 0xC00004);                         // DMA source, mid
    a.moveWImmAbs(0x9700, 0xC00004);                         // DMA source, high (68k memory)
    a.moveLImmAbs(0x40000080, 0xC00004);                     // VRAM write command: starts the transfer
    a.braW(loop);
    return a.rom;
}

// The timing-observing images: what a real game does that a tight register-write loop does not. Each one's main loop
// reads the VDP's status and HV counter (which sync the components at points the host did not choose), folds them into
// a PSG volume write, and spends a data-dependent number of clocks in `mulu`; the flags add the interrupt pairs, a Z80
// running its own program behind a 68k bus request, and a 68k-to-VDP DMA started from the VINT handler, which blocks
// the 68k inside the control-port write for thousands of clocks. They are here because a stop model that is transparent
// on a `bra .` spin can still move the emulation on a ROM that watches its own timing.
//
//   $0     SP $00FFFE00, reset PC $000200
//   $70    HINT (autovector 4) -> $400
//   $78    VINT (autovector 6) -> $420
//   $100   the SEGA header
//   $200   VDP register writes, optional Z80 program, PSG tone, then the loop
//   $400   the HINT handler
//   $420   the VINT handler
//
// `ints` enables VINT and HINT (reg 0's 0x10 and reg 1's 0x20), `dma` also sets reg 1's DMA bit, `z80` loads a Z80
// program through the bus request window and then reads Z80 RAM in the main loop. Without `ints` the VDP's interrupt
// pins stay quiet, which is the no-interrupt variant a case uses when it wants the VDP reads alone.
inline std::vector<std::uint8_t> timingRom(bool z80, bool ints, bool dma) {
    Asm a;
    a.at = 0;
    a.l(0x00FFFE00);  // SP
    a.l(0x00000200);  // reset PC
    a.at = 0x70;
    a.l(0x00000400);  // HINT
    a.at = 0x78;
    a.l(0x00000420);  // VINT
    std::memcpy(a.rom.data() + 0x100, "SEGA MEGA DRIVE ", 16);
    for (std::uint32_t i = 0; i < 0x200; ++i) a.rom[0x1000 + i] = std::uint8_t(i * 7 + 3);  // the DMA source

    a.at = 0x200;
    a.moveWImmAbs(ints ? 0x8014 : 0x8004, 0xC00004);                                  // reg 0: HINT enable
    a.moveWImmAbs(ints ? (dma ? 0x8174 : 0x8164) : (dma ? 0x8154 : 0x8144), 0xC00004);  // reg 1: display, VINT, DMA, mode 5
    a.moveWImmAbs(0x8A10, 0xC00004);                                                  // reg 10: HINT every 16 lines
    a.moveWImmAbs(0x8C81, 0xC00004);                                                  // reg 12: H40
    a.moveWImmAbs(0x8F02, 0xC00004);                                                  // reg 15: autoinc 2
    if (z80) {
        // Toggle a byte in Z80 RAM at $4000, read it back, store the program's state, and loop. The 68k asserts the
        // bus request, loads the program one byte at a time, releases reset, and then releases the bus.
        static const std::uint8_t prog[] = {0x3E, 0x2B, 0x32, 0x00, 0x40, 0x3E, 0x80, 0x32, 0x01, 0x40,
                                            0x3E, 0x2A, 0x32, 0x00, 0x40, 0x21, 0x00, 0x10, 0x34, 0x7E,
                                            0x32, 0x01, 0x40, 0x18, 0xF9};
        a.moveWImmAbs(0x0100, 0xA11100);
        a.moveWImmAbs(0x0000, 0xA11200);
        for (unsigned i = 0; i < sizeof(prog); ++i) a.moveBImmAbs(prog[i], 0xA00000 + i);
        a.moveWImmAbs(0x0100, 0xA11200);
        a.moveWImmAbs(0x0000, 0xA11100);
    }
    a.moveBImmAbs(0x8E, 0xC00011);  // PSG channel 0: latch, then the tone's high bits, then full volume
    a.moveBImmAbs(0x01, 0xC00011);
    a.moveBImmAbs(0x90, 0xC00011);
    if (ints) {
        a.w(0x46FC);  // move.w #$2000,sr: supervisor, interrupts at level 5 and below masked (VINT 6 and HINT 4 pass)
        a.w(0x2000);
    }
    const std::uint32_t loop = a.at;
    a.w(0x3039);
    a.l(0xC00004);  // move.w $C00004,d0 (status)
    a.w(0x3239);
    a.l(0xC00008);  // move.w $C00008,d1 (HV)
    a.w(0xD441);    // add.w d1,d2
    a.w(0x33C2);
    a.l(0xFF0010);  // move.w d2,$FF0010
    a.w(0xC6C1);    // mulu.w d1,d3
    a.w(0x33C3);
    a.l(0xFF0014);  // move.w d3,$FF0014
    a.w(0x0242);
    a.w(0x000F);  // andi.w #$F,d2
    a.w(0x0002);
    a.w(0x0090);  // ori.b #$90,d2
    a.w(0x13C2);
    a.l(0xC00011);  // move.b d2,$C00011 (PSG channel 0 volume)
    if (z80) {
        a.moveWImmAbs(0x0100, 0xA11100);
        a.w(0x1039);
        a.l(0xA01000);  // move.b $A01000,d0 (Z80 RAM through the window)
        a.w(0x13C0);
        a.l(0xFF0012);  // move.b d0,$FF0012
        a.moveWImmAbs(0x0000, 0xA11100);
    }
    a.braW(loop);

    a.at = 0x400;  // HINT
    a.w(0x5279);
    a.l(0xFF0000);  // addq.w #1,$FF0000
    a.w(0x4E73);    // rte

    a.at = 0x420;  // VINT
    a.w(0x5279);
    a.l(0xFF0002);  // addq.w #1,$FF0002
    a.w(0x33F9);
    a.l(0xC00008);
    a.l(0xFF0004);  // move.w $C00008,$FF0004
    if (dma) {
        // Source $0100 in the VDP's window, 256 words, then a VRAM write through the control port: a 68k-to-VDP DMA
        // (reg 23's high bit clear), which the VDP runs with the 68k stalled.
        a.moveWImmAbs(0x9300, 0xC00004);
        a.moveWImmAbs(0x9401, 0xC00004);
        a.moveWImmAbs(0x9500, 0xC00004);
        a.moveWImmAbs(0x9608, 0xC00004);
        a.moveWImmAbs(0x9700, 0xC00004);
        a.moveLImmAbs(0x40000080, 0xC00004);
    }
    a.w(0x4E73);  // rte
    return a.rom;
}

// VDP status/HV reads feeding the PSG, no interrupts: the components are synced by port handlers in the middle of the
// 68k's own instructions, which is where a rebase or a stop must not happen.
inline std::vector<std::uint8_t> hvPsgRom() { return timingRom(false, false, false); }
// The same loop with VINT and HINT enabled: interrupt entry and exit sync at points of the VDP's choosing.
inline std::vector<std::uint8_t> vintHintRom() { return timingRom(false, true, false); }
// And with the Z80 running its own program: the bus arbiter grants the Z80 the bus between syncs, so what the 68k reads
// back from Z80 RAM depends on where the syncs fell.
inline std::vector<std::uint8_t> z80TimingRom() { return timingRom(true, true, false); }
// And with a 68k-to-VDP DMA started from the VINT handler: the 68k stops inside the control-port write for thousands
// of clocks, so a stop bound of one instruction is not a bound of a few hundred clocks.
inline std::vector<std::uint8_t> vintDmaRom() { return timingRom(true, true, true); }

}  // namespace rp::blastem::test
