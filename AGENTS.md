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
routing, LSDj sync once Step 08 lands), use the `systems: [...]` form:

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

`make -C build cli-sync-smoke` runs the bundled two-instance smoke and dumps
PNGs into `build/`.

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
