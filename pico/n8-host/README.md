# Stage 2 - N8 USB host (Edio over PIO-USB)

Makes the Pico a **USB host** (via [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
+ TinyUSB `cdc_host`) that speaks the Everdrive N8 "Edio" protocol, so MIDI can be
delivered to the N8's cart FIFO with no PC in the loop. The N8 is a class-compliant
USB CDC-ACM device (VID:PID `38df:0017`), so the standard TinyUSB host CDC path
handles it - no FTDI/CP210x/CH34x drivers.

Part of the standalone "MIDI -> N8, no computer" device (see [`../README.md`](../README.md)).
One Pico does the whole bridge: MIDI in is a hardware UART (stage 1), the N8 host is
PIO-USB - they don't contend.

## Slices

| Slice | State | What |
|-------|-------|------|
| 2.1 | **done (HW-verified)** | USB host enumerates the N8 + Edio `CMD_STATUS` handshake (`main.c`) |
| 2.2 | **done (HW-verified)** | `edio.c/.h` port: status + sysInfo + memRD |
| 2.3 | **done (HW-verified)** | `fifoWR` = memWR(0x1810000, midi): C4 note-on -> EverMIDI Pulse1, proven by reading the N8 sniffer back |
| 2.4 | **done (HW-verified)** | the bridge: MIDI (UART1/GP5, reuse `../midi-in/midi.c`) -> `fifoWR`. Launchpad -> real NES, no PC; 2A03 audio measured off the L6 at 261 Hz (PAL C4) |

## Dependencies (not in the repo yet - see "one-time setup")

- The Pico SDK's **TinyUSB submodule** (the host CDC stack).
- **Pico-PIO-USB** cloned alongside the SDK.

```sh
git -C /workspaces/pico-sdk submodule update --init lib/tinyusb
git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git /workspaces/Pico-PIO-USB
```

(Baking these into the devcontainer, like the OpenOCD bake, is a follow-up.)

## Wiring (USB-A host socket on this Pico)

```
  USB-A socket   ->   Pico
    VBUS (pin 1) ->   VBUS   (phys pin 40, 5V)
    D-   (pin 2) ->   GP3    (phys pin 5)
    D+   (pin 3) ->   GP2    (phys pin 4)
    GND  (pin 4) ->   GND    (phys pin 38)
```

**Identifying the socket contacts:** a *receptacle* is pin-mirrored vs a *plug* - looking
**into the mouth**, the 4 USB-2.0 contacts read **4-3-2-1** left->right, i.e.
**GND, D+, D-, VBUS**. So VBUS is the contact on the **right**. Get VBUS/GND backwards
and nothing enumerates (the device is unpowered - its pull-up needs VBUS); verify with a
meter (the two outer contacts are the power pair). D+/D- must be the consecutive pair
GP2/GP3 (PIO-USB drives D-, = D+ + 1); if it powers but won't enumerate, swap them
(harmless). Optional 22-33R series on D+/D-. Then plug the **N8's USB cable** into this
socket (the same A-plug that went to the PC). Keep the debug probe attached (SWD flash + console);
the stage-1 MIDI opto circuit feeds GP5 (UART1 RX) - the bridge (slice 2.4) forwards it. PIO-USB
forces a 120 MHz system clock (`set_sys_clock_khz(120000)`).

## Build + flash + verify

```sh
export PICO_SDK_PATH=/workspaces/pico-sdk         # devcontainer sets this
cmake -S . -B build -G Ninja && cmake --build build
sudo openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
     -c "program build/n8_host.elf verify reset exit"      # button-free SWD
sudo stty -F /dev/ttyDbgProbe 115200 raw -echo && cat /dev/ttyDbgProbe
```

With the N8 plugged into the socket and the **EverMIDI ROM running** on the NES, on boot
you see the Edio probe, then - **play a MIDI keyboard into the TRS input** - one line per
forwarded message plus a read-only Pulse1 report:

```
[n8-host] device mounted: addr 1  VID:PID 38df:0017   <- N8!
[n8-host] --- Edio probe ---
[n8-host] CMD_STATUS -> 5 (busy - ROM running)
[n8-host] SYS_INF: serial=00035AAD.00002C4D  device_id=0x17 (N8 PRO)  sw=0103 hw=0001
[n8-host] --- probe done ---
[bridge] MIDI -> FIFO: 90 3c 7f          <- note-on C4 (2A03 ch1 -> Pulse1)
[sniff] Pulse1 ON <<< NES is playing it  ($4015=0f P1_timer=397)
[bridge] MIDI -> FIFO: 80 3c 00          <- note-off
[sniff] Pulse1 off  ($4015=0f P1_timer=0)
```

The bridge forwards only channel-voice messages (`0x80-0xEF`); MIDI clock / sensing /
transport are dropped so they can't flood the FIFO. `sniff_report()` injects nothing - it
reads the running ROM's `$4000-$401F` write-mirror and reports Pulse1 on/off as live
confirmation. **Audio proof:** with a held note, the 2A03 output recorded off the Zoom L6
(channel 3) measures a 261 Hz fundamental with odd harmonics (a square wave) = PAL C4,
exactly the pitch the `P1_timer=397` predicts. So: MIDI keyboard -> real NES audio, no PC.

Defaults to `pico2` (RP2350). Override with `-DPICO_BOARD=pico`;
`-DPICO_PIO_USB_PATH=...` if Pico-PIO-USB lives elsewhere.
