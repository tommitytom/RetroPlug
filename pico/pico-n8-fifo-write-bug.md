# Pico N8 bridge: memWR to the cart FIFO (0x1810000) never reaches the running NES code

The Raspberry Pi Pico 2 (RP2350) N8 bridge (`pico/n8-host/`) hosts the Everdrive N8 Pro over
Pico-PIO-USB + TinyUSB `cdc_host` and speaks Edio. **Reads and RAM writes work; writes to the
cart FIFO do not** - so no MIDI (and no menu command) ever reaches the running NES code. This
is THE blocker: the whole "MIDI -> N8, no PC" bridge depends on `fifoWR = memWR(0x1810000, ...)`.

## Symptom (confirmed, cleanest repro)

On a freshly power-cycled N8 sitting at its file-browser **menu** (empty cart FIFO), the Pico:
- `memWR(0x1000000 /*SRM*/, {ca fe ba be})` then `memRD` back -> reads `ca fe ba be`. **RAM write works.**
- `fifoWR({'*','t'})` = `memWR(0x1810000, {2a 74})` as the *first* FIFO write since power-up ->
  the menu never replies `k`. **FIFO write is not seen.**

The N8 menu answers `*t`->`k` / `*v`->2064 bytes when driven by the host
(`retroplug-cli n8-load`), so the menu is responsive; only the Pico's FIFO write fails.

Consequence: every earlier "hardware-verified" result for the Pico bridge (repeating note,
"261 Hz audio", the play trace) was actually the **host's** residual note - the N8 was hot-moved
from the host to the Pico while BlipToaster held that note. The Pico never drove it.

## What works from the Pico
USB enumeration + CDC mount; `CMD_STATUS` (0 at menu / non-zero running); `SYS_INF`;
`memRD` (SRM, sniffer 0x1802000); **`memWR` to SRM (0x1000000) round-trips**. So the Edio
command/response transport over PIO-USB is fundamentally working.

## Ruled out (each tested on hardware)
- **Framing**: byte-identical to the working host path. Host (edlink `DeviceIO_V1.MemWR`,
  RetroPlug `Edio::memWR`) = `TxCMD(0x1A)` + `Tx32(addr)` + `Tx32(len)` + `Tx8(0 exec)` +
  `TxData`. `fifoWR = memWR(ADDR_FIFO)`. The Pico builds the same bytes.
- **Address**: `ADDR_FIFO = 0x1810000` matches host + FPGA (`pi.sv`: `pi_dst==3 & addr[21:16]==1`).
- **Exec flag**: `0`, matches host for both RAM and FIFO.
- **Write shape**: coalesced (one contiguous CDC write of cmd+header+data) AND split (cmd /
  header / data as separate writes) AND split-with-a-3ms-gap between header and data - all fail.
- **Read side**: a write-only "blind boot" (`*n`+path+`*s`, ignoring all replies) leaves
  `CMD_STATUS` at 0 (still at menu) - so it is the *write* that doesn't land, not the reply read.
- **FIFO full**: fails as the first clean write after a power-cycle (empty FIFO).
- **TX not flushed**: instrumented `edio_write` - the TX FIFO drains to empty in 0 ms after every
  write (incl. FIFO writes), i.e. the bytes leave the Pico and the bulk transfer starts/keeps up.
- **DTR/RTS**: asserting `tuh_cdc_connect()` (DTR|RTS) on mount - as a kernel/FTDI host does on
  open - makes no difference.
- **Exact host write chunking**: `strace` of a working host FIFO memWR shows FIVE separate
  `write()`s - `cmd(4)`, `addr(4)`, `len(4)`, `exec(1)`, `data(N)`, with `exec` in its own
  1-byte write. Replicating that exactly on the Pico (five separate `edio_write`s, each drained
  to completion so each is its own USB transfer) **still fails**. So the kernel coalesces those
  writes into URBs anyway, or the difference is below the syscall-chunking level. Either way,
  write chunking is not the cause.

Note: `edio_mem_wr` now uses the identical 5-part sequence for BOTH SRM and FIFO. SRM works,
FIFO doesn't - same code, same transfer mechanics, only the destination address differs. So it
is not a transfer-mechanics bug (data toggle, packetization, chunking); it is specifically how
the N8 MCU routes a FIFO-addressed write, and that routing is sensitive to a wire-level property
of the Pico-PIO-USB host that a kernel cdc-acm host provides incidentally.

## usbmon capture of the WORKING host FIFO write (bus 3, dev 55)

Captured `/sys/kernel/debug/usb/usbmon/3u` while the host ran
`retroplug-n8-hwtest memwr 0x1810000 <90 3c 7f>` (which drives the note that provably plays).
Facts established:
- **N8 is FULL SPEED (12 Mbps) on the PC too** (`/sys/.../3-6/speed` = 12) - so the Pico's
  full-speed PIO-USB is NOT a speed mismatch.
- **Bulk endpoints are 64-byte** (`ep_01` OUT, `ep_81` IN); interrupt `ep_82` (8B) is the ACM
  notification. Same for any host.
- The memWR OUT is **five separate bulk-OUT URBs**: `2bd41ae5`(4) / `00008101`(4) / `03000000`(4)
  / `00`(1) / `903c7f`(3), each completing status 0, no ZLP. The Pico now emits this exact shape.
- The host asserts **`SET_CONTROL_LINE_STATE` DTR|RTS=0x0003** (control OUT, bRequest 0x22) before
  the writes and clears it (0x0000) after. The Pico now does this synchronously and **verified**
  (`tuh_cdc_get_dtr()==1`). **No effect - FIFO write still not seen.**
- The host keeps ~18 bulk-IN URBs pending; the FIFO write itself produces **no IN traffic** (the
  next IN completion is the separate memRD verify), so IN-polling depth is not required for the
  write to land.

So the Pico now matches the host at every observable layer - speed, endpoints, DTR, exact URB
byte sequence and chunking - and SRM memWR still works while the FIFO memWR (issued by the same
code microseconds later, only the address differs) still doesn't. The remaining difference is
**below the URB layer**: the actual wire timing/framing of Pico-PIO-USB (a software/PIO USB host)
vs a silicon host controller, which the N8 MCU's FIFO-write path is sensitive to and its
RAM-write path is not. This is not fixable by shaping `write()`s / URBs from the CDC API, and is
invisible to usbmon (which logs the host's URBs, not the Pico's PIO wire).

## Verdict
This is a **Pico-PIO-USB limitation** for the N8's cart-FIFO write path, not a bug in the bridge's
Edio/framing (all of which matches the working host byte-for-byte). Realistic resolutions:
- **Pi Zero (or any full-Linux SBC) as the USB host** - uses the proven kernel `cdc-acm` path;
  drives the N8 FIFO correctly, at the cost of the bare-RP2350 form factor. Recommended.
- Ask **krikzz** whether host->cart-FIFO writes need a wire-level property a PIO-USB host omits.
- Deep-dive **Pico-PIO-USB** bulk-OUT timing (inter-packet gaps, PID/toggle, bit timing) with a
  hardware USB analyzer on the Pico's D+/D- - the only way to see the actual difference.

## Isolation summary
Same memWR command, same session: **addr 0x1000000 works, addr 0x1810000 does not.** Transport,
framing, exec, and TX completion are identical. The only variable is the destination, and the FIFO
destination is exactly the one the host drives successfully. So the divergence is below the Edio
byte layer - in how the N8's (closed) MCU firmware handles a FIFO-destined memWR over the Pico's
specific USB delivery, in a way that RAM writes are insensitive to.

## Remaining hypotheses (need tools we don't have here)
1. The N8 MCU routes FIFO-destined memWR through a path sensitive to a subtle USB characteristic of
   the Pico-PIO-USB host that a kernel/FTDI host satisfies incidentally: bulk-OUT packet
   timing/size, ZLP/short-packet handling, double-buffering, or SOF-relative scheduling.
2. A Pico-PIO-USB / TinyUSB `cdc_host` bulk-OUT quirk specific to this device's endpoints.

## How to resolve
- **USB bus capture is the way in, but not from this dev container** - usbmon has no module,
  debugfs won't mount, and there's no tcpdump/tshark here. Capture on the **actual host machine**
  instead: `sudo modprobe usbmon` then Wireshark on the N8's bus (or `cat
  /sys/kernel/debug/usb/usbmon/<bus>u`) while running a working FIFO write
  (`retroplug-n8-hwtest memwr 0x1810000 <3-byte-file>` or `retroplug-cli n8-load`). Diff those
  URBs against the Pico's PIO-USB output (needs a hardware analyzer or a second capture) - the
  bulk-OUT that carries `2b d4 1a e5 00 00 81 01 ...` is the one to compare.
- The N8 **MCU firmware** ("nesos") is closed; FPGA cores + Edio examples are open
  (krikzz/edn8-pro-pub). The FIFO-write routing that matters is on the MCU side.
- Ask krikzz / the N8 community whether host->cart-FIFO writes over USB need anything a minimal
  TinyUSB CDC host would omit that a kernel cdc-acm host provides.
- **Fallback**: a full-Linux SBC (e.g. Raspberry Pi Zero) as the USB host uses the same kernel
  cdc-acm driver that provably works - it would drive the N8 correctly at the cost of the
  bare-Pico form factor.

## Status of the rest of the bridge
Everything up to the FIFO write is solid on the Pico: PIO-USB host + CDC, Edio command/response,
memRD, memWR-to-RAM, plus the MIDI-in parser and the (untestable-until-this-works) menu-boot
sequence. Only `fifoWR` delivery is blocked. The `slice 2.3`/`slice 2.4` commits that claimed the
MIDI bridge was hardware-verified are therefore incorrect (they observed the host's residual note)
and should be corrected.

## Repro from this tree
`pico/n8-host/` (build with `PICO_SDK_PATH` + `cmake --build build`, flash over SWD). The current
`boot_bliptoaster()` runs the minimal diagnostic (SRM round-trip + one clean `*t`) on mount. The N8
must be at its menu (power-cycle first). Console on `/dev/ttyDbgProbe` @ 115200.
