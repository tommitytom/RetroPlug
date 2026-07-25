# Engineering Report & Integration Plan: LSDj-level "risa" support in RetroPlug

**Author:** Lead architect · **Date:** 2026-07-19 · **Status:** M0 spike complete — every blocking unknown resolved GREEN
**Audience:** RetroPlug engineers (no prior risa knowledge assumed)

---

## 0. M0 spike results (2026-07-19) — all blocking unknowns resolved

Built risa 2.2.1 from source (distro cc65 2.19 is too old — it exports the C-stack ZP pointer as `sp`, but risa's `crt0.s` uses the newer `c_sp`; built cc65 git `547d923` to `/usr/local`, then `make all` after adding a lowercase `kits/psr150.rik` copy for the case-sensitive FS). Produced `build/risa-pal.nes` (557072 B = 0x88010) + `build/risa-pal.lbl` (VICE labels) + `.dbg`. Then booted it in RetroPlug's **real Mesen core** via a native harness spike (`packages/retroplug/test-native/risa-m0-spike.test.ts`). Results:

| Unknown (from §1 / critique) | Result | Evidence |
|---|---|---|
| Mesen supports MMC5 (mapper 5) + boots risa | ✅ **Yes** | Mesen iNES parse: `NES 2.0: Yes · Mapper: 5 Sub: 0 · PRG 512KB · CHR 32KB · Save RAM: 64 KB · Battery: Yes`; system constructed + activated. |
| `readSram` returns the **full 64 KB** battery (vs 8 KB `$6000` window) | ✅ **Full 64 KB** | `readSram len=65536` (`0x10000`). Song management rides the existing `readSram`/`loadSram` seam — **no native banked-WRAM work for M1/M2**. Also clears the `SnapshotRegistry kMaxRamBytes=64K` ceiling exactly. |
| `readRam` exposes NES internal 2 KB per block | ✅ **Yes** | `readRam len=2048`. The runtime overlay (M4) needs **zero native work**. |
| RSAV catalog really sits at `0x8000` | ✅ **Confirmed** | `sram@0x8000 = "RSAV"` on a real emulated battery — validates §2.3 against hardware. |
| Battery bit / iNES flags | ✅ **Set** | risa `crt0.s:81` sets flags6 bit 1; Mesen reports `Battery: Yes`, `Save RAM: 64 KB`. |
| MMC5 audio renders | ✅ **Yes** | Loaded `hevander.srm` demo (65 KB = 64 KB + 1 KB tail), pressed START: `idle=0.00015 → playing=0.01335` RMS (~90×). |

**Verdict: proceed.** The one required native change (`MesenBackend` `romBytes`, §7.1) and the new NES-DMC encoder (§4.1) are unchanged; nothing in M0 surfaced a blocker. Note: the existing NES rom provider auto-attached the N8-MIDI FIFO role (`[NesN8MidiRole] FIFO attached at $40F0/$40F1`) — harmless for risa, but the risa provider should decide whether to attach it (§10). Remaining watch item: PAL vs NTSC symbol snapshot for M4 (both ROMs built; addresses may differ).

---

## 1. Executive summary

### What risa is

**risa** is an LSDj-style tracker that runs on the **NES/Famicom** instead of the Game Boy. Concretely it is a **512 KB iNES ROM using the MMC5 mapper (mapper 5)** with **5 sequencer tracks mapped 1:1 to the APU channels** — Pulse1, Pulse2, Triangle, Noise, and **DMC** (the DPCM sample channel). It is the NES analog of everything RetroPlug already does for LSDj on Game Boy: it has a battery-backed multi-song save (its `.sav`), on-ROM drum kits / UI themes / fonts, and live playback state in RAM that a UI overlay can read. The reference tooling is a browser "rom_patcher" (plain ES modules under `tools/rom_patcher/`) plus Python oracles (`tools/wav2dmc.py`, `tools/risa_save.py`, `tools/migrate_legacy_sav.py`). Version examined: **risa 2.2.1** (`APP_VERSION_TEXT = "2.2.1"`).

### What "LSDj-level support" entails

RetroPlug's LSDj integration is a well-defined 4-layer stack (recapped in §6). "LSDj-level risa support" means reproducing each layer for risa:

1. **Save codec + song management** — read/write risa's `.sav`, list/load/export/import/delete/reorder its saved songs, plus its `.risong` interchange file. (LSDj analog: `src/lsdj/codec/` + `lsdjSongOps.ts`.)
2. **ROM asset view/patch + non-destructive override role** — view and replace on-ROM kits (DPCM), themes (palette), and fonts (CHR), persisted as path-linked overrides applied in memory at construct. (LSDj analog: `src/lsdj/rom/` + `lsdjAssetsRole.ts`.)
3. **Native sample compiler** — import WAV → an on-ROM kit bank. This is the one genuinely new DSP algorithm: **NES DPCM (DMC) encoding**, which shares nothing at the byte level with LSDj's Game Boy 4-bit nibble PCM. (LSDj analog: `packages/native/src/lsdj/KitCompiler`.)
4. **Runtime state reader + dev overlay** — decode live playhead/tempo/screen/instrument from RAM. (LSDj analog: `src/lsdj/runtime/` + `LsdjOverlay.tsx`.)

### Feasibility verdict

**Feasible, and structurally easier than LSDj in several places, with one required native change and one genuinely new native algorithm.** The reasons it is tractable:

- **risa is an NES ROM → it is already a first-class RetroPlug platform.** `detectPlatform` returns `"nes"` and `defaultCoreFor("nes") === "mesen"` (`packages/retroplug/src/platform.ts:15-24`). risa is **not a new system type** — it is an NES ROM served by the existing Mesen core plus risa-specific feature roles, exactly as LSDj is a `gb` ROM served by SameBoy plus `lsdj-sync`/`lsdj-assets` roles.
- **The per-block RAM read seam already works for NES.** `MesenNesSystem::getMemory(rp::MemoryType::Ram)` returns `NesInternalRam` (the 2 KB internal RAM `$0000–$07FF`), which `SnapshotRegistry` republishes **every audio block** and exposes as `backend.readRam(id)` on the Engine RPC facet (`BackendRpcRegistration.hpp:47`). risa keeps **all** playhead/tempo/screen state in that 2 KB window, so the runtime overlay needs **zero native work**.
- **The save codec is pure TS with no compression to port.** risa's records use per-collection **presence bitsets**, not LSDj's block RLE — there is no `rle.ts` equivalent to write, and byte-preserving song management treats records as opaque blobs.
- **risa is your own open-source ROM, so there is no offset-drift problem.** The entire LSDj drift-detection machinery (`detect.ts`, `gen-lsdj-offsets.mjs`, the three `*.generated.ts` tables, the ~550-ROM corpus) exists only because LSDj is closed-source with per-version offset drift. risa ships a symbol table (VICE `.lbl` → Mesen `.mlb`, plus a cc65 `.dbg`), so runtime offsets are **authored from symbols, not detected**.

The two costs:

- **One required native change:** `MesenBackend::build` must honor `spec.romBytes` (the effective/patched ROM) instead of always slurping `spec.romPath`. Today it slurps unconditionally (`MesenBackend.cpp:45`), while `SameBoyBackend` has the branch (`SameBoyBackend.cpp:50-54`). Without this, non-destructive ROM asset overrides silently do nothing on NES. This is ~15 lines.
- **One new native algorithm:** an **NES DPCM encoder** (`wav2dmc.py` port) for kit sample import. It cannot reuse LSDj's nibble packer, but it can reuse the entire surrounding native scaffold (`SampleCache`, `Effects`, r8brain resample, enkiTS fan-out, the `compileKit` RPC pattern).

### Biggest risks & unknowns

1. **Does Mesen (RetroPlug's `deps/mesen`) fully support MMC5 audio + WRAM banking?** MMC5 is a common mapper and Mesen is a mature emulator, so this is low-risk, but it is **unverified in-tree** and gates everything. Must boot a real risa ROM in RetroPlug's Mesen first (M0 spike).
2. **What does `GetMemory(NesSaveRam)` actually return for a 64 KB MMC5 WRAM cart — the full 64 KB (all 8 banks) or only the currently-banked `$6000` 8 KB window?** If it returns the full 64 KB, song management works through the existing `readSram`/`loadSram` seam with no native work (the `.sav` **is** the 64 KB WRAM image). If it returns only 8 KB, a native banked-WRAM seam is required. **This single answer sizes M1/M2.** (Findings assume the full battery; must verify.)
3. **Does risa set the iNES battery flag (flags6 bit 1)?** RetroPlug gates the whole `.sav` path on `header[6] & 0x02` (`platform.ts:81`). The findings did not read risa's iNES flags6/7. If unset, RetroPlug won't treat risa as having a battery. Must verify against a real ROM.
4. **NES ROMs have no cartridge-title field**, so risa detection can't mirror LSDj's `title(header).startsWith("LSDJ")` cheaply. risa's version is the ASCII string `"RISA V"` found by **scanning the whole PRG**, not at a fixed offset. Detection needs a longer prefix read + scan, or a stable fixed-offset magic. Genuine design decision (§10).
5. **RAM symbol-address stability.** cc65 BSS/ZP addresses reshuffle on every risa build. The runtime reader must resolve offsets from the exact ROM build's `.lbl`, or bundle a per-version snapshot keyed on `APP_VERSION_TEXT`. End users have only `risa.nes` (no `.lbl`).
6. **DMC encoder byte-parity (PAL vs NTSC, and the ±2 clamp quirk).** `wav2dmc.py` is the declared source of truth and ships **PAL rates only**; risa builds both PAL and NTSC ROMs. Byte-identity requires reproducing its 32-tap fixed-point sinc and a deliberately non-hardware-accurate ±2 delta clamp. This pushes toward a **TS port** over a native r8brain re-implementation for the parity-critical path.

---

## 2. Risa SAV format

### 2.1 Container

The battery save is the **raw 64 KB MMC5 WRAM image**: 8 × 8 KB banks (`WRAM_BANK_SIZE = 0x2000`, `WRAM_BANKS = 8`, `SAVE_FILE_SIZE = 0x10000`) mapped at CPU `$6000–$7FFF` and bank-switched via MMC5 register `$5113` (`REG_WRAM_BANK`). RetroPlug reads/writes only the first 64 KB; everything else is passthrough.

Accepted on-disk container sizes (source: `tools/rom_patcher/src/save_manager/constants.js:1-3`, `catalog.js:156-175`, `tools/risa_save.py:11,148-164`):

| Size | Meaning | RetroPlug handling |
|---|---|---|
| `0x8000` (32 KB) | truncated "MMC5 rescue" | zero-extend to `0x10000` |
| `0x10000` (64 KB) | raw risa `.sav` (Mesen) | use as-is |
| `0x10400` (65 KB) | 64 KB + 1 KB tail; **zero tail = Everdrive, non-zero = Mesen** | split tail, **preserve** for re-emit |
| `0x40000` (256 KB) | Analogue Pocket wrapper; save is the first `0x10000` | unwrap; **preserve** the 256 KB wrapper for re-download |

This multi-container normalization has **no LSDj analog** (LSDj's `.sav` is a fixed 128 KiB) and is net-new TS. Mirror `normalizeSaveContainer` / `toDownloadBytes` in `catalog.js`.

### 2.2 Whole-file byte map (64 KB core)

Live banks 0–3 are the **working song** the firmware edits in RAM; catalog banks 4–7 are the **archive**. RetroPlug song management operates on the catalog. (Sources: `src/seq_data.h:5-8,19,25-30,48-54`; `src/seq_data_internal.h:16-21`.)

| File offset | Bank | Role | Within-bank contents (offsets relative to bank base) |
|---|---|---|---|
| `0x0000–0x1FFF` | 0 `BANK_PHRASES` | live | phrases `0x00–0x7F` (64 B each) |
| `0x2000–0x3FFF` | 1 `BANK_DATA` | live | chains `@0x0000`, song `@0x1000`, instruments `@0x1280`, grooves `@0x1580`, **`N8T` autosave magic `@0x1E80`** (ver `0x0C`=12), project-settings `@0x1E84` (8 B), song-name `@0x1E8C` |
| `0x4000–0x5FFF` | 2 `BANK_TABLES` | live | tables `@0x0000`, shared aux notes `@0x1000` |
| `0x6000–0x7FFF` | 3 `BANK_PHRASES_HI` | live | phrases `0x80–0xFE` |
| `0x8000–0xFFFF` | 4–7 | **catalog** | **RSAV** catalog (header `@0x8000`, records from `0x8100`) |

> **Two independent version numbers — do not conflate.** The live working song carries an **`N8T`** autosave signature at bank-1 offset `0x1E80` with version **12** (`SAVE_MAGIC_VER`/`AUTOSAVE_MAGIC_VER = 0x0C`). The catalog carries an **`RSAV`** signature with version **2**. They are completely separate formats with separate version axes (`src/seq_data.h:30-44`).

### 2.3 RSAV catalog header (`region = 0x8000`, 0x100 bytes)

Sources: `tools/risa_save.py:64-68,21`; `src/seq_data_save.c:83-91`; `catalog.js:201-205`.

| Abs offset | Rel | Size | Field |
|---|---|---|---|
| `0x8000` | `+0x00` | 4 | magic `"RSAV"` = `52 53 41 56` |
| `0x8004` | `+0x04` | 1 | version (`2` current, `1` legacy) |
| `0x8005` | `+0x05` | 1 | `count` — number of song records (u8, max 255) |
| `0x8006` | `+0x06` | 2 | `used` u16 LE — record bytes **after** the header |
| `0x8008` | `+0x08` | `0xF8` | reserved (zero) |
| `0x8100` | `+0x100` | — | first record |

Validity: magic matches, version byte matches expected, and `used ≤ SAVE_REGION_SIZE − SAVE_HEADER_SIZE = 0x7F00`. Directory integrity check: walking record lengths from `0x8100` must land exactly at `0x8100 + used` (parse throws otherwise). **Records are variable-length and tightly packed** — `record[i+1].offset = record[i].offset + record[i].length`. There is no per-slot index table and no block-allocation bitmap; the header `count` + `used` + per-record `length` **are** the directory (contrast LSDj's fixed 32-slot table + 191-entry alloc bitmap).

`SAVE_CHUNK_SIZE (0x100)` is a **free-space reporting rounding only** (`entries[].chunks = ceil(len/0x100)`; firmware reports free as `(free+0xFF)>>8`), **not** an allocation unit — records are byte-granular (`catalog.js:225,7`; `src/seq_data_save.c:985-991`).

### 2.4 Song record header (16 bytes; identical in the catalog and in a `.risong`)

Sources: `src/seq_data_save.c:16-20,635-643`; `tools/risa_save.py:70-74`; `record_codec.js:50-77`.

| Rel | Size | Field |
|---|---|---|
| `+0x00` | 2 | `length` u16 LE (total record length **including** this 16-byte header; must equal the emitted buffer length) |
| `+0x02` | 8 | `name` (`SONG_NAME_LEN=8`, space `0x20` padded, NUL-terminated; empty → `"UNTITLED"`) |
| `+0x0A` | 1 | `rec_ver` (2–7; current `SAVE_REC_PAYLOAD_VER = 7`) |
| `+0x0B` | 5 | reserved (zero) |
| `+0x10` | `length−0x10` | payload (see §2.5) |

Song name length is **8** — the same as LSDj — so the UI name plumbing is reusable.

### 2.5 Record payload (bitmask-compressed) — field layouts

The payload is **present-only**: every collection is prefixed by a presence bitset (1 bit/slot, **little-bit-endian within each byte**: `byte[idx>>3] & (1 << (idx & 7))`), and within each present chain/phrase/table a second per-row 16-bit mask means only non-default rows emit. **There is no RLE.** Fixed counts: `SEQ_TRACK_COUNT=5`, `SONG_ROWS=128`, `CHAIN_COUNT=128`, `PHRASE_COUNT=255`, `INST_COUNT=64`, `INST_SIZE=12`, `TABLE_COUNT=32`, `GROOVE_COUNT=16`. Sentinels: `CHAIN/PHRASE/NOTE/INST/TABLE_EMPTY = 0xFF`; `NOTE_OFF = 0xFE` (a real note, distinct from empty); `FX_NONE=0`, `FX_TABLE=1`.

Payload order (rec_ver 7; sources `record_codec.js:76-197`, `risa_save.py:316-526`, `src/seq_data_save.c:629-756`):

- **(A) Project settings — 8 bytes**, copied verbatim from WRAM `PROJECT_SETTINGS_OFFSET 0x1E84`: `[0]tempo_hi`, `[1]tempo_lo` (u16 BPM 0–999), `[2]transpose`, `[3]theme_idx` (0–15), `[4]key_repeat` (hi=delay, lo=speed), `[5]note_preview` (0/1), `[6]dirty`, `[7]font_idx` (0–3). (`src/seq_data.h:61-77`.)
- **(B) Song matrix** — per track (5): a 16-byte row bitmask (128 rows) then one chain-id byte per set bit. Absent row = `CHAIN_EMPTY 0xFF`. No section-level bitset. (In RAM: `SONG_OFFSET 0x1000` bank 1, 5×128 flat.)
- **(C) Chains** — 16-byte presence bitset (128); per present chain a 2-byte row mask (16 rows) + 2 bytes/row `[phrase_idx, transpose]`. Row emitted iff `phrase_idx != 0xFF`. (RAM: 32 B/chain.)
- **(D) Phrases** — presence bitset **32 B for 255 (rec_ver ≥6)** / 16 B for 128 (older); per present phrase a 2-byte row mask + 4 bytes/row `[note, instrument, fxType, fxVal]`. **One** fx column. Emitted unless all-default (`0xFF,0xFF,0,0`). (RAM: 64 B/phrase, split across banks 0 and 3.)
- **(E) Aux phrases (Pulse2/echo note lane, rec_ver ≥3 only)** — **three shapes**: rec_ver ≥6 → its own independent 32-byte bitset (255) + per phrase 2-byte mask + 1 note/row; rec_ver 5 → single lane iterated over the phrase bitset (128); rec_ver 3–4 → two legacy lanes merged into the shared lane (legacy fills only where shared == `0xFF`). (RAM: `AUX_SHARED_OFFSET 0x1000` bank 2.)
- **(F) Instruments** — 8-byte presence bitset (64) then raw **12-byte** records (see §2.6).
- **(G) Tables** — presence bitset **4 B for 32 (rec_ver ≥2)** / 2 B for 16 (older); per present table a 2-byte row mask + 6 bytes/row `[vol, transpose, fx1Type, fx1Val, fx2Type, fx2Val]`. **Two** fx lanes. Emitted unless `vol==0xFF && transpose==0 && fx1Type==0 && fx2Type==0`. `TABLE_STOP` changed `0x10 → 0x20` across versions. (RAM: 8 B/row; **6 B serialized** — don't conflate.)
- **(H) Grooves** — 2-byte presence bitset (16) then per present groove `[len (1–16)][len step bytes]`. (RAM: 17 B/groove, `GROOVE_OFFSET 0x1580` bank 1.)

Decoder asserts final position == `length`.

### 2.6 Instrument (12 bytes), type at byte 6

Sources: `src/seq_data.c:236-243`, `src/seq_data.h:149-184`, `record_codec.js:406-445`.

`[0]duty`; `[1]volume/ENV_A`; `[2]env_rate/ENV_D`; `[3]table_speed`; `[4]sweep_config`; `[5]extra` (bit0 `PIT_LOG`, bit1 `GTRANS_OFF`); **`[6]type`** (`0`=PULSE, `1`=TRIANGLE, `2`=NOISE, `3`=DMC, `4`=WAVE; `0xFF`=empty); `[7]table_idx/last` (**or pre-v4 DMC kit index**); `[8]fine` (signed i8, N/32 semitone); `[9]aux_vol` (lo nibble `0x0F` attenuation, `0x70` store-mode); **`[10]`** = ENV_R (pulse/noise) **or DMC kit index (rec_ver ≥4)** — overloaded, decode strictly by `[6]`; `[11]aux_pw` / wave selector.

### 2.7 Version deltas (rec_ver 2→7)

`v2`: table bitset 4 B (else 2 B). `v3`: aux-phrase section introduced. `v4`: DMC kit index byte `[7]→[10]` (`[7]:=0xFF`). `v5`: aux lanes 2→1 (shared). `v6`: phrase count 128→255 (bitset 16→32 B) **and** aux gets its own 32-byte bitset. `v7`: pulse/noise volume envelope re-encoded across `[1]/[2]/[10]` as attack/decay/release (branchy, **one-way/lossy** — treat like an LSDj sav upgrade). Legacy v1 catalog lived at `0x6000` (banks 3–7, `0xA000` = 40 KB), version byte 1; v1→v2 migration relocates forward to `0x8000`, restamps version 2, clears the tail, and **rejects if `used > 0x7F00`** (too full for the 32 KB v2 region).

> **Conflict/uncertainty:** the C serializer `seq_data_save.c` was not read in full — `record_codec.js` and `risa_save.py` agree, but the C is ground truth for the ROM-written catalog. For a **byte-preserving container codec** (RetroPlug's actual need for list/load/save/reorder), the payload internals are **irrelevant** — records are opaque blobs, exactly as LSDj treats compressed project blocks. The full payload codec is needed only for `.risong` export dependency pruning and validation.

---

## 3. Risa ROM format

### 3.1 iNES / MMC5 bank map

A risa `.nes` is a **16-byte iNES header + 512 KB PRG (32×16 KB = 64×8 KB banks) + 32 KB CHR (4×8 KB font banks)**, MMC5 (mapper 5). Header bytes: `[0..3]="NES\x1a"`, `[4]=0x20` (PRG /16 KB → 512 KB), `[5]=0x04` (CHR /8 KB → 32 KB). The patcher validates **magic + total size only** (`length == 16 + PRG + CHR`); it does **not** check the mapper (`rom.js:62-71`).

Bank roles (64×8 KB; `nes.cfg:13-74`): UI/sequencer code banks 0–17, empty pad 18–24, SAW-WAVE DMC 25–27, ORG-WAVE DMC 28–30, **32 DPCM kit banks 31–62** (`PRG_KIT_0..31`), fixed/resident code+vectors bank 63. Kit banks map at `$C000–$DFFF` via MMC5 `$5116` (only `$C000–$FFFF` can feed the DMC sample fetcher).

### 3.2 Header-derived layout (the patcher computes offsets, never hard-codes)

`RomImage` (`rom.js:245-258`) derives all asset offsets from the header so they float with ROM size:

```
prg8kBanks   = header[4] * 2
lastPrgBank  = prg8kBanks - 1
kitFirstBank = lastPrgBank - 32
kitOffset    = 16 + kitFirstBank * 0x2000     // 512 KB → 0x3E010
fixedOffset  = 16 + lastPrgBank  * 0x2000     // 512 KB → 0x7E010  (theme table lives here)
chrOffset    = 16 + header[4]   * 0x4000      // 512 KB → 0x80010  (font banks)
```

A RetroPlug port **must** compute these, not hard-code `0x3E010`/`0x80010`. `computeLayout` throws if `kitFirstBank < 16`.

### 3.3 Kit bank (8 KB) — locate & edit

Sources: `src/kit.h:10-37`; `kit_bank_parser.js`. One kit bank = 7872 B sample data + 320 B metadata:

| Rel | Size | Field |
|---|---|---|
| `0x0000–0x1EBF` | 7872 | DPCM sample data (64-byte-aligned slots) |
| `0x1EC0` | 16 | kit name (ASCII, NUL-pad, ≤6 meaningful chars) |
| `0x1ED0` | 48 | 16 × 3-char sample names (uppercase) |
| `0x1F00` | 64 | 16 × 4-byte index entries (see §4) |
| `0x1F40` | 1 | `KIT_MAGIC = 0xA5` (populated marker) |

Slot present iff its index-entry addr byte `!= 0xFF`; bank populated iff `byte[0x1F40] == 0xA5`. `isKitPopulated(idx)` reads `kitOffset + idx*0x2000 + 0x1F40`.

**The kit-metadata MIRROR (no LSDj analog).** Because firmware sees one kit bank at a time through `$5116`, the build bakes a flat mirror of every kit's name/sample-names/present-mask into a resident UI bank so the list UI needn't page. It is located by the 6-byte magic `KIT_META_MAGIC = A5 5A 4B 54 4D 45` ("..KTME") via hint offsets (`PRG_UI_8`, older ROMs `PRG_UI_4`) then bounded scan; layout: 6-byte magic + `kit_names[32][16]` + `kit_sample_names[32][48]` + `kit_slot_present[32][16]` (2566 B). **Any kit edit must rewrite both the bank and the mirror** (`setKitBank` + `updateKitMeta`), or the on-device kit list goes stale (`rom.js:31-46,260-285`).

### 3.4 Theme region

Themes live in a separate table located by `THEME_META_MAGIC = A5 5A 54 48 4D 45` ("..THME") scanned in the **fixed bank** (`fixedOffset`, span `0x2000`). Layout after the 6-byte magic: **16 records × 7 bytes** then **16 names × 4 bytes** (182 B total). Each record = 7 NES-palette-index bytes for roles `[bg, normal, shaded, alternate, status, cursor, selection]`, each `≤ 0x3F`. See §4.

> The two META magics differ only in bytes 2–3 (`KTME` vs `THME`) — easy to transpose. Old ROMs may lack either table (`hasMetaTables`/`hasThemeTables` false); degrade gracefully.

### 3.5 CHR font banks

CHR sits at `chrOffset = 16 + PRG size`, size `header[5]*0x2000`; slot count `= chrSize/0x2000` (risa = 4). `getChrFontSlot(idx)`/`setChrFontSlot(idx)` slice/write an 8 KB bank at `chrOffset + slot*0x2000`. **No marker** — position is deterministic. One slot = 512 tiles × 16 B, **NES planar 2bpp** (see §4).

### 3.6 App-version detection & write-back

App version = ASCII scan for `"RISA V"` across `HEADER_SIZE..chrOffset`, parsing the trailing `\d+.\d+.\d+` (`rom.js:86-103`). It gates save-catalog layout (`romVersionAtLeast(2,0,0)` selects v2 vs legacy). All edits are in a cloned buffer (`buffer.slice(0)`), mutated in place at computed offsets, re-blobbed for download; **ROM size never changes → no header/checksum rewrite** (`rom.js:47-60`). `rom_upgrade.js` carries all kits+themes+fonts onto a new firmware ROM, rebuilding kit metadata from bank contents.

---

## 4. Risa assets

### 4.1 DPCM kit (`.rik`) + DMC encoding

**Two representations.** (1) `.rik` = a **store-only (`level:0`) fflate ZIP** of `kit.json` (v2 schema) + one WAV or raw `.dmc` file per slot — the editable interchange format. (2) The **packed 8 KB kit bank** (§3.3) that lands in the ROM.

`kit.json` v2 (`wav2dmc.py:25-40`, `model.js:248-320`): `{version:2, name, samples:[{slot:0-15, file, source_type:"wav"|"dmc", name(≤3), rate:0-15, loop, ...WAV edit fields}]}`. WAV edit fields: `trim_start/trim_end`, `gain_db`, `normalize`, `pitch_semitones`, `highpass_hz`, `lowpass_hz`, `eq_on/eq_freq/eq_q/eq_gain_db`, `fade_out_ms`. `source_type:"dmc"` packs raw bytes and ignores edit fields.

**Kit index entry (4 bytes at `0x1F00 + slot*4`)** — literally the NES DMC hardware register values (`src/kit.h:21-31`): `[0]addr` = `$4012` (sample offset = `addr*64`, `SAMPLE_ALIGN=64`; `0xFF`=empty); `[1]len` = `$4013` (byte length = `len*16+1`, `LENGTH_STEP=16`); `[2]rate` (0–15 → PAL rate table); `[3]flags` (bit0 = loop).

**DMC hardware constraints:** start address 64-byte aligned; length `= len*16+1` bytes, valid `1,17,33,…`, **max 4081** (`DMC_MAX_BYTES`); samples packed greedily in slot order 0–15 with 64-byte alignment (sample region overflow past 7872 B is a hard error).

**The `wav2dmc.py` encode pipeline (per WAV slot; `wav2dmc.py:256-479`, the declared source of truth):** load mono int16 → trim/gain/fade → optional RBJ biquad HP/peak-EQ/LP (Q=0.7071) at input rate, order HP→peak→LP → **32-tap Hann-windowed sinc resample** to the target PAL DPCM rate (14-bit fixed-point coeffs) → one-pole DC blocker (`R=255/256`) → int16→7-bit unsigned centered on 64 → **1-bit delta encode** (counter starts 64, **±2 per bit, clamped 0..127**, LSB-first packing) → pad to a legal `16k+1` length. Raw `.dmc` slots skip all of that.

**PAL DPCM rate table (16 entries; `wav2dmc.py:84-89`):** `4177.40, 4696.63, 5261.41, 5579.22, 6023.94, 7044.94, 7917.18, 8397.01, 9446.63, 11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09` Hz (default index 12). **NTSC differs ~1%** and the patcher/wav2dmc ship **PAL only**; `gen_wave_assets.py` has an NTSC table.

**Native KitCompiler reuse: partial.** RetroPlug's `KitCompiler`/`SampleUtil` emit LSDj's Game Boy 4-bit inverted nibble PCM at a fixed 11468 Hz — a **fundamentally different codec** (8 samples/byte 1-bit delta vs 2 samples/byte 4-bit; 8 KB bank vs 16 KB; metadata at top vs bottom; per-slot PAL rate + loop vs single fixed rate). The final pack stage **cannot** be reused. Everything around it can: `SampleCache` (miniaudio WAV/MP3/FLAC decode + content-hash dedupe), `Effects` (gain/filter/dither), r8brain resample, the enkiTS per-sample fan-out, and the `compileKit` RPC harness. **A new NES-DMC encoder is required** (`convertF32ToDpcm` alongside `convertScaledF32ToNibbles`).

> **Conflict/uncertainty (hashing only):** `kit_fingerprint.js` uses `slotByteLength = (lenReg+1)*16+1`, 16 B larger than `kit_bank_parser.js`'s `lenReg*16+1` (the hardware-correct length). Use `len*16+1` for actual sample extraction; the discrepancy only affects the semantic-fingerprint hashing range.
>
> **Parity caveat:** `encoder.js`/`pack.js` are byte-parity-verified against `wav2dmc.py` on the **unfiltered** path only (`fixtures/parity.html`); filtered slots may differ by a few bytes and that is accepted upstream. Do not gate tests on filtered-slot byte-identity. The ±2 clamp is a deliberate non-hardware-accurate quirk shared by encoder and decoder — **preserve it**.

### 4.2 Theme (`.rit`) / NES palette

A theme is **not RGB** — it is 7 named roles, each a single **6-bit index (0x00–0x3F)** into the fixed 64-entry NES master palette (`palette.js NES_PALETTE`, hardcoded `#rrggbb` per index, compiled into the tool, **not read from ROM**), plus a 4-char ASCII name. `.rit` is **bare JSON, not zipped** (`{version:1, theme:{name, bg, normal, shaded, alternate, status, cursor, selection}}`, each role a `"0xNN"` string). Roles validated to `≤0x3F`, name to 4 ASCII (`&0x7F`). Role order `[bg,normal,shaded,alternate,status,cursor,selection]` is load-bearing. `.rit` is **readable structured JSON → stores inline in the `.rplg` with no base64**, satisfying the repo rule directly (simpler than LSDj palettes, which needed the `colorSets` workaround).

### 4.3 Font (CHR)

NES CHR, standard 2bpp 8×8 tiles, 16 B/tile, but **PLANAR** (`bytes 0–7` = bitplane 0 rows 0–7, `bytes 8–15` = bitplane 1) — **unlike GB's interleaved per-row plane pairs**. `pixel(x,y) = ((p0>>(7-x))&1) | (((p1>>(7-x))&1)<<1)`. One font slot = one 8 KB CHR bank = 512 tiles. Tiles `0x00–0x7F` = ASCII glyphs (tile id = `charCodeAt`); `0x80–0xFF` = inverted mirrors + special UI tiles; `0x100+` = sprite/cursor tiles. The **`.chr` interop file is the raw 8192-byte bank, no header/version** — import = `new Uint8Array(buf)`, export = `toBytes()`. A user-imported `.chr` is treated as an already-complete 8 KB bank (the build-time mirror/graph/header synthesis in `gen_fonts.py` is not re-run).

---

## 5. Risa runtime RAM/WRAM state

### 5.1 Where the state lives

**All UI-overlay-relevant state is in the NES internal 2 KB RAM (`$0000–$07FF`), not in banked cartridge WRAM.** BSS is linked at `$0300` size `$0500`; zero-page (`ZP`) at `$28` size `$D8` (`nes.cfg:80,168,170`). cc65 prefixes C symbols with `_` (`_seq_mode`, `_bss_song_row`, …).

Playback state:
- **`seq_mode`** (ZP): master mode `0=STOPPED,1=SONG,2=CHAIN,3=PHRASE,4=PREVIEW`. `==0` → stopped.
- **`seq_active`** (ZP): per-track active bitmask (bit t = track t producing sound). `==0` → stopped.
- Per-track playheads (5-byte BSS arrays, index = track 0–4 = Pulse1/Pulse2/Triangle/Noise/DMC): `bss_song_row`/`bss_song_last_row` (macro `seq_get_song_row(t) = last_row==0xFF ? row : last_row`), `bss_chain_idx`/`bss_chain_row`, `bss_phrase_idx`/`bss_phrase_last_idx`/`bss_phrase_last_row`, `zp_phrase_row`, plus table (`bss_table_idx`, `bss_table_last_row`, `zp_table_row`) and groove (`bss_groove_last_idx/pos`, `zp_groove_idx/pos`). Convenience macros `seq_get_*` in `seq.h` encode the last-vs-live selection.
- **`seq_current_bpm`** (u16 BSS): live BPM 40–295, or **296 = TEMPO_MODE_4X** sentinel. Updated at the `seq_set_tempo_byte` chokepoint so it reflects `Txx` FX.
- **`apu_current_note[5]`**, **`bss_last_inst[5]`** (playing instrument per track; distinct from the *edited* one).
- **`kit_active_idx`** (from `kit_bank.s`): active DPCM kit bank index.

UI/editor state (all internal RAM, `ui_common.c`): `ui_current_screen` (`0=PHRASE,1=CHAIN,2=SONG,3=INSTRUMENT,4=GROOVE,5=TABLE,6=SETTINGS`), `ui_cursor_row/col`, `ui_track` (focused channel), `sg_scroll_top`, drill-in indices `ui_nav_{phrase,chain,inst,groove,table}_idx` (`ui_nav_inst_idx` = instrument being edited), selection block `ui_sel_*`.

Banked cart WRAM (`$6000`, MMC5 `$5113`) holds the **data bytes** a playhead points at (song/chain/phrase/instrument/table/groove content, bank layout per §2.2), needed only for a **live pattern-content** overlay — a position/tempo/screen overlay never reads it.

### 5.2 Address resolution

The build emits a **VICE `.lbl`** (`build/risa-pal.lbl`, lines `al <hex-addr> <name>`) converted to a **Mesen `.mlb`** (`risa.mlb`), plus a cc65 `.dbg`. Internal-RAM addresses are absolute CPU addresses, so `$0000–$07FF` indexes the `readRam` buffer directly. **Classify each symbol by CPU-address band:** `$0000–$07FF` → internal RAM (`readRam` index = addr), `$6000–$7FFF` → PRG-RAM (`readSram` index = addr−`0x6000`), `≥$8000` → ROM (ignore for state).

> **Two hard problems, both structural:** (1) cc65 BSS/ZP addresses **reshuffle on every build** — the offset snapshot must match the exact loaded ROM (bundle a per-version snapshot keyed on `APP_VERSION_TEXT`, or ship the `.lbl` alongside). (2) End users have only `risa.nes` (no `.lbl`). (3) `build/risa-pal.lbl` was **not present in the examined checkout** — the snapshot generator must run against a produced label file (needs an in-tree or supplied build).

### 5.3 The NES `readRam` seam — already present

**Confirmed in-tree:** `MesenNesSystem::getMemory(rp::MemoryType::Ram)` returns `NesInternalRam` (2 KB) (`MesenNesSystem.cpp:434-440`); `SnapshotRegistry` publishes it **every audio block** (`SnapshotRegistry.cpp:107-137`, gated only by `size ≤ kMaxRamBytes=64KB`); it is exposed as `readRam` on the Engine facet (`BackendRpcRegistration.hpp:47`, `EngineRpcService.cpp:156`) → `backend.readRam(id)` (`realBackend.ts:140`). So **the runtime position/tempo/screen overlay needs zero native work.**

**The gap:** `readRam` carries only `MemoryType::Ram` = NES internal 2 KB. Cartridge WRAM `$6000–$7FFF` maps to `MemoryType::Sram` (`NesSaveRam`, `MesenNesSystem.cpp:441`), published only **coarsely (~0.5 s, `kStateIntervalSec`)** via `readSram`, or via the control-thread-only `readMemory(Sram)`. There is **no per-block seam for `$6000` WRAM**. This only matters for a **live pattern-content** overlay (deferred); song management and position overlays don't need it.

---

## 6. RetroPlug LSDj architecture recap (the shape a risa port mirrors)

The seams a risa port copies, layer by layer:

**Layer A — codec + model (pure TS, `packages/retroplug/src/lsdj/`).** `model.ts` = a zod SSOT (enum-name arrays double as byte↔name tables; every field defaulted so `parse({})` yields a full image). `codec/sav.ts` = the 128 KiB image (working song `@0`, header `@0x8000` with `'jk'` magic at `0x813E`, 32-slot name/version tables, 191-entry alloc bitmap, RLE archive) + **byte-level slot ops** (`listProjects`, `decompressSlot`, `injectSong`, `freeSong`, `freeSongSlot`, `loadSongToWorking`, `savSongName/Version`). `codec/song.ts` decodes/encodes the `0x8000` song body to/from the model, branching per-version bit math over version-stable region offsets (`codec/regions.ts`); **only a byte-lossless round-trip when given a template**. `codec/bits.ts` (`BitView`/`BitWriter`) isolates all raw bit math (JS int32-signedness hazard). `codec/rle.ts` = LSDj block RLE (two decompressors: scattered in-sav vs sequential stream). `codec/lsdsng.ts`/`lsdprj.ts` = interchange files. `index.ts` = the `savFrom`/`savFromJson`/`savToJson` authoring barrel. **CRITICAL invariant: all song-management edits go byte-level, never through the model** (re-encode without a template loses ~300 bytes/song).

**Layer B — ROM assets + override role.** `src/lsdj/rom/` = a pure-TS clone-on-load / patch-in-place / `bytes()` write-back view (`rom.ts` `LsdjRom`, `find.ts` marker scanner, `kit.ts`/`palette.ts`/`font.ts`/`names.ts`/`buildKit.ts`). `src/lsdjAssetsRole.ts` = the `lsdj-assets` **feature role** (`category:"feature"`, `scope:"system"`, `onConstruct` only, zod `{overrides:[]}`), each override `{type,slot,name?,path?,colorSets?,erase?,lsdprjKit?}` — **binary assets link by path, palettes inline as `colorSets`** (no base64). `onConstruct(spec,caps,config)` reads the base ROM (`caps.readFile(spec.romPath)`), folds overrides onto an `LsdjRom` in memory, returns `{...spec, romBytes: patched}`. `romBytes` is an additive `ConstructSpec` channel honored by the backend instead of slurping `romPath` (romPath still travels for watcher + `.sav` resolution).

**Layer C — native kit compiler.** `packages/native/src/lsdj/` — `KitCompiler` fans per-sample resample+encode across enkiTS; `SampleCache` (miniaudio decode + hash dedupe), `KitUtil::compileSample` (`Effects` → r8brain resample to 11468 Hz → nibble-pack), `KitUtil::buildKit`. Reached over the Engine facet as `compileKit(KitCompileSpec)→Bytestring` (**lazily constructed on first use** — `EngineRpcService.cpp:309-332`); TS `audioDriver.compileKit`. Pure-TS `buildKitBank`/`sampleBytesFromBank` splice single samples without native re-encode.

**Layer D — runtime reader + overlay.** `src/lsdj/runtime/` — `identify.ts` (version from title `@0x134`), `offsets.ts`+`layout.ts` (per-version WRAM offset layout), `reader.ts` (`decodeLsdjState(wram, layout)`, pure), the drift tables + `detect.ts`. UI: `ui/screens/grid/{LsdjOverlay.tsx, useLsdjRuntime.ts, lsdjDebug.ts}` (backtick-toggled, per-`frame`-event `readRam` pull, signature-deduped).

**Cross-cutting seams (generic, reused as-is).** `SystemsStore.readSram/loadSram/newSram/reset` funnel all battery mutation through `rebuildInPlace` (a **cold-boot** seeded from `sramBytes`). The Songs edit cycle: `readSram(id) → byte-level op → backend.writeFileAtomic(savPath) → loadSram(id,target)`. `applyConstructHooks` folds every attached role's `onConstruct` on fresh add **and** on `rebuildInPlace` (reload/loadState/loadSram) — so overrides re-apply on reload (`systemsStore.ts:618`). `ConstructCaps = {savFromJson, fileExists, readFile, pngDecode}`. ROM classification: `detectPlatform`/`defaultCoreFor` (`platform.ts`); feature roles attach via `romProviders.ts` matching ROM identity. `sramAutoSave.ts` computes a **semantic dirty signature** (`lsdjSramSignature` normalizes LSDj's per-frame clock churn via a canonical `encodeSong(decodeSong(working))` re-encode; `sramSignature = lsdjSramSignature(b) ?? hashBytes(b)`). Persistence: the `.sav` is **outside** the JSON version-stamp/migration model (`migrate.ts`) — it carries its own internal format-version byte; role config is additive (no migration).

---

## 7. Gap analysis — exists vs must-build

### Already usable for NES / Mesen (no new work)

- **NES is a first-class platform + core:** `platform.ts` (`nes→mesen`), `detectPlatform` (iNES magic), `MesenBackend`/`MesenNesSystem`, `MesenNesDebugSession`.
- **Per-block internal-RAM read seam:** `readRam` works for NES today (`getMemory(Ram)→NesInternalRam`, per-block publish, Engine facet). Proven equivalent to the GB path by `test-native/lsdj-wram-seam.test.ts`.
- **Battery save + cold-boot machinery:** `readSram`/`loadSram`/`rebuildInPlace`/`writeFileAtomic` are byte-opaque and console-agnostic — a risa system routes through them unchanged (**pending the WRAM-size verification, §2.2 unknown #2**).
- **Generic role + construct-hook plumbing:** `RoleRegistry`, `onConstruct`, `applyConstructHooks`, the `ConstructSpec.romBytes` channel (already defined and marshaled: `BackendTypes.hpp:71`, `EngineRpcService.cpp:62`), `romProviders`.
- **Native sample scaffold:** `SampleCache`, `Effects`, r8brain, enkiTS, the lazy `compileKit` RPC pattern.
- **NES cc65 symbol parsing exists natively:** `Cc65DbgParser` + `MesenNesDebugSession::loadLabels` (`.dbg` → name→CPU addr).
- **Host zip/unzip + pngDecode** exist on the Host facet (`BackendRpcRegistration.hpp:32-35`) for `.risong`/`.rik`/`.chr`/`.png`.
- **NES DMC audio plays out of the box** — Mesen renders the DMC channel; only the *compile* (wav→DMC) is new.

### Must build new

1. **`MesenBackend` `romBytes` honor (REQUIRED native change).** `MesenBackend::build` slurps `spec.romPath` unconditionally (`MesenBackend.cpp:45`); `SameBoyBackend` has the branch (`SameBoyBackend.cpp:50-54`). Without it, risa ROM asset overrides are silently inert. The plumbing already exists downstream: `MesenNesConfig.romBytes` is a field, and `MesenNesSystem`'s constructor already takes+stores `romBytes` (`rom_`). The fix is local (~15 lines): add the `embeddedRom / spec.romBytes / slurp` branch and pass the chosen bytes through, mirroring SameBoy. **Verify** whether `MesenNesSystem::onActivate` boots from `rom_` (bytes) or `cfg.romPath` — if bytes (as the ctor implies), no deeper change is needed.
2. **NES-DMC encoder (the one new algorithm).** `wav2dmc.py` port: resample to the chosen PAL/NTSC DMC rate → 7-bit map → 1-bit ±2-clamp delta → `16k+1` pad. TS for byte-parity (safer) or native reusing the scaffold (faster) — see §10.
3. **risa save codec (TS):** RSAV catalog reader/writer (variable-length compacting heap; `catalog.js` port), container normalization (32 KB/64 KB/65 KB tail/256 KB Pocket), record header parse, `.risong`/`.rik`/`.rit` codecs, optional payload codec (`record_codec.js`) for export/validation.
4. **risa ROM view/patch (TS):** header-derived layout, kit bank DMC codec + the **dual-write kit-metadata mirror**, NES-palette theme table, planar-CHR fonts, crc32 kit fingerprints.
5. **risa runtime reader (TS):** `.lbl`/`.mlb` parser → authored per-version symbol snapshot → pure `decodeRisaState(ram)`. **No drift-detection machinery** (risa ships symbols).
6. **risa ROM detection:** an NES ROM has no title field; risa's `"RISA V"` is a PRG scan. Needs a longer prefix read or fixed-offset magic (§10).
7. **App-level orchestration:** `risaAssetsRole.ts`, `risaSongOps.ts`, `risaSongImport.ts`, `risaSramSignature`, a risa `romProvider`, UI overlay + menus.
8. **(Deferred) per-block banked-WRAM seam** for a live pattern-content overlay; **legacy save migration**; WAVE/zsaw import.

> **Correction to one finding:** the runtime-area finding at one point suggests the `$6000` content overlay is the main NES gap. In practice the **required, blocking** native change is the `MesenBackend` `romBytes` branch (for asset overrides, milestone M3); the banked-WRAM seam is optional and deferred. Both are real, but only the former is on the critical path.

---

## 8. Proposed module map

Mirror the LSDj layout one-for-one. **New files** unless marked *(extend)* / *(change)* / *(share)*.

### TS codec + model — `packages/retroplug/src/risa/`

| New file | LSDj analog | Purpose |
|---|---|---|
| `risa/model.ts` | `lsdj/model.ts` | zod SSOT for the risa song/instrument/table/groove model (enum-name↔byte tables; defaulted). |
| `risa/index.ts` | `lsdj/index.ts` | authoring barrel (`risaFrom`/`risaFromJson`/`risaToJson` + decode/encode re-exports). |
| `risa/codec/record.ts` | `codec/song.ts` | port `record_codec.js` — parse/make/normalize the versioned (v2–v7) song record; **optional `template` param** for byte-lossless re-encode. |
| `risa/codec/sav.ts` | `codec/sav.ts` | RSAV catalog (`catalog.js` port): container normalization, header parse, `listSongs`, `writeRecord`/`deleteRecord`/`moveRecord` (compacting heap), `loadSongToWorking`. |
| `risa/codec/risong.ts` | `codec/lsdsng.ts`+`lsdprj.ts` | `.risong` zip reader/writer + manifest schema (`song_package.js`) + reachability walk (`reachability.js`). |
| `risa/codec/rik.ts` | *(new)* | `.rik` zip + `kit.json` v2 codec. |
| `risa/codec/rit.ts` | (part of `palette.ts`) | trivial `.rit` JSON theme codec. |
| — | `codec/bits.ts` | **share:** import `lsdj/codec/bits.ts` directly, or hoist to `src/codec/bits.ts`. Handles the little-bit-endian bitset math. |
| — | `codec/rle.ts` | **not needed** — risa uses presence bitsets, no RLE. |

### TS ROM view — `packages/retroplug/src/risa/rom/`

| New file | LSDj analog | Purpose |
|---|---|---|
| `risa/rom/rom.ts` | `rom/rom.ts` | `RisaRom.fromBytes()` clone / patch-in-place / `bytes()`; header-derived layout; `isRisa`. |
| `risa/rom/find.ts` | `rom/find.ts` | 6-byte-magic scanner (hint-offset then bounded scan). |
| `risa/rom/constants.ts` / `types.ts` | same | offsets, magics, counts, model types. |
| `risa/rom/kit.ts` | `rom/kit.ts` | kit bank parse/pack (DMC index table + `0xA5` magic) **and the dual-write kit-metadata mirror**. |
| `risa/rom/buildDmc.ts` | `rom/buildKit.ts` | pure-TS single-sample splice (`buildDmcBank`/`sampleBytesFromBank`). |
| `risa/rom/theme.ts` | `rom/palette.ts` | NES theme table (16×7 records + 16×4 names) + fixed NES master-palette constant. |
| `risa/rom/font.ts` | `rom/font.ts` | planar-CHR tile codec; whole-8 KB-bank replace. |
| `risa/rom/index.ts` | `rom/index.ts` | barrel. |

### TS runtime — `packages/retroplug/src/risa/runtime/`

| New file | LSDj analog | Purpose |
|---|---|---|
| `runtime/reader.ts` | `runtime/reader.ts` | pure `decodeRisaState(ram, layout)` over the 2 KB internal-RAM snapshot. |
| `runtime/types.ts` | `runtime/types.ts` | `RisaState` + `RisaLayout`. |
| `runtime/symbols.ts` | `runtime/offsets.ts` | parse `.lbl`/`.mlb` → name→CPU-addr; classify by band. |
| `runtime/layout.ts` | `runtime/layout.ts` | resolve required symbol names → concrete `readRam` offsets. |
| `runtime/risaSymbols.<ver>.generated.ts` | `driftLayouts.generated.ts` | **authored** per-version snapshot keyed on `APP_VERSION_TEXT` (not detected). |
| `runtime/identify.ts` | `runtime/identify.ts` | risa ROM detection (iNES + `"RISA V"` scan). |
| — | `runtime/detect.ts`, `gen-lsdj-offsets.mjs`, drift tables | **not needed** — risa ships symbols. |

### TS app-level orchestration — `packages/retroplug/src/`

| File | LSDj analog | Purpose |
|---|---|---|
| `risaSav.ts` (new) | `lsdjSav.ts` | thin re-export barrel of `src/risa`. |
| `risaSongOps.ts` (new) | `lsdjSongOps.ts` | byte-level catalog ops (`deleteSongInSav`/`addSongToSav`/`replaceSongInSav`/`importAllSongs`). |
| `risaSongImport.ts` (new) | `lsdjSongImport.ts`+`lsdjLsdprjImport.ts` | `readSram→op→writeFileAtomic→loadSram` batch cycle; `.risong` import (kit dedupe by semantic fingerprint, remap DMC kit refs). |
| `risaAssetsRole.ts` (new) | `lsdjAssetsRole.ts` | the `risa-assets` feature role (`onConstruct` → `romBytes`). |
| `sramAutoSave.ts` *(extend)* | same | add `risaSramSignature` (detect `RSAV`@`0x8000`, normalize live-bank volatile bytes); add to `sramSignature` dispatch. |
| `romProviders.ts` *(extend)* | same | add a risa provider attaching `risa-assets` (+ optional risa-runtime marker). |
| `systemRoles.ts` *(extend, maybe)* | same | widen `ConstructCaps` with `unzip` **only if** `.rik`/`.risong` are read at construct. |

### Native — `packages/native/src/`

| File | LSDj analog | Purpose |
|---|---|---|
| `system/mesen/MesenBackend.cpp` *(change)* | `SameBoyBackend.cpp:50-54` | **honor `spec.romBytes`** over `romPath`. Required. |
| `risa/DmcEncoder.{hpp,cpp}` (new) | `lsdj/SampleUtil.hpp` (`convertScaledF32ToNibbles`) | the `wav2dmc.py` NES-DPCM encoder (`convertF32ToDpcm`). |
| `risa/DmcKitCompiler.{hpp,cpp}` (new) **or** generalize `lsdj/KitCompiler` to take an encoder strategy (preferred) | `lsdj/KitCompiler.{hpp,cpp}` | reuse `SampleCache`/`Effects`/r8brain/enkiTS; swap the pack stage. |
| `host/rpc/EngineRpcService.{hpp,cpp}` *(extend)* | `compileKit` | add lazy `compileDmc(RisaKitCompileSpec)→Bytestring`. |
| `host/rpc/BackendRpcRegistration.hpp` *(extend)* | line 69 | register `compileDmc` on the Engine facet. |
| `host/rpc/BackendTypes.hpp` *(extend)* | `KitCompileSpec` | `RisaKitCompileSpec` DTO (per-slot rate/loop). |
| `audioDriver.ts` *(extend)* | `compileKit` | `audioDriver.compileDmc(spec)`. |

### UI — `packages/retroplug/ui/screens/`

| File | LSDj analog | Purpose |
|---|---|---|
| `grid/RisaOverlay.tsx` (new) | `LsdjOverlay.tsx` | dev overlay. |
| `grid/useRisaRuntime.ts` (new) | `useLsdjRuntime.ts` | per-`frame` `readRam` pull + signature dedupe. |
| `grid/risaDebug.ts` (new/share) | `lsdjDebug.ts` | toggle store. |
| `menu/menuDefs.ts` *(extend)* | same | risa Songs + Kits/Themes/Fonts submenus (memoize inventory by `romPath`). |

---

## 9. Phased implementation plan

Each milestone lists deliverables, the RetroPlug analog it copies, the native/TS split, and headless verification (mirroring `pnpm test` pure-TS mock / `test:native` real host+cores / `test:ui` LVGL / `test:plugin` Catch2).

### M0 — Spike: boot a real risa ROM in RetroPlug's Mesen (0.5–1 day, de-risking)

Answer the three blocking unknowns before writing code. **Deliverables:** load a risa `.nes` into RetroPlug; confirm (a) Mesen boots MMC5 mapper 5 + produces audio (including DMC), (b) `readSram(id)` returns the 64 KB WRAM (dump it, look for `RSAV` at `0x8000`) vs only 8 KB, (c) `header[6] & 0x02` battery bit is set (else the `.sav` path won't engage). **Native/TS:** none (investigation). **Verify:** `test-native` throwaway that constructs a risa system, `screenshot`, and dumps `readRam`/`readSram` lengths + `RSAV` presence.

### M1 — Save catalog codec (read-only) + Songs list (TS-only)

**Deliverables:** `risa/codec/sav.ts` container normalization + RSAV header parse + `listSongs` (cheap: walk record headers for name/version/length, no payload decode); `risaSav.ts` barrel; risa ROM `identify.ts` + a `romProvider`. **Analog:** `sav.ts listProjects` + `romProviders`. **Split:** TS. **Verify:** `pnpm test risa` (golden: parse `RAVER.risong`/a real `.sav`, assert count/names/lengths, cross-check against `risa_save.py`); `test:native` boots a risa system and lists catalog songs via `readSram`.

### M2 — Song load / export / import / delete / reorder + `.risong` (TS-only)

**Deliverables:** byte-level catalog ops in `risa/codec/sav.ts` (`writeRecord`/`deleteRecord`/`moveRecord` — memmove tail, rewrite `count`+`used`, zero freed bytes) + `risaSongOps.ts`; `loadSongToWorking` (write record into live banks 0–3 + cold-boot); `risa/codec/risong.ts` (zip via host `unzip`/`zip`, manifest, reachability walk); `risaSongImport.ts` (`readSram→op→writeFileAtomic→loadSram` cycle; `.risong` import with semantic-fingerprint kit dedupe + DMC-ref remap); extend `sramAutoSave` with `risaSramSignature`. **Analog:** `lsdjSongOps`/`lsdjSongImport`/`lsdjLsdprjImport`. **Split:** TS (kit compile for `.risong` **import** deferred to M5 — import that reuses existing ROM kits works now; import needing a **new** kit bank waits for M5, or appends the bundled 8 KB bank verbatim). **Verify:** `pnpm test` byte-identity round-trip corpus (insert/delete/move must preserve directory integrity — `used`/`count` desync throws); `test:native` load-song-to-working boots the loaded song; UI Songs menu via `test:ui`.

### M3 — ROM kit/theme/font view + replace + the `MesenBackend` `romBytes` change (TS + 1 native change)

**Deliverables:** `risa/rom/*` (RisaRom, header layout, kit parse/pack + **dual-write mirror**, theme table, planar CHR fonts, crc32 fingerprints); `risaAssetsRole.ts` (`onConstruct`→`romBytes`); menu asset submenus (memoized by `romPath`, trial-apply-to-validate); **`MesenBackend.cpp` honors `spec.romBytes`**. **Analog:** `lsdj/rom/*` + `lsdjAssetsRole` + the SameBoy `romBytes` branch. **Split:** TS (view/patch/role) + native (the backend branch). **Verify:** `test-native/risa-rom.test.ts` (read a real ROM, patch a theme inline + a kit/font by path, boot the patched image, assert asset present and ROM otherwise byte-identical); `test/systems/risa-assets.test.ts` (store override → `romBytes` on the spec, reload re-applies via `applyConstructHooks`). Optionally a Catch2 `retroplug-*-test` asserting `MesenBackend` boots from `spec.romBytes`.

### M4 — Runtime reader + dev overlay (TS-only)

**Deliverables:** `runtime/symbols.ts` (`.lbl`/`.mlb` parser, band classification), `runtime/layout.ts`, an authored `risaSymbols.<ver>.generated.ts` from a produced `risa-pal.lbl`, pure `runtime/reader.ts` (`decodeRisaState` over the 2 KB `readRam` snapshot; replicate the `last_row==0xFF` selection macros and the `bpm==296` sentinel); `RisaOverlay.tsx` + `useRisaRuntime.ts`. **Analog:** `lsdj/runtime/*` (minus all drift machinery) + `LsdjOverlay.tsx`. **Split:** TS (no native work — `readRam` already carries NES internal RAM). **Verify:** `test-native` boots risa, drives it with `pressButton`, asserts `decodeRisaState` reports `playing`/`songRow`/`screen`/`bpm` correctly; `test:ui` overlay renders/dedupes.

### M5 — Native NES-DMC kit compile (native + TS)

**Deliverables:** `risa/DmcEncoder` (`wav2dmc.py` port), `DmcKitCompiler` (or generalized `KitCompiler` with an encoder strategy) reusing `SampleCache`/`Effects`/r8brain/enkiTS; `compileDmc` RPC (lazy) + `audioDriver.compileDmc`; `.rik` WAV import wired to it; pure-TS `buildDmcBank` twin for single-sample splices. **Analog:** `KitCompiler`/`compileKit`/`buildKit.ts`. **Split:** native (encoder+compiler+RPC) + TS (`.rik` codec, splice, driver). **Verify:** Catch2 `retroplug-risa-dmc-test` (byte-parity vs `wav2dmc.py`/`fixtures/parity.html` goldens on the **unfiltered** path; do **not** gate on filtered-slot byte-identity); build it in `build-tsan/` for the ThreadSanitizer pass like the render-host test; `test-native` `.rik`→bank→boot→DMC audio renders.

### M6 — Deferred / optional

Legacy save migration (v1 catalog → v2; legacy fixed-slot `.sav` → RSAV, the large `migrate_legacy_sav.py` port); a **per-block banked-WRAM seam** (`ram2` triple in `SnapshotRegistry` from `getMemory(Sram)` + `readWorkRam` RPC, ~15 native lines) for a live **pattern-content** overlay; WAVE/ORG/SAW and zsaw-family import. **Verify:** as above per piece; the banked-WRAM seam gets a Catch2/`test-native` twin of `lsdj-wram-seam.test.ts`.

---

## 10. Open questions / decisions for the user

1. **New system type vs role on NES.** *Recommendation: role on existing NES/Mesen* — risa is an iNES ROM, so `detectPlatform→"nes"`, `core→"mesen"` already; add risa-specific **feature roles** (`risa-assets`, risa-runtime) via a `romProvider`, exactly as LSDj is a `gb` ROM + `lsdj-sync`/`lsdj-assets`. No new `Platform`/`Core` enum value. Confirm you don't want a distinct "risa" surface for UX reasons.

2. **DMC encoder: reuse `KitCompiler` scaffold with a new encoder (native) vs port `wav2dmc.py` to TS.** *The parity-critical path favors a TS port*: `wav2dmc.py` is the declared source of truth, and byte-identity requires reproducing its 32-tap fixed-point sinc + the ±2 delta clamp — a native r8brain (`CDSPResampler24`) re-implementation risks drift. But native reuses `SampleCache`/`Effects`/enkiTS and matches the `compileKit` pattern. Options: (a) TS-only encoder (`encoder.js` port — already byte-parity-verified on the unfiltered path); (b) native, accepting "good-enough" DMC (like LSDj uses CDSPResampler24) and **not** gating on byte-parity; (c) native that faithfully reproduces the fixed-point sinc. **Decision needed.**

3. **PAL vs NTSC.** risa builds both; `wav2dmc.py`/patcher ship **PAL rates only**. The kit stores a rate *nibble*; actual Hz depends on region (~1% apart). Which region does RetroPlug target for kit compile, and does it read the loaded ROM's region (and Mesen's region setting) to pick the table? **Decision needed** (default: PAL, matching the reference tooling).

4. **Stable RAM symbol addresses without building risa in-tree.** cc65 BSS/ZP addresses reshuffle per build; end users have only `risa.nes`. Options: (a) **bundle** a generated per-version symbol snapshot keyed on `APP_VERSION_TEXT` (like `driftLayouts.generated.ts`, but authored); (b) **ship the `.lbl`/`.mlb` alongside the ROM**, link it by path like an asset, resolve fresh per ROM; (c) build risa in-tree to emit the `.lbl` for the generator. Note `build/risa-pal.lbl` was **absent** from the examined checkout — the generator needs a produced label file regardless. **Decision needed.**

5. **risa ROM detection (no NES title field).** risa's version is `"RISA V"` found by scanning the whole PRG, not a fixed offset. Options: (a) read a **larger ROM prefix** into `RomContext.header` and scan it (today providers see a short header prefix); (b) ask upstream to expose a **fixed-offset identity magic**; (c) detect via the theme/kit-meta magics. **Decision needed** — affects `romProviders` and how much of the ROM TS ever sees.

6. **Battery detection.** Does risa set the iNES battery flag (`flags6` bit 1)? RetroPlug gates the `.sav` path on `header[6] & 0x02` (`platform.ts:81`). If unset, either patch detection to force-battery for risa, or accept that saves won't persist. **Verify against a real ROM** (M0).

7. **Does `GetMemory(NesSaveRam)` return the full 64 KB MMC5 WRAM or only the `$6000` 8 KB window?** Determines whether song management works through the existing `readSram`/`loadSram` seam (full 64 KB = yes, the `.sav` **is** that image) or needs a native banked-WRAM read. **Verify in M0.** This is the single highest-leverage unknown.

8. **Whole-ROM asset patch scope: also rewrite the kit-metadata mirror tables?** A faithful kit replace must update `kit_names`/`kit_sample_names`/`kit_slot_present` + the locator magic, or the on-device kit list goes stale. Confirm RetroPlug does full patching (recommended) vs individual banks only.

9. **Live pattern-content overlay: accept coarse (~0.5 s) `Sram` refresh, or build the per-block banked-WRAM seam (M6)?** The position/tempo/screen overlay (M4) needs neither. **Decide when scoping M6.**

10. **`.rik`/`.risong` zip at construct.** `ConstructCaps` currently lacks `unzip` (host has it on the Host facet). Either widen `ConstructCaps` with `unzip`, or **pre-expand** archives in TS before construct so the role only ever links raw `.dmc`/8 KB-bank bytes by path (matching the no-base64 rule). *Recommendation: pre-expand in TS*; the role links the extracted asset by path. **Confirm.**

---

**Bottom line.** risa maps cleanly onto RetroPlug's LSDj architecture with one required native change (`MesenBackend` `romBytes`), one new native algorithm (NES-DMC encode), and a set of TS codec/rom/runtime modules that are in several places *simpler* than their LSDj counterparts (no RLE, no offset-drift detection, inline-JSON themes). The critical de-risking step is M0: confirm Mesen boots MMC5 and that `readSram` exposes the full 64 KB battery WRAM. Everything downstream is a faithful mirror of an existing, well-tested stack.


---

## Appendix A — Independent verification of load-bearing native claims

The claims that most affect cost/feasibility were re-checked directly against the source in both repos (not just the exploration agents). Results:

| Claim | Verdict | Evidence |
|---|---|---|
| Mesen supports MMC5 **including expansion audio** | ✅ Confirmed | `deps/mesen/Core/NES/Mappers/Nintendo/MMC5.h`, `Mmc5MemoryHandler.h`, and `Core/NES/Mappers/Audio/Mmc5Audio.h` all present. risa's own `src/crt0.s:4` documents "MMC5 … expansion audio". (An end-to-end audio confirmation is still the M0 spike, but the mapper + aux-pulse handler exist.) |
| `MesenBackend::build` ignores `spec.romBytes` today | ✅ Confirmed (required change is real) | `packages/native/src/system/mesen/MesenBackend.cpp:45` slurps `spec.romPath` unconditionally; there is no `embeddedRom`/`romBytes` branch, unlike `SameBoyBackend.cpp:44–53`. |
| …but the downstream plumbing already exists (change is small) | ✅ Confirmed | `MesenNesConfig.romBytes` field exists (`MesenNesConfig.hpp:45`); `MesenNesSystem` ctor takes `romBytes` and stores `rom_` (`MesenNesSystem.cpp:80–83`) and boots from it via `VirtualFile(rom_.data(), …)` (`MesenNesSystem.cpp:117`). So only `build()`'s **selection** of bytes is missing — ~10–15 lines mirroring SameBoy. (This corrects the synthesis, which implied it was already fully wired, and the critique, which implied no Mesen-side plumbing exists at all. Both are half-right: system boots from bytes; backend never passes them.) |
| RetroPlug battery gate = iNES flags6 bit 1 | ✅ Confirmed | `packages/retroplug/src/platform.ts:81`: `if (platform === "nes") return header.length > 6 && (header[6] & 0x02) !== 0;` |
| risa's ROM sets the battery bit | ✅ Confirmed (Open Q #6 = yes) | `src/crt0.s:81`: `NES_MIRRORING|((NES_MAPPER & 15)<<4)|$02  ; bit 1 = battery SRAM at $6000`. |
| risa declares a full 64 KB battery WRAM | ✅ Confirmed — **de-risks the #1 unknown** | risa is **NES 2.0** (`crt0.s:82` `(NES_MAPPER&$f0)|$08`) and byte 10 = `$A0` (`crt0.s:85`: "PRG-NVRAM: 64KB … 8 MMC5 WRAM banks, battery-backed"). A NES-2.0-compliant emulator allocates the full 64 KB, so `GetMemory(NesSaveRam)` should return the whole 64 KB image, not an 8 KB window. **Still worth a runtime confirm in M0** (does RetroPlug's Mesen parse NES-2.0 PRG-NVRAM, and does the 64 KB Sram clear the `SnapshotRegistry` `kMaxRamBytes=64KB` ceiling — it sits exactly at it). |
| NES internal RAM flows through the per-block `readRam` seam | ✅ Confirmed | `MesenNesSystem.cpp:439` maps `rp::MemoryType::Ram → ::MemoryType::NesInternalRam`; risa keeps all playhead/tempo/screen state in that 2 KB window (`src/seq.h`) → runtime overlay needs no native work. |

### Corrections / caveats carried forward from the critique (still open)

- **NES 2.0 header parsing.** risa's byte 7 low nibble is `8` (NES 2.0). Confirm `detectPlatform`/`detectRomFormat` and the `header[6]&0x02` gate behave correctly on a NES-2.0 header (they read bytes 4/5/6, which are iNES-compatible, so this is expected-fine — but verify PRG/CHR size math doesn't misread the NES-2.0 size MSBs).
- **PAL vs NTSC symbol snapshot.** risa ships separate PAL and NTSC builds with the **same** `APP_VERSION_TEXT`; their cc65 BSS/ZP addresses can differ. The runtime offset snapshot must be keyed on **(version, region)**, not version alone (see §10 #4).
- **Test-ROM + `.lbl` provenance.** No built `risa.nes` or `build/risa-pal.lbl` exists in-tree; the source (with a cc65 devcontainer/Dockerfile) is at `/workspaces/risa-v2.2.1-source`. M0 and M4 need a cc65 build to produce the ROM and label file — establish that build path first.
- **64 KB Sram vs `SnapshotRegistry` cap.** `kMaxRamBytes = 64*1024`; a 64 KB `readSram` is exactly at the ceiling — confirm the Sram snapshot isn't dropped.

---

## Appendix B — Completeness critic (raw)

The following adversarial review was run against the synthesis; the resolved items above answer several of its points (MMC5 support, battery gate, romBytes plumbing). The unresolved items (MMC5 *expansion-audio* end-to-end, `NesSaveRam` runtime size, ROM/`.lbl` provenance, PAL/NTSC disambiguation) remain the M0 de-risking checklist.

Skeptical review of the risa integration plan. Citations are to the plan's own references and the findings.

UNSUPPORTED / CONTRADICTED CLAIMS
1. "NES DMC plays out of the box — Mesen renders the DMC channel" and "MMC5 is common… low-risk." No finding verifies RetroPlug's `deps/mesen` supports mapper 5 at all. Worse, the plan conflates the standard APU DMC channel with MMC5, and never mentions MMC5 *expansion audio* (extra pulse + PCM). Whether risa drives MMC5 expansion channels is unexamined — if it does, and Mesen omits them, playback is wrong, not just untested.
2. "`MesenNesConfig.romBytes` is a field, and `MesenNesSystem`'s constructor already takes+stores `romBytes` (`rom_`)… ~15 lines." The findings say the opposite: "SameBoyBackend uses spec.romBytes… **Mesen ignores it**" (`SameBoyBackend.cpp:50-54`). No finding confirms any Mesen-side romBytes plumbing. The whole "~15 lines / M3 is cheap" estimate rests on an unverified assumption the plan itself later hedges (§7.1).
3. Battery gate "`platform.ts:81` … `header[6] & 0x02`." Asserted as fact; no finding read platform.ts battery logic. Treat as unverified, not grounding.
4. Runtime symbol snapshot "keyed on `APP_VERSION_TEXT` (\"2.2.1\")." Contradicted by findings: risa ships **separate PAL and NTSC builds** with the *same* version string but potentially different cc65 BSS/ZP addresses (`risa-pal.lbl` vs an NTSC label file). Version alone cannot disambiguate the two — this key is insufficient.

MISSED AREAS / READ NEXT
- `deps/mesen` MMC5 mapper source (Mmc5.h/.cpp) — verify mapper-5 support, `$5113`/`$5116` banking, MMC5 expansion audio, and precisely what `GetMemory(NesSaveRam)` returns for a 64 KB MMC5 cart. This gates M0/M1/M2 and is the single most load-bearing unread file.
- `SnapshotRegistry` `kMaxRamBytes = 64*1024` cap (findings, SnapshotRegistry.hpp:84-86): a 64 KB WRAM `readSram` sits exactly at the ceiling — confirm the Sram triple isn't capped/skipped.
- `src/seq_data_save.c` (the on-cart serializer, ground truth) and `record_codec.js` (21 KB, **unread** per findings) — needed for any payload/.risong work in M2.
- `tools/rom_patcher/src/kit_fingerprint.js` (unread) — M2's "semantic-fingerprint kit dedupe" and the `(len+1)*16+1` vs `len*16+1` discrepancy depend on it.
- NES 2.0 header flags6/7 on a real ROM (mapper/battery/submapper) — never read; detection + battery both depend on it.

HIGHEST-RISK UNKNOWNS (resolve before coding)
1. Does RetroPlug's Mesen actually emulate MMC5 (banking + any expansion audio) correctly? Everything else is moot otherwise.
2. `GetMemory(NesSaveRam)`: full 64 KB or one 8 KB `$6000` window? Sizes M1/M2 and interacts with the 64 KB snapshot cap.
3. Provenance of a test `risa.nes` **and** `risa-pal.lbl`: neither exists in-tree (source is at `/workspaces/risa-v2.2.1-source`, no built ROM, `build/risa-pal.lbl` absent). M0 and M4 are blocked until a cc65 build path is established — the plan never specifies this.
4. iNES/NES2.0 battery + mapper flags on a real ROM.
5. PAL/NTSC symbol-snapshot disambiguation.

WHERE THE PLAN IS VAGUE BUT MUST BE CONCRETE
- M0 "load a risa `.nes`": no source for the ROM or `.lbl` is named — the actual blocker.
- "MesenBackend honors romBytes": must resolve whether `MesenNesSystem::onActivate` reads `rom_` bytes or `cfg.romPath`; the plan defers this to "verify" yet still calls the change trivial.
- risa detection (§10 #5): romProviders currently see only a short header prefix (LSDj uses `readFilePrefix(0x150)`), but `"RISA V"` requires a whole-PRG scan — the concrete mechanism (how many bytes TS sees) is undecided, yet M1 depends on it.

---

## Supporting a new risa release

Most of the integration is version-agnostic and needs nothing: the iNES-header fingerprint, the
save-catalog + song-record codec (gated on the record version in the data, not the app version), and the
ROM asset layer (kits / themes / fonts are located by magic scan, so they survive layout shifts). What
IS per-version is the **runtime RAM layout** - cc65 reshuffles the BSS/ZP addresses on every build, so a
release with no bundled snapshot resolves no layout and the whole tracker submenu greys out as
"(Unsupported Version)", taking Songs, Kits, Themes, Fonts, the live overlay and render song-length
auto-detect with it.

1. **Build the release from source** to get its label files. Needs cc65 (source-built - distro 2.19 is
   too old, see M0 above) and python3; `make all` writes `build/risa-pal.lbl` + `build/ntsc/risa-ntsc.lbl`.
   The shipped tree may need a lowercase `kits/psr150.rik` copy on a case-sensitive filesystem.
2. **Generate the snapshot**: `RISA_SRC=<tree> RISA_VERSION=<x.y.z> node
   packages/retroplug/scripts/gen-risa-symbols.mjs`. It merges - older versions stay - and fails loudly if
   a symbol moved between the PAL and NTSC builds.
3. **Certify it against the RELEASED ROM.** A local build is *not* byte-identical to the developer's
   binary (a different cc65 build reshuffles codegen), so the label file alone doesn't prove the addresses
   fit the ROM users run. Copy `test-native/risa-230-layout.test.ts`: it boots the released ROM and
   asserts the decode is coherent and advances. Wrong addresses decode as garbage and fail there.

Two things to check in the release's own source rather than assume:

- **`SAVE_RECORD_VERSION` / `SAVE_MAGIC_VER`** (`src/seq_data.h`, `tools/rom_patcher/src/save_manager/`).
  Both were unchanged through 2.3.0. A bump means codec work.
- **Instrument type reuse.** 2.3.0 repurposed type 4 from WAVE to Z-Saw *within the same record version*,
  discriminated only by a marker byte - see `migrateInstruments` in `src/risa/codec/working.ts`. Diffing
  `src/seq_data.h`'s `INST_*` block against the previous release catches this class of change.

Host sync (2.3.0 and later) needs nothing per-version: the `RISAxyz` marker carries the version, and the
role attaches on its presence.
