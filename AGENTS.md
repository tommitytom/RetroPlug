# Agent guide

This file holds rules and pointers only: how something works goes in the [spec/](spec/README.md) doc that owns the
topic, and a line here links to it. Keep it under 200 lines; a new rule cuts or moves one.

## Start here

- [README.md](README.md): what RetroPlug is, how to build, the project layout.
- [spec/README.md](spec/README.md): the architecture, one doc per concern. The shell is `packages/native/` (C++) and
  `packages/retroplug/` (TS and the UI); every other package is listed in spec/00's
  [Package layout](spec/00-overview.md#package-layout), and how packages work is spec/06's
  [The packages](spec/06-build-test.md#the-packages).
- [docs/lsdj.md](docs/lsdj.md): the LSDj domain reference, and how to test LSDj sync, DAW timing and audio quality
  headlessly.
- What is left to do is [spec/07-remaining-work.md](spec/07-remaining-work.md); the hosts and harnesses run by hand
  (Reaper, Renoise, the SDL legs, `retroplug-cli test`) are [spec/14](spec/14-hosts-harnesses.md).

## Workflow rules

- **Don't push to remotes or open PRs without an explicit ask.** The user pushes their own work.
- **Commits carry no `Co-Authored-By` trailer and no attribution of any kind.**
- **When a commit finishes work listed in spec/07, it deletes that item there**: its checklist line and its detail
  entry, never a tick left in place ([spec/07](spec/07-remaining-work.md)).
- **Clone `--recursive`** (`deps/dpf.js` is a nested submodule), and after a fresh checkout also run `pnpm install` in
  `deps/dpf.js/deps/lv_binding_js`, or the UI bundle cannot resolve `react`
  ([the dpf.js seam](spec/06-build-test.md#the-dpfjs-seam)). Don't bump a submodule pointer (in either repo) without
  checking: they are managed deliberately.
- **`deps/sameboy` is dirty after a configure, and that is EXPECTED**: the configure applies RetroPlug's tracked
  patches. Never reset / stash / commit it, and never bump its pointer to absorb it
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **Every edit to the vendored `deps/mesen` carries a `RetroPlug:` marker comment**, and a Mesen merge runs both modes
  of `tools/build-mesen-all-consoles.sh` (`--off` catches a dropped guard)
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **A mquickjs pin bump that fails the configure's hash guard means regenerating the wrapper, not loosening the guard**
  ([The device build](spec/06-build-test.md#the-device-build)).
- **Never `rm -rf build` to fix CMake**: investigate first. Build with `build.sh` / `build.bat`; a single target with
  `cmake --build build --target <t> -j$(nproc)` (always `-j`: the default is single-threaded)
  ([One configure](spec/06-build-test.md#one-configure)).
- **Windows is `/W4 /WX` on our own targets**: fix our code; suppress by NUMBER, and only what we do not own. CI's
  compiler (`windows-2025-vs2026`) matches the dev box's, so a CI-only warning is fixed, not explained away
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **Mesen LTO (`RETROPLUG_MESEN_LTO`) stays off in the dev loop and on in releases**
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **`retroplug-sdl` is not in releases until the standalone is release-ready** (`release.yml`'s packaging steps are
  allowlists; [spec/13](spec/13-subsystems.md)).
- **Never commit derived artifacts**: the embedded bundle C arrays (`build/native/*bundle_data.c`).
- **Config migrations**: every config root is versioned raw JSON; a newer stamp is refused and latched read-only, an
  older one migrated up, and only the latest schema is kept
  ([spec/05-data-persistence.md](spec/05-data-persistence.md#persistence-policy-the-precise-statement)).
- **A `device: true` node keeps to the portable settings types and wraps its desktop hooks in `#ifndef RP_DEVICE`**
  ([spec/02](spec/02-native-host.md#packages-generated-registration-and-device-mode)).
- **A package imports only its declared dependencies and exports no wildcard**; adding one follows spec/06's
  "Adding a package" checklist ([The packages](spec/06-build-test.md#the-packages)).
- **A harness that can reach an audio, video or serial device runs under the self-tested device guard**, and a MIDI
  harness lists only the fakes (`RP_MIDI_PORT_ALLOW`) ([the device guard](spec/14-hosts-harnesses.md#the-device-guard)).

## Verification

- **Verify headlessly before claiming done: a "tests pass" claim needs an actual exit-zero.** Which command proves
  which change is
  [spec/06-build-test.md](spec/06-build-test.md#the-headless-verification-loop--which-command-proves-which-change); the
  hosts and harnesses run by hand are [spec/14](spec/14-hosts-harnesses.md).
- **On Windows, run the suites through `test.bat`**, not bare `pnpm test*`
  ([Hosts → test tiers](spec/06-build-test.md#hosts--test-tiers)).
- **A case that cannot run skips with `skip(reason)`, and the skip is counted**: it THROWS, so no `catch` may stand
  between it and the harness, and `packages/retroplug/scripts/skip-baseline.json` pins which files may skip
  ([Skips](spec/06-build-test.md#skips-are-reported-and-ratcheted)).
- **The test-speed traps**: a slow suite is nearly always one slow file, not the pool; `renderAudio` takes
  milliseconds, not frames; and every system a test builds is removed once its state is read
  (`s.project.systems.removeSystem(id)` for a store-built one)
  ([Hosts → test tiers](spec/06-build-test.md#hosts--test-tiers)).
- **A parity failure is a real change to the audio, never noise**; an intentional one re-records the groups it moves
  in the same commit ([the parity baseline](spec/06-build-test.md#the-parity-baseline-test-nativeparity)).
- **The kit and render goldens compare bytes on `linux-x64-gnu13` only**; elsewhere a case must agree with itself
  ([kit](spec/06-build-test.md#the-kit-baselines-kit-baseline-kit-compile),
  [render](spec/06-build-test.md#the-render-baseline-render-baseline-render-chunks-render-cli)).

## Gotchas

- **lv_binding_js ignores `insertChildBefore` (it always appends)**: wrap a swappable child in a stable per-id
  `StableSlot`, or Re-key the parent on the visible set ([Framework gotchas](spec/03-ts-layer.md#framework-gotchas)).
- **The UI face is monospaced by symbol substitution**: switch faces by regenerating the font files, never in
  `lv_conf.h` ([Framework gotchas](spec/03-ts-layer.md#framework-gotchas)).
