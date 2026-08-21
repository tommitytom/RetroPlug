# Stage 1 - MIDI IN

Reads MIDI from a TRS jack through a **6N138** optoisolator into a Pico's
**UART1 (GP5)**, decodes it with a reusable parser, and prints one human-readable
event per message to the console. This proves the opto + UART receive chain and
gives us the event model the later N8-bridge stage forwards.

Hardware-verified 2026-08-14 (a Novation Launchpad through the full chain).
Part of the standalone "MIDI keyboard -> real NES/N8, no computer" device
(see [`../README.md`](../README.md)).

## Files

- `midi.{h,c}` - pure-C MIDI byte-stream parser (running status + realtime
  interleave + SysEx skip). No hardware deps; reused by later stages and the
  offline host test.
- `midi_print.{h,c}` - a `midi_sink` that prints a readable line per message
  (clock `0xF8` + active-sensing `0xFE` filtered; transport shown).
- `midi_in_test.c` - firmware: UART1/GP5 -> parser -> printer, LED toggles per byte.

## Wiring (6N138, output side at 3.3V)

```
  MIDI side (ISOLATED - shares no ground with the Pico)
    TRS Ring  (DIN pin 4) --[220R]-- 6N138 pin 2  (LED anode)
    TRS Tip   (DIN pin 5) ---------- 6N138 pin 3  (LED cathode)
    1N4007 across pins 2<->3, band (cathode) -> pin 2   (reverse-parallel)
    TRS Sleeve (DIN pin 2) --------- not connected

  Pico 3.3V side
    6N138 pin 8 (Vcc) -- Pico 3V3   (phys pin 36)   [+ optional 0.1uF to GND]
    6N138 pin 5 (GND) -- Pico GND   (phys pin 38)
    6N138 pin 6 (Vo)  -- 330R pull-up to 3V3, and to GP5 (UART1 RX, phys pin 7)
    6N138 pin 7 (Vb)  -- 5k1 to GND
```

TRS jack is wired **MIDI Type A** (Novation MK3, Korg, Make Noise, TE...). A
Type B source (Arturia, older gear) needs Tip/Ring swapped. Pin-6 pull-up is
220-470R (330R here); the **pin-7 resistor (~5k, 5k1 here) is not optional** -
without it the 6N138's slow Darlington edge causes UART framing errors at 3.3V.

## Build

The devcontainer provides the ARM toolchain, the Pico SDK, and `PICO_SDK_PATH`
(see [`../README.md`](../README.md)):

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

**Defaults to the Pico 2 (RP2350)** - the board in the hardware lab. For an
original RP2040 Pico, add `-DPICO_BOARD=pico`. Outside the devcontainer,
`export PICO_SDK_PATH=/opt/pico-sdk` first. Output: `build/midi_in_test.uf2`.

## Flash

**Over SWD via the Debug Probe (recommended - no BOOTSEL).** Wire the probe's
**D** (SWD) 3-pin to the Pico's debug header (SWCLK/GND/SWDIO), then:

```sh
sudo openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
     -c "program build/midi_in_test.elf verify reset exit"
```

Programs + resets in one shot, no button. The devcontainer ships the
RP2350-capable **Raspberry Pi OpenOCD fork** for this (stock OpenOCD only does
rp2040). SWD also gives real debugging - `openocd` opens a gdb server on :3333.

**Over USB (BOOTSEL) - fallback / very first flash.** Hold BOOTSEL while
resetting/replugging power, then:

```sh
sudo picotool load -x build/midi_in_test.uf2      # loads + reboots into the app
```

Both rely on the container-side USB passthrough (bind-mounted `/dev/bus/usb` +
`.devcontainer/hw-usb-perms.sh`), which lives on `main`.

## Verify

Watch the console. In the devcontainer the Raspberry Pi Debug Probe's UART is
pinned to a stable node by the postStart hook:

```sh
sudo stty -F /dev/ttyDbgProbe 115200 raw -echo
cat /dev/ttyDbgProbe
```

(The probe's **U** (UART) 3-pin port must be wired to the Pico's GP0/GP1 + GND.)
Play a note and you get decoded events; clock/sensing are filtered:

```
NoteOn  ch1  C5   ( 72) vel 69
ChanAT  ch1  = 74
NoteOff ch1  C5   ( 72)
```

The onboard LED also toggles on every received byte - a sanity check with no
terminal at all.

## Host test (no hardware)

The parser is pure C, so it can be exercised offline:

```sh
gcc -std=c11 -I. -o /tmp/miditest midi.c midi_print.c host_test.c
/tmp/miditest <raw-midi-bytes-file>
```
