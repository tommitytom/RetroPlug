# EverDrive N8 Pro over USB — capability survey

What the N8 Pro can do over its USB serial link, what RetroPlug already drives, and what's
still on the table. Grounded in krikzz's own (partially-open) sources, not guesswork.

## TL;DR

RetroPlug's `Edio` implements roughly a fifth of the N8 link protocol — enough to load a ROM,
push MIDI/sync to the cart FIFO, read/write battery SRAM, list SD dirs, and (new) poke device
memory. The firmware exposes a lot more over the same wire: **screen capture, console reset,
a live console-state sniffer (APU/PPU/CPU/RAM/OAM), full save-states, arbitrary SD file read,
FPGA core loading, RTC, flash access, memory test/CRC, and a TCP/IP bridge.** Most are a small
addition to `Edio` (a new opcode, a menu command, or just pointing `memRD`/`memWR` at a new
address).

## Sources — the N8 Pro is *partially* open

- **`nesos.nes`** (the on-screen menu OS) — **closed**, binary firmware only.
- **`github.com/krikzz/edn8-pro-pub`** — **open**: FPGA mapper cores (`fpga/`), the low-level
  hardware-access example (`edio/everdrive.{c,h}`), and the NSF player. Cloned to
  `/workspaces/edn8-pro-pub`.
- **`github.com/krikzz/edlink`** — **open**: the C# PC-side link tool. Cloned to
  `/workspaces/edlink`. `edlink/Device/DeviceIO_V1.cs` = the full opcode list;
  `edlink/DEV_EDN8/*` = the N8 device + menu commands; `edlink/edlink/Help.cs` = the CLI.

Key implication: the audio/graphics/state paths we care about are documented in the open FPGA +
`edio` sources; we do **not** need the closed OS to reach them.

## What RetroPlug already drives (`packages/native/src/host/n8/`)

| Area | Ops | Where |
|---|---|---|
| Handshake | `CMD_STATUS` (0x10) | `Edio::connect` |
| Device memory | `CMD_MEM_RD` (0x19), `CMD_MEM_WR` (0x1A) | `Edio::memRD/memWR`; SRAM dump/restore, expansion-volume poke |
| Cart FIFO | `CMD_MEM_WR` → `ADDR_FIFO` | `Edio::fifoWR`; MIDI bridge + risa sync |
| SD dir list | `CMD_F_DIR_LD/SIZE/GET` (0xC5/C6/C8) | `Edio::listDir` |
| SD file **write** | `CMD_F_FOPN/FWR/FCLOSE` (0xC9/CC/CE) | `Edio::fileOpen/Write/Close`; ROM upload |
| Menu | `'t'` test, `'n'` select, `'s'` start, `'r'` reboot | `N8Menu` |
| CLI/harness | `n8-load` (load/`--srm`/`--dump-sram`/`--ls`), `n8-bridge`, `n8-sync`, `retroplug-n8-hwtest` (dump/load/restore/**peek/poke**) | `cli/sessions/n8-*.ts`, `test/n8/n8-sd-hwtest.cpp` |

## Full command set (`edlink/Device/DeviceIO_V1.cs`)

`✓` = used by our Edio.

| Op | Code | Meaning | Op | Code | Meaning |
|---|---|---|---|---|---|
| `CMD_STATUS` ✓ | 0x10 | handshake / status | `CMD_FIFO_WR` | 0x23 | write cart FIFO |
| `CMD_GET_MODE` | 0x11 | current mode | `CMD_UART_WR` | 0x24 | UART passthrough |
| `CMD_RST_MCU` | 0x12 | reset the MCU | `CMD_RST_EFU` | 0x25 | reset firmware-update unit |
| `CMD_GET_VDC` | 0x13 | voltage/diagnostic | `CMD_SYS_INF` | 0x26 | device info (serial/versions) |
| `CMD_RTC_GET` | 0x14 | read clock | `CMD_GAME_CTR` | 0x27 | game counter (not controller) |
| `CMD_RTC_SET` | 0x15 | set clock | `CMD_UPD_EXEC` | 0x28 | execute firmware update |
| `CMD_FLA_RD` | 0x16 | read flash | `CMD_HOST_RST` | 0x29 | **reset the console (host)** |
| `CMD_FLA_WR` | 0x17 | write flash | `CMD_DISK_INIT` | 0xC0 | mount SD |
| `CMD_FLA_WR_SDC` | 0x18 | flash write from SD | `CMD_F_DIR_*` | 0xC3–C8 | dir open/read/load/size/path/get |
| `CMD_MEM_RD` ✓ | 0x19 | read device mem | `CMD_F_FOPN` ✓ | 0xC9 | open file |
| `CMD_MEM_WR` ✓ | 0x1A | write device mem | `CMD_F_FRD` | 0xCA | **read file** (we lack this) |
| `CMD_MEM_SET` | 0x1B | fill memory | `CMD_F_FRD_MEM` | 0xCB | read file → device mem |
| `CMD_MEM_TST` | 0x1C | memory test | `CMD_F_FWR` ✓ | 0xCC | write file |
| `CMD_MEM_CRC` | 0x1D | memory CRC | `CMD_F_FWR_MEM` | 0xCD | device mem → file |
| `CMD_FPG_USB` | 0x1E | load FPGA core (USB) | `CMD_F_FCLOSE` ✓ | 0xCE | close file |
| `CMD_FPG_SDC` | 0x1F | load FPGA core (SD) | `CMD_F_FPTR` | 0xCF | seek |
| `CMD_FPG_FLA` | 0x20 | load FPGA core (flash) | `CMD_F_FINFO` | 0xD0 | file info |
| `CMD_RTC_CAL` | 0x21 | RTC calibrate | `CMD_F_FCRC` | 0xD1 | file CRC |
| `CMD_USB_WR` | 0x22 | USB write | `CMD_F_DIR_MK`/`F_DEL`/`F_AVB` | 0xD2/D3/D5 | mkdir / delete / free space |
| `CMD_USB_RECOV` | 0xF0 | recovery | `CMD_RUN_APP` | 0xF1 | run app |

## Device address map (PI bus — targets for `memRD`/`memWR`)

From `edn8-pro-pub/edio/everdrive.h`.

| Address | Size | Region |
|---|---|---|
| `ADDR_PRG` `0x0000000` | 8 MB (`SIZE_PRG`) | PRG ROM (loaded game code) — live-patchable |
| `ADDR_CHR` `0x0800000` | 8 MB (`SIZE_CHR`) | CHR ROM (loaded game tiles) — live-patchable |
| `ADDR_SRM` `0x1000000` | 256 KB (64 KB game) | battery RAM (our SRAM dump/restore) |
| `ADDR_CFG` `0x1800000` | 16 cfg regs @ +32 | **system config** (mapper, master_vol, ctrl…) — write-only over USB |
| `ADDR_SSR` `0x1802000` | ~16 KB | **save-state "sniffer"** — live console state (see below) |
| `ADDR_FIFO` `0x1810000` | 2 KB (`SIZE_FIFO`) | cart FIFO (our MIDI/sync path) |
| Flash `ADDR_FLA_MENU/FPGA/RECO/ICOR` | 0x00000/0x40000/0x80000/0xC0000 | menu / fpga / recovery / firmware |

## System config registers (`ADDR_CFG`, `MapConfig` = `scfg[0..9]`)

Written as a block at `0x1800000` with the register section at **offset 32** (USB view;
`edlink` `ConfigReset` uses `cfg_base=32`). So `scfg[i]` is at `0x1800020 + i`. **Write-only
over USB** (reads return `0xFF`); the value is **live** but the OS reloads its stored copy on
reboot/mapload.

| idx | addr | field | notes |
|---|---|---|---|
| 0 | 0x1800020 | `map_idx` | mapper number (+ high nibble in scfg[2]) |
| 1 | 0x1800021 | `prg_msk` | PRG/SRM size masks |
| 2 | 0x1800022 | `chr_msk` | CHR mask + map_idx high |
| 3 | **0x1800023** | **`master_vol`** | **expansion-audio volume** — `exp*master_vol/128`; 0=mute, 128=unity, 255≈2× |
| 4 | 0x1800024 | `map_cfg` | mirroring, chr-ram, prg-ram, map sub |
| 5/6/8 | 0x1800025/26/28 | `ss_key_*` | save-state save/load/menu button combos |
| 7 | 0x1800027 | `map_ctrl` | ctrl bits: reset-delay, in-game-menu, **cheats**, ss button, NES/Famicom, unlock |
| 9 | 0x1800029 | `jmp_val` | jumper |

**VERIFIED on hardware (2026-08-13):** poking `master_vol` at `0x1800023` on a running VRC6
cart tracked the expansion audio on the L6 exactly — `0x00` mutes to noise floor, `0x20` = −12 dB,
`0x80` unity, `0xF0` ≈ +5 dB (within ~0.3 dB of `master_vol/128`).

## The save-state "sniffer" — live console state (`ADDR_SSR = 0x1802000`)

The N8 continuously captures the running console into a memory-mapped buffer (the same data a
save-state uses). Readable via `memRD` — a debugger/inspector over USB. Structure per
`everdrive.h` (offsets within the sniffer buffer; exact device-address mapping + read semantics
**unverified** — confirm before relying on it):

| offset | size | contents |
|---|---|---|
| 0x0000 | 2 KB | **WRAM** (console RAM) |
| 0x0800 | 4 KB | **VRAM** (nametables) |
| 0x1800 | 128 | mapper regs |
| 0x1880 | 32 | **APU registers** (live audio state) |
| 0x18A0 | 32 | **PPU palette** (screen colours) |
| 0x18C0 | 4 | PPU regs (ctrl, mask, scroll) |
| 0x18FF | 1 | `SS_HIT` (who invoked in-game menu) |
| 0x1900 | 256 | **OAM** (sprites) |
| 0x19C8 | 4 | **CPU regs** (a, x, y, sp) |
| 0x1A00/0x1C00 | 512/1 KB | mapper memory |
| 0x2000 | 8 KB | CHR |

Reading APU regs off real silicon would directly validate the emulator's
`getApuState`/`getExpansionAudioState` used in the EverMIDI audio-tuning work.

## Menu commands (`edlink/DEV_EDN8/MenuCmd.cs`, sent through the FIFO menu channel)

| char | meaning | ours? |
|---|---|---|
| `'t'` | test / handshake | ✓ `N8Menu::test` |
| `'n'` | select game (install) | ✓ `appInstall` |
| `'s'` | run game | ✓ `appStart` |
| `'r'` | reboot | ✗ HANGS the console to a gray screen from the menu (see caveat) |
| `'h'` | halt | ✗ |
| `'v'` | **VRAM dump** (screen capture) | ✓ `vramDump` (`n8-load --screenshot`) |

## Capability catalog — what's not yet exposed

Grouped by payoff. "Effort" is the add to `Edio`/tooling.

### Most useful for this workflow

| Capability | How | Effort | Notes |
|---|---|---|---|
| ✅ **Screen capture over USB** | menu `'v'` (2 KB VRAM + 16-byte palette) + `memRD` CHR → PNG (`edlink` `screen`, `MenuImage.MakeImage`) | menu cmd + assembly | DONE (`n8-load --screenshot`): dropped the `/dev/video0` capture card for menu grabs |
| ⛔ **Reset console over USB** | `edlink` `reset` = `Test()`+`'r'`+single-ack | tried, does not work | NOT VIABLE on our N8. Implemented + hardware-tested the EXACT `edlink` sequence (`'*t'` handshake then `'*r'` + one ack); from the file browser `'*r'` reboots into a solid **gray screen** and hangs (menu stops answering `'*t'`; recovery needs a power-cycle — strictly worse than the smart-plug). `'*r'` is menu-FIFO-serviced so it can only be sent from the menu, exactly where it hangs. `CMD_HOST_RST` (0x29) is Genesis/PC-Engine only; `CMD_RST_EFU` (0x25) reboots into the firmware-update unit; `CMD_RST_MCU` (0x12) is the bootloader — none is a clean file-browser reboot. A clean return-to-menu would need `ed_exit_game`'s config write (`map_idx=255`+`UNLOCK` to `ADDR_CFG`) + reset, which is unverified RE. Reverted. |
| **Live state sniffer read** | `memRD` at `ADDR_SSR` (APU/PPU/CPU/RAM/OAM) | just `memRD` | On-hardware audio/graphics state; validate emulator |
| ✅ **Arbitrary SD file read** | `CMD_F_FRD` (0xCA) / `CMD_F_FRD_MEM` (0xCB) | 1 opcode | DONE (`n8-load --get-file`): dump `EDN8/sysdata/registry.bin` to **persist** expansion-volume; pull any save/config/OS file |

### Powerful / cool

| Capability | How | Notes |
|---|---|---|
| **Save-state capture/restore** | sniffer region + save-state buffer + `ss_key_*` cfg | snapshot/restore a running game over USB |
| **PRG/CHR hot-patch** | `memWR` → `ADDR_PRG`/`ADDR_CHR` | live-patch a running ROM's code or graphics (we already have `memWR`) |
| **Load FPGA mapper core** | `CMD_FPG_USB` (0x1E) / `ed_cmd_fpg_init_usb` | swap cartridge hardware personality over USB |
| **RTC get/set/cal** | `CMD_RTC_GET/SET/CAL` (0x14/15/21) | read/set the console clock |
| **Device info** | `CMD_SYS_INF` (0x26) → serial, sw/boot ver, device id; `edlink` `devinf` | includes NES-vs-Famicom form factor |
| **Memory fill/test/CRC** | `CMD_MEM_SET/TST/CRC` (0x1B/1C/1D); `edlink` `diag` | fast SRAM verify, self-test |
| **Flash read/write** | `CMD_FLA_RD/WR` (0x16/17); `edlink` `flard`/`flawr` | cart firmware / recovery |
| **Full SD file ops** | copy/move/delete/mkdir/info/seek/crc/free (`CMD_F_*` 0xC3–D5) | complete filesystem, not just write |
| **Cheats engine** | `map_ctrl` bit 2 + `CheatSlot`/`CheatList` (`everdrive.h`) | Game-Genie codes via config |
| **In-game menu / halt hooks** | `map_ctrl` ss bits; menu `'h'` | trigger the in-game menu / halt |
| **TCP/IP bridge** | `edlink` `netgate` | tunnel the cart link over the network |
| **UART / USB passthrough** | `CMD_UART_WR` (0x24), `CMD_USB_WR` (0x22) | raw byte channels |

## Caveats & gotchas

- **Config registers are write-only over USB** — `memRD` of `0x1800000` returns `0xFF`; the audio
  is the only readback. Values are **live-only**; a reboot/mapload reloads the OS's stored copy.
  Persisting a config change means also editing `EDN8/sysdata/registry.bin` (needs `CMD_F_FRD` to
  read it first).
- **USB address offset trap:** the NES-CPU-side `MapConfig` layout starts at offset 0, but over
  USB the config block sits at **offset 32** (`master_vol` = `0x1800023`, not `0x1800003`). Poking
  the wrong one silently no-ops.
- **`*r` menu reboot HANGS the console (confirmed 2026-08-13, feature reverted).** We implemented the
  EXACT `edlink` `DeviceCmd.Reset` (`'*t'` handshake to re-sync the framing, then `'*r'` + one ack) and
  tested on real hardware: from the file browser, `'*r'` reboots into a solid **gray screen** and the
  6502 does not return to the menu (the menu stops answering `'*t'`; file/mem ops over the MCU still
  work, but recovery needs a power-cycle). Since `'*r'` is menu-FIFO-serviced it can ONLY be sent from
  the menu — exactly where it hangs — so there is no context in which it usefully returns to the browser.
  `CMD_HOST_RST` (0x29) is Genesis/PC-Engine only (never used on the N8); `CMD_RST_EFU` (0x25) reboots
  into the firmware-update unit; `CMD_RST_MCU` (0x12) is the bootloader. The only plausible clean path is
  `ed_exit_game`'s config write (`map_idx=255` + `MAP_CTRL_UNLOCK` to `ADDR_CFG`) followed by a reset —
  device-side C in `edn8-pro-pub`, but unverified over USB. Left for a future slice.
- **The file API works while a game runs** (verified — `--ls` responded with EverMIDI running).
- **`ed_cmd_game_ctr` is a counter**, not controller injection — no button-injection over USB was
  found.

## Suggested next builds (highest payoff-to-effort)

1. ✅ **Screen capture over USB** (menu `'v'` + CHR read + PNG) — DONE (`n8-load --screenshot`).
2. ✅ **`CMD_F_FRD` file-read** — DONE (`n8-load --get-file`): reads `registry.bin`, saves, any SD file.
3. ⛔ **Reset-over-USB** — tried, NOT VIABLE: `'*r'` from the menu hangs the console to a gray screen
   (the exact `edlink` sequence; reverted). See the caveat + capability catalog for the full finding.
4. **Sniffer read helper** — `memRD` wrappers for APU/PPU/OAM to inspect real-hardware state (next best).

---
*Reference: `/workspaces/edlink` (DeviceIO_V1.cs, DEV_EDN8/*, Help.cs), `/workspaces/edn8-pro-pub`
(edio/everdrive.{c,h}, fpga/base_sv/{sys_cfg,everdrive,var}.sv). Verified items dated 2026-08-13
on the physical N8 + Zoom L6 lab.*
