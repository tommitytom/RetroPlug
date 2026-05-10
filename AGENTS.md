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
