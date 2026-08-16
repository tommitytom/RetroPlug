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
| 2.3 | **done (HW-verified)** | `fifoWR` = memWR(0x1810000, midi): C4 note-on -> EverMIDI Pulse1, proven by reading the N8 sniffer back (`note_and_sniff`) |
| 2.4 | todo | the bridge: MIDI (UART1/GP5, reuse `../midi-in/midi.c`) -> `fifoWR` |

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
the MIDI opto circuit can stay wired (unused until slice 2.4). PIO-USB forces a
120 MHz system clock (`set_sys_clock_khz(120000)`).

## Build + flash + verify

```sh
export PICO_SDK_PATH=/workspaces/pico-sdk         # devcontainer sets this
cmake -S . -B build -G Ninja && cmake --build build
sudo openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
     -c "program build/n8_host.elf verify reset exit"      # button-free SWD
sudo stty -F /dev/ttyDbgProbe 115200 raw -echo && cat /dev/ttyDbgProbe
```

With the N8 plugged into the socket, and the **EverMIDI ROM running** on the NES, you
should see the probe followed by a repeating FIFO-consumption proof:

```
[n8-host] device mounted: addr 1  VID:PID 38df:0017   <- N8!
[n8-host] CDC mounted (idx 0, VID:PID 38df:0017)
[n8-host] --- Edio probe ---
[n8-host] CMD_STATUS -> 5 (busy - ROM running)
[n8-host] SYS_INF: serial=00035AAD.00002C4D  device_id=0x17 (N8 PRO)  sw=0103 hw=0001
[n8-host] --- probe done ---
[sniff] magic=53 $4000=bf $4002=8d $4003=01 $4015=0f  P1_timer=397  <<< PULSE1 ACTIVE - EverMIDI consumed it!
```

`note_and_sniff()` fifoWRs a C4 note-on, then reads the N8 **sniffer** (a running game's
live `$4000-$401F` write-mirror) back: `$4015` bit0 set + a non-zero Pulse1 timer means
EverMIDI received the MIDI over the FIFO and drove the 2A03 - deterministic proof
independent of the audio rig. At the N8 **menu** instead, `CMD_STATUS` is `0 (OK)` and
the write still issues but nothing consumes it (Pulse1 idle).

Defaults to `pico2` (RP2350). Override with `-DPICO_BOARD=pico`;
`-DPICO_PIO_USB_PATH=...` if Pico-PIO-USB lives elsewhere.
