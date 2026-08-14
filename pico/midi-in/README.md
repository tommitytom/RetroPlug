# Stage 1 - MIDI IN (smoke test)

Reads raw MIDI from a TRS jack through a **6N138** optoisolator into a Raspberry Pi
Pico's **UART1 (GP5)** and prints each byte as hex to the console. This proves the
opto + UART receive chain end to end before we parse anything or talk to the N8.

Part of the standalone "MIDI keyboard -> real NES/N8, no computer" device
(see [`../README.md`](../README.md)).

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
    6N138 pin 6 (Vo)  -- 470R pull-up to 3V3, and to GP5 (UART1 RX, phys pin 7)
    6N138 pin 7 (Vb)  -- 4.7k to GND
```

TRS jack is wired **MIDI Type A**. A Type B source needs Tip/Ring swapped.
The `4.7k` on pin 7 is not optional - without it the 6N138's slow Darlington
edge causes UART framing errors at 3.3V.

## Build

Needs the ARM toolchain (`gcc-arm-none-eabi`) and the Pico SDK.

```sh
export PICO_SDK_PATH=/workspaces/pico-sdk        # wherever you cloned it
cmake -S . -B build -G Ninja
cmake --build build
```

Output: `build/midi_in_test.uf2` (plus `.elf` for SWD flashing).

Default board is the RP2040 `pico`. For a Pico 2 (RP2350) add `-DPICO_BOARD=pico2`.

## Flash + verify (with the Raspberry Pi Debug Probe)

SWD flash:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 5000" -c "program build/midi_in_test.elf verify reset exit"
```

(or hold BOOTSEL, plug USB, and drag `midi_in_test.uf2` onto the RPI-RP2 drive.)

Watch the console (the debug probe's UART, wired to GP0/GP1):

```sh
screen /dev/ttyACM0 115200      # or: minicom -D /dev/ttyACM0 -b 115200
```

Play a note on a MIDI source plugged into the TRS jack. You should see, for C4:

```
90 3C 7F      note on,  ch1, C4, vel 127
80 3C 00      note off, ch1, C4
```

The onboard LED also toggles on every received byte, so you get a sanity check
even without a terminal.
