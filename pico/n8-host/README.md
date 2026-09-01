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
| 2.2 | **done (HW-verified)** | `edio.c/.h` port: status + sysInfo + memRD - all reads work over PIO-USB |
| 2.3 | **BLOCKED** | `fifoWR` = memWR(0x1810000, midi). Byte-identical to the working PC host, but the N8 ACKs the write and never routes it to the cart FIFO **over Pico-PIO-USB** (memWR to RAM works). See **`../../pico-n8-fifo-write-bug.md`**. |
| 2.4 | **BLOCKED (on 2.3)** | the bridge (MIDI UART1/GP5, reuse `../midi-in/midi.c` -> `fifoWR`) + `edio_menu_*` autonomous boot are CODED but can't reach the N8 until the FIFO write works. All prior "verified" audio was the PC host's residual note, not the Pico. |

> **The blocker is a Pico-PIO-USB limitation, not a bug in this code** - the Edio framing is
> byte-for-byte identical to the PC host that drives the N8 correctly. The likely fix is a
> **silicon** USB host: build with `-DRP_NATIVE_USB=ON` and wire the N8 to the RP2350's native
> USB pins (see the toggle below). Full analysis + everything ruled out: `../../pico-n8-fifo-write-bug.md`.

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

On boot you see the Edio probe (reads work), then `boot_bliptoaster()` tries the menu handshake:

```
[n8-host] device mounted: addr 1  VID:PID 38df:0017   <- N8!
[n8-host] --- Edio probe ---
[n8-host] CMD_STATUS -> 0 (OK)
[n8-host] SYS_INF: serial=00035AAD.00002C4D  device_id=0x17 (N8 PRO)  sw=0103 hw=0001
[n8-host] --- probe done ---
[n8-host] menu handshake (*t)...
```

Over PIO-USB the `*t` handshake never gets its `k` reply, because the FIFO write it rides on
doesn't reach the N8's running code (slice 2.3, above) - so `boot_bliptoaster()` gives up and the
bridge forwards MIDI that also never lands. The reads (probe, `CMD_STATUS`, `SYS_INF`, `memRD`)
all work. `midi_to_fifo()` forwards only channel-voice messages (`0x80-0xEF`), dropping clock /
sensing / transport / aftertouch so they can't flood the FIFO; `sniff_report()` is read-only.

### Native-USB toggle (the intended fix for slice 2.3)

`-DRP_NATIVE_USB=ON` builds against the RP2350's **silicon** USB host controller (native rhport 0)
instead of PIO-USB, whose software wire-timing is the suspected cause of the FIFO-write failure:

```sh
cmake -S . -B build-native -G Ninja -DRP_NATIVE_USB=ON && cmake --build build-native
```

Native uses the Pico's own USB D+/D- pins (not GP2/GP3), so the N8 must be wired there and the
Pico powered via VSYS - different wiring from the PIO-USB socket above.

Defaults to `pico2` (RP2350). Override with `-DPICO_BOARD=pico`;
`-DPICO_PIO_USB_PATH=...` if Pico-PIO-USB lives elsewhere.
