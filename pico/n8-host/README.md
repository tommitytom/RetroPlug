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
| 2.5 | **done (HW-verified 2026-10-30)** | BlipToaster parity with the PC bridge: **SysEx** forwarded verbatim, the **expansion master volume** set when the cart needs it, aftertouch still filtered (BlipToaster ignores it). See "BlipToaster parity" below - Test G through the Pico matches the console numbers BlipToaster documented from the PC. |
| 2.6 | **done (HW-verified 2026-10-30)** | the standalone box (`box.c`): status LED, watchdog + recovery reboots with backoff, several ROMs staged in flash and picked over MIDI (`F0 7D 50 01 <n> F7`), the choice saved on the SD. `faults.sh`: random NES power cuts, 8/8 rounds recovered and played. See "The standalone box". |

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
./loop.sh --rom a.nes --rom b.nes             # stage ROMs in Pico flash; the box boots the chosen one
```

The Pico is halted BEFORE the power-cycle: a working firmware left running would see the N8 come
back and boot a ROM through its menu itself, so the freshly flashed one would find a game running
and nothing to answer `*t`. It refuses to run while the N8 is enumerated on the PC (the cable is
in the wrong place, so the run would test nothing); `--force` overrides that. The console is kept
in `/tmp/n8-host-console.log` (`--log`). Exit 0 = done / `--until` seen, 1 = `--fail` seen or
`--until` timed out, 2 = setup failed.

**Booting a freshly built ROM: `--rom <file.nes>` (repeatable).** The N8's SD card is only reachable
over its USB, which belongs to the Pico, so `loop.sh` writes the ROMs into the **Pico's own flash**
(at `0x10200000`, 2 MB past the firmware: per ROM a 64-byte header - `N8RM`, size, SD path - then the
ROM, padded to 4 KB; one erased 4 KB block after the last ends the table). Once the menu answers `*t`,
the firmware copies the chosen one to `usb-games/pico/<name>.nes` with Edio's file commands
(`edio_file_put`: the same open / write / close sequence as the PC's `n8-load --put-file`) and boots
it from there: ~5 s for a 288 KB ROM, and no upload at all when its CRC says it is already there (see
"The standalone box"). Its own `usb-games/pico/` folder keeps it clear of the files other tools put
on the shared card. A run that flashes without `--rom` erases the first header, and the firmware
boots `N8_ROM_PATH` (default the older `usb-games/n8-midi.nes`; `-DN8_ROM_PATH=...` for another file
already on the card). `stress.sh --rom` and `faults.sh --rom` pass it through.

### Driving the bridge from the container

A USB MIDI cable (CH345, `1a86:752d`) on the dev machine has its OUT plugged into the Pico's TRS
jack (Type A), so the bridge can be driven by script. Its ALSA card number moves with plug order -
find it by name, after `bash .devcontainer/hw-usb-perms.sh` has mirrored the node:

```sh
M=$(amidi -l | awk '/CH345/{print $2; exit}')    # e.g. hw:5,0,0
amidi -p $M -S 'B0 07 7F'                        # prime: the ROM drops its first message after boot
amidi -p $M -S '90 45 7F'; sleep 2; amidi -p $M -S '80 45 00'   # A4 on ch1 -> Pulse1
```

The console shows `[sniff] p1=235/f (440 Hz) ...` - each voice as `<timer>/<level>`: this ROM
keeps a voice's timer at note-off and silences it through its level, so `p1=235/0` is the note
released, not a stuck one - plus a `[stats]` line once a second while anything moves (bytes in,
messages parsed / forwarded, fifoWRs and their largest batch, and every way a byte can be lost:
UART overrun, framing error, full receive ring; and `lat=avg/worst` in µs for the window).
`-DN8_BRIDGE_VERBOSE=1` also prints each
forwarded batch. The L6 capture's ch3 measures the pitch
(`retroplug-cli analyze-capture <wav> --channel 3 --expect-hz 440`).
Don't confuse it with the **L6's** own MIDI ports (`L6 MIDI I/O Port` etc.): those are USB
control ports for the mixer, not a MIDI output, and writing to them can change capture settings.

### BlipToaster parity: SysEx, expansion volume, aftertouch

- **SysEx is forwarded verbatim.** The parser hands every SysEx byte (F0, payload, F7) to
  `midi_sysex_to_fifo()`, which puts them in the same batch as the channel messages, so order is
  kept and a long SysEx simply spans batches. A SysEx cut short by another status byte gets a
  synthesised F7, so the ROM never sees one left open. BlipToaster takes N163 waves, GM System On
  (a warm reboot) and its settings this way. `[stats]` counts them (`sysex=`).
- **The expansion master volume** (VRC6 / VRC7 / N163 / Sunsoft 5B / MMC5) lives in the N8's
  config block (`0x1800020` + 3). Whenever forwarding starts - after the Pico booted a ROM, or on
  finding one already running - `exp_vol_check()` reads that block (the live config the N8 OS
  writes at hand-over; system region, not a cart DMA). For an expansion mapper whose volume is 0
  it writes `N8_EXP_VOL` (default 128, unity) and reads it back; a non-zero setting is left
  alone. On this rig the N8 OS itself set 129-130 at every hand-over, so the raise has only been
  exercised by forcing the register to 0 first (it then read back 128) - the PC tools' "0 after a
  power-cycle" warning was not reproduced here.
- **Aftertouch stays filtered.** BlipToaster's parser reads poly-aftertouch and channel pressure
  and ignores both, and the older n8-midi.nes hung on the flood a Launchpad sends, so forwarding
  them would only cost FIFO bandwidth.

The base `bliptoaster.nes` is **2A03 only**: its mapper 69 (FME-7) banks the sample kits and drives
no 5B voices - expansion audio needs a variant build (`bliptoaster-s5b.nes` etc.). Through the Pico
(`--rom`), the 5B build plays A4 / C5 on ch6 at 440.06 Hz / 524.47 Hz, -27.9 dBFS on the L6's ch5.

**BlipToaster's Test G** (its HARDWARE-TESTING.md: an N163 wave upload over SysEx while a note
sounds), run through the Pico on the N163 build, against the numbers documented from the PC:

| step (L6 capture) | through the Pico | BlipToaster doc (PC host) |
|---|---|---|
| control: 2A03 A4, ch3 | 440.30 Hz (+1.2 c), -32.9 dBFS | 440.31 Hz (+1.2 c), -34 dBFS |
| N163 A3 on ch6, ch5 | 219.99 Hz (-0.1 c), -43.8 dBFS | 219.99 Hz (-0.1 c), -44.8 dBFS |
| flat wave (SysEx) while it sounds | -77.4 dBFS: silent, no note-off | -75.7 dBFS |
| restore (SysEx) | -43.8 dBFS | -44.8 dBFS |
| square (SysEx) | -39.5 dBFS (+4.2 dB over the sine) | -41.2 dBFS (+3.6 dB) |
| note-off | floor | floor |

### The stress test: `stress.sh`

Boots the ROM fresh through `loop.sh`, then judges every case on the Pico's own counters (nothing
lost on the way in, everything forwarded) and on the ROM's state read back through the sniffer
(still in sync: A4 sounds at timer 235, then goes silent):

- latency: 40 isolated messages, worst case within 1.5 ms (see below);
- bursts of 25 / 100 / 500 note pairs at wire speed, each in a single write;
- a four-voice chord on and off (Pulse 1 / 2, triangle, noise);
- running status and realtime bytes - as the cable re-serialises them: USB-MIDI packets carry
  whole messages, so the cable restores omitted status bytes (the parser's own handling of
  running status and mid-message realtime bytes is `../midi-in/parser_test.c`);
- SysEx pass-through: a device inquiry and a 40-byte SysEx between notes, counted and in sync;
- a paced soak (`--soak <s>`, `--rate <msgs/s>`; spawning amidi caps it near 300/s);
- 40000 messages as one unbroken stream - the cable's own limit, ~800 msgs/s;
- the NES power-cycled under a live stream: the Pico must re-boot the ROM itself and play again.

**Throughput.** The first run lost ~70% of every burst to UART overruns: receive was polled between
forwards, and a forward blocked the loop long enough for the UART's 32-byte hardware FIFO to
overflow. Receive is now interrupt-driven into a 4 KB ring, and every message parsed in one pass of
the main loop goes out as one fifoWR (a batch of up to 64). Full run since: 50,832 messages, 0 lost.

**Latency** - a message's last byte off the UART to the N8 acknowledging the write: **~0.6 ms
average, ~1.04 ms worst** (it was ~4.6 ms typical with 11-19 ms outliers). Four things had to go:

- **Five USB transfers per write.** Pico-PIO-USB runs host transactions from its 1 ms
  start-of-frame interrupt, one packet per endpoint per frame, and `edio_mem_wr` sent cmd / addr /
  len / exec / data as five writes: 5 frames. One coalesced write is one frame.
- **The UART receive timeout.** A 3-byte message is under the RX interrupt's 4-byte threshold, so
  it waited ~1 ms for the timeout. The idle main loop now reads the UART itself (IRQ masked), and
  the IRQ stays the safety net while the loop is blocked.
- **The sniff's 512-byte read** - eight packets, ~10 ms of main loop. It reads the 22 bytes it uses.
- **Console output.** A `[stats]` line blocks ~11 ms on the 115200 console. Diagnostics now run
  only once MIDI has been quiet for 100 ms.

What remains is the frame itself: a write waits up to 1 ms for the next start-of-frame. Going
under that means sending outside Pico-PIO-USB's frame schedule, i.e. patching the library. Add the
MIDI wire itself (3 bytes at 31250 baud = ~1 ms, before the Pico sees the last byte) for the full
picture.

**The ROM's share** - `-DN8_APU_LAT=1` builds a measurement firmware that, after a lone ch1
note-on, polls Pulse1's volume (`$4000`) through the N8's live APU mirror until the ROM has written
it. Each poll is a 1-byte memRD (~2 USB frames), so the answer is a bracket, and its floor is the
USB round trip, not the ROM. 20 notes each, same rig (2026-10-30):

| ROM | polls until the note showed | `$4000` written, after the last MIDI byte |
|---|---|---|
| BlipToaster (`ed0c843`) | **1, every note** | within ~0-3 ms: below what the probe resolves |
| `n8-midi.nes` (older)   | 3, every note | between ~2.7 and ~6.7 ms (average bracket) |

BlipToaster busy-polls the cart FIFO, so its own share is likely tens of µs; resolving it needs a
cycle-exact measurement (the emulator), not this probe.

## The standalone box

`box.c` turns the bridge into something that runs unattended. Everything here was exercised on the
rig; what was not is said so at the end.

**Status LED** (the Pico's own, GP25):

| pattern | meaning |
|---|---|
| short blip once a second | no N8 on the socket, or the NES is off |
| 4 Hz blink | N8 up, not forwarding yet: probe, menu, upload, boot |
| solid, dropping out briefly per batch | forwarding MIDI |
| double blink | the boot failed, or the menu is stuck (power-cycle the NES) |
| n quick blinks | acknowledging a ROM pick (n = ROM index + 1) |

**Watchdog and recovery.** A 3 s hardware watchdog reboots a hung Pico; every Edio wait pumps it
(`edio_set_idle_hook`), so a long upload or install never trips it. The box also reboots itself on
purpose (`box_recover`) when the link is wedged in a way a fresh USB host clears: a device on the
socket that never enumerates (8 s), the N8 not answering `CMD_STATUS` (3 checks a second apart,
while forwarding and MIDI is quiet), or 5 failed writes in a row. Why it rebooted and how many
times in a row survive the reboot in watchdog scratch 0-2 (the SDK owns 4-7); consecutive
recoveries back off 5 / 10 / 20 / 40 / 60 s and 60 s of a healthy link clears the count, so a fault
the box cannot clear (the N8 itself wedged) is an error LED, not a reboot loop. Only a watchdog reset
continues the count - a debugger's reset (a reflash) is a fresh start.

A boot never outlives its N8: a power cut mid-boot unmounts it, and a new one can be at a fresh menu
before a slow step returns, so every step checks a mount generation and abandons a boot whose N8 has
gone (`boot abandoned`), and every Edio wait fails at once when the link drops instead of running
out its timeout. `CMD_STATUS` before `*t` says whether a game already runs (forward at once) or the
menu is up. A menu that reports status 0 but never answers `*t` - seen once, after a power cut whose
"off" the Zigbee plug applied 2.3 s late, leaving the NES off ~2.7 s - is held as **stuck**: no
forwarding (a `2A` data byte is the menu's `*` prefix), the error LED, a retry every 20 s.

**ROM selection.** `loop.sh --rom a.nes --rom b.nes ...` stages several ROMs in the Pico's flash
(2 MB at `0x10200000`, about six BlipToaster builds). The box boots the one picked over MIDI, else
the one saved on the SD card as last booted (`usb-games/pico/box.cfg`), else the first. Its CRC-32 is
saved with the choice, so an unchanged ROM is not uploaded again: a power-on goes from the N8 mounting
to the ROM installed in ~0.75 s instead of ~5.6 s. `box.cfg` is only rewritten when the choice or the
ROM changes, so an ordinary power-on writes nothing to the card. If the menu cannot load a file the
CRC said was there (someone deleted it), the box uploads it again and retries once.

The box's own SysEx - `F0 7D 50 ...` (BlipToaster's are `7D 42`); acted on, never forwarded:

| message | effect |
|---|---|
| `F0 7D 50 01 <n> F7` | boot staged ROM n at the next menu: power-cycle the NES (or press RESET, if your N8 returns to its menu on RESET) - a pending pick also survives a recovery reboot |
| `F0 7D 50 02 F7` | print the staged ROMs, the booted one and the pending pick on the console; blink the booted index + 1 |
| `F0 7D 50 7E F7` / `7D` | `-DN8_TEST_HOOKS=1` builds only: hang (the watchdog must fire) / fake a dead link (recovery must) |

The menu coming back is noticed by `CMD_STATUS` turning 0 after a game was seen, and the chosen ROM is
booted again.

**Tests (2026-10-30).**
- Selection: two ROMs staged (the base build, mapper 69; the N163 build, mapper 19). A fresh box
  uploaded and booted ROM 0; a MIDI pick of ROM 1 + a power-cycle uploaded and booted the N163 build;
  the next power-cycle booted it again from the saved choice with no upload. The CRCs the box computed
  (`f39a6773`, `1c8dffcd`) match the gzip trailer's independent CRC-32 of the same files.
- Watchdog: the hang hook rebooted the Pico 3.1 s later, reported as recovery #1, and it resumed.
- Recovery and backoff: the dead-link hook recovered after 3 failed checks; triggered again inside a
  60 s backoff window, 53 failed checks went by with no reboot until the window closed.
- LED: sampled over SWD (GP25 `OUTTOPAD`): solid while forwarding, the 1 s blip with the NES off, the
  4 Hz blink through an upload.
- `faults.sh` (24 random cuts, 8 rounds): 8/8 recovered and played, 6 boots abandoned and redone,
  no recovery reboots, no uploads. It found three bugs on the way, all fixed: a boot outliving its N8
  (then a ~48 s blind `*t` against a running game), a re-upload fired by a lost link's timeout, and a
  false "never enumerated" reboot from a timer left stale by a long boot.
- `stress.sh` on the box firmware: PASS, 50,805 messages, 0 lost.

**Without the probe (2026-10-30).** With the debug probe unplugged - no console, no SWD - and judged
only by the video capture and the L6: after a NES power-cycle the box booted BlipToaster by itself
(on screen) and A4 / C5 played at 440.31 / 522.16 Hz; three rounds of three random power cuts each
ended in a 440.30 Hz A4. Then the Pico's own power was unplugged and replugged - a cold boot with no
debugger ever attached: with a game still running it forwarded at once (440.30 / 522.16 Hz), and after
a NES power-cycle it drove the menu and booted the ROM (440.30 Hz).

**RESET.** On this rig the NES's RESET button restarts the running ROM in place - the N8 does not show
its menu (a 10 fps brightness trace of the press: ~2 s blank, ~1.4 s of lost signal, then BlipToaster
again, never the menu's brighter frames) - so the box has nothing to do and plays on (440.30 Hz after
two presses). Whether RESET goes to the menu is the N8's own setting; the box's menu-return path
(`CMD_STATUS` 0 after a game) is for when it does, and that path is the one not seen here.

**Not yet verified on hardware:** the "never enumerated" recovery firing for a real non-enumerating
device (the only trigger seen was the false positive above, now fixed); the menu-return path just
described (this N8 never returned to its menu on RESET); and the error LED's double blink (no boot
failed or stuck while it was being sampled).

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
