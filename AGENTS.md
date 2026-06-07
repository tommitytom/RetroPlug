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

1. **DSP / behaviour change** — write a TypeScript test and run
   `make -C build cli-ts-test` (TAP output, nonzero exit on failure). This is
   the canonical headless path: it bypasses the plugin format entirely, tests
   the same code path that ends up in every wrapper, and can **read emulator
   state and branch on it** — memory regions, CPU registers (SameBoy/NES/GBA),
   instruction stepping, audio/frame capture, MIDI/serial-out capture, host
   transport, link groups, kit patching, and LSDj sav authoring (see
   "Authoring LSDj state in TypeScript" below). `make -C build cli-smoke` is a
   quick one-liner that runs the mGB chord smoke (`test/ts/gb/mgb.test.ts`).
   The harness embeds the txiki/QuickJS runtime in
   `retroplug-cli --test`. It also exposes Mesen's NES debugger headlessly —
   **profiling** (`emu.beginProfile`/`readProfile` + cc65 `.dbg` symbols via
   `loadLabels`), disassembly, trace, call stack, and conditional
   breakpoints/watchpoints/stepping — for finding bottlenecks / debugging the
   evermidi NES ROM. See [test/ts/README.md](test/ts/README.md).
2. **UI change** — prefer a **headless UI test**: write a
   `test/ts/ui/*.test.ts` and run `make -C build ui-ts-test` (TAP, no Xvfb).
   It boots the REAL React UI bundle on a software LVGL display
   (`retroplug-ui-test` runner + [test/ui/UiTestHarness.cpp](test/ui/UiTestHarness.cpp))
   and exposes a `ui` global ([test/harness/ui.ts](test/harness/ui.ts)):
   `boot` / `loadRom` / `pump`, `snapshot`/`snapshotPng`, `findByText` /
   `findByTextContaining` / `findByTestId` / `findFirstByType` / `countByType`
   (→ `WidgetInfo`), and input driving `tapKey` / `clickAt`. Queries walk the
   live LVGL tree; `testId` comes from a `globalThis.__rp_tagTestId` ref hook
   (inert in production). Runs in ~0.1–0.5 s/file, asserts on structure not just
   pixels. See [test/ts/ui/](test/ts/ui/) for examples (chrome, tile, menu).
   For an eyeball check of the live standalone, `make -C build screenshot`
   (writes `/tmp/retroplug.png`); read the PNG via the Read tool. Drive input
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
   (runs `test/ts/gb/mgb.test.ts`, which writes `/tmp/cli-smoke.wav`) or
   `reaper-analyze-lsdj-sync` (runs `test/ts/gb/lsdj/sync_pattern.test.ts`,
   which writes the per-system WAVs via `emu.writeWav`) stages the WAV into the
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
   `make -C build reaper-mgb-author` when [test/ts/gb/mgb.test.ts](test/ts/gb/mgb.test.ts)
   (which emits `/tmp/mgb_smoke_author.rplg` via `emu.saveRplg`) or
   [tools/reaper-mgb-author.lua](tools/reaper-mgb-author.lua) change.
7. **Arduinoboy startup-sync latency** —
   `make -C build reaper-lsdj-arduinoboy-metro` renders
   [examples/reaper/lsdj_arduinoboy_metro.rpp](examples/reaper/lsdj_arduinoboy_metro.rpp)
   — a 2-track project with LSDj (panned hard-L, configured for
   `LsdjSyncMode::MidiSyncArduinoboy` via the autoload .rplg) and a
   ReaSynth click track (panned hard-R, one note per quarter beat at
   120 BPM). Then runs
   [tools/reaper-timing-analyze.py](tools/reaper-timing-analyze.py)
   which detects the first onset in each channel and reports the
   offset between host transport start (click[0]) and LSDj's first
   audible sample. Pass/fail threshold: ±50 ms (one 24 PPQN tick at
   120 BPM ≈ 21 ms, plus envelope attack + one plugin block at
   1024/44100 ≈ 23 ms). Surfaces drift in `PpqUtil::eachTick()` and
   in `LsdjSyncRole`'s startup byte sequence (0xFA + first 0xF8).
   Per-beat sync drift is *not* covered — default LSDj instrument 00
   sustains the first row's note across the whole phrase, masking
   subsequent retriggers; envelope-edit nav in the bootstrap script
   would be needed to surface those. Regenerate the fixture with
   `make -C build reaper-lsdj-arduinoboy-author` when
   [test/ts/gb/lsdj/lsdj_arduinoboy_metro.test.ts](test/ts/gb/lsdj/lsdj_arduinoboy_metro.test.ts)
   (which emits `/tmp/lsdj_arduinoboy_metro_author.rplg` via `emu.saveRplg`)
   or [tools/reaper-lsdj-arduinoboy-author.lua](tools/reaper-lsdj-arduinoboy-author.lua)
   change.

## Reaper headless: env-var autoload

The plugin honours `RETROPLUG_AUTOLOAD_PROJECT=path/to/foo.rplg` at
construction: if set, the .rplg (pure PKZIP from `projectConfigToZip` —
no base64) is loaded as the initial project. Lets a host instantiate the
plugin with a preconfigured ROM without authoring the DPF state chunk
by hand. Used by `tools/run-reaper-author.sh` to bake the configured state
into the fixture, and available for any new Reaper-driven test.

The canonical way to produce a `.rplg` is now a TS harness test: author the
state (sav + roles), then `emu.saveRplg("/tmp/foo.rplg")`. Then:

```sh
RETROPLUG_AUTOLOAD_PROJECT=/tmp/foo.rplg \
    tools/run-reaper-render.sh your_project.rpp
```

`tools/run-reaper-author.sh OUTPUT.rpp RENDER_DIR AUTHOR.lua FIXTURE.rplg`
takes a pre-built `.rplg` directly. (`retroplug-cli --save-rplg` still works as
the lower-level mechanism, and a legacy `.json` fixture arg is still accepted.)
Without the env var, the plugin starts empty (matches normal DAW behaviour).

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
`at_ms` ≥ 15000 **on a fresh ROM**. The far better option — and what the TS
tests do — is to boot from an authored sav: `emu.savFromJson(...)` produces a
valid SRAM image, so LSDj skips the self-test entirely and reaches the song
screen in ~3–6 s. See "Authoring LSDj state in TypeScript" below.

## Authoring LSDj state in TypeScript (canonical)

The LSDj-driving tests used to navigate the UI with fragile `SELECT/A`+arrow
chords in JSON `--script` files to build song/sync state. That state is just
bytes in the `.sav`, so tests now **author it directly** with the sav codec and
boot LSDj straight into it — fast (a valid sav skips the 12–15 s self-test) and
robust (no timing-sensitive navigation). Every former JSON test now lives under
[test/ts/](test/ts/) as a `*.test.ts` (run all with `make -C build cli-ts-test`,
or one with `make -C build cli-ts-test-<slug>` where `<slug>` is the path under
`test/ts` with `/`→`-`).

The pattern (see [test/ts/gb/lsdj/sync_pattern.test.ts](test/ts/gb/lsdj/sync_pattern.test.ts)
or [lsdj_arduinoboy_metro.test.ts](test/ts/gb/lsdj/lsdj_arduinoboy_metro.test.ts)):

```ts
const fill = <T>(n, f) => Array.from({ length: n }, f);
const sav = emu.savFromJson(JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },         // PROJECT-screen SYNC (None/Lsdj/Midi/Keyboard/AnalogIn/AnalogOut)
    rows:    fill(256, () => ({ chains: [null,null,null,null] })),  // rows[0].chains[0]=0 → chain 00
    chains:  /* chains[0] = { phrases:[0,...], transpositions:[...] } */,
    phrases: /* phrases[0] = { notes:[1,...], instruments:[0,...], commands:[...], commandValues:[...] } */,
    instruments: /* instruments[0] = { type:"pulse", ... } */,
  },
}));
const sys = emu.loadRom(rom, sav, /*lsdjSyncMode*/ "MidiSyncArduinoboy", /*linkGroup*/ 1);
```

Fixed-size arrays must be built in full with `fill(...)` — `DefaultIfMissing`
fills missing struct fields but not missing array elements.

`emu.loadRom(path, sav?, lsdjSyncMode?, linkGroup?)`:
- `sav` — an `ArrayBuffer` from `savFromJson` (or `readMemory(sys, Mem.Sram)`).
- `lsdjSyncMode` — the `LsdjSyncRole` config: `"MidiSync"`, `"MidiMap"`,
  `"KeyboardMidi"`, `"MidiPassthrough"`, `"MidiSyncArduinoboy"`,
  `"ArduinoboyMaster"`, … (distinct from the in-sav PROJECT `syncMode`).
- `linkGroup` — same nonzero value on two systems puts them in a shared
  `LinkGroup` (lockstep serial-bit ferrying) for LSDj link-cable sync.

Other harness bindings these tests use (see [test/harness/index.ts](test/harness/index.ts)):
`setTransport(bool)` / `setBpm(n)` (simulated host transport → the role's MIDI
clock), `drainMidi(sys)` / `drainSerial(sys)` (role MIDI-out / GB serial-out
capture), `runMsPerSystem(ms)` (per-system audio — proves link sync), `writeWav`
(dump audio for the reaper MCP), `saveRplg` (snapshot → `.rplg` for the Reaper
DAW fixtures), `patchKit(sys, slot, name, samples)` (compile + queue a kit).

## Driving the LSDj UI (only when authoring can't)

Authoring savs covers song/sync/instrument state. If a test genuinely needs to
drive the live UI (e.g. exercising a menu interaction), the harness exposes
`emu.chord(sys, buttons, opts?)` and `emu.tap(sys, button, holdMs?)` (see
[test/harness/index.ts](test/harness/index.ts); `gb/smoke.test.ts` uses them).

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
The `retroplug-cli --script` JSON runner (with the `chord`/`tap` event forms in
`cli/Script.hpp`) still exists for ad-hoc exploration, but it has no committed
example scripts — author savs in TS instead.

## LSDJ link-cable sync

Covered by three TS tests under [test/ts/gb/lsdj/](test/ts/gb/lsdj/):

- [sync_pattern.test.ts](test/ts/gb/lsdj/sync_pattern.test.ts) — positive: two
  instances on the same `linkGroup`, both authored SYNC=LSDJ, START on the
  leader. Verifies sync via **per-system audio** (`emu.runMsPerSystem`): the
  follower produces audio (and its RMS tracks the leader's) only because it
  synced. Also writes `/tmp/lsdj-sync-pattern_sys{0,1}.wav` for the reaper MCP.
- [sync_negative.test.ts](test/ts/gb/lsdj/sync_negative.test.ts) — control:
  same setup with SYNC=None. The follower stays **silent** (never starts). If it
  ever produces audio, the positive test isn't measuring real sync.
- [sync_smoke.test.ts](test/ts/gb/lsdj/sync_smoke.test.ts) — two-instance boot +
  audio plumbing.

If link sync is genuinely broken, the follower's per-system RMS stays at 0 in
the positive test — look at `src/system/sameboy/LinkGroup.cpp` and the
`serialStart` / `serialEnd` callbacks in `SameBoySystem.cpp`. `runMsPerSystem`
isolates each instance's audio (the canonical way to tell synced playback from a
healthy-looking mix of two desynced instances).

## Pitfalls cheat-sheet

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

## LSDj manual lookup

Every English LSDj manual (1.0b → 9.2.6) plus the upstream `CHANGELOG.txt`
is indexed for keyword + semantic search:

```
tools/lsdj-manual-setup.sh                # one-time: venv + deps + index
tools/lsdj-search "midi sync mode"
tools/lsdj-search --mode vec "how do two units stay in time"
tools/lsdj-search --show-images "PROJECT screen"
tools/lsdj-search --lsdj-version 6.0.0 "midi sync"   # docs relevant to v6.0.0
tools/lsdj-search --only-changelog "noise table"     # changelog-only
tools/lsdj-manual.py versions             # list every indexed source
```

`--lsdj-version <ver>` picks the **most recent manual whose version is ≤
<ver>** — for LSDj 9.4.2 that's `LSDj_9_2_6.pdf` (the newest English
manual), for 6.0.0 it's `LSDj_5_8_4.pdf`. The changelog is always
included alongside (suppress with `--no-changelog`).

The setup script creates `tools/.venv`, installs `pymupdf`, `fastembed`,
`sqlite-vec`, `numpy`, then runs `tools/lsdj-manual.py index` to produce:

- `../resources/manuals/lsdj_manual.md` — readable markdown built from the
  highest-version manual only (Read + grep fallback when the search index
  is missing).
- `../resources/manuals/lsdj_manual_images/<ver>/` — per-version PDF page
  images. `--show-images` returns paths the agent can `Read` directly.
- `../resources/manuals/lsdj_index.db` — SQLite with FTS5 BM25 + sqlite-vec
  cosine, fused via reciprocal-rank fusion in hybrid (default) mode. Schema
  has a `sources` table (one row per indexed PDF + one for the changelog)
  and a `chunks` table that references it.
- `../resources/manuals/lsdj_embed_cache.db` — sha256(chunk_text) →
  embedding cache. Re-running `index` after a no-op manual change is fast
  (no re-embedding); changes only re-embed affected chunks.

To populate the full archive (~35 PDFs + CHANGELOG.txt + ~550 ROM ZIPs):

```
python3 ../resources/download_lsdj.py                           # everything
python3 ../resources/download_lsdj.py --no-roms --dry-run       # preview
python3 ../resources/download_lsdj.py --variant stable          # subset
```

The downloader is stdlib-only (no venv) and auto-invokes
`tools/lsdj-manual.py index` after it finishes (skip with `--no-index`).
Japanese / French manual variants are deliberately excluded — the search
index is English-only. Downloads are idempotent: re-running skips files
already on disk unless `--force` is passed.

These artifacts live in a sibling `resources/` directory outside the repo
(default `../resources/` relative to the repo root). Override with
`RETROPLUG_RESOURCES_DIR=/some/path` if your layout differs.

Pick `--mode fts` for exact LSDj terminology ("FX command", "groove",
"R command"), `--mode vec` for paraphrased / vague questions, default
`hybrid` when in doubt.

## LSDJ Arduinoboy build (aboy)

All LSDj ROMs live under [../resources/roms/lsdj/](../resources/roms/lsdj/)
(outside the repo). Two canonical builds are required for the headless test
matrix; the two `LsdjSyncMode` families need different ROMs:

| ROM | Title @0x134 | Supported `lsdj_sync_mode` values |
| --- | --- | --- |
| `lsdj/lsdj9_4_2.gb` | `LSDj-v9.4.2` (stock) | `Off`, `MidiSync`, `MidiMap`, `KeyboardMidi`, `MidiPassthrough` |
| `lsdj/lsdj9_3_3-arduinoboy.gb` | `LSDj-v9.3.3aboy` | All of the above plus `MidiSyncArduinoboy` and `ArduinoboyMaster` |

Running `python3 ../resources/download_lsdj.py` (see §"LSDj manual lookup")
populates the full archive into the same directory — stable releases as
`lsdj<ver>.gb`, arduinoboy variants as `lsdj<ver>-arduinoboy.gb`, develop
snapshots as `lsdj<ver>-develop.gb`. Non-LSDj ROMs (e.g. Nanoloop GBA, mGB,
n8-midi) live one level up at `../resources/roms/`.

The sniffer ([src/system/sameboy/RomSniffer.cpp](src/system/sameboy/RomSniffer.cpp))
treats both ROMs as `RomKind::Lsdj` (any title starting with `LSDj`). The role's
`onAttach` logs `build=stock` vs `build=arduinoboy` based on whether the title
contains `aboy` — check the stderr line `[RetroPlug] LSDJ sync role attached
(mode=…, build=…)` to confirm which build is loaded.

### PROJECT-screen SYNC cycle (aboy v9.3.3)

The on-screen SYNC value is the working-song byte at `0x3fbd`. The model
`SyncMode` enum (`src/lsdj/model/Types.hpp`) authors values 0–5 directly via
`settings.syncMode` — [test/ts/gb/lsdj/sync_modes.test.ts](test/ts/gb/lsdj/sync_modes.test.ts)
authors each and asserts the byte. The aboy-only MI.MAP / MI.OUT (6 / 7) are
past the model enum (see "Master mode" below). The full cycle order:

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
aboy-specific. PRELISTEN row reads `ON` for OFF / LSDJ / KEYBD / MI.MAP /
MI.OUT and `N/A` for MIDI / ANA.IN / AN.OUT — so PRELISTEN is NOT a reliable
"is a sync mode selected" indicator on the aboy build. Read the SYNC field
text (or the `0x3fbd` byte) directly.

### Master mode (MI.OUT) verification

A role opts into serial-out capture via `RomRole::wantsSerialOut()`;
`LsdjSyncRole` enables it when its config is `ArduinoboyMaster`. From a TS test,
drain the captured bytes with `emu.drainSerial(sys)` (raw GB serial-out — ground
truth, whatever LSDJ wrote to its SB register) and `emu.drainMidi(sys)` (the
`ArduinoboyMaster` decoder's MIDI output). See
[test/ts/gb/lsdj/arduinoboy_master.test.ts](test/ts/gb/lsdj/arduinoboy_master.test.ts),
which authors SYNC=KEYBD + the `ArduinoboyMaster` role, presses START, and
asserts thousands of captured bytes (the synthetic-clock + capture path).
(`retroplug-cli --event-logs DIR` still writes `<stem>_serial_sys<N>.txt` /
`<stem>_midi_sys<N>.txt` for the `--script` runner, if you need on-disk logs.)

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
covering each protocol byte). **Functional MI.OUT end-to-end with LSDJ is NOT
yet verified** (see "Known gotcha" below). The decoder matches the firmware
spec, which is the closest verification path available.

### Known gotcha: reaching functional MI.OUT mode

The aboy MI.OUT SYNC value is byte 7 — past the model `SyncMode` enum (0–5).
You can *write* byte 7 into the working song (patch `0x3fbd` in the sav
ArrayBuffer before `loadRom`), and LSDJ boots with it, but it does **not** engage
the MI.OUT protocol: with byte 7 forced, LSDJ emits only idle `0x00`/`0xFF` on
the serial port, not the `0x7D`/`0x7F`/note protocol bytes. This was confirmed
in [test/ts/gb/lsdj/arduinoboy_master.test.ts](test/ts/gb/lsdj/arduinoboy_master.test.ts)
(the probe is documented in its header). The original UI-navigation approach also
couldn't reach MI.OUT (the aboy ROM stops accepting `A+Right` past KEYBD).

So MI.OUT end-to-end remains future work: it needs a savestate fixture captured
from LSDJ already *in* MI.OUT mode (e.g. a real-hardware or fully-emulated
session), not just the SYNC byte set. Until then, **MI.OUT is verified via the
decoder unit tests + the serial-out capture path** (arduinoboy_master.test.ts
asserts thousands of captured bytes in KEYBD mode via `emu.drainSerial`), not
via a functional MI.OUT playback.
