#pragma once

// Synthetic Mega Drive images, hand-assembled: no assembler, so CI needs none. Each is a 64 KiB cart (one Sega bank)
// with the "SEGA MEGA DRIVE " header at $100, no battery, a stack pointer of $00FFFE00 and a reset vector of $000200
// in its vectors, a table of `move.b #value,(address).L` register writes at $200, and a program after them, ended by a
// `bra .` spin. A program must start on an even address and leave room for the two spin bytes: `image()` refuses
// anything else, because an odd start is an address error and an overrun is no image at all, and either proves nothing.
//
// The 68k encodings used, and nothing else, so a reader can check a byte against a manual:
//   13FC 00vv aaaaaaaa   move.b #vv,(aaaaaaaa).L
//   1039 aaaaaaaa       move.b (aaaaaaaa).L,d0
//   3039 aaaaaaaa       move.w (aaaaaaaa).L,d0
//   13C0 aaaaaaaa       move.b d0,(aaaaaaaa).L
//   33C1 aaaaaaaa       move.w d1,(aaaaaaaa).L
//   7200                moveq #0,d1
//   0200 00mm           andi.b #mm,d0
//   0C00 00mm           cmpi.b #mm,d0
//   5241                addq.w #1,d1
//   67dd / 66dd         beq.s / bne.s (dd is signed, from the address two bytes past the opcode)
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

// A YM2612 channel 1 patch plus key-on, then the PSG's tone: the FM and PSG paths both sound, and a case can tell them
// apart by rendering psgOnlyRom() as well. The registers are the minimum for a note: LFO off, DAC on, operator 1's
// multiplier/level/attack/decay/sustain, channel 1's frequency and algorithm, stereo both, all operators keyed on.
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

// A VDP status/HV poll in a tight loop: `move.w $C00004,d0; bra.s .`. Every real game's vblank wait is this loop, and
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

}  // namespace rp::blastem::test
