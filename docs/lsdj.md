# LSDj & DAW/audio testing

The LSDj domain reference, plus how to verify LSDj sync / DAW timing / audio
quality headlessly.

**On tooling:** the test procedures below run on the **legacy** CLI + reaper
harness (`retroplug-cli`, `test/ts/**`, `emu.*`, `pnpm test:cli` / `reaper:*`).
Greenfield is growing its own DSP-role tests (`pnpm test:greenfield-native`; see
[spec/06-build-test.md](../spec/06-build-test.md)), and the LSDj sync roles are being
reimplemented in TypeScript (see [spec/04-roles-dsp-kernel.md](../spec/04-roles-dsp-kernel.md)
and `spec/07-migration.md`). Until greenfield replaces the full LSDj/DAW matrix,
this legacy harness is the way to run it — and the **domain facts** here (the sav
model, screen map, Arduinoboy protocol, manual lookup) are build-agnostic.

---

## Authoring LSDj state in TypeScript (canonical)

The LSDj-driving tests used to navigate the UI with fragile `SELECT/A`+arrow
chords in JSON `--script` files to build song/sync state. That state is just
bytes in the `.sav`, so tests now **author it directly** with the sav codec and
boot LSDj straight into it — fast (a valid sav skips the 12–15 s self-test) and
robust (no timing-sensitive navigation). Every former JSON test now lives under
[test/ts/](../test/ts/) as a `*.test.ts` (run all with `pnpm test:cli`,
or one with `pnpm test:cli <slug>` where `<slug>` is the path under
`test/ts` in slash or dash form — e.g. `gb/lsdj/sav` or `gb-lsdj-sav` — and a
directory prefix like `gb/lsdj` runs every test under it).

The pattern (see [test/ts/gb/lsdj/sync_pattern.test.ts](../test/ts/gb/lsdj/sync_pattern.test.ts)
or [lsdj_arduinoboy_metro.test.ts](../test/ts/gb/lsdj/lsdj_arduinoboy_metro.test.ts)):

```ts
const sav = emu.savFromJson(JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },              // PROJECT-screen SYNC (None/Lsdj/Midi/Keyboard/AnalogIn/AnalogOut)
    rows:    [{ chains: [0] }],                   // rows[0].chains[0]=0 → chain 00
    chains:  [{ phrases: [0] }],                  // chains[0].phrases[0]=0 → phrase 00
    phrases: [{ notes: [1], instruments: [0] }],  // phrases[0]: step 0 = note 1 / instrument 0
    instruments: [{ type: "pulse" }],             // instruments[0]
  },
}));
const sys = emu.loadRom(rom, sav, /*lsdjSyncMode*/ "MidiSyncArduinoboy", /*linkGroup*/ 1);
```

Fixed arrays may be short or omitted: the sav codec pads each to its full
on-disk length with default elements (`0` / `null` / `None` / a default struct),
so a fixture only specifies the cells it sets. Serialization always writes the
full length, so on-disk encoding and JSON round-trips are unchanged; supplying
more than the fixed length is an error. (Implemented by `FixedArray<T,N>` in
[packages/native/src/lsdj/model/FixedArray.hpp](../packages/native/src/lsdj/model/FixedArray.hpp).)

`emu.loadRom(path, sav?, lsdjSyncMode?, linkGroup?)`:
- `sav` — an `ArrayBuffer` from `savFromJson` (or `readMemory(sys, Mem.Sram)`).
- `lsdjSyncMode` — the `LsdjSyncRole` config: `"MidiSync"`, `"MidiMap"`,
  `"KeyboardMidi"`, `"MidiPassthrough"`, `"MidiSyncArduinoboy"`,
  `"ArduinoboyMaster"`, … (distinct from the in-sav PROJECT `syncMode`).
- `linkGroup` — same nonzero value on two systems puts them in a shared
  `LinkGroup` (lockstep serial-bit ferrying) for LSDj link-cable sync.

Other harness bindings these tests use (see [test/harness/index.ts](../test/harness/index.ts)):
`setTransport(bool)` / `setBpm(n)` (simulated host transport → the role's MIDI
clock), `drainMidi(sys)` / `drainSerial(sys)` (role MIDI-out / GB serial-out
capture), `runMsPerSystem(ms)` (per-system audio — proves link sync), `writeWav`
(dump audio for the reaper MCP), `saveRplg` (snapshot → `.rplg` for the Reaper
DAW fixtures), `loadRplg(path)` (inverse of `saveRplg`: rebuild the project from
a `.rplg`, config + per-system savestate, exactly as the plugin does on load —
use it to round-trip a fixture in-harness and reproduce what a DAW sees on
reload), `patchKit(sys, slot, name, samples)` (compile + queue a kit).

## Driving the LSDj UI (only when authoring can't)

Authoring savs covers song/sync/instrument state. If a test genuinely needs to
drive the live UI (e.g. exercising a menu interaction), the harness exposes
`emu.chord(sys, buttons, opts?)` and `emu.tap(sys, button, holdMs?)` (see
[test/harness/index.ts](../test/harness/index.ts); `gb/smoke.test.ts` uses them).

LSDJ relies on two-key chords (`SELECT+CURSOR` to change screen, `A+CURSOR` to
change a field). `emu.chord` encodes the working timing (modifier held ~200 ms
before the key, released in reverse) — **never press both keys simultaneously**,
LSDJ drops the chord. The screen map (empirically verified; manual `Figure 1.2`):

```
                PROJECT
                  │  SELECT+UP / +DOWN
                  ▼
SONG  ◄────────► CHAIN  ◄────────► PHRASE
        SELECT+RIGHT      SELECT+RIGHT
```

`SELECT+LEFT` from SONG enters LIVE mode (the grid wraps). The `LEAD` / `SYNC` /
`WAIT` indicators in the SONG-screen right margin are the runtime confirmation
that link-cable sync is flowing once START is pressed (manual §5.1.2 / §5.1.3).
The `retroplug-cli --script` JSON runner (the embedded TypeScript CLI in
[packages/cli](../packages/cli), with `chord`/`tap`/`midi`/`screenshot` event forms)
still exists for ad-hoc exploration, but it has no committed example scripts —
author savs in TS instead.

### Pitfalls cheat-sheet

Most of these only bite when driving the live UI; **authoring a sav sidesteps
them entirely** (no boot wait, no navigation). They still apply to `emu.chord`/
`emu.tap` based tests.

- **Boot before ~15 s on a fresh ROM** — `getFrame`/`screenshot` captures the
  GB boot ROM or LSDJ's cartridge self-test, not the song screen. Boot from an
  authored sav and ~3–6 s is enough (the self-test is skipped).
- **Simultaneous chord keys** — pressing both keys at once is silently dropped
  by LSDJ. Use `emu.chord` (modifier leads ~200 ms).
- **Cursor-moving keys auto-repeat** — LSDJ's default `KEY DEL/REPEAT 7/2` means
  holding >7 frames (~117 ms) starts auto-repeating. `emu.tap`'s default
  `holdMs: 50` is below that threshold; longer holds fire multiple moves.
- **Mix audio alone can't prove sync** — two desynced instances still mix into
  a healthy-looking WAV. Use `emu.runMsPerSystem` and check each instance's RMS.
- **`SELECT+LEFT` from SONG enters LIVE mode**, not a "previous screen" — the
  screen grid wraps. Stick to `SELECT+UP/DOWN/RIGHT` for vanilla nav.
- **`A` in PROJECT on items like `HELP` or `LOAD/SAVE SONG` triggers them**
  rather than cycling a value. Confirm the cursor is on the right field first.

## LSDj link-cable sync

Covered by three TS tests under [test/ts/gb/lsdj/](../test/ts/gb/lsdj/):

- [sync_pattern.test.ts](../test/ts/gb/lsdj/sync_pattern.test.ts) — positive: two
  instances on the same `linkGroup`, both authored SYNC=LSDJ, START on the
  leader. Verifies sync via **per-system audio** (`emu.runMsPerSystem`): the
  follower produces audio (and its RMS tracks the leader's) only because it
  synced. Also writes `/tmp/lsdj-sync-pattern_sys{0,1}.wav` for the reaper MCP.
- [sync_negative.test.ts](../test/ts/gb/lsdj/sync_negative.test.ts) — control:
  same setup with SYNC=None. The follower stays **silent** (never starts). If it
  ever produces audio, the positive test isn't measuring real sync.
- [sync_smoke.test.ts](../test/ts/gb/lsdj/sync_smoke.test.ts) — two-instance boot +
  audio plumbing.

If link sync is genuinely broken, the follower's per-system RMS stays at 0 in
the positive test — look at `packages/native/src/system/sameboy/LinkGroup.cpp` and the
`serialStart` / `serialEnd` callbacks in `SameBoySystem.cpp`. `runMsPerSystem`
isolates each instance's audio (the canonical way to tell synced playback from a
healthy-looking mix of two desynced instances).

## Capturing the Game Boy screen from a script

`retroplug-cli` can dump per-system framebuffers to PNG — the deterministic way
to see what LSDj (or any other ROM) is actually showing without booting the
plugin or standalone UI. Add a screenshot event to the script JSON:

```json
{ "at_ms": 15000, "screenshot": "post_boot", "system": 0 }
```

Or pass `--final-screenshot` to dump every system once at script end. Output
filenames: `<scriptStem>_<name>_sys<idx>.png` under `--screenshot-dir`
(defaults to the dir of `out_wav`, then cwd).

**Boot timing.** Two boot sequences sit between `at_ms: 0` and the LSDj song
screen: (1) SameBoy plays the Game Boy boot ROM (white screen + chime, ~1.5 s);
(2) LSDj runs its own cartridge/SRAM self-test on first boot of a fresh ROM
(`CARTRIDGE TEST ROM...` then `SRAM...`) — on the bundled `lsdj9_4_2.gb` this can
take **12–15 s**. Schedule any screenshot expecting the song screen at
`at_ms` ≥ 15000 **on a fresh ROM**. The far better option — and what the TS tests
do — is to boot from an authored sav (`emu.savFromJson(...)`), so LSDj skips the
self-test and reaches the song screen in ~3–6 s.

## Reaper / DAW verification

The plugin honours `RETROPLUG_AUTOLOAD_PROJECT=path/to/foo.rplg` at construction:
if set, the `.rplg` (pure PKZIP) is loaded as the initial project — lets a host
instantiate the plugin with a preconfigured ROM without authoring the DPF state
chunk by hand. The canonical way to produce a `.rplg` is a TS harness test:
author the state (sav + roles), then `emu.saveRplg("/tmp/foo.rplg")`. Then:

```sh
RETROPLUG_AUTOLOAD_PROJECT=/tmp/foo.rplg \
    tools/run-reaper-render.sh your_project.rpp
```

`tools/run-reaper-author.sh OUTPUT.rpp RENDER_DIR AUTHOR.lua FIXTURE.rplg` takes a
pre-built `.rplg` directly. Without the env var, the plugin starts empty (matches
normal DAW behaviour).

- **Audio-quality check on a render** — `pnpm reaper:analyze-smoke` (runs
  `test/ts/gb/mgb.test.ts` → `/tmp/cli-smoke.wav`) or `reaper:analyze-lsdj-sync`
  (runs `test/ts/gb/lsdj/sync_pattern.test.ts`, per-system WAVs via `emu.writeWav`)
  stages the WAV into the reaper-mcp-server's projects dir; then ask the `reaper`
  MCP server for loudness/LUFS, frequency content, dynamics, stereo imaging.
  Catches regressions that aren't "no audio" but "audio is wrong" (clipping,
  channel imbalance, DC offset, spectrum shift). The MCP server is installed at
  `/opt/reaper-mcp-server`; the projects dir defaults to
  `../resources/reaper/projects/` (override with `RETROPLUG_REAPER_DIR`).
- **VST3 plugin host check** — `pnpm reaper:mgb-smoke` renders
  [examples/reaper/mgb_smoke.rpp](../examples/reaper/mgb_smoke.rpp) headlessly
  through real Reaper 7.x: instantiates `retroplug.vst3`, plays a C-major chord
  through mGB, writes `build/reaper-mgb-smoke.wav`. First end-to-end proof the
  plugin works inside a DAW (not just `retroplug-cli`, which bypasses DPF).
  Headless plumbing: `tools/run-reaper-render.sh` (Xvfb + openbox + dummy jackd +
  EULA auto-dismiss). Regenerate the fixture with `pnpm reaper:mgb-author`.
- **Arduinoboy startup-sync latency** — `pnpm reaper:lsdj-arduinoboy-metro`
  renders [examples/reaper/lsdj_arduinoboy_metro.rpp](../examples/reaper/lsdj_arduinoboy_metro.rpp)
  (LSDj hard-L on `MidiSyncArduinoboy`, a ReaSynth click hard-R, one note/quarter
  at 120 BPM), then [tools/reaper-timing-analyze.py](../tools/reaper-timing-analyze.py)
  reports the offset between host transport start and LSDj's first audible sample.
  Pass/fail ±50 ms. Surfaces drift in `PpqUtil::eachTick()` and in `LsdjSyncRole`'s
  startup byte sequence (`0xFA` + first `0xF8`). A stock-MidiSync counterpart,
  `reaper:lsdj-midi-metro`, measures the same number through `MidiSync`.
- **MidiSync per-beat drift over time** — `pnpm reaper:lsdj-midi-drift` renders
  an **hour-long** project (LSDj hard-L on `MidiSync`, click hard-R, one note/beat
  at 120 BPM), then `reaper-timing-analyze.py --drift` pairs each LSDj onset to its
  reference beat and reports mean/median/max-abs/stddev drift, a per-minute trend,
  and a linear accumulation slope (ms/min). **Fails** if max-abs drift exceeds
  ±50 ms or >1 % of beats go unmatched. Answers "how accurate is MidiSync in the
  DAW, and does it drift over an hour?" **Caveat:** the one-click-per-beat spacing
  assumes LSDj's default groove (6 ticks/step → 4 steps/beat at 24 PPQN); if the
  analyzer's matched-beat count isn't ≈ the beat count, adjust the phrase spacing.

Each reaper fixture is regenerated with its `pnpm reaper:*-author` script when the
matching `test/ts/gb/lsdj/*_author.test.ts` or `tools/reaper-*-author.lua` changes.

## LSDj manual lookup

Every English LSDj manual (1.0b → 9.2.6) plus the upstream `CHANGELOG.txt` is
indexed for keyword + semantic search:

```
tools/lsdj-manual-setup.sh                # one-time: venv + deps + index
tools/lsdj-search "midi sync mode"
tools/lsdj-search --mode vec "how do two units stay in time"
tools/lsdj-search --show-images "PROJECT screen"
tools/lsdj-search --lsdj-version 6.0.0 "midi sync"   # docs relevant to v6.0.0
tools/lsdj-search --only-changelog "noise table"     # changelog-only
tools/lsdj-manual.py versions             # list every indexed source
```

`--lsdj-version <ver>` picks the **most recent manual whose version is ≤ <ver>**
— for LSDj 9.4.2 that's `LSDj_9_2_6.pdf`, for 6.0.0 it's `LSDj_5_8_4.pdf`. The
changelog is always included (suppress with `--no-changelog`). Pick `--mode fts`
for exact LSDj terminology ("FX command", "groove", "R command"), `--mode vec`
for paraphrased / vague questions, default `hybrid` when in doubt.

The setup script creates `tools/.venv` (pymupdf, fastembed, sqlite-vec, numpy),
then runs `tools/lsdj-manual.py index` to produce, under `../resources/manuals/`:
`lsdj_manual.md` (readable markdown from the highest-version manual — Read + grep
fallback when the index is missing), `lsdj_manual_images/<ver>/` (per-version PDF
page images; `--show-images` returns paths to `Read`), `lsdj_index.db` (SQLite
FTS5 BM25 + sqlite-vec cosine, fused via reciprocal-rank fusion), and
`lsdj_embed_cache.db` (sha256→embedding cache; re-indexing only re-embeds changed
chunks).

To populate the full archive (~35 PDFs + CHANGELOG.txt + ~550 ROM ZIPs):

```
python3 ../resources/download_lsdj.py                           # everything
python3 ../resources/download_lsdj.py --no-roms --dry-run       # preview
python3 ../resources/download_lsdj.py --variant stable          # subset
```

The downloader is stdlib-only (no venv) and auto-invokes `tools/lsdj-manual.py
index` after it finishes (skip with `--no-index`). Japanese / French variants are
excluded (the index is English-only). Downloads are idempotent (re-running skips
files on disk unless `--force`). These artifacts live in a sibling `resources/`
directory outside the repo (default `../resources/`, override with
`RETROPLUG_RESOURCES_DIR`).

## LSDj Arduinoboy build (aboy)

All LSDj ROMs live under `../resources/roms/lsdj/` (outside the repo). Two
canonical builds are required for the headless test matrix; the two
`LsdjSyncMode` families need different ROMs:

| ROM | Title @0x134 | Supported `lsdj_sync_mode` values |
| --- | --- | --- |
| `lsdj/lsdj9_4_2.gb` | `LSDj-v9.4.2` (stock) | `Off`, `MidiSync`, `MidiMap`, `KeyboardMidi`, `MidiPassthrough` |
| `lsdj/lsdj9_3_3-arduinoboy.gb` | `LSDj-v9.3.3aboy` | All of the above plus `MidiSyncArduinoboy` and `ArduinoboyMaster` |

Running `python3 ../resources/download_lsdj.py` populates the archive — stable
releases as `lsdj<ver>.gb`, arduinoboy variants as `lsdj<ver>-arduinoboy.gb`,
develop snapshots as `lsdj<ver>-develop.gb`. Non-LSDj ROMs (Nanoloop GBA, mGB,
n8-midi) live one level up at `../resources/roms/`.

The sniffer ([packages/native/src/system/sameboy/RomSniffer.cpp](../packages/native/src/system/sameboy/RomSniffer.cpp))
treats both ROMs as `RomKind::Lsdj` (any title starting with `LSDj`). The role's
`onAttach` logs `build=stock` vs `build=arduinoboy` based on whether the title
contains `aboy` — check the stderr line `[RetroPlug] LSDJ sync role attached
(mode=…, build=…)` to confirm which build is loaded.

### PROJECT-screen SYNC cycle (aboy v9.3.3)

The on-screen SYNC value is the working-song byte at `0x3fbd`. The model
`SyncMode` enum (`packages/native/src/lsdj/model/Types.hpp`) authors values 0–5
directly via `settings.syncMode` —
[test/ts/gb/lsdj/sync_modes.test.ts](../test/ts/gb/lsdj/sync_modes.test.ts) authors
each and asserts the byte. The aboy-only MI.MAP / MI.OUT (6 / 7) are past the
model enum (see Master mode below). The full cycle order:

| Byte | SYNC value | Extra row visible |
| --- | --- | --- |
| 0 | OFF       | — |
| 1 | LSDJ      | — |
| 2 | MIDI      | — |
| 3 | KEYBD     | PS/2 DELAY 06 |
| 4 | ANA.IN    | TICKS/STEP 06 |
| 5 | AN.OUT    | TICKS/STEP 06 |
| 6 | MI.MAP    | — |
| 7 | MI.OUT    | — |

Note: stock LSDJ's manual (v9.2.6) does NOT document MI.OUT / MI.MAP — those are
aboy-specific. PRELISTEN row reads `ON` for OFF / LSDJ / KEYBD / MI.MAP / MI.OUT
and `N/A` for MIDI / ANA.IN / AN.OUT — so PRELISTEN is NOT a reliable "is a sync
mode selected" indicator on the aboy build. Read the SYNC field text (or the
`0x3fbd` byte) directly.

### Master mode (MI.OUT) verification

A role opts into serial-out capture via `RomRole::wantsSerialOut()`;
`LsdjSyncRole` enables it when its config is `ArduinoboyMaster`. From a TS test,
drain the captured bytes with `emu.drainSerial(sys)` (raw GB serial-out — ground
truth) and `emu.drainMidi(sys)` (the `ArduinoboyMaster` decoder's MIDI output).
See [test/ts/gb/lsdj/arduinoboy_master.test.ts](../test/ts/gb/lsdj/arduinoboy_master.test.ts),
which authors SYNC=KEYBD + the `ArduinoboyMaster` role, presses START, and
asserts thousands of captured bytes (the synthetic-clock + capture path).

### Synthetic Arduinoboy clock (subtle but load-bearing)

LSDJ in MI.OUT (and KEYBD) uses the GB serial port in **external-clock** mode
(`SC=0x80`). Real Arduinoboy hardware provides the clock pulses that shift the
GB's SB register. SameBoy by default does nothing here. To make this verifiable
headlessly, [packages/native/src/system/sameboy/SameBoySystem.cpp](../packages/native/src/system/sameboy/SameBoySystem.cpp)
drives one bit per audio sample in `writeAudioSample` whenever
`(SC & 0x81) == 0x80` and serial-out capture is enabled:

```cpp
const auto sc = gb_->io_registers[GB_IO_SC];
if ((sc & 0x81) == 0x80) {
    const bool outBit = (gb_->io_registers[GB_IO_SB] & 0x80) != 0;
    captureSerialOutBit(outBit);
    GB_serial_set_data_bit(gb_, true);
}
```

This runs ~5.5 kHz faster than real Arduinoboy (~8 kHz) but the byte protocol is
rate-independent so the captured bytes are correct.

**Pitfall:** the bit-start callback gives the outgoing bit as its `bit_received`
parameter; this is the bit being SENT. Do NOT read `GB_serial_get_data_bit` in
the bit-end callback — by then SB has shifted and the MSB is the next bit to send,
giving every captured byte a one-bit offset.

### Arduinoboy MI.OUT byte protocol

Reference: [Mode_LSDJ_Midiout.ino](https://github.com/trash80/Arduinoboy/blob/master/Arduinoboy/Mode_LSDJ_Midiout.ino)
in the trash80/Arduinoboy firmware. (Don't confuse with `Mode_LSDJ_MasterSync.ino`
— a simpler "send one row byte + clock ticks" mode for sync slaves; MI.OUT is the
per-channel-note protocol.) The MI.OUT byte stream uses 7-bit values (high bit
always 0). Decoder rules:

| Byte range | Meaning |
| --- | --- |
| `0x00..0x6F` | Value byte. Completes the most recent pending command. |
| `0x70..0x73` | Command: NoteOn channel (byte-0x70). Next byte = note number (0 = NoteOff). |
| `0x74..0x77` | Command: Control Change channel (byte-0x74). Next byte = CC value. |
| `0x78..0x7B` | Command: Program Change channel (byte-0x78). Next byte = patch. |
| `0x7C` | Reserved / no-op. The firmware consumes the value byte but does nothing. |
| `0x7D` | Transport start — emit `0xFA`. |
| `0x7E` | Transport stop — emit `0xFC`. |
| `0x7F` | Clock tick — emit `0xF8`. |
| `0x80+` | NOT part of MI.OUT. Captured for diagnostics but the decoder ignores them. |

LSDJ-side effect commands that drive this protocol (placed in note/table cells):

- **Nxx** — sends a NoteOn absolute (N00 = NoteOff, N01–N6F = MIDI notes 1–112).
- **Qxx** — sends a NoteOn relative to the channel's current pitch.
- **Xxx** — sends a CC. (Arduinoboy supports several CC-encoding modes; the
  [ArduinoboyMaster](../packages/native/src/system/sameboy/roles/ArduinoboyMaster.cpp)
  decoder uses the simplest mapping `CC# = m`; refine when there's a use case.)
- **Yxx** — sends a Program/Patch change.

The decoder is unit-tested in
[packages/native/test/ArduinoboyMasterTests.cpp](../packages/native/test/ArduinoboyMasterTests.cpp)
(11 cases covering each protocol byte).

**Known gotcha — reaching functional MI.OUT mode.** The aboy MI.OUT SYNC value is
byte 7, past the model `SyncMode` enum (0–5). You can *write* byte 7 into the
working song (patch `0x3fbd` before `loadRom`) and LSDJ boots with it, but it does
**not** engage the MI.OUT protocol — LSDJ emits only idle `0x00`/`0xFF`, not the
`0x7D`/`0x7F`/note bytes (confirmed in `arduinoboy_master.test.ts`; the
UI-navigation approach also can't reach MI.OUT — the aboy ROM stops accepting
`A+Right` past KEYBD). So functional MI.OUT end-to-end remains future work: it
needs a savestate captured from LSDJ already *in* MI.OUT mode. Until then, MI.OUT
is verified via the decoder unit tests + the serial-out capture path (KEYBD mode
via `emu.drainSerial`), not via functional MI.OUT playback.
