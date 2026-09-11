# N8 Pro over-USB: cool things we can do with SRAM (and beyond)

Brainstorm of what the Everdrive N8 Pro USB link opens up now that we can read + write cart
SRAM and drive host sync. Grouped by how far off each is. Companion to
[n8-bridge-plan.md](n8-bridge-plan.md) (the sync bridge roadmap).

## What we can already do (the primitives)

- **Read/write any N8 memory address** over USB via `Edio::memRD` / `memWR` (CMD `0x19` / `0x1A`):
  - `ADDR_SRM = 0x1000000` - cart **battery RAM** (a game's song), 64 KB game region. Round-trips
    byte-identical.
  - `ADDR_PRG` (PRG), `ADDR_CHR` (CHR / tile graphics), `ADDR_CFG`, `ADDR_SSR`, `ADDR_FIFO`.
- **Native save restore** - write a `.srm` to `EDN8/gamedata/<rom>/bram.srm` (SD file API) and let the
  menu copy it into cart SRAM at hand-off. Direct `memWR(ADDR_SRM)` also works but corrupts the running
  menu, so the gamedata route is the safe one.
- **SD file API** - read / write / list files and directories (`Edio::listDir`, `fileOpen`/`fileWrite`).
- **Host-sync playback** - `retroplug-cli n8-sync` turns a MIDI clock/transport into risa's arm/clock/stop
  protocol; verified audible on the real 2A03.
- **RetroPlug codecs** - full risa + LSDj `.sav` decode/encode, runtime readers, and the `compileDmc`
  DPCM kit compiler, all reusable against bytes we pull off the cart.
- **Menu VRAM dump** (`*v`) - 2 KB nametable + 16 B palette, but only while the **menu** is running
  (menu-only screen, not a running game's).
- **Tooling today:** `retroplug-cli n8-load` (`--srm` / `--dump-sram` / `--ls` / `--sram-only`),
  `n8-sync`, `n8-bridge`.

---

## Ready basically now (codec + `--dump-sram` already exist)

### 1. `--show-song` inspector
Dump `ADDR_SRM`, decode with the risa/LSDj codec, print the song: chains, phrases, instruments, tempo,
kit assignments. A reliable "what's on the cart right now" without needing a screen.
*Effort: low (codec + dump both exist).*

### 2. Rip a hardware jam into RetroPlug
Read cart SRAM -> wrap as a `.rplg`/`.sav` -> open in the emulator. Noodle on the real NES, then pull it
into RetroPlug to render a clean WAV, export stems, or keep editing. We already push the other direction
(gamedata restore), so this closes the loop.
*Effort: low.*

### 3. Save library / backup / diff
Enumerate every `EDN8/gamedata/*/bram.srm` (via `--ls` + file read), archive them, git-track, and **diff**
revisions. Undo-history / time-machine for hardware WIPs - see exactly which rows changed between jam
sessions.
*Effort: low-medium.*

---

## The killer workflow: round-trip hardware <-> RetroPlug

### 4. Two-way song sync
Compose in RetroPlug's UI -> push to the cart -> jam/perform on real silicon under DAW sync (already
working) -> pull back to refine. The sav codec makes it lossless. This is the one most worth building out
properly.
*Effort: medium (mostly glue over 1 + 2).*

---

## Cooler / more ambitious

### 5. Savestate ripping -> the framebuffer + live state
The `gamedata/<rom>/` folders also hold N8 **savestates** (`NN.SAV`, ~48 KB). A NES savestate contains the
*internal* WRAM + PPU **nametables** + palette + OAM - exactly what is NOT on the cart bus. Dump an existing
risa `NN.SAV`, parse the N8 savestate layout, and you can reconstruct risa's actual **screen** (nametable +
CHR + palette -> PNG) *and* read its **live playhead** (internal WRAM via the risa runtime reader). That is
a real "risa screen over USB", via savestates instead of the menu-only `*v`.
*Unknowns: triggering a savestate over USB, reverse-engineering the `.SAV` format. Cheap to start - dump one
and poke at it. Effort: medium-high.*

### 6. Emulator-vs-silicon validation
Render a song in Mesen and on the real 2A03 (sync + audio capture), then compare. A regression harness that
checks RetroPlug's NES emulation against actual hardware - genuinely novel.
*Effort: medium.*

### 7. Custom DMC sample kits onto the cart
RetroPlug's `compileDmc` builds 1-bit DPCM kits; splice one into a risa save/ROM and push it, so hardware
risa plays custom samples.
*Effort: medium (reuses the risa-rom kit tooling).*

### 8. Incremental diff-writes
`memWR` takes any addr + len, so push only the bytes that changed for near-instant song updates while
iterating. A speed optimization for any of the write-back workflows above.
*Effort: low, once round-trip exists.*

### 9. Menu screenshot via `*v`
`n8-load --screenshot <png>`: `*v` (nametable + palette) + read CHR -> render a real PNG of the **menu**
screen. No capture card. Useful for verifying menu state (which folder, that a load landed); proves the
VRAM+CHR -> PNG pipeline that #5 needs for a running game.
*Effort: low-medium.*

---

## Suggested order

1. **`--show-song`** + **rip-to-`.rplg`** (#1, #2) - immediate value, unlocks the round-trip.
2. **Round-trip** (#4) - the practical win.
3. **Savestate dump experiment** (#5) - cheap to probe, and it cracks the risa-screen-over-USB itch.

Everything here reuses existing pieces (`Edio` memRD/memWR + file API, the risa/LSDj codecs, `compileDmc`,
`n8-sync`); nothing needs new hardware.
