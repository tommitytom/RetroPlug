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
- Always build in parallel: pass `-j$(nproc)` (or `-j` followed by the core
  count) to `cmake --build`. The default is single-threaded and turns a
  full build into a multi-minute serial slog.

## Verification loop for code changes

The headless tooling described in README.md's "Headless workflows" section
exists for agents to verify their own work without bothering the user. In
order of preference:

1. **DSP / behaviour change** — `make -C build cli-smoke` or run
   `retroplug-cli` with a custom script. Bypasses the plugin format
   entirely; tests the same code path that ends up in every wrapper.
2. **UI change** — `make -C build screenshot` (writes
   `/tmp/retroplug.png`); read the PNG via the Read tool. Combine with
   `tools/standalone-key.sh` to drive input mid-run.
3. **DPF wrapper / format change** — `make -C build validate` (runs
   `clap-validator` + `pluginval`). Catches ABI / state-restore /
   threading regressions in the format adapters.
4. **Pure C++ logic change** — `make -C build retroplug-tests &&
   build/test/retroplug-tests` (Catch2). Covers transport queues,
   `Project`, framebuffer.

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
    { "rom": "resources/roms/lsdj9_4_2.gb", "link_group": 1 },
    { "rom": "resources/roms/lsdj9_4_2.gb", "link_group": 1 }
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

- `resources/manuals/lsdj_manual.md` — readable markdown (Read + grep
  fallback if the search index is missing)
- `resources/manuals/lsdj_manual_images/` — page images extracted from the
  PDF (LSDj UI screens and diagrams). `--show-images` returns paths the agent
  can `Read` directly.
- `resources/manuals/lsdj_index.db` — SQLite with FTS5 BM25 + sqlite-vec
  cosine, fused via reciprocal-rank fusion in hybrid (default) mode.

Pick `--mode fts` for exact LSDj terminology ("FX command", "groove",
"R command"), `--mode vec` for paraphrased / vague questions, default
`hybrid` when in doubt.
