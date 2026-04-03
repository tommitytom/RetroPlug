# EverMIDI ROM — Agent Instructions

## Project Overview

This directory contains the NES ROM source for **EverMIDI**, a real-time MIDI synthesizer that runs on an EverDrive-N8 (Pro) flash cartridge. It receives MIDI messages over USB and plays them through the NES APU hardware. Optional expansion audio chips (VRC6, VRC7, Sunsoft 5B, Namco 163) can be enabled at build time — each requires a separate ROM since only one mapper can be active.

## Build System

- **Toolchain:** cc65 suite (`cc65`, `ca65`, `ld65`)
- **Target:** NES, 6502 CPU
- **Build:** Run `make` in this directory

### Build variants

```bash
make                    # APU only (NROM mapper 0) -> n8-midi.nes
make MAPPER=vrc6        # + VRC6 expansion (mapper 24) -> n8-midi-vrc6.nes
make MAPPER=vrc7        # + VRC7 expansion (mapper 85) -> n8-midi-vrc7.nes
make MAPPER=s5b         # + Sunsoft 5B expansion (mapper 69) -> n8-midi-s5b.nes
make MAPPER=n163        # + Namco 163 expansion (mapper 19) -> n8-midi-n163.nes
make all-mappers        # builds all five variants
```

The `MAPPER` variable sets a `-DUSE_xxx` flag passed to both cc65 and ca65. This controls:
- The iNES mapper number in the ROM header (crt0.s)
- Mapper-specific bank setup in the startup code (crt0.s)
- Which expansion audio source file is compiled and linked
- MIDI channel routing in main.c

Build steps (see [Makefile](Makefile)):
1. `.c` files → `.s` (cc65 with `-O`)
2. `.s`/`.asm` files → `.o` (ca65 with `-t nes --cpu 6502`)
3. `.o` files linked with `nes.cfg` → ROM file

## File Map

| File | Purpose |
|------|---------|
| [main.c](main.c) | APU channel control, MIDI parsing loop, frequency LUTs |
| [main.h](main.h) | Shared types (`u8`, `u16`, `u32`) and CC constants |
| [vrc6.c](vrc6.c) / [vrc6.h](vrc6.h) | VRC6 expansion audio (2 pulse + 1 sawtooth), mapper 24 |
| [vrc7.c](vrc7.c) / [vrc7.h](vrc7.h) | VRC7 expansion audio (6 FM channels, YM2413), mapper 85 |
| [sunsoft.c](sunsoft.c) / [sunsoft.h](sunsoft.h) | Sunsoft 5B expansion audio (3 square, AY-3-8910), mapper 69 |
| [n163.c](n163.c) / [n163.h](n163.h) | Namco 163 expansion audio (4 wavetable channels), mapper 19 |
| [everdrive.c](everdrive.c) / [everdrive.h](everdrive.h) | EverDrive FIFO/USB communication protocol |
| [sys.c](sys.c) / [sys.h](sys.h) | PPU rendering, controller input, screen buffer |
| [crt0.s](crt0.s) | Assembly entry point, mapper-conditional bank setup, NES vectors |
| [zeropage.inc](zeropage.inc) | Zero-page symbol declarations |
| [nes.cfg](nes.cfg) | ld65 memory map and segment configuration |

## Coding Conventions

- **Types:** Use `u8`, `u16`, `u32` (defined in [main.h](main.h)); avoid `int` except where cc65 requires it
- **Naming:** `UPPER_CASE` macros, `snake_case` functions and variables, `_prefixed` per-channel state variables
- **Hardware access:** Direct register access via volatile pointer macros — e.g., `#define REG_FOO (*(volatile u8 *)0xADDR)`
- **Memory:** No dynamic allocation; static arrays and zero-page variables only
- **Performance:** Use pre-computed lookup tables for all frequency/timer calculations — do not use division or floating point at runtime
- **Expansion chip ifdefs:** Use `#ifdef USE_VRC6` / `#elif defined(USE_VRC7)` / `#elif defined(USE_S5B)` / `#elif defined(USE_N163)` / `#else` (APU-only default). Same pattern in crt0.s: `.ifdef USE_VRC6` / `.elseif .defined(USE_VRC7)` / etc.

## MIDI Channel Mapping

### APU channels (always present)

| MIDI Ch | Hardware | Function |
|---------|----------|----------|
| 1 | APU Pulse 1 | `pulse1_note_on/off` |
| 2 | APU Pulse 2 | `pulse2_note_on/off` |
| 3 | APU Triangle | `tri_note_on/off` |
| 4 | APU Noise | `noise_note_on/off` |
| 5 | Reserved | — |

### Expansion channels (mapper dependent)

| Define | MIDI Ch | Hardware | Function |
|--------|---------|----------|----------|
| `USE_VRC6` | 6 | VRC6 Pulse 1 | `vrc6_p1_note_on/off` |
| `USE_VRC6` | 7 | VRC6 Pulse 2 | `vrc6_p2_note_on/off` |
| `USE_VRC6` | 8 | VRC6 Sawtooth | `vrc6_saw_note_on/off` |
| `USE_VRC7` | 6-11 | VRC7 FM 1-6 | `vrc7_note_on/off(ch)` |
| `USE_S5B` | 6 | S5B Channel A | `s5b_note_on/off(0)` |
| `USE_S5B` | 7 | S5B Channel B | `s5b_note_on/off(1)` |
| `USE_S5B` | 8 | S5B Channel C | `s5b_note_on/off(2)` |
| `USE_N163` | 6-9 | N163 Channels 0-3 | `n163_note_on/off(ch)` |

## MIDI CC Assignments

| CC# | Effect |
|-----|--------|
| 1 (Mod Wheel) | Duty cycle (APU pulse: 0-3, VRC6 pulse: 0-7); VRC7: instrument patch; S5B: noise toggle; N163: wave select |
| 7 (Volume) | Master volume → 4-bit internal level |
| 75 | APU pulse sweep direction (0-42=off, 43-85=down, 86-127=up) |
| 76 | APU pulse sweep shift (0=off, 1-127 → shift 1-7) |
| 123 | All notes off (silence channel) |

## Valid MIDI Note Ranges

| Channel | Min | Max |
|---------|-----|-----|
| APU Pulse 1/2 | 33 (A1) | 127 (G8) |
| APU Triangle | 21 (A0) | 127 (G8) |
| APU Noise | 36 (C2) | 67 (G4) |
| VRC6 Pulse 1/2 | 21 (A0) | 127 (G8) |
| VRC6 Sawtooth | 24 (C1) | 127 (G8) |
| VRC7 FM 1-6 | 21 (A0) | 127 (G8) |
| Sunsoft 5B A/B/C | 21 (A0) | 127 (G8) |
| Namco 163 0-3 | 21 (A0) | 127 (G8) |

## Key Constraints

- **No interrupts:** IRQ/NMI handlers are bare RTI; the main loop polls the EverDrive FIFO directly
- **No heap:** cc65's `malloc` is not used; all storage is static or stack-based
- **Triangle has no velocity:** NES triangle channel is fixed amplitude — velocity parameter is ignored
- **APU timers are 11-bit; VRC6/S5B timers are 12-bit; VRC7 uses 9-bit fnum + 3-bit octave; N163 uses 18-bit frequency register**
- **ROM size:** PRG is limited to ~31.5 KB; be mindful when adding large lookup tables or new code
- **One mapper per ROM:** Only one expansion chip can be active at a time. Each variant is a separate .nes file
- **VRC7 register timing:** Requires NOP delays between address and data writes (~6 cycles after address, ~84 cycles after data)
