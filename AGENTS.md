# Agent guide

The substantive docs live in two files; both are short and worth reading
before starting:

- [README.md](README.md) — RetroPlug2: what it is, build, headless tooling,
  layout, roadmap pointer.
- [dpfjs.md](dpfjs.md) — the DPF + lv_binding_js + React framework slice
  (architecture, parameter sync, JS API, hot reload, validation). This will
  eventually move out into its own template repo; treat it as portable.

Most of what an agent needs day-to-day is in those two. The rules below are
the parts that don't naturally fit either.

## Workflow rules

- Don't push to remotes or open PRs without an explicit ask. The user pushes
  their own work.
- Don't commit changes to `deps/lv_binding_js` or
  `deps/lv_binding_js/deps/txiki` from the parent repo without checking —
  the submodule pointers are managed deliberately.
- Don't `rm -rf build` to "fix" CMake — investigate first. The configured
  build dir is load-bearing for the development loop.
- Treat the embedded UI bundle as derived; never check in
  `build/ui/bundle.js` or `build/ui/bundle_data.c`.
- The typed RPC client at `build/ui/generated/PluginService.ts` is also
  derived (regenerated from `PluginRpcService`'s OpenRPC schema by
  `tools/gen-rpc-ts.js` whenever the service signatures change). Never
  commit it.
- Always build in parallel: pass `-j$(nproc)` (or `-j` followed by the core
  count) to `cmake --build`. The default is single-threaded and turns a
  full build into a multi-minute serial slog.
- Nothing has been released yet. Don't write migration code, version-gating,
  or backwards-compatibility shims for on-disk formats (project state, DPF
  state, kit-patch persistence, config schemas, etc.). When changing a
  serialized shape, just change it — no "fall back to old format" branches,
  no `version: 2` fields, no read-old / write-new. Saved projects from the
  pre-release period are expected to break.

## Known framework gotchas

These are non-obvious behaviours that have eaten time in prior debug
sessions. Search this section before assuming your code is wrong.

### `lv_binding_js` ignores `insertChildBefore` (always appends)

React reorders children at LVGL widget level by calling
`insertChildBefore`, but `deps/lv_binding_js/src/render/native/core/basic/comp.cpp:38`
**ignores the `beforeChild` argument and always appends**. Consequences:

- Swapping a component type at a stable React position (e.g. replacing
  one `<EmulatorTile>` in a row with `<Menu>`) leaves the new component
  at the END of the LVGL child list, not in its React source position.
  Visually the swap appears in the wrong slot.
- Mid-list inserts (adding a row to a grid) also land last regardless
  of position.

Two known workarounds, both already in the codebase:

- **Stable per-id wrapper Views**: wrap each swappable item in a
  fixed-key `<View>` whose position in the parent never changes. Only
  the wrapper's single child swaps — `appendChild` lands correctly
  when the parent has at most one existing child. Example:
  [ui/SystemGrid.tsx](ui/SystemGrid.tsx)'s `slot-${sys.id}` wrapper.
- **Re-key the parent on the visible set** to force a full unmount /
  remount: every child mounts fresh via `appendChild` in JSX order.
  Example: [ui/menu/Menu.tsx:329-339](ui/menu/Menu.tsx#L329-L339).

If you see a tile / row / menu rendering in a confusingly different
position than its React source suggests, this is almost certainly why.

### `cmake --build build --target retroplug` does NOT rebuild the standalone

The umbrella `retroplug` target builds the static plugin library and
runs `ui-regenerate` — but `bin/retroplug` (the standalone) is produced
by `retroplug-jack`. Building `--target retroplug` after a UI change
will regenerate `bundle.js` but leave `bin/retroplug` linked against
the previous bytecode. Symptom: screenshots show old behaviour even
though the bundle is fresh.

Use bare `cmake --build build -j$(nproc)` (no `--target`) when
verifying UI changes, or `--target retroplug-jack` for standalone-only.

## Verification loop for code changes

The headless tooling described in README.md's "Headless workflows" section
exists for agents to verify their own work without bothering the user. In
order of preference:

1. **DSP / behaviour change** — `make -C build cli-smoke` or run
   `retroplug-cli` with a custom script. Bypasses the plugin format
   entirely; tests the same code path that ends up in every wrapper.
2. **UI change** — `make -C build screenshot` (writes
   `/tmp/retroplug.png`); read the PNG via the Read tool. Drive input
   mid-run with `tools/standalone-key.sh` (keyboard) or
   `tools/standalone-mouse.sh` (mouse). JS-side `console.log/warn/error`
   calls surface as `[js:<level>] ...` lines on the standalone's stderr
   (`/tmp/retroplug-stdout.log` when launched via run-standalone.sh).
   Set `RETROPLUG_DEBUG_OVERLAY=1` in the env to render each tile's
   system id as a red overlay — useful for confirming visual position
   matches `systems[]` order.
3. **DPF wrapper / format change** — `make -C build validate` (runs
   `clap-validator` + `pluginval`). Catches ABI / state-restore /
   threading regressions in the format adapters.
4. **Pure C++ logic change** — `make -C build retroplug-tests &&
   build/test/retroplug-tests` (Catch2). Covers transport queues,
   `Project`, framebuffer.
5. **Audio-quality check on a render** — `make -C build reaper-analyze-smoke`
   (or `reaper-analyze-lsdj-sync`) stages the WAV into the
   reaper-mcp-server's projects dir; then ask the `reaper` MCP server for
   loudness/LUFS, frequency content, dynamics, stereo imaging. Use this to
   catch regressions that aren't "no audio produced" but "audio is wrong"
   (clipping, channel imbalance, DC offset, spectrum shift). The MCP
   server itself is installed in the devcontainer image at
   `/opt/reaper-mcp-server`; the projects dir defaults to
   `../resources/reaper/projects/` (override with `RETROPLUG_REAPER_DIR`,
   same convention as `RETROPLUG_RESOURCES_DIR`).
6. **VST3 plugin host check** — `make -C build reaper-mgb-smoke` renders
   [examples/reaper/mgb_smoke.rpp](examples/reaper/mgb_smoke.rpp)
   headlessly through real Reaper 7.x: instantiates retroplug.vst3, plays
   a C-major chord through mGB, writes `build/reaper-mgb-smoke.wav`.
   First end-to-end proof that the plugin works inside a DAW host (not
   just `retroplug-cli` which bypasses DPF). Headless plumbing lives in
   `tools/run-reaper-render.sh` (Xvfb + openbox + dummy jackd + EULA
   auto-dismiss). The .RPP is self-contained — the plugin chunk embeds
   the mGB ROM via getState() — and is regenerated with
   `make -C build reaper-mgb-author` when `examples/scripts/mgb_smoke.json`
   or [tools/reaper-mgb-author.lua](tools/reaper-mgb-author.lua) change.

## Reaper headless: env-var autoload

The plugin honours `RETROPLUG_AUTOLOAD_PROJECT=path/to/foo.rplg` at
construction: if set, the .rplg (pure PKZIP from `projectConfigToZip` —
no base64) is loaded as the initial project. Lets a host instantiate the
plugin with a preconfigured ROM without authoring the DPF state chunk
by hand. Used by `tools/run-reaper-author.sh` to bake mGB into the
fixture, and available for any new Reaper-driven test:

```sh
build/bin/retroplug-cli --script examples/scripts/<your>.json \
    --save-rplg build/<your>.rplg
RETROPLUG_AUTOLOAD_PROJECT=build/<your>.rplg \
    tools/run-reaper-render.sh your_project.rpp
```

Without the env var, the plugin starts empty (matches normal DAW
behaviour).

Trust but verify: an agent's claim that "tests pass" should be backed by an
actual exit-zero from one of these commands.

## Capturing the Game Boy screen from a script

`retroplug-cli` can dump per-system framebuffers to PNG. This is the
deterministic way to see what LSDj (or any other ROM) is actually showing
without booting the plugin or standalone UI.

Add a screenshot event to the script JSON:

```json
{ "at_ms": 15000, "screenshot": "post_boot", "system": 0 }
```

Or pass `--final-screenshot` to dump every system once at script end. Output
filenames: `<scriptStem>_<name>_sys<idx>.png` under `--screenshot-dir`
(defaults to the dir of `out_wav`, then cwd).

**Boot timing.** Two boot sequences sit between `at_ms: 0` and the LSDj song
screen:

1. SameBoy plays the Game Boy boot ROM (white screen + chime, ~1.5 s).
2. LSDj runs its own cartridge/SRAM self-test on first boot of a fresh ROM
   (visible as `CARTRIDGE TEST ROM...` then `SRAM...`). On the bundled
   `lsdj9_4_2.gb` this can take **12–15 s**.

Schedule any screenshot you expect to capture the LSDj song screen at
`at_ms` ≥ 15000, or pre-load a save state to skip the self-test. The example
`examples/scripts/lsdj_sync_smoke.json` lands at 15 s and 19 s for this
reason.

## Multi-instance / sync scripts

For debugging features that touch more than one system (serial link, MIDI
routing, LSDj link-cable sync), use the `systems: [...]` form:

```json
{
  "duration_ms": 20000,
  "systems": [
    { "rom": "../resources/roms/lsdj9_4_2.gb", "link_group": 1 },
    { "rom": "../resources/roms/lsdj9_4_2.gb", "link_group": 1 }
  ],
  "midi_routing": "SendToAll",
  "events": [
    { "at_ms": 15000, "screenshot": "boot", "system": 0 },
    { "at_ms": 15000, "screenshot": "boot", "system": 1 }
  ]
}
```

Same nonzero `link_group` puts instances into a shared `LinkGroup` (lockstep
serial-bit ferrying). `midi_routing` mirrors the plugin: `SendToAll`,
`FourChannelsPerInstance`, `OneChannelPerInstance`, `MidiChannelToInstance`.

Convenience targets:

- `make -C build cli-sync-smoke` — two-instance boot+screenshot smoke (no sync setup; proves the plumbing).
- `make -C build cli-lsdj-sync` — full LSDj link-cable sync demo (boots + configures SYNC=LSDJ + plays a one-note pattern; writes per-system WAVs).
- `make -C build cli-lsdj-sync-negative` — control test that omits the SYNC setup; pair with the positive demo when investigating sync.

## Chord input (`SELECT+arrow`, `A+RIGHT`)

LSDJ relies heavily on two-key chords: `SELECT+CURSOR` to change screen,
`A+CURSOR` to change a field value, etc. **Do not send both keys at the same
`at_ms`** — LSDJ frequently misses the chord. Use the `chord` event form,
which encodes the working timing (modifier held ~200 ms before the key):

```json
{ "at_ms": 17000, "chord": ["Select", "Up"],  "system": 0 }
{ "at_ms": 17600, "chord": ["A", "Right"],    "system": 0 }
```

Optional `stagger_ms` (default 200) and `hold_ms` (default 200) control the
gap between modifier and key, and how long the key is held. The form expands
into four `pressButton` calls internally — see `cli/Script.hpp`.

If you hand-roll chord events with `button`/`down` for some reason, give the
modifier a 200 ms head start before the key, and release in reverse order.
Same-`at_ms` chords are the single most common reason an LSDJ-driving script
silently does nothing.

## LSDJ screen navigation

The screen map (empirically verified — also see `Figure 1.2` on p11 of the
manual):

```
                PROJECT
                  │  SELECT+UP / +DOWN
                  ▼
SONG  ◄────────► CHAIN  ◄────────► PHRASE
        SELECT+RIGHT      SELECT+RIGHT
```

(`PROJECT` sits above the SONG/CHAIN/PHRASE row. `SELECT+LEFT` from SONG
enters LIVE mode — not what you usually want.) From SONG, the canonical path
to set sync mode is:

1. `chord: ["Select", "Up"]` — SONG → PROJECT (cursor lands on TEMPO).
2. `tap: "Down"` × 2 — move cursor to the SYNC field.
3. `chord: ["A", "Right"]` — cycle SYNC: `OFF → LSDJ → MIDI → …`. **One**
   chord with the new form lands on LSDJ; if you see LSDJ then `MIDI` /
   `KEYBD`, you've overshot.
4. `chord: ["Select", "Down"]` — PROJECT → SONG.

Cycle status by checking `SYNC LSDJ` in the PROJECT screen, or the
`PRELISTEN` row beside it: `ON` means SYNC OFF, `N/A` means a sync mode is
selected. The `LEAD` / `SYNC` / `WAIT` indicators in the right margin of the
SONG screen are the runtime confirmation that link-cable sync is actually
flowing once START is pressed (manual §5.1.2 / §5.1.3).

## LSDJ link-cable sync — full recipe

The canonical end-to-end test is at
[examples/scripts/lsdj_sync_pattern.json](examples/scripts/lsdj_sync_pattern.json)
(also `make -C build cli-lsdj-sync`). It:

1. Boots two LSDJ instances on the same `link_group: 1`.
2. Navigates each to PROJECT and sets SYNC=LSDJ.
3. Builds a minimal song: chain 00 → phrase 00 → C-2 note (the smallest
   thing that produces audio).
4. Presses START on instance 0.
5. Screenshots both instances at +1 s, +4 s, +8 s, +11 s of playback.
6. With `--per-system-wav`, writes one WAV per instance for sync analysis.

Expected outcome:

- After step 2: both PROJECT screens show `SYNC LSDJ`, `PRELISTEN N/A`.
- After step 4: instance 0 shows `LEAD` in the SONG-screen right margin;
  instance 1 shows `SYNC`. The labels persist for the duration of playback.
- Visual lockstep: every screenshot pair (sys0/sys1 at the same `at_ms`)
  is identical in cursor position and right-margin indicators.
- Audio: the per-system WAVs are byte-identical until the moment START is
  pressed, then diverge only by a small phase offset (the link-cable sync
  signal has ~ms latency). The follower's audio is the same content as the
  leader, shifted by N samples.

If sync is genuinely broken, the symptom in this test will be one of:

- `LEAD` / `SYNC` indicators don't appear (LinkGroup serial-bit ferrying not
  working — see `src/system/sameboy/LinkGroup.cpp` and the `serialStart` /
  `serialEnd` callbacks in `SameBoySystem.cpp`).
- Indicators appear but the per-system WAVs diverge in *content* (not just
  phase) — same pattern would be playing at different song positions.
- Indicators flicker or only appear on one side — handshake race.

Use [examples/scripts/lsdj_sync_negative.json](examples/scripts/lsdj_sync_negative.json)
as the control: same flow, but without setting SYNC=LSDJ. The `LEAD`/`SYNC`
labels must NOT appear there. If they do, your positive test isn't measuring
what you think.

## Cross-correlating per-system audio

`--per-system-wav` writes one WAV per instance alongside the mix WAV.
Quick comparison via the `tools/.venv` Python (already installed for the
manual indexer):

```python
import wave, struct
def load(p):
    w = wave.open(p, 'rb'); n = w.getnframes()
    raw = w.readframes(n); w.close()
    s = struct.unpack('<' + 'h' * (n * 2), raw)
    return [(s[2*i] + s[2*i+1]) / 2 for i in range(n)]  # mono mix
a, b = load('/tmp/lsdj-sync-pattern_sys0.wav'), load('/tmp/lsdj-sync-pattern_sys1.wav')
same = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), len(a))
print(f'leading identical samples: {same} ({same/44100:.3f}s)')
```

Identical leading samples = number of audio samples both instances rendered
*exactly* the same content. A clean LSDJ sync test should land somewhere
around the `at_ms` of the first `Start` press (everything before that is
lockstep boot + setup; after that, link-cable phase offset starts the
divergence).

`cmp` works for a quick byte-level check too: `cmp sys0.wav sys1.wav` —
reports the offset of the first differing byte.

## Pitfalls cheat-sheet

- **Same-`at_ms` chord** — silently dropped by LSDJ. Use the `chord` form.
- **Screenshot before ~15 s** — captures the GB boot ROM or LSDJ's
  cartridge self-test, not the song screen. The first usable screenshot is
  around `at_ms: 15000` on a fresh ROM.
- **Cursor-moving keys auto-repeat** — LSDJ's default `KEY DEL/REPEAT 7/2`
  means holding for >7 frames (~117 ms) starts auto-repeating. The `tap`
  form's default `hold_ms: 50` is below that threshold; longer holds will
  fire multiple cursor moves.
- **The first `A+CURSOR` in PROJECT sometimes no-ops** when using hand-rolled
  chord events with same-`at_ms` timing — using the `chord` form removes the
  ambiguity and each chord cycles exactly once.
- **Mix WAV alone can't prove sync** — two desynchronised instances
  mixed together still produce a WAV, and the peaks may even look healthy.
  Use `--per-system-wav` and compare files when you need certainty.
- **`SELECT+LEFT` from SONG enters LIVE mode**, not a "previous screen" —
  the screen grid wraps. Stick to `SELECT+UP/DOWN/RIGHT` for vanilla nav.
- **`A` in PROJECT on items like `HELP` or `LOAD/SAVE SONG` triggers them**
  rather than cycling a value. Always confirm the cursor is on the field
  you want before pressing `A+CURSOR`.

## LSDj manual lookup

The LSDj 9.2.6 manual is indexed for keyword + semantic search:

```
tools/lsdj-manual-setup.sh                # one-time: venv + deps + index
tools/lsdj-search "midi sync mode"
tools/lsdj-search --mode vec "how do two units stay in time"
tools/lsdj-search --show-images "PROJECT screen"
```

The setup script creates `tools/.venv`, installs `pymupdf`, `fastembed`,
`sqlite-vec`, `numpy`, then runs `tools/lsdj-manual.py index` to produce:

- `../resources/manuals/lsdj_manual.md` — readable markdown (Read + grep
  fallback if the search index is missing)
- `../resources/manuals/lsdj_manual_images/` — page images extracted from the
  PDF (LSDj UI screens and diagrams). `--show-images` returns paths the agent
  can `Read` directly.
- `../resources/manuals/lsdj_index.db` — SQLite with FTS5 BM25 + sqlite-vec
  cosine, fused via reciprocal-rank fusion in hybrid (default) mode.

These artifacts live in a sibling `resources/` directory outside the repo
(default `../resources/` relative to the repo root). Override with
`RETROPLUG_RESOURCES_DIR=/some/path` if your layout differs.

Pick `--mode fts` for exact LSDj terminology ("FX command", "groove",
"R command"), `--mode vec` for paraphrased / vague questions, default
`hybrid` when in doubt.

## LSDJ Arduinoboy build (aboy)

The sibling [../resources/roms/](../resources/roms/) directory (outside the
repo) ships two LSDJ ROMs plus a Nanoloop GBA ROM. The two `LsdjSyncMode`
families need different ROMs:

| ROM | Title @0x134 | Supported `lsdj_sync_mode` values |
| --- | --- | --- |
| `lsdj9_4_2.gb` | `LSDj-v9.4.2` (stock) | `Off`, `MidiSync`, `MidiMap`, `KeyboardMidi`, `MidiPassthrough` |
| `lsdj9_3_3-arduinoboy.gb` | `LSDj-v9.3.3aboy` | All of the above plus `MidiSyncArduinoboy` and `ArduinoboyMaster` |

The sniffer ([src/system/sameboy/RomSniffer.cpp](src/system/sameboy/RomSniffer.cpp))
treats both ROMs as `RomKind::Lsdj` (any title starting with `LSDj`). The role's
`onAttach` logs `build=stock` vs `build=arduinoboy` based on whether the title
contains `aboy` — check the stderr line `[RetroPlug] LSDJ sync role attached
(mode=…, build=…)` to confirm which build is loaded.

### PROJECT-screen SYNC cycle (aboy v9.3.3)

Empirically mapped via [examples/scripts/lsdj_aboy_sync_discovery.json](examples/scripts/lsdj_aboy_sync_discovery.json)
(see `make -C build cli-lsdj-aboy-sync-discovery` — each A+Right with a
screenshot after). Starting from `OFF`, each `chord: ["A", "Right"]` (on
the SYNC field in PROJECT) cycles forward:

| Cycle # | SYNC value | Extra row visible |
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
aboy-specific. PRELISTEN row reads `ON` for OFF / LSDJ / KEYBD / MI.MAP /
MI.OUT and `N/A` for MIDI / ANA.IN / AN.OUT — so PRELISTEN is NOT a reliable
"is a sync mode selected" indicator on the aboy build. Read the SYNC field
text directly.

### Master mode (MI.OUT) verification

The CLI captures LSDJ's serial-out byte stream when a role opts into it via
`RomRole::wantsSerialOut()`. The `LsdjSyncRole` enables this when its config
is `ArduinoboyMaster`. The capture buffers are populated in memory on every
run that exercises that mode, but the on-disk artifacts are **opt-in**: pass
`--event-logs DIR` to `retroplug-cli` and the two files below land under
`DIR` (omit the flag and nothing is written, even when bytes are captured).

- `<scriptStem>_serial_sys<N>.txt` — every completed serial-out byte, one per
  line as `<absSample> 0x<hex>`. **Ground truth: whatever LSDJ actually wrote
  to its SB register.** Inspect first.
- `<scriptStem>_midi_sys<N>.txt` — the `ArduinoboyMaster` decoder's output
  (one MIDI event per line, raw bytes hex). Empty when the decoder doesn't
  recognize any of the raw bytes — that's expected to evolve as more of the
  protocol gets implemented.

The `cli-lsdj-arduinoboy-master` make target passes `--event-logs` pointing
at `${CMAKE_BINARY_DIR}/lsdj-arduinoboy-master`, so the artifacts always
appear there for that target.

### Synthetic Arduinoboy clock (subtle but load-bearing)

LSDJ in MI.OUT (and KEYBD) uses the GB serial port in **external-clock** mode
(`SC=0x80`). Real Arduinoboy hardware provides the clock pulses that shift the
GB's SB register. SameBoy by default does nothing here — the GB just sits
waiting. To make this verifiable headlessly,
[src/system/sameboy/SameBoySystem.cpp](src/system/sameboy/SameBoySystem.cpp)
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

This runs ~5.5 kHz faster than real Arduinoboy (which clocks at GB hardware
serial rate, ~8 kHz) but the byte protocol is rate-independent so the
captured bytes are correct.

**Pitfall:** the bit-start callback gives the outgoing bit as its `bit_received`
parameter; this is the bit being SENT (the peer receives it). Do NOT read
`GB_serial_get_data_bit` in the bit-end callback — by then SB has shifted and
the MSB is the next bit to send, giving every captured byte a one-bit offset.

### Arduinoboy MI.OUT byte protocol

Reference: [Mode_LSDJ_Midiout.ino](https://github.com/trash80/Arduinoboy/blob/master/Arduinoboy/Mode_LSDJ_Midiout.ino)
in the trash80/Arduinoboy firmware. (Don't confuse with `Mode_LSDJ_MasterSync.ino`
— that's a simpler "send one row byte + clock ticks" mode used for sync
slaves driving LSDJ; MI.OUT is the per-channel-note protocol.)

The MI.OUT byte stream uses 7-bit values (high bit always 0). Decoder rules:

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
| `0x80+` | NOT part of MI.OUT. Captured in `_serial_sys<N>.txt` for diagnostics but the decoder ignores them. |

LSDJ-side effect commands that drive this protocol (placed in note/table cells
in the LSDJ song editor):

- **Nxx** — sends a NoteOn absolute (N00 = NoteOff, N01–N6F = MIDI notes 1–112).
- **Qxx** — sends a NoteOn relative to the channel's current pitch.
- **Xxx** — sends a CC. (Arduinoboy hardware supports several CC-encoding modes:
  high-nibble CC# + low-nibble value, single CC scaled 0x00..0x6F, seven CCs.
  The [ArduinoboyMaster](src/system/sameboy/roles/ArduinoboyMaster.cpp)
  decoder uses the simplest mapping `CC# = m` for clarity; refine when there's
  a use case.)
- **Yxx** — sends a Program/Patch change.

The decoder is unit-tested in
[test/ArduinoboyMasterTests.cpp](test/ArduinoboyMasterTests.cpp) (11 cases
covering each protocol byte). **End-to-end with LSDJ is NOT yet headlessly
verified** because the master demo can't reliably navigate the aboy ROM to
MI.OUT (see "Known gotcha" below). The decoder matches the firmware spec,
which is the closest verification path available.

### Known gotcha: cycling SYNC past position 3 (KEYBD) via script

Each `A+Right` chord in the SYNC field reliably cycles 0→1, 1→2, 2→3
(OFF → LSDJ → MIDI → KEYBD), but the 4th and subsequent chord events appear
to be dropped in scripts that fire the chord pattern straight through (gaps
of 1000–1500 ms). The same chord sequence DOES advance through all positions
in [lsdj_aboy_sync_discovery.json](examples/scripts/lsdj_aboy_sync_discovery.json)
— the only obvious difference is intermediate screenshots between chords.
The current [lsdj_arduinoboy_master.json](examples/scripts/lsdj_arduinoboy_master.json)
documents this limitation and lands on KEYBD; a follow-up agent should either:
1. Reproduce the discovery script's exact pattern (chord + screenshot + 1400 ms
   gap) inside the master script.
2. Ship a pre-configured savestate fixture where LSDJ is already in MI.OUT,
   bypassing UI navigation entirely.

Until then, **MI.OUT end-to-end is verified via unit tests + the discovery
script + the serial-out diagnostic log**, not via the master demo's automated
playback.
