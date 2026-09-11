# EverDrive-N8 Pro: menu reboot `'*r'` (edlink `reset`) hangs the console to a gray screen

## Summary

Issuing the menu **reboot** command `'*r'` over USB while the N8 is sitting at its **file browser**
reboots the console into a **solid gray screen** and hangs. The menu never returns: it stops answering
the `'*t'` handshake, and the only way to recover is a physical **power-cycle**. Low-level device access
(memory reads, the SD file API) keeps working over USB throughout, so the MCU/USB layer is alive - it is
the 6502 menu application that does not come back.

This is the exact sequence edlink's `reset` command performs (`DEV_EDN8/DeviceCmd.Reset()` =
`MenuCmd.Test()` + `MenuCmd.Reset()`), so it should reproduce with `edlink reset` run against a unit at
the file browser.

## Hardware / firmware

- Device: **EverDrive-N8 Pro**, NES form factor.
- Device (via `CMD_SYS_INF` over USB): **EverDrive-N8 PRO** (device id 0x17), serial `00035AAD.00002C4D`,
  **NES form factor**, bootloader `0x0100`, sw `0x103` / hw `0x1`, MCU-core build **2025-11-08**, assemble
  date 2022-10-19.
- SD-file fingerprints (in case they pin the build):
  - `EDN8/nesos.nes` = 147472 bytes
  - `EDN8/syscore/iocore.bin` = 131080 bytes
  - `EDN8/syscore/n8nsf.nes` = 40976 bytes
  - `EDN8/mappers.png` = 24608 bytes
- Host: Linux, USB CDC (`/dev/ttyACM0`) at 921600 baud.

## Reproduction

Preconditions: N8 powered on, sitting at its **file browser** (no game running).

Using the official tool:

```
edlink reset
```

Or, at the protocol level (edlink V1 framing; all menu commands are a `CMD_MEM_WR` (0x1A) of the 2-byte
payload `'*' <char>` to the FIFO at `ADDR_FCI_FIFO` = 0x1810000):

1. Open the CDC port @ 921600, handshake: `CMD_STATUS` (0x10) -> expect `0xA5xx`. **OK.**
2. `MenuCmd.Test()`: write `'*t'` (`2A 74`) to the FIFO, read 1 byte -> `'k'` (0x6B). **OK - menu alive.**
3. `MenuCmd.Reset()`: write `'*r'` (`2A 72`) to the FIFO, read 1 ack byte.

On the wire, step 3's FIFO write is:

```
2B D4 1A E5  00 00 81 01  02 00 00 00  00  2A 72
└ CMD_MEM_WR ┘ └ addr LE ─┘ └ len=2 LE ┘ exec  '*' 'r'
```

## Expected vs observed

- **Expected:** the console reboots back to the file browser (a fresh menu).
- **Observed:** the screen goes black briefly, then to a **solid uniform gray** (the NES backdrop colour,
  no rendering), and stays there indefinitely (confirmed unchanged after 45 s untouched). The menu no
  longer answers `'*t'` (times out). The USB CDC port **re-enumerates** during the reboot (the `ttyACM`
  node cycles), consistent with a full FPGA reconfigure. `CMD_MEM_RD` and the `CMD_F_*` SD file API keep
  responding over USB the whole time, so only the 6502 menu app is stuck.

## Recovery

Only a physical power-cycle restores the file browser. After power-cycle the menu answers `'*t'` and
renders normally again.

## What I ruled out

- `CMD_HOST_RST` (0x29): defined but only used by `DEV_MEGA` / `DEV_TED` (Genesis / PC-Engine reset line),
  never in the N8 path.
- `CMD_RST_EFU` (0x25): reboots into the firmware-update unit, not the file browser.
- `CMD_RST_MCU` (0x12): enters the MCU bootloader (service mode), not the file browser.
- Not my reconnect/polling: the hang persists even when the host issues `'*r'` and then leaves the device
  completely untouched.

## Questions

1. Is `'*r'` (menu reboot) intended to be issued while the **file browser** is active, or only to reset a
   **running game** back to the menu? (Since `'*r'` is serviced by the menu's FIFO poll loop, it can only
   be delivered from the menu - which is exactly where it hangs.)
2. If it is meant to work from the menu, is there additional host-side handshaking expected after `'*r'`
   beyond reading the single ack byte?
3. Is there a documented **"return to file browser over USB"** path - e.g. writing a `MapConfig` with
   `map_idx = 255` + `MAP_CTRL_UNLOCK` to `ADDR_CFG` (as device-side `ed_exit_game()` does in
   `edn8-pro-pub`) followed by a reset - or a menu command we are missing?

## Note on independent reproduction

This was found and reproduced with an independent reimplementation of the edlink V1 protocol (byte-identical
framing, verified against the `edlink` and `edn8-pro-pub` sources), running on Linux - not the official
`edlink` binary (no .NET Framework runtime on the test host). Please confirm with `edlink reset` on a unit
at the file browser; given the sequences are identical, it should reproduce.

---
*Suggested channels: a GitHub issue on `krikzz/edn8-pro-pub`, the krikzz.com forum, or krikzz support.*
