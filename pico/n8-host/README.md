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
| 2.2 | **done (HW-verified)** | `edio.c/.h` port: status + sysInfo + memRD over PIO-USB |
| 2.3 | **done (HW-verified 2026-10-30)** | `fifoWR` = memWR(0x1810000): the menu answers `*t`, and `boot_bliptoaster()` installs + boots the MIDI ROM through it, no PC (`loop.sh`, 5/5). It was blocked for months by the probe reading cart SRAM while the menu booted - see [`../pico-n8-fifo-write-bug.md`](../pico-n8-fifo-write-bug.md). |
| 2.4 | **done (HW-verified 2026-10-30)** | the bridge: MIDI UART1/GP5 (reusing `../midi-in/midi.c`) -> one `fifoWR` per channel-voice message. Driven by a USB MIDI cable (CH345) into the TRS jack, with `amidi`: A4 / C5 on ch1 read back as Pulse1 timer 235 / 198 on the N8 sniffer and measured **440.3 Hz / 522.2 Hz** on the L6 capture (ch3), silent after note-off. (The August "verified" results were the PC host's residual note.) |

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
    D-   (pin 2) ->   GP3    (phys pin 5)  --[5k1]-- GND (phys pin 8)
    D+   (pin 3) ->   GP2    (phys pin 4)  --[5k1]-- GND (phys pin 3)
    GND  (pin 4) ->   GND    (phys pin 38)
```

**The two pull-downs are not optional.** A USB host holds D+/D- low through ~15k resistors so a
device's pull-up can announce it, and the line falling back low is how a disconnect is seen.
Without them the RP2350's internal pull-downs are all there is, and on this board's chip
(revision **A2**, erratum **RP2350-E9**) a pad that has been high leaks enough to sit at ~2.2V
against them. Before they were fitted, a NES power-cycle under a live link went unnoticed forever
(no unmount, no re-mount, every read failing), and attach was unreliable. With them: 10/10 clean
mounts, and the power-cycle is seen. They are **5k1**, under the 8.2k Raspberry Pi gives as the
E9 limit (above ~9k does not reliably clear it) - out of the USB spec's 15k range, but with the
N8's 1.5k pull-up D+ still idles at ~2.55V, well above the ~2.0V input-high threshold.

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

**The N8 stays powered from this socket.** Its MCU runs off VBUS, not the NES, so a NES
power-off does not unplug it: it re-enumerates as **`0483:5740`** (ST's virtual COM port, which
answers Edio `CMD_STATUS`) while the console is off, then comes back as `38df:0017`. The firmware
binds only the latter.

**Never touch cart memory (PRG / CHR / SRM) as the N8 comes up.** Its menu is still booting, and
a cart-memory access is a DMA that takes the bus off the console: a single 16-byte SRM read there
left the menu's FIFO command channel dead until the next power-cycle (`*t` unanswered in 9 of 10
trials, against 8 of 8 without the read). `n8_probe()` sticks to `CMD_STATUS` and `SYS_INF`.

**The probe makes TinyUSB asserts halt the core.** OpenOCD sets `C_DEBUGEN` in `DHCSR`
(`0xe000edf0`) when it attaches and leaves it set when it exits, and TinyUSB's `TU_ASSERT`
executes `BKPT` whenever that bit is set. So with the probe merely plugged in, the first failed
USB transfer - the N8 dropping off under a live link, e.g. a NES power-cycle - parks the core on
a breakpoint for good: silent console, PC stuck in `cdch_xfer_cb`, and it reads exactly like a
hung host stack. `loop.sh` clears the bit after every flash/reset; any manual OpenOCD session
sets it again (`loop.sh --no-build --no-flash` clears it). Without a debugger the assert just
returns false, which is what the standalone box sees.

**Telling E9 from a short:** both D+ and D- reading high looks like a D+/D- short. Over SWD,
clear input-enable on GP2/GP3 for a moment (pads at `0x4003800c` / `0x40038010`, bit 6) and
re-read `IO_BANK0` STATUS bit 17 (`0x40028010` / `0x40028018`): a latch drops to 0, a short
doesn't.

## The hands-off loop: `loop.sh`

With the N8's cable left in this Pico's host socket, one command does a whole iteration -
build, halt the Pico, power-cycle the NES (a fresh N8 menu no host has touched), flash over SWD,
and capture the timestamped console until a verdict:

```sh
./loop.sh --until 'booted by the Pico' --fail "no 'k'"   # exit 0 = the Pico booted a ROM
./loop.sh --no-power                          # keep what the NES is running; the Pico still resets
./loop.sh --no-build --no-flash --secs 30     # just reset the Pico and watch
./loop.sh --native                            # the RP_NATIVE_USB build, in build-native/
./loop.sh --debug                             # + TinyUSB's attach/enumeration trace, in build-debug/
./loop.sh -DN8_MENU_TRIES=3                   # any CMake define; each set gets its own build dir
```

The Pico is halted BEFORE the power-cycle: a working firmware left running would see the N8 come
back and boot a ROM through its menu itself, so the freshly flashed one would find a game running
and nothing to answer `*t`. It refuses to run while the N8 is enumerated on the PC (the cable is
in the wrong place, so the run would test nothing); `--force` overrides that. The console is kept
in `/tmp/n8-host-console.log` (`--log`). Exit 0 = done / `--until` seen, 1 = `--fail` seen or
`--until` timed out, 2 = setup failed.

### Driving the bridge from the container

A USB MIDI cable (CH345, `1a86:752d`) on the dev machine has its OUT plugged into the Pico's TRS
jack (Type A), so the bridge can be driven by script. Its ALSA card number moves with plug order -
find it by name, after `bash .devcontainer/hw-usb-perms.sh` has mirrored the node:

```sh
M=$(amidi -l | awk '/CH345/{print $2; exit}')    # e.g. hw:5,0,0
amidi -p $M -S 'B0 07 7F'                        # prime: the ROM drops its first message after boot
amidi -p $M -S '90 45 7F'; sleep 2; amidi -p $M -S '80 45 00'   # A4 on ch1 -> Pulse1
```

The console shows `[bridge] MIDI -> FIFO: 90 45 7f` then `[sniff] timer=235   440 Hz`; the L6
capture's ch3 measures it (`retroplug-cli analyze-capture <wav> --channel 3 --expect-hz 440`).
Don't confuse it with the **L6's** own MIDI ports (`L6 MIDI I/O Port` etc.): those are USB
control ports for the mixer, not a MIDI output, and writing to them can change capture settings.

## Build + flash + verify

`loop.sh` does all of this; by hand:

```sh
export PICO_SDK_PATH=/workspaces/pico-sdk         # NOT the image's /opt/pico-sdk: it has no TinyUSB
cmake -S . -B build -G Ninja && cmake --build build
sudo openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
     -c "program build/n8_host.elf verify reset exit"      # button-free SWD
sudo stty -F /dev/ttyDbgProbe 115200 raw -echo && cat /dev/ttyDbgProbe
```

A good boot from a fresh N8 menu:

```
[n8-host] device mounted: addr 1  VID:PID 38df:0017   <- N8!
[n8-host] --- Edio probe ---
[n8-host] CMD_STATUS -> 0 (OK)
[n8-host] SYS_INF: serial=00035AAD.00002C4D  device_id=0x17 (N8 PRO)  sw=0103 hw=0001
[n8-host] --- probe done ---
[n8-host] menu handshake (*t)...
[n8-host] menu up -> install usb-games/n8-midi.nes (*n)...
[n8-host] installed -> boot (*s); BlipToaster starting...
[n8-host] BlipToaster booted by the Pico - no PC used.
```

If a game is already running, the menu isn't there to answer `*t`, so `boot_bliptoaster()` gives
up after `N8_MENU_TRIES` (~48 s by default) and the bridge forwards MIDI to whatever runs.
`midi_to_fifo()` forwards only channel-voice messages (`0x80-0xEF`), dropping clock / sensing /
transport / aftertouch so they can't flood the FIFO; `sniff_report()` is read-only.

### Native-USB toggle

`-DRP_NATIVE_USB=ON` builds against the RP2350's **silicon** USB host controller (native rhport 0)
instead of PIO-USB. It was meant as the fix for the FIFO write, which turned out not to need it;
it remains an option, and it sidesteps E9 (the native PHY's pads are not affected):

```sh
cmake -S . -B build-native -G Ninja -DRP_NATIVE_USB=ON && cmake --build build-native
```

Native uses the Pico's own USB D+/D- pins (not GP2/GP3), so the N8 must be wired there and the
Pico powered via VSYS - different wiring from the PIO-USB socket above.

Defaults to `pico2` (RP2350). Override with `-DPICO_BOARD=pico`;
`-DPICO_PIO_USB_PATH=...` if Pico-PIO-USB lives elsewhere.
