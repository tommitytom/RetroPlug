# Mega Drive on BlastEm (spec 1): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run Sega Mega Drive / Genesis ROMs in RetroPlug on BlastEm 1.0 (a new console package, `rp.blastem.md`,
sample-accurate stepping, port-2 host device, band-limited audio, multi-instance, pad, battery, savestates), and clock
genmddj from the DAW transport over controller port 2 through a minimal new cart package, `packages/genmddj`.

**Architecture:** `deps/blastem` is a git submodule of our own mirror; every edit there is a commit on its `retroplug`
branch carrying a `RetroPlug:` marker. `packages/blastem` compiles BlastEm's Mega Drive file set from CMake, with
`cpu_dsl.py` generating the 68k and Z80 interpreters into the build tree, and adds a `BlastemMdSystem` (a `SystemBase`)
that stops the 68k at an absolute master clock (`run_until(mclk)`), a port-2 `IO_CUSTOM` device fed by a
`(mclk, level)` queue, and a blip_buf audio sink that replaces `render_audio.c`. `packages/genmddj` is the TS cart
package: ROM identity, `mdSyncLevels`, the `genmddj-sync` role and the `rp.genmddj.sync` node over `smsggdj`'s counter
state machine, reworked to take its level encoder as a parameter.

**Tech Stack:** C99 (BlastEm, vendored), C++20 (the system, Catch2), TypeScript on QuickJS/txiki
(`@retroplug/testing`), CMake + pnpm, Python 3 (BlastEm's code generator), blip_buf 1.1.0 (C).

**Design:** [docs/megadrive-blastem-design.md](megadrive-blastem-design.md). Read it first. The code study it cites is
`.superpowers/mdspike/STEPPING-STUDY.md` (gitignored; its BlastEm file:line references are the pristine 1.0 snapshot in
`deps/blastem`). Where this plan and the design disagree on a name or a path, the plan follows the tree and says so.

**Rules that bind every task** (from [AGENTS.md](../AGENTS.md), plus this branch's mirror procedure):

- Commits carry **no** `Co-Authored-By` trailer and no attribution of any kind. Never push.
- Never use the emdash character anywhere (code, comments, docs, commit messages).
- **Every edit under `deps/blastem` is a commit on the mirror's `retroplug` branch, and it is then pushed to the LOCAL
  mirror**: made inside `deps/blastem` (subject line, blank line, body, and a `RetroPlug:` marker comment at the edit
  site), then pushed from the outer repo:

  ```bash
  git -C deps/blastem -c protocol.file.allow=always push \
      /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
  ```

  The task steps below write it as `cd deps/blastem`, the commit, then the SAME push WITHOUT the `-C` (inside the
  submodule, `-C deps/blastem` would name `deps/blastem/deps/blastem`, which does not exist), then `cd ../..`. Either
  form is right in its own directory; a `cd` followed by a `-C` is not.

  and only then the outer repo's pointer commit. The push target is the local mirror repo, never the GitHub URL: the
  local mirror is the single source the user pushes to GitHub from. The mirror is
  `https://github.com/tommitytom/RetroPlug-BlastEm` and **only the user pushes it**, so ask them, after the CI-and-gate
  task, to push `upstream` and `retroplug` from the local mirror. Until they do, CI cannot resolve `deps/blastem`; that
  is the one red CI cause this plan tolerates, and only until the push.
- **A mirror commit is not in the local mirror until it is pushed there.** Every task that edits `deps/blastem` ends
  with the push above (from `deps/blastem` itself, or with `-C` from the outer repo, never both) and prints
  `git -C deps/blastem log --oneline -1` and the mirror's
  `git -C /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm log --oneline -1` as its expected result: two
  lines with the same subject means the mirror has it.
- Never bump `deps/mesen` or `deps/sameboy`: both pointers are managed deliberately. `deps/sameboy` shows as modified
  after a configure (its patches are applied); that is expected, and it is never added, reset or stashed.
- **`resume_context` is called only with a stop set.** BlastEm's own frontends end a frame from
  `render_framebuffer_updated` (`render_sdl.c` and `libblastem.c` call `system_request_exit` there); a host that stops
  at block ends must not, or a block longer than a frame stops short and its drain starves. So `run_until` sets
  `gen->stop_cycle` before every call, and the video sink never ends a frame.
- **A vendored target is judged by `packages/native/cmake/warnings.cmake`'s opt-in list**, never by a global flag:
  `retroplug-blastem` and `retroplug-blastem-test` go in it, `blastem` never does (that list is the targets that get
  `/W4 /WX`).
- The generated `m68k.c` / `z80.c` (56.7 MB and 3.0 MB) are **build-tree artifacts**: never committed, never in a
  source list, never edited.
- Python 3 is a hard configure requirement once `cmake/blastem.cmake` is included: a configure without it fails, with
  a message naming it.
- Build a target with `cmake --build build --target <t> -j$(nproc)`; never `rm -rf build`; `./build.sh` / `build.bat`
  for the whole tree. A UI change needs the bare `cmake --build build -j$(nproc)`.
- A "tests pass" claim needs an actual exit-zero from the command.
- A case that cannot run skips with `skip(reason)` (TS) or `SKIP(...)` (Catch2), and the TS skip is counted in
  `packages/retroplug/scripts/skip-baseline.json`. Fixtures are reached through `__RESOURCES_DIR__` /
  `__REPO_RESOURCES_DIR__`, never a path literal.
- Comments: no "step N" / "task N" references; match the surrounding comment density and voice.
- A case's block info is `AudioBlockInfo info{};` and then `info.frames = N;`: the struct is an aggregate, and an
  uninitialised `sampleRate` is a bug even where the system takes its rate from the activation.

## Global constraints

Copied from the design (section 2, 3, 5); every task's requirements include them.

- **Clock.** Each instance keeps a 64-bit absolute master-clock count. Host sample `n` at rate `R` is master clock
  `floor(n * MCLK / R)`, in exact integer arithmetic, and a sample-rate change re-anchors at the current sample.
  `MCLK` is the *core's* `gen->master_clock` after boot: NTSC 53,693,175 and PAL 53,203,395 (`genesis.c:26-27`). The
  design's PAL figure (53,203,424) is the hardware nominal; the tree's constant is what the emulated chips actually
  run at, so the tree wins and the mapping reads the field, never a constant.
- **Sample rates at the chips.** Each source tells the sink its own native rate as `master_clock / sample_divider`
  (`render_audio_adjust_clock`'s two arguments), and the sink never assumes one. The YM2612's divider is
  `MCLKS_PER_YM * 6 * OPN2_NUM_OPERATORS` = 7 * 6 * 24 = 1008 master clocks per stereo sample (`ym2612.c:144-146`: the
  chip's `clock_inc` is `clock_div * 6` because it runs six slots per operator sample, and the registered divider is
  `clock_inc * OPN2_NUM_OPERATORS`), which is 53,267 Hz NTSC, the chip's own sample rate. The PSG's is 240 (223,722 Hz),
  and the VDP test register's Z80 clock select halves it to `MCLKS_PER_YM * 16` = 112 (479,403 Hz).
  BlastEm's 3390 Hz one-pole (`audio.lowpass_cutoff`, `DEFAULT_LOWPASS_CUTOFF` in genesis.c) and its YM/PSG gains (0 dB
  each by default) apply at those native rates, before resampling, exactly as `render_audio.c` applies them.
- **Video.** `FrameBufferTriple` is 320 wide by 224 lines (NTSC) or 240 (PAL), allocated once at boot: the shell's
  frame slot is sized at claim and never resized (SnapshotRegistry.cpp), so the picture's own mode cannot size it. An
  H32 picture (256 wide) is centred in the 320-wide frame with the backdrop either side, and a 192-line mode is centred
  vertically; the design's "320 or 256 wide" is a picture size, not the frame's. BlastEm's pixel format is
  `0xFFRRGGBB`, which is exactly `FrameBufferTriple`'s.
- **Pad.** A 6-button pad on port 1. Port 2 is the host device when a role drives it, otherwise unplugged.
- **Sync wire.** genmddj's IN24: a 2-bit counter on `$A10005` bit 5 (TR) and bit 6 (TH), read once per frame and
  counted as `(current - last) & 3`; `$A1000B` bit set means that pin is an output (the ROM's own latch reads back).
  At most 3 clocks per frame survive. 24 PPQN.
- **Savestates.** BlastEm's own serializer, prefixed with its own 8-byte identifier (`BLSTSZ\x01\x07`); any other
  prefix is refused with a message, never deserialized.
- **Battery.** BlastEm's storage as it stands, so an odd-byte SRAM keeps its compacted layout (byte `i` is address
  `$200001 + 2i`).

## Review focus

Five input classes the design implies but no single task's happy path exercises; each line names the task whose test
pins it.

1. **A `.bin`/`.gen` file that is not a Mega Drive ROM** (an SMS `.bin`, a headerless homebrew image) must not become a
   Mega Drive system: the `SEGA` header at `$100` decides, and an SMS `.bin` still goes to Mesen (Task 15's
   classification test, Task 18's provider test).
2. **Port 2 read as player 2 by a pad ROM with no sync role attached**, and **genmddj in OUT/PULSE mode driving TR and
   TH**: no phantom levels, and the ROM's own output latch wins on the pins it drives (Task 8's direction test, Task
   19's role-only-on-genmddj test).
3. **Two instances, one of them running VDP DMA**: the DMA must read its own work RAM, not the other instance's (Task
   11's two-instance test).
4. **A savestate from another serializer version or another console**: refused with a reason, never misread (Task 13's
   test).
5. **A staged sync level whose sample falls inside a host block of any size**: the level lands on the same audio
   sample whether the block is 1 frame or 4096 (Task 9's invariance test, Task 8's offset test).

## File map

| File | New / changed | Responsibility |
|---|---|---|
| `.gitmodules`, `deps/blastem` | new | the mirror submodule, `retroplug` branch checked out |
| `cmake/blastem.cmake` | new | the `blastem` static library: `cpu_dsl.py` generation, the Mega Drive file set, `rom.db.c`, warning policy |
| `CMakeLists.txt` | changed | `include(cmake/blastem.cmake)` beside SameBoy and Mesen |
| `packages/blastem/package.json`, `tsconfig.json` | new | the package's manifest (native entry, app, consoles, fixtures) |
| `packages/blastem/native/CMakeLists.txt` | new | `retroplug-blastem` sources and the `retroplug-blastem-test` binary |
| `packages/blastem/native/src/system/blastem/BlastemCore.hpp` | new | the one place BlastEm's C headers are included (`extern "C"`) |
| `packages/blastem/native/src/system/blastem/BlastemShim.cpp` | new | the frontend globals, `read_bundled_file`, `alloc_code`, and the stubs of the systems that are not compiled |
| `packages/blastem/native/src/system/blastem/BlastemMdConfig.hpp` | new | `BlastemMdConfig`, `MdRegion` |
| `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}` | new | the system: boot, the triad, `run_until`, the sample mapping, pad, battery, savestate, memory |
| `packages/blastem/native/src/system/blastem/BlastemAudio.{hpp,cpp}` | new | the `render_audio.h` source API over blip_buf, the native-rate low-pass, the host-rate ring |
| `packages/blastem/native/src/system/blastem/BlastemVideo.{hpp,cpp}` | new | `render_get_framebuffer` / `render_framebuffer_updated` into a `FrameBufferTriple` |
| `packages/blastem/native/src/system/blastem/BlastemPortDevice.{hpp,cpp}` | new | the `(mclk, level)` queue and the `$A10005` / `$A1000B` answers |
| `packages/blastem/native/src/system/blastem/InputTypes.hpp` | new | `MdButton` (the shared wire-byte table's MD column) |
| `packages/blastem/native/src/system/blastem/BlastemRom.hpp` | new | the ROM gate (`SEGA` at `$100`) |
| `packages/blastem/native/src/system/blastem/BlastemBackend.{hpp,cpp}` | new | `rp::blastem::buildMdCore`, the node's build function |
| `packages/blastem/native/src/system/blastem/BlastemNode.cpp` | new | `rp.blastem.md` |
| `packages/blastem/native/test/SyntheticRom.hpp` | new | the hand-assembled 68k images (header, vectors, FM/PSG/port writes) |
| `packages/blastem/native/test/SyntheticRom.test.cpp` | new | the images' pinned hashes and their header and vectors |
| `packages/blastem/native/test/BlastemBuild.test.cpp` | new | the link test: a context builds and frees |
| `packages/blastem/native/test/BlastemBoot.test.cpp` | new | boot, region, teardown |
| `packages/blastem/native/test/BlastemStop.test.cpp` | new | the stop's clock, and the rebase past the 32-bit window |
| `packages/blastem/native/test/BlastemVideo.test.cpp` | new | geometry and a rendered frame |
| `packages/blastem/native/test/BlastemPad.test.cpp` | new | a button held on port 1's pad |
| `packages/blastem/native/test/BlastemAudio.test.cpp` | new | non-silence, determinism, the low-pass and the mix |
| `packages/blastem/native/test/BlastemMemory.test.cpp` | new | work RAM, the Z80's RAM, the odd-byte battery |
| `packages/blastem/native/test/BlastemPort.test.cpp` | new | offsets, late levels, direction, block carry, the sample mapping across a rate change |
| `packages/blastem/native/test/BlastemInvariance.test.cpp` | new | 1 / 64 / 1024 / 4096 frames identical, and not vacuously |
| `packages/blastem/native/test/BlastemInstances.test.cpp` | new | two instances, interleaved and on two threads, each against its own solo render |
| `packages/blastem/native/test/BlastemSaves.test.cpp` | new | state round trips, the truncated and foreign refusals |
| `packages/blastem/ts/app.ts`, `ts/enums.ts` | new | the `blastem` core role (the core's system role) and the settings labels |
| `packages/native/package.json`, `packages/native/cmake/tests.cmake` | changed | the engine-graph test's devDependency on `@retroplug/blastem` and its test include path |
| `packages/native/test/engine-graph/GraphDocs.hpp`, `SystemNode.test.cpp` | changed | `romFor`'s Mega Drive row and the three engine-graph cases |
| `packages/native/test/engine-graph/golden/product-registrations.json` | re-recorded | every product row gains `rp.blastem.md` |
| `packages/native/test/engine-graph/ScriptNodes.test.cpp` | changed | the script bundle's node list and span labels gain `rp.genmddj.sync` |
| `packages/blastem/ts/consoles.ts` | new | the Mega Drive descriptor: detection, battery, region, gain |
| `packages/blastem/ts/fixtures.ts` | new | synthetic ROM images for the parity group, published as `./testFixtures` |
| `packages/blastem/ts/test/consoles.test.ts`, `fixtures.test.ts` | new | detection and image identity |
| `packages/retroplug/src/keyCodes.ts`, `bindingMap.ts` | changed | X / Y / Z / Mode on the pad's wire bytes 8 to 11 |
| `packages/retroplug/src/systemsStore.ts` | changed | `detectBattery` reads the console's provider prefix, so a Mega Drive's `$1B0` is seen |
| `packages/retroplug/test/paths/rom-format.test.ts`, `test/systems/battery.test.ts` | changed | the Mega Drive's classification and battery cases (both are real tests, not the recorded tables) |
| `packages/retroplug/test/consoles/registry.test.ts` | changed | the registered cores, platforms and sniff tiers the new descriptor moves |
| `packages/retroplug/test/consoles/{cases.ts,recorded.ts}` | unchanged | the pre-descriptor tables the registry test compares: the Mega Drive's cases live in the real tests above, not here |
| `packages/native/cmake/warnings.cmake` | changed | `retroplug-blastem` and `retroplug-blastem-test` join the opt-in `/W4 /WX` list, `blastem` never does |
| `packages/retroplug/package.json`, `packages/retroplug/scripts/run-plugin-tests.mjs` | changed | `retroplug-blastem-test` in `test:plugin` |
| `packages/retroplug/test-native/parity/md.test.ts`, `scenes.ts`, `PARITY_TARGET` untouched | new / changed | the Mega Drive parity group |
| `packages/retroplug/test-native/parity/baseline/md.json` | new | the group's recorded digests |
| `packages/genmddj/package.json`, `tsconfig.json` | new | the cart package's manifest |
| `packages/genmddj/ts/romDetect.ts`, `romProvider.ts`, `app.ts`, `protocol.ts` | new | identity, the provider, the role, the node module |
| `packages/genmddj/ts/mdSync.ts` | new | `mdSyncLevels` and genmddj's limits |
| `packages/genmddj/ts/genmddjSyncNode.ts` | new | `rp.genmddj.sync` |
| `packages/genmddj/ts/test/{romDetect,mdSync,genmddjSyncNode}.test.ts` | new | the package's tests |
| `packages/smsggdj/ts/smsSyncNode.ts` | changed | the encoder becomes an instance parameter |
| `packages/smsggdj/ts/smsSync.ts` | changed | exports `CounterSyncEncoder` (the state machine, extracted) |
| `packages/retroplug/test/{_featureRoles.ts,system-settings/registry.test.ts,systems/store-graph.test.ts}` | changed | the role and node lists that pin every package |
| `packages/retroplug/scripts/skip-baseline.json` | changed | the fixture-gated genmddj case |
| `package.json`, `packages/retroplug/package.json` | changed | `blastem` and `genmddj` in the product lists and the shell's dependencies |
| `tools/fetch-genmddj-fixture.sh` | new | builds the genmddj ROM from a checkout into the resources tree |
| `packages/retroplug/test-native/genmddj-sync.test.ts` | new | the fixture-gated end-to-end sync proof |
| `packages/retroplug/test-native/md-bench.test.ts` | new | the profile host's xRT bench |
| `spec/00-overview.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/08-profiling.md`, `spec/13-subsystems.md`, `AGENTS.md` | changed | docs |
| `.github/workflows/build.yml` | changed | Python 3 in the four jobs |

---

### Task 1: `deps/blastem`, the mirror's submodule

**Files:** create `.gitmodules` entry and the `deps/blastem` submodule (no source change yet).

**Interfaces:**
- Produces: `deps/blastem` checked out on branch `retroplug` at commit `6d42bbf` ("BlastEm 1.0.0 (hg
  2b2a182f4522), upstream snapshot"), with `.gitmodules` naming the GitHub mirror. Every later BlastEm task commits
  inside `deps/blastem`, pushes that branch to the local mirror, and then moves this pointer.

- [ ] **Step 1: Check the mirror and add it**

The mirror is a local git repo with two branches, `upstream` and `retroplug`, both at the pristine 1.0 snapshot. Its
remote (the GitHub URL) is empty until the user pushes, so the submodule is added **from the local path**:

```bash
cd /workspaces/Megadrive
git -C .superpowers/megadrive/RetroPlug-BlastEm branch -a
git -C .superpowers/megadrive/RetroPlug-BlastEm log --oneline -1
git -c protocol.file.allow=always submodule add -b retroplug \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm deps/blastem
```

Expected: the branch list shows `retroplug` and `upstream`; the log shows `6d42bbf BlastEm 1.0.0 (hg
2b2a182f4522), upstream snapshot`; the add prints `Cloning into 'deps/blastem'...` and exits 0.

- [ ] **Step 2: Point `.gitmodules` at the real remote**

A committed `.gitmodules` must name the remote a fresh clone fetches from, not this machine's path. Replace the
entry's `url` line:

```
[submodule "deps/blastem"]
	path = deps/blastem
	url = https://github.com/tommitytom/RetroPlug-BlastEm
	branch = retroplug
```

Then make the local clone agree. `git submodule sync` REWRITES the clone's `origin` to the URL in `.gitmodules`, so
after it `deps/blastem`'s origin is the (still empty) GitHub URL. That is correct for a fresh clone and it is why
every mirror push in this plan names the local path explicitly instead of `origin`:

```bash
git submodule sync deps/blastem
git -C deps/blastem remote -v
```

Expected: `synchronizing submodule url for 'deps/blastem'`, then `origin  https://github.com/tommitytom/RetroPlug-BlastEm
(fetch)` and the same `(push)`. A mirror commit is published with the `git -C deps/blastem -c protocol.file.allow=always
push /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug` form (the rules block has it), never with a
bare `git push origin`.

- [ ] **Step 3: Check the checkout**

```bash
git -C deps/blastem rev-parse --abbrev-ref HEAD
git -C deps/blastem log --oneline -1
tail -1 deps/blastem/COPYING
git submodule status deps/blastem
```

Expected: `retroplug`; `6d42bbf BlastEm 1.0.0 (hg 2b2a182f4522), upstream snapshot`; the GPL-3.0 title line; and the
status line starts with a single space (no `-`, which would mean not initialised).

- [ ] **Step 4: Commit the pointer**

```bash
git add .gitmodules deps/blastem
git commit -m "build(blastem): deps/blastem, our mirror of BlastEm 1.0 on its retroplug branch"
```

Expected: one commit touching `.gitmodules` and `deps/blastem` (a gitlink), nothing else. `git status` still shows
`deps/sameboy` modified if the tree has been configured; that is expected and never added.

- [ ] **Step 5: Ask the user to push the mirror**

Nothing in the outer repo depends on the push, but CI does: its submodule step fetches `deps/blastem` from
`https://github.com/tommitytom/RetroPlug-BlastEm`, which is empty until the user pushes. **Ask the user, in this step,
to push `upstream` and `retroplug` from the LOCAL mirror `/.superpowers/megadrive/RetroPlug-BlastEm` (this plan's own
checkouts, `deps/blastem` included, push into that mirror, never into GitHub).** Do not push it yourself. Every later BlastEm commit needs the same ask before a CI
run is expected to be green; this plan marks those points.

---

### Task 2a: the `blastem` library builds

The riskiest unknown in the design (MSVC compiling the 56.7 MB generated `m68k.c`, and the trimmed file set's missing
pieces) is settled first, in three tasks: **2a** builds the library, **2b** adds the frontend shim and the link test
that allocates, resets and frees a context, and **2c** wires the package in and puts the first CI run behind an
explicit gate.

**Files:**
- Create: `cmake/blastem.cmake`, `packages/blastem/native/src/system/blastem/BlastemCore.hpp`
- Modify: `CMakeLists.txt` (include the new file)
- Mirror (`deps/blastem`, branch `retroplug`): `vdp.c` (the threaded-renderer guard)

**Interfaces:**
- Consumes: nothing.
- Produces: the CMake target `blastem`, a static library with include root `deps/blastem` plus
  `${CMAKE_BINARY_DIR}/blastem-gen`; and `BlastemCore.hpp`, the one place BlastEm's C headers are included from C++.

- [ ] **Step 1: The mirror commit: the threaded renderer is compiled out**

The upstream library build does not compile: `vdp_thread`'s type (`render_thread`) exists only in the SDL frontend,
which a library build does not link. Guard the two places it is used, exactly as the spike's patch did
(`.superpowers/mdspike/blastem-1.0-libbuild.patch`), with our own marker:

In `deps/blastem/vdp.c`, before `vdp_context *init_vdp_context(`:

```c
/* RetroPlug: the threaded VDP renderer is a singleton (a file-static thread) and an SDL frontend's, so a library
 * build that stubs render_is_threaded_video() to 0 must not reference either. BlastEm is built without it here. */
#if !defined(_WIN32) && !defined(IS_LIB)
static render_thread vdp_thread;
#endif
```

and in `init_vdp_context` itself, the two `#ifndef _WIN32` blocks that follow become
`#if !defined(_WIN32) && !defined(IS_LIB)`. Check with:

```bash
grep -n "IS_LIB" deps/blastem/vdp.c
```

Expected: one `#if !defined(_WIN32) && !defined(IS_LIB)` before the static, and two more around the
`render_is_threaded_video()` branch and the `render_create_thread` call. Then commit inside the submodule:

```bash
cd deps/blastem
git add vdp.c
git commit -m "vdp: compile the threaded renderer out of a library build

The render_thread type and render_create_thread() are the SDL frontend's,
so a library build that stubs render_is_threaded_video() to 0 cannot
reference them. RetroPlug links BlastEm as a static library without SDL."
git -c protocol.file.allow=always push \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
cd ../..
```

Expected: `git -C deps/blastem log --oneline -1` and
`git -C /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm log --oneline -1` print the same subject (the
push put it in the mirror), and the outer repo's `deps/blastem` shows as modified.

- [ ] **Step 2: `cmake/blastem.cmake`, the library**

```cmake
# BlastEm: the Sega Mega Drive core (vendored at deps/blastem, our mirror's `retroplug` branch), built as a static
# library for the blastem package. BlastEm's own Makefile is NOT used: this builds the Mega Drive file set only, on its
# portable interpreter (NEW_CORE) everywhere, never its x86 JIT, and generates the 68k and Z80 interpreters with
# cpu_dsl.py into the build tree (derived artifacts, never committed).
set(BLASTEM_DIR "${CMAKE_SOURCE_DIR}/deps/blastem")

if(NOT EXISTS "${BLASTEM_DIR}/cpu_dsl.py")
    message(FATAL_ERROR "deps/blastem is empty. Run: git submodule update --init --recursive")
endif()

# cpu_dsl.py is Python 3 and a hard configure requirement: m68k.c, z80.c and their headers exist only as its output,
# and the 32X's and LaserActive's headers are included by genesis.h and 32x.h even though their cores are not built.
find_package(Python3 3.6 REQUIRED COMPONENTS Interpreter)

set(_BLASTEM_GEN "${CMAKE_BINARY_DIR}/blastem-gen")
file(MAKE_DIRECTORY "${_BLASTEM_GEN}")

# One generator run per core, in the build tree: cpu_dsl.py writes the core's .h into its working directory (the build
# tree, so nothing lands in the source) and its .c to stdout (cpu_dsl.py's writeHeader + main). The dispatch is
# upstream's: `call` per 68k instruction (the handlers are too large for one function on MSVC) and `goto` per Z80
# instruction (Z80_DISPATCH:=goto, Makefile:13, 93). sh2 and upd78k2 are generated for their HEADERS only, so their
# dispatch value is irrelevant; `call` matches upstream's Makefile:435.
function(_blastem_generate_core name dispatch)
    add_custom_command(
        OUTPUT "${_BLASTEM_GEN}/${name}.c" "${_BLASTEM_GEN}/${name}.h"
        COMMAND "${Python3_EXECUTABLE}" "${BLASTEM_DIR}/cpu_dsl.py" -d ${dispatch} "${BLASTEM_DIR}/${name}.cpu"
                > "${_BLASTEM_GEN}/${name}.c"
        WORKING_DIRECTORY "${_BLASTEM_GEN}"
        DEPENDS "${BLASTEM_DIR}/cpu_dsl.py" "${BLASTEM_DIR}/${name}.cpu"
        COMMENT "cpu_dsl.py ${name}.cpu (${dispatch}) -> build/blastem-gen/${name}.c"
        VERBATIM)
endfunction()

_blastem_generate_core(m68k call)
_blastem_generate_core(z80 goto)
# The 32X and LaserActive cores are not compiled, but genesis.h includes 32x.h -> sh2.h and the file set's own sources
# include upd78k2.h, so their headers must exist.
_blastem_generate_core(sh2 goto)
_blastem_generate_core(upd78k2 goto)

# BlastEm's ROM database, which romdb.c parses through read_bundled_file() ("rom.db"). Upstream's Makefile turns it
# into a C string with sed (Makefile:437); this does the same with CMake's own string handling, escaping the one double
# quote the file carries (rom.db:868, in a comment) and appending the newline each line had.
file(STRINGS "${BLASTEM_DIR}/rom.db" _BLASTEM_ROMDB_LINES)
set(_BLASTEM_ROMDB "/* Generated from deps/blastem/rom.db by cmake/blastem.cmake. Do not edit. */\nconst char rom_db_data[] =\n")
foreach(_line IN LISTS _BLASTEM_ROMDB_LINES)
    string(REPLACE "\\" "\\\\" _line "${_line}")
    string(REPLACE "\"" "\\\"" _line "${_line}")
    string(APPEND _BLASTEM_ROMDB "    \"${_line}\\n\"\n")
endforeach()
string(APPEND _BLASTEM_ROMDB "    ;\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BLASTEM_DIR}/rom.db")
file(WRITE "${_BLASTEM_GEN}/rom.db.c" "${_BLASTEM_ROMDB}")

# The Mega Drive file set: the shell and bus (system, genesis, io, vdp, the m68k and z80 cores and their
# disassemblers), the ROM database and the cartridge mappers a Mega Drive cart can carry (Sega, J-Cart, multi-game,
# Sega 32X-cart mappers aside), the sound chips (the YM2612 and YSA2, the PSG, the MSM6258 in the RF5C164), the
# serialiser and state support, the tern config tree, and the bundled zlib chd.c and zip.c need. NOT compiled: 32X,
# Sega CD (segacd, cdd_*, cd_graphics, lc8951, cdimage, mediaplayer), SMS (sms, i8255, korean_sms_multi), Coleco,
# LaserActive, the Pico (pico_pcm, ymz263b), the VGM player (gen_player), the terminal, Nuklear, SDL, and the x86 JIT
# (m68k_core, m68k_core_x86, z80_to_x86, gen_x86, backend_x86). BlastemShim.cpp in packages/blastem provides the
# functions genesis.c and romdb.c still reference in the branches a Mega Drive never takes.
set(_BLASTEM_SOURCES
    system.c genesis.c vdp.c io.c romdb.c hash.c i2c.c nor.c
    sega_mapper.c multi_game.c jcart.c realtec.c xband.c radica.c megawifi.c sft_mapper.c
    serialize.c gst.c saves.c
    tern.c util.c paths.c config.c backend.c arena.c gen.c mem.c
    ym2612.c ymf262.c ym_common.c psg.c rf5c164.c wave.c flac.c vgm.c event_log.c render_audio.c
    chd.c zip.c z80inst.c disasm.c 68kinst.c)

set(_BLASTEM_ABS "")
foreach(_src IN LISTS _BLASTEM_SOURCES)
    list(APPEND _BLASTEM_ABS "${BLASTEM_DIR}/${_src}")
endforeach()

file(GLOB _BLASTEM_ZLIB CONFIGURE_DEPENDS "${BLASTEM_DIR}/zlib/*.c")

add_library(blastem STATIC
    ${_BLASTEM_ABS}
    ${_BLASTEM_ZLIB}
    "${_BLASTEM_GEN}/m68k.c" "${_BLASTEM_GEN}/z80.c" "${_BLASTEM_GEN}/rom.db.c"
    # The four generated HEADERS too, and not as an afterthought: a custom command's OUTPUT that no target consumes is
    # never built, so a build that names only m68k.c and z80.c leaves sh2.h and upd78k2.h (which genesis.h reaches
    # through 32x.h, and which upd78k2-using sources include) missing. A .h in a target's sources costs no compile and
    # makes CMake own the rule.
    "${_BLASTEM_GEN}/m68k.h" "${_BLASTEM_GEN}/z80.h" "${_BLASTEM_GEN}/sh2.h" "${_BLASTEM_GEN}/upd78k2.h")
set_source_files_properties("${_BLASTEM_GEN}/m68k.c" "${_BLASTEM_GEN}/z80.c" "${_BLASTEM_GEN}/rom.db.c"
                            PROPERTIES GENERATED TRUE)

# The chips' maths (the synthesised sine and log tables) is libm's. MSVC keeps it in the CRT; the GNU and Apple
# toolchains need -lm, which a static library propagates to its consumers.
if(NOT WIN32)
    target_link_libraries(blastem PUBLIC m)
endif()

target_include_directories(blastem SYSTEM PUBLIC "${BLASTEM_DIR}" "${_BLASTEM_GEN}")
# NEW_CORE selects the generated interpreters. IS_LIB is BlastEm's own library-build switch (it drops the debugger and
# the oscilloscope and keeps the sound-chip chunk size small). DISABLE_NUKLEAR keeps SDL's headers out of config.c.
# PUBLIC, not PRIVATE: the package's own translation units include BlastEm's headers, and without NEW_CORE genesis.h
# takes its JIT branch (m68k_core.h) while BlastemCore.hpp includes the generated m68k.h: two declarations of
# m68k_context in one unit, which does not compile.
# The core is GNU C99 and is not ours to fix: warnings are silenced for it alone, never globally (on MSVC it is off
# /W4 /WX in packages/native/cmake/warnings.cmake, and /wd suppressions belong there with a reason).
target_compile_definitions(blastem PUBLIC NEW_CORE IS_LIB DISABLE_NUKLEAR)
set_target_properties(blastem PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(blastem PRIVATE -w)
endif()
# The interpreter is a single 1.94-million-line translation unit: MSVC needs /bigobj for the section count it produces.
if(MSVC)
    target_compile_options(blastem PRIVATE /bigobj)
endif()
```

- [ ] **Step 3: `BlastemCore.hpp`, the one include point**

```cpp
#pragma once

// The one place BlastEm's C headers are seen from C++. Everything else in this package includes this header instead,
// so the extern "C" wrap is written once (the vendored headers carry no C++ guards of their own).

namespace rp::blastem {

// BlastEm's frontend callbacks take no instance argument, so the callbacks that have none of their own
// (render_get_framebuffer, render_framebuffer_updated, io_custom_input, the audio source entry points) read this. One
// instance runs one call at a time; BlastemShim.cpp defines it, and every system method sets it on entry. Declared here,
// where every translation unit that calls into the core can see it: a definition with no declaration is not a seam.
extern thread_local void* currentInstance;

}  // namespace rp::blastem

extern "C" {
#include "genesis.h"      // genesis_context, alloc_config_genesis, MCLKS_PER_*
#include "io.h"           // io_port, io_data_read/write, io_control_write
#include "m68k.h"          // the generated core: m68k_reset, m68k_execute, m68k_context
#include "render.h"        // pixel_t, FRAMEBUFFER_*, render_get_framebuffer, render_framebuffer_updated
#include "render_audio.h"  // audio_source, render_audio_source, render_put_stereo_sample
#include "serialize.h"     // serialize_buffer, deserialize_buffer, init_serialize/init_deserialize
#include "system.h"        // system_header
#include "util.h"          // fatal_error, warning
#include "vdp.h"           // vdp_context, read_dma_value, vdp_cycles_to_frame_end
}
```

- [ ] **Step 4: Build the library**

Wire it in and build it: in `CMakeLists.txt`, after `include(cmake/sameboy.cmake)`:

```cmake
# --- BlastEm: Sega Mega Drive emulator core (submodule at deps/blastem, our mirror's retroplug branch) ---
include(cmake/blastem.cmake)
```

then

```bash
cmake -S . -B build && cmake --build build --target blastem -j$(nproc)
```

Expected: the configure reports the Python 3 it found (and fails with `deps/blastem is empty` when the submodule is not
checked out); the build runs `cpu_dsl.py` four times into `build/blastem-gen`, compiles `build/blastem-gen/m68k.c`
(about 2 minutes at -O2, a 20 MB object) and the rest, and exits 0. `ls build/blastem-gen` shows `m68k.{c,h}`,
`z80.{c,h}`, `sh2.{c,h}`, `upd78k2.{c,h}` and `rom.db.c`. If `sh2.h` is missing, the library's source list is not
naming it (a generated OUTPUT nothing consumes is never built).

- [ ] **Step 5: Commit 2a and publish the mirror**

```bash
git add cmake/blastem.cmake CMakeLists.txt deps/blastem
git commit -m "build(blastem): the Mega Drive file set as a static library

cpu_dsl.py generates the 68k, Z80, 32X and LaserActive headers and the two
interpreters into the build tree (Python 3, a configure requirement), the
ROM database is compiled in, and the threaded renderer is out of a library
build. The interpreter is NEW_CORE everywhere: never the JIT."
```

Expected: exit 0, and the commit carries the mirror's `vdp.c` commit as a gitlink move. **Ask the user to push the
local mirror's `upstream` and `retroplug`** (the rules block has the form); 2c is where a CI run is expected to be
green.

---

### Task 2b: the frontend shim and the link test

BlastEm is written for its own frontend. This task provides the globals and callbacks it expects, the stubs for the
systems that are not compiled, and the case that proves a context builds, runs, and frees.

**Files:**
- Create: `packages/blastem/native/src/system/blastem/BlastemShim.cpp`,
  `packages/blastem/native/src/system/blastem/BlastemNode.cpp`,
  `packages/blastem/native/CMakeLists.txt`, `packages/blastem/native/test/BlastemBuild.test.cpp`

**Interfaces:**
- Consumes: `cmake/blastem.cmake`'s `blastem` target, `BlastemCore.hpp`.
- Produces: the target `retroplug-blastem` (the package's library) and `retroplug-blastem-test` (its Catch2 binary);
  `rp_blastem_package`, the package's `extern "C"` entry, with no node types yet (Task 14 fills it in); and
  `rp::blastem::currentInstance`'s definition.

- [ ] **Step 1: `BlastemShim.cpp`, the frontend BlastEm expects**

BlastEm is written for its own frontend: five globals, one bundled-file reader, and the render callbacks. This file
provides them, and the ~70 functions a Mega Drive cannot reach (because `gen->mars`, `gen->expansion` and the SMS,
Pico, Coleco, LaserActive and CD media paths are never set up for `SYSTEM_GENESIS`) as grouped, commented stubs.

```cpp
// BlastEm's frontend seam, for a host that is RetroPlug rather than BlastEm's own SDL/GTK shell. Two halves:
//
//   1. What every build needs and libblastem.c would otherwise provide: the config tree, the globals genesis.c and
//      io.c read (current_system, headless, z80_enabled, ...), read_bundled_file() for the ROM database, and the
//      render callbacks that are not the video or audio sink (those are BlastemVideo.cpp / BlastemAudio.cpp and
//      are the shell's video and audio sinks, which land beyond this file as it is introduced here).
//   2. Stubs for the systems this build does not compile. genesis.c, romdb.c and io.c still reference them, but only
//      behind a field a Mega Drive never sets (gen->mars for the 32X, gen->expansion for the Sega CD, a mapper type
//      only a Pico, Coleco, LaserActive or SMS cart selects), so no emulated Mega Drive instruction reaches one.
//      Each group says which field keeps it unreachable.
#include "system/blastem/BlastemCore.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace rp::blastem {

// The instance the frontend callbacks belong to (declared in BlastemCore.hpp).
thread_local void* currentInstance = nullptr;

}  // namespace rp::blastem

extern "C" {

// The ROM database, compiled in by cmake/blastem.cmake. It is a C file's symbol, so the declaration has to sit inside
// this block or the link finds a mangled name.
extern const char rom_db_data[];

// -- The globals libblastem.c would define ---------------------------------------------------------------
// The config tree, built once by the first system that activates and never written after (every instance reads the
// same settings: the region is a build parameter, not a config value, so no per-instance tree is needed).
tern_node* config = nullptr;
system_header* current_system = nullptr;
// NEVER written. vdp.c reads it at init (it mallocs its own line buffer when set), at free (it frees that buffer) and
// at every frame end (it skips the publish), so a value that changed between those three either leaks the first
// instance's buffer or writes a released one. With it 0, the VDP always asks render_get_framebuffer() for a buffer,
// which is the seam this file and Task 6 supply.
int headless = 0;
int exit_after = 0;
int z80_enabled = 1;
char* save_filename = nullptr;
uint8_t use_native_states = 1;

static const system_media kNoMedia = {};
const system_media* current_media(void) { return &kNoMedia; }

void process_events(void) {}

// BlastEm's ROM database is compiled in (rom.db.c), as libblastem.c does it. `config.c` is the other caller: it asks
// for "systems.cfg" and "default.cfg" through here, and BOTH ARE DELIBERATELY NULL. A null leaves
// get_systems_config()/get_model() on BlastEm's built-in default model name (`get_model`'s "md1va3"), whose values are
// the ones this console wants: tmss off, fm "discrete 2612", vsram 40, vram 64. Bundling either file would mean a
// second generated C string for no behaviour change.
char* read_bundled_file(char* name, uint32_t* sizeret) {
    if (!std::strcmp(name, "rom.db")) {
        *sizeret = static_cast<uint32_t>(std::strlen(rom_db_data));
        char* ret = static_cast<char*>(std::malloc(*sizeret + 1));
        std::memcpy(ret, rom_db_data, *sizeret + 1);
        return ret;
    }
    return nullptr;
}

uint8_t get_host_address(iface_info*) { return 0; }            // megawifi.c, only a WiFi cart's mapper calls it
void bindings_reacquire_capture(void) {}
void bindings_release_capture(void) {}
void bindings_set_mouse_mode(uint8_t) {}
void init_terminal(void) {}

// -- The render seam's defaults (Task 6 replaces the video half, Task 7 the audio one) ----------------------
pixel_t render_map_color(uint8_t r, uint8_t g, uint8_t b) { return 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | b; }
void render_set_video_standard(vid_std) {}
uint8_t render_create_window(char*, uint32_t, uint32_t, window_close_handler) { return 0; }
void render_destroy_window(uint8_t) {}
void render_framebuffer_updated(uint8_t, int) {}
// A framebuffer the VDP can draw into before the sink exists, and the one case of this file that has to be real: with
// headless 0 the VDP asks for a buffer at init (vdp.c:524) and after every published frame, so a stub returning null
// would write through null on the first line. 512 rows is what headless's own allocation is (vdp.c:525).
pixel_t* render_get_framebuffer(uint8_t which, int* pitch) {
    static pixel_t kScratch[512 * LINEBUF_SIZE];
    (void)which;
    *pitch = LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t));
    return kScratch;
}
uint8_t render_get_active_framebuffer(void) { return 0; }
uint8_t render_fullscreen(void) { return 0; }
uint32_t render_overscan_top(void) { return 0; }
uint32_t render_overscan_bot(void) { return 0; }
uint8_t render_should_release_on_exit(void) { return 0; }
uint8_t render_is_threaded_video(void) { return 0; }
// The audio *backend* half of render_audio.h. BlastEm's own render_audio.c is compiled at first (its source/registry
// and its linear resampler); these are what its backend half needs: no sync, no device, one buffer's worth of
// minimum. The shell's own sink replaces the whole file and these with it.
uint8_t render_is_audio_sync(void) { return 0; }
void* render_new_audio_opaque(void) { return nullptr; }
void render_free_audio_opaque(void*) {}
void render_lock_audio(void) {}
void render_unlock_audio(void) {}
uint32_t render_min_buffered(void) { return 1024; }
uint32_t render_audio_syncs_per_sec(void) { return 0; }
void render_audio_created(audio_source*) {}
void render_do_audio_ready(audio_source*) {}
void render_source_paused(audio_source*, uint8_t) {}
void render_source_resumed(audio_source*) {}
void render_buffer_consumed(audio_source*) {}

// -- The Sega CD (gen->expansion is NULL for a Mega Drive) ------------------------------------------------
segacd_context* alloc_configure_segacd(system_media*, uint32_t, uint8_t, rom_info*) { return nullptr; }
void free_segacd(segacd_context*) {}
void scd_run(segacd_context*, uint32_t) {}
void scd_adjust_cycle(segacd_context*, uint32_t) {}
uint32_t gen_cycle_to_scd(uint32_t, genesis_context*) { return 0; }
void segacd_config_updated(segacd_context*) {}
void segacd_format_bram(uint8_t*, size_t) {}
memmap_chunk* segacd_main_cpu_map(segacd_context*, uint8_t, uint32_t* n) { *n = 0; return nullptr; }
void segacd_register_section_handlers(segacd_context*, deserialize_buffer*) {}
void segacd_serialize(segacd_context*, serialize_buffer*, uint8_t) {}
void segacd_set_speed_percent(segacd_context*, uint32_t) {}
uint8_t parse_cue(system_media*) { return 0; }
uint8_t parse_toc(system_media*) { return 0; }
uint32_t make_iso_media(system_media*, const char*) { return 0; }
uint32_t make_chd_media(system_media*, const char*) { return 0; }

// -- The 32X (gen->mars is NULL) --------------------------------------------------------------------------
s32x* alloc_32x(system_media*, uint8_t, uint8_t) { return nullptr; }
void free_32x(s32x*) {}
void s32x_run(s32x*, uint32_t) {}
void s32x_adjust_cycles(s32x*, uint32_t) {}
void s32x_set_speed(s32x*, uint32_t) {}
void s32x_write_hint(s32x*, uint32_t) {}
void s32x_video_run(s32x_video*, uint32_t) {}
uint8_t s32x_video_composite(s32x_video*, pixel_t*, uint8_t*, uint32_t, uint8_t) { return 0; }
void s32x_fb_debug(pixel_t*, uint32_t, s32x_video*) {}

// -- The other systems' allocators (no Mega Drive path calls one) ------------------------------------------
coleco_context* alloc_configure_coleco(system_media*) { return nullptr; }
// Four parameters, exactly as sms.h declares them: the prototype's three-parameter stub compiled only while nothing
// included sms.h, which is not a property to rely on.
sms_context* alloc_configure_sms(system_media*, system_type, uint32_t, uint8_t) { return nullptr; }
laseractive* alloc_laseractive(system_media*, uint32_t) { return nullptr; }
media_player* alloc_media_player(system_media*, uint32_t) { return nullptr; }
gen_player* alloc_config_gen_player(void*, uint32_t) { return nullptr; }
gen_player* alloc_config_gen_player_reader(event_reader*) { return nullptr; }
}
```

The s32x read/write trampolines (`s32x_68k_read`, `s32x_68k_read_b`, `s32x_68k_write`, `s32x_68k_write_b`,
`s32x_fb_read_b`, `s32x_fb_read_w`, `s32x_fb_write_b`, `s32x_fb_write_w`, `s32x_overwrite_write_b`,
`s32x_overwrite_write_w`, `s32x_write_hint_b`) return 0 or the context pointer, per the declarations in
`deps/blastem/32x.h`; the SMS mapper writers (`sms_cart_ram_write`, `sms_codemasters_bank0_write`,
`sms_codemasters_bank1_write`, `sms_codemasters_bank2_write`, `sms_sega_mapper_write`, `super_game_30_write`),
`pico_pcm_*` and `ymz263b_*` (9 and 9 of them), the five `socket_*` (net.h; only `IO_GENERIC`, which a Mega Drive's
port 2 is not), `get_host_address` and `init_terminal` are the same shape. **Write every one of them, then let the
linker's list be the check:** after the first build, every symbol it names must already be in this file.

- [ ] **Step 2: `BlastemNode.cpp`, the package entry with no node types yet**

```cpp
// The blastem package's entry (packages/blastem/package.json names it): a product registers the node types this
// returns. Empty until the console's node lands, so the package can be built and linked while its core is proved.
#include <cstdint>

#include "graph/node.hpp"

namespace rp::host {
namespace {

// No node types yet, and no zero-length array either (MSVC refuses one with C2466): the registry reads a null table
// with a count of 0, which is a package that registers nothing.
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.blastem","version":"1.0.0"})", 0, nullptr};

}  // namespace
}  // namespace rp::host

extern "C" const rp_package* rp_blastem_package(void) { return &rp::host::kPackage; }
```

- [ ] **Step 3: The package's `CMakeLists.txt`**

`packages/blastem/native/CMakeLists.txt`:

```cmake
# The blastem package's native half: the Mega Drive core over the vendored BlastEm library, its shim, its ROM gate, the
# build function rp.blastem.md's request carries, and the node type. Added by retroplug_package(blastem)
# (cmake/packages.cmake).

retroplug_package_library(retroplug-blastem
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemShim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system/blastem/BlastemNode.cpp)
target_include_directories(retroplug-blastem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-blastem
    PUBLIC  retroplug-system    # SystemBase, the build request, the host's system services
            retroplug-node-sdk  # graph/node.hpp
            blastem)            # deps/blastem: the emulator (which carries -lm on the POSIX toolchains)

retroplug_add_test_binary(
    NAME     retroplug-blastem-test
    SOURCES  test/BlastemBuild.test.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test   # SyntheticRom.hpp, a later task's images
    LIBS     retroplug-blastem)
```

The link test, `packages/blastem/native/test/BlastemBuild.test.cpp`:

```cpp
// The library's link proof: the whole Mega Drive file set, the generated 68k and Z80 interpreters, the shim and the
// ROM database compile and link, and a context builds from a real image and frees. Everything else in this package
// builds on this case, and it is what a new CI platform fails first.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/blastem/BlastemCore.hpp"

namespace {

// BlastEm owns its cart: alloc_config_genesis takes the pointer, byteswaps it in place over nearest_pow2(size) bytes
// and free_genesis releases it with aligned_free. So the buffer must be aligned_calloc's, and its alignment must fit in
// the ONE byte aligned_free reads before the pointer (`ret[-1] = (uint8_t)align`, util.c:44): 4096 truncates to 0 and
// frees a misaligned pointer. 16 is BlastEm's own alignment (romdb.c:274).
void* blastemRomCopy(const std::vector<std::uint8_t>& rom) {
    std::size_t size = 1;
    while (size < rom.size()) size <<= 1;
    void* copy = aligned_calloc(1, size, 16);
    if (!copy) return nullptr;
    std::memcpy(copy, rom.data(), rom.size());
    return copy;
}

// The smallest thing BlastEm will boot: 0x200 bytes with a valid "SEGA" header at $100, a stack pointer and a reset
// vector at $000, and a `bra .` at the reset vector.
std::vector<std::uint8_t> minimalRom() {
    std::vector<std::uint8_t> rom(0x200, 0x00);
    const std::uint8_t vectors[8] = {0x00, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x02, 0x00};
    std::memcpy(rom.data(), vectors, sizeof(vectors));
    std::memcpy(rom.data() + 0x100, "SEGA MEGA DRIVE ", 16);
    rom[0x200 - 2] = 0x60;  // at 0x1FE the image's last word, unreachable in this case
    return rom;
}

}  // namespace

TEST_CASE("the BlastEm library links and a Mega Drive context builds", "[blastem][build]") {
    const std::vector<std::uint8_t> rom = minimalRom();
    genesis_context* gen = alloc_config_genesis(blastemRomCopy(rom), static_cast<std::uint32_t>(rom.size()), nullptr,
                                                0, 0, REGION_U);
    REQUIRE(gen != nullptr);
    CHECK(gen->master_clock == 53693175u);
    CHECK(gen->work_ram != nullptr);
    CHECK(gen->vdp != nullptr);
    m68k_reset(gen->m68k);
    // free_genesis is static in genesis.c: the header's own free is the door. A misaligned cart copy aborts here
    // ("double free or corruption"), which is exactly what this case exists to catch.
    gen->header.free_context(&gen->header);
}
```

- [ ] **Step 4: Put the package on the build**

A package's native target exists only for a product that lists it (`cmake/packages.cmake` walks the product lists), so
the list entry comes before the target can be built. In root `package.json`, add `"blastem"` to
`retroplug.products.desktop` and `retroplug.products.plugin` after `"mesen"` (its dependencies precede it; `genmddj`
joins after `"smsggdj"` in Task 18):

```json
      "desktop": ["core", "audio", "audio-portaudio", "audio-sdl", "midi", "launchpad", "system", "sameboy", "dsp",
                  "mgb", "lsdj", "lsdj-launchpad", "graph-runtime", "n8-wire", "n8", "mesen", "blastem", "smsggdj",
                  "risa", "host-io", "native", "bliptoaster"],
      "plugin": ["core", "system", "sameboy", "dsp", "mgb", "lsdj", "graph-runtime", "n8-wire", "n8", "mesen",
                 "blastem", "smsggdj", "risa", "host-io", "native", "bliptoaster"]
```

`packages/blastem/package.json` for now (its TS members arrive in Tasks 14 and 15; the `retroplug` member is present
from the start because the generator refuses a listed package without one):

```json
{
  "name": "@retroplug/blastem",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "description": "The BlastEm core's half of the app: the Sega Mega Drive core, its ROM gate, the build function and the rp.blastem.md node.",
  "dependencies": {
    "@retroplug/node-sdk": "workspace:*",
    "@retroplug/system": "workspace:*"
  },
  "devDependencies": {
    "@retroplug/testing": "workspace:*"
  },
  "retroplug": {
    "native": { "target": "retroplug-blastem", "entry": "rp_blastem_package", "device": false }
  }
}
```

then `packages/retroplug/package.json`'s `dependencies` gains `"@retroplug/blastem": "workspace:*"`, and

```bash
pnpm install
```

Expected: exit 0, and `pnpm check:boundaries` passes (the package imports only what it declares).

- [ ] **Step 5: Build and see the link test pass**

```bash
cmake -S . -B build
cmake --build build --target retroplug-blastem-test -j$(nproc)
build/bin/retroplug-blastem-test
```

Expected: the configure finds the Python 3 it needs and does not say `deps/blastem is empty`; the build compiles
`build/blastem-gen/m68k.c` (about 2 minutes at -O2, a 20 MB object) once, then links; the binary exits 0 with
`All tests passed (4 assertions in 1 test case)`. If the linker names a symbol the shim lacks, add it there and
rebuild: every name must come from the shim, never from linking an excluded system's source.

- [ ] **Step 6: Commit 2b**

```bash
git add packages/blastem package.json packages/retroplug/package.json pnpm-lock.yaml
git commit -m "build(blastem): the frontend shim, the package and the link test

BlastEm's callbacks take no instance, so the shim defines the frontend globals
and the callbacks, stubs the systems this build leaves out, and hands the VDP
a static framebuffer. The link test allocates the context from an aligned
copy of its cart (aligned_calloc's alignment is one byte, so 16, not 4096),
resets it and frees it."
```

Expected: exit 0.

---

### Task 2c: `test:plugin`, CI, and the gate

**Files:**
- Modify: `packages/native/cmake/warnings.cmake`, root `package.json` (`test:plugin`),
  `packages/retroplug/scripts/run-plugin-tests.mjs`, `.github/workflows/build.yml`

**Interfaces:**
- Produces: `retroplug-blastem-test` as part of `pnpm test:plugin`, and the CI job that proves the generated
  interpreter compiles on every platform. **Nothing after this task builds on this file set until the Windows job is
  green** (Step 6).

- [ ] **Step 1: `warnings.cmake`'s opt-in list**

`_rp_werror_targets` (`packages/native/cmake/warnings.cmake`) is the list of targets that DO get `/W4 /WX`; everything
not in it is built at the default level. So add our two targets to it, beside the other consoles' (the comment there
names them), and never `blastem`:

```cmake
        retroplug-sameboy retroplug-mesen retroplug-host-io
        # The Mega Drive console package: the system, the node and the build function.
        retroplug-blastem
        # The console packages' test binaries, whose cases build their cores directly and link no shell target.
        retroplug-sameboy-test retroplug-mesen-test retroplug-blastem-test
```

Expected: `grep -n "retroplug-blastem" packages/native/cmake/warnings.cmake` shows both names, and
`grep -n "\bblastem\b" packages/native/cmake/warnings.cmake` shows neither of the bare library's.

- [ ] **Step 2: The test binary in `test:plugin`**

`test:plugin` lives in the ROOT `package.json` (not packages/retroplug's), and its `cmake-build.js` argument list gains
`retroplug-blastem-test` after `retroplug-mesen-test`. `packages/retroplug/scripts/run-plugin-tests.mjs`'s `BINARIES`
array is a list of plain strings: add `"retroplug-blastem-test"` to it after `"retroplug-mesen-test"`, and one line to
the file's header comment naming the Mega Drive cases.

Expected: `pnpm test:plugin` builds and runs the new binary (it is one of the pool's processes).

- [ ] **Step 3: Python 3 in CI**

In `.github/workflows/build.yml`, add to all four jobs (after the submodule step):

```yaml
      - name: Python 3 for BlastEm's core generator
        uses: actions/setup-python@v5
        with:
          python-version: "3.12"
```

- [ ] **Step 4: The Windows job's portability fixes (only when it fails)**

The CI job on `windows-2025-vs2026` is the one build this task cannot run locally. When it first reaches this commit,
its errors are BlastEm's GNU C meeting cl.exe, and **each fix is a mirror commit with a `RetroPlug:` marker**, pushed
to the local mirror as the rules block says, not a change to the outer tree: an include cl.exe needs (`<stdint.h>`
where a source assumed a POSIX header), a function MSVC has under another name, or a warning that is a build error
there. Suppress a warning only by NUMBER (`target_compile_options(blastem PRIVATE /wdNNNN)` in `cmake/blastem.cmake`,
one line each with the reason), never by lowering the warning level, and never for a target of ours. If the generated
`m68k.c` is the only obstacle, clang-cl for the `blastem` target alone is the design's fallback; do not reach for it
until a mirror fix has failed. Commit the mirror fixes as Task 2a did, and ask the user to push before re-running CI.

- [ ] **Step 5: Commit 2c**

```bash
git add packages/native/cmake/warnings.cmake package.json \
        packages/retroplug/scripts/run-plugin-tests.mjs .github/workflows/build.yml
git commit -m "build(blastem): the console's test binary in test:plugin, and Python 3 in CI

The Mega Drive package's own targets join warnings.cmake's opt-in /W4 /WX
list; the vendored library stays out of it. CI's four jobs install Python 3,
which the core generator is a configure requirement for."
```

- [ ] **Step 6: The gate: push the mirror, then wait for Windows**

**Ask the user to push `upstream` and `retroplug` from the LOCAL mirror**
(`/workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm`) to the GitHub remote, then wait for the
`windows-2025-vs2026` job to be green before any later task's build is trusted: that job is the only proof the
57 MB generated `m68k.c` compiles under MSVC, and it is the whole reason 2a exists. A failure there is fixed as
Step 4 says, with the mirror pushed again, not worked around in the outer tree. While the push is pending, say so in
the commit that lands next: CI's submodule step cannot resolve `deps/blastem` and that is the one expected red.

### Task 3: The synthetic Mega Drive ROMs

Every native case in this plan boots an image built here: hand-assembled 68k bytes, no assembler, so CI needs none.

**Files:**
- Create: `packages/blastem/native/test/SyntheticRom.hpp`, `packages/blastem/native/test/SyntheticRom.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt` (the test source)

**Interfaces:**
- Consumes: nothing (the header is C++ only; the cases that boot an image are later tasks).
- Produces: `rp::blastem::test::Write` (`{std::uint32_t address; std::uint8_t value;}`),
  `rp::blastem::test::image(const std::vector<std::uint8_t>& program, const std::vector<Write>& writes = {})`,
  `fmPsgRom()`, `psgOnlyRom()`, `portPollRom()`, `portPollLoopRom()`, `portTimestampRom()`, `syncEdgeRom()`, all
  returning a 64 KiB `std::vector<std::uint8_t>`, and `fnv1a(const std::vector<std::uint8_t>&)`.

**The two rules every image obeys, and why they are stated here:** a short branch's displacement is measured from the
address TWO BYTES PAST THE OPCODE (a `bra.s`/`beq.s` at address A with displacement d lands at A + 2 + d; a `.s` branch
has no extension word), and `image()` puts the register-write table FIRST (at $200) and the program after it, so a
program's first byte is at `$200 + 8 * writes.size()`. A wrong either way boots, renders silence and can make a case
pass while proving nothing, which is what the pinned hashes and the ROM cases' non-silence assertions exist to prevent.

- [ ] **Step 1: Write the generator**

`SyntheticRom.hpp`:

```cpp
#pragma once

// Synthetic Mega Drive images, hand-assembled: no assembler, so CI needs none. Each is a 64 KiB cart (one Sega bank)
// with the "SEGA MEGA DRIVE " header at $100, no battery, a stack pointer of $00FFFE00 and a reset vector of $000200
// in its vectors, a table of `move.b #value,(address).L` register writes at $200, and a program after them, ended by a
// `bra .` spin.
//
// The 68k encodings used, and nothing else, so a reader can check a byte against a manual:
//   13FC 00vv aaaaaaaa   move.b #vv,(aaaaaaaa).L
//   1039 aaaaaaaa       move.b (aaaaaaaa).L,d0
//   13C0 aaaaaaaa       move.b d0,(aaaaaaaa).L
//   33C1 aaaaaaaa       move.w d1,(aaaaaaaa).L
//   7200                moveq #0,d1
//   0200 00mm           andi.b #mm,d0
//   0C00 00mm           cmpi.b #mm,d0
//   5241                addq.w #1,d1
//   67dd / 66dd         beq.s / bne.s (dd is signed, from the address two bytes past the opcode)
//   60dd                bra.s
//   60FE                bra.s  -2 (a spin)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rp::blastem::test {

struct Write {
    std::uint32_t address;
    std::uint8_t  value;
};

inline void push32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

// `= {}` on `writes`, and the includes above, are not style: without the default, every no-write image (portPollRom,
// syncEdgeRom) fails to compile, and without <cstring> the memcpy below has no declaration.
inline std::vector<std::uint8_t> image(const std::vector<std::uint8_t>& program,
                                       const std::vector<Write>& writes = {}) {
    std::vector<std::uint8_t> rom(0x10000, 0x00);
    const std::uint8_t vectors[8] = {0x00, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x02, 0x00};  // SP $00FFFE00, PC $000200
    std::memcpy(rom.data(), vectors, sizeof(vectors));
    std::memcpy(rom.data() + 0x100, "SEGA MEGA DRIVE ", 16);
    std::size_t at = 0x200;
    for (const Write& w : writes) {
        const std::uint8_t ins[8] = {0x13, 0xFC, 0x00, w.value,
                                     std::uint8_t(w.address >> 24), std::uint8_t(w.address >> 16),
                                     std::uint8_t(w.address >> 8), std::uint8_t(w.address)};
        std::memcpy(rom.data() + at, ins, sizeof(ins));
        at += sizeof(ins);
    }
    std::memcpy(rom.data() + at, program.data(), program.size());
    at += program.size();
    rom[at] = 0x60;  // bra.s *
    rom[at + 1] = 0xFE;
    return rom;
}

inline std::uint32_t fnv1a(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t h = 2166136261u;
    for (std::uint8_t b : bytes) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}
// The PSG: latch channel 0's tone (low nibble), its high six bits, then full volume. The SN76489 register layout, so
// the same bytes sound on the Master System: Mesen's SmsAudio case uses this exact sequence.
inline std::vector<std::uint8_t> psgOnlyRom() {
    return image({0x60, 0xFE}, {{0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

// A YM2612 channel 1 patch plus key-on, then the PSG's tone: the FM and PSG paths both sound, and a case can tell them
// apart by rendering psgOnlyRom() as well. The registers are the minimum for a note: LFO off, DAC on, operator 1's
// multiplier/level/attack/decay/sustain, channel 1's frequency and algorithm, stereo both, all operators keyed on.
inline std::vector<std::uint8_t> fmPsgRom() {
    return image({0x60, 0xFE},
                 {{0xA04000, 0x22}, {0xA04001, 0x00}, {0xA04000, 0x27}, {0xA04001, 0x00},
                  {0xA04000, 0x2B}, {0xA04001, 0x80}, {0xA04000, 0x30}, {0xA04001, 0x01},
                  {0xA04000, 0x40}, {0xA04001, 0x00}, {0xA04000, 0x50}, {0xA04001, 0x1F},
                  {0xA04000, 0x60}, {0xA04001, 0x00}, {0xA04000, 0x70}, {0xA04001, 0x00},
                  {0xA04000, 0x80}, {0xA04001, 0x0F}, {0xA04000, 0x90}, {0xA04001, 0x00},
                  {0xA04000, 0xA4}, {0xA04001, 0x22}, {0xA04000, 0xA0}, {0xA04001, 0x69},
                  {0xA04000, 0xB0}, {0xA04001, 0x06}, {0xA04000, 0xB4}, {0xA04001, 0xC0},
                  {0xA04000, 0x28}, {0xA04001, 0xF0},
                  {0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

// `move.b $A10005,d0; move.b d0,$FF0000; bra .`: what the ROM reads off controller port 2, in work RAM's first byte.
// ONE read, then the spin: this is the image for a case that reads the byte once (the memory cases).
inline std::vector<std::uint8_t> portPollRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05, 0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00});
}

// The same read and store, FOREVER: the port device's cases push a level one block and read the byte the next, which
// one read cannot show. The loop is 12 bytes of instruction plus the branch, so the `bra.s` at loop + 12 has its PC at
// loop + 14 and the displacement is -14 = 0xF2.
inline std::vector<std::uint8_t> portPollLoopRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00,             // move.b d0,$FF0000
                  0x60, 0xF2});                                   // bra.s  loop
}

// Polls port 2 in a tight loop, counting its polls, and leaves the loop the first time TR/TH differ from their idle
// level ($60): it stores the count at $FF0000 (a word) and the byte it read at $FF0002. The count is the poll loop's own
// clock, so a case reads WHEN a level arrived in units the ROM can see, not in host samples.
//
//   $200  moveq #0,d1                 ($204 is the loop's head; the moveq runs once)
//   $202  move.b $A10005,d0           <- loop
//   $208  andi.b #$60,d0
//   $20C  cmpi.b #$60,d0
//   $210  bne.s  store                ($04: the level changed, so leave)
//   $212  addq.w #1,d1                (one poll counted)
//   $214  bra.s  loop                 ($EC: $202 - $216)
//   $216  move.w d1,$FF0000           store:
//   $21C  move.b d0,$FF0002
inline std::vector<std::uint8_t> portTimestampRom() {
    return image({0x72, 0x00,                                     // moveq #0,d1
                  0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                         // andi.b #$60,d0
                  0x0C, 0x00, 0x00, 0x60,                         // cmpi.b #$60,d0
                  0x66, 0x04,                                     // bne.s  store
                  0x52, 0x41,                                     // addq.w #1,d1
                  0x60, 0xEC,                                     // bra.s  loop
                  0x33, 0xC1, 0x00, 0xFF, 0x00, 0x00,             // move.w d1,$FF0000
                  0x13, 0xC0, 0x00, 0xFF, 0x00, 0x02});           // move.b d0,$FF0002
}

// Holds PSG channel 0 at full volume, then silences it the first time TR/TH leave their idle level, and records that
// it happened at $FF0001. An audio edge a case can time: the level's sample is the sample the volume drops on. Its
// program starts at $218 (three PSG writes precede it), its loop is 14 bytes, and its branch is 0xF0 ($200 - $210).
//
//   $200  $C00011 <- $8E, $01         the three writes, from the table below
//   $218  move.b $A10005,d0           <- loop
//   $21E  andi.b #$60,d0
//   $222  cmpi.b #$60,d0
//   $226  beq.s  loop                 ($F0: the branch at $226 has its PC at $228, and $218 - $228 is -16)
//   $228  move.b #$9F,$C00011         channel 0, attenuation 15 (silence)
//   $230  move.b #$01,$FF0001         the flag
inline std::vector<std::uint8_t> syncEdgeRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                         // andi.b #$60,d0
                  0x0C, 0x00, 0x00, 0x60,                         // cmpi.b #$60,d0
                  0x67, 0xF0,                                     // beq.s  loop
                  0x13, 0xFC, 0x00, 0x9F, 0x00, 0xC0, 0x00, 0x11, // move.b #$9F,$C00011 (channel 0 level 15)
                  0x13, 0xFC, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x01},// move.b #$01,$FF0001 (the flag)
                {{0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

}  // namespace rp::blastem::test
```

Every displacement above is checked by the case that boots the image, not by this comment: the port cases assert that
the ROM stays in its loop while the level is idle and leaves it when the level changes (the prototype's run of the
first draft, whose `beq.s` was written against the wrong anchor, exited immediately with a count of 0 and only the
hash case noticed).

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/SyntheticRom.test.cpp`:

```cpp
// The images are code, so this pins them: a hash per image (any byte that moves changes it), the header and vectors
// a Mega Drive needs, and the one property a later case depends on and a hash cannot state (the write table's order).
//
// The hashes are the corrected generators' (the first draft's `beq.s $F2` in the two poll loops landed inside an
// address operand, and its fmPsgRom omitted the three PSG writes its hash had been taken without): every one was
// recomputed from the bytes above, and the numbers are the bytes' own, never a target to edit towards.
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"

TEST_CASE("the synthetic images are the bytes later cases boot", "[blastem][roms]") {
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::psgOnlyRom()) == 0x6397D2DFu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::fmPsgRom()) == 0x0DCF5FCAu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portPollRom()) == 0xB7DE6771u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portPollLoopRom()) == 0xCBCC6E6Bu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portTimestampRom()) == 0x5EFF37F1u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::syncEdgeRom()) == 0x64D03188u);
}

TEST_CASE("an image carries the Mega Drive header and vectors", "[blastem][roms]") {
    const std::vector<std::uint8_t> rom = rp::blastem::test::portPollRom();
    CHECK(rom.size() == 0x10000);
    CHECK(std::memcmp(rom.data() + 0x100, "SEGA MEGA DRIVE ", 16) == 0);
    CHECK(rom[0x00] == 0x00);  // stack pointer $00FFFE00
    CHECK(rom[0x01] == 0xFF);
    CHECK(rom[0x04] == 0x00);  // reset vector $000200
    CHECK(rom[0x06] == 0x02);
    CHECK(rom[0x200] == 0x10);  // the program's first byte is at $200
}

TEST_CASE("an image's write table is the order the registers expect", "[blastem][roms]") {
    // A hash says a byte moved, not that the table still means what it meant: the YM2612 needs each data byte after its
    // address byte on the same port, and the PSG's three writes must come last in the FM image (latch, high bits,
    // volume). 8 bytes a write, the table first at $200.
    const std::vector<std::uint8_t> rom = rp::blastem::test::fmPsgRom();
    const std::uint8_t first[8] = {0x13, 0xFC, 0x00, 0x22, 0x00, 0xA0, 0x40, 0x00};
    CHECK(std::memcmp(rom.data() + 0x200, first, 8) == 0);
    const std::size_t last = 0x200 + 8 * 32;  // the 33rd write, the last PSG one
    const std::uint8_t psg[8] = {0x13, 0xFC, 0x00, 0x90, 0x00, 0xC0, 0x00, 0x11};
    CHECK(std::memcmp(rom.data() + last, psg, 8) == 0);
}
```

Add `test/SyntheticRom.test.cpp` to `retroplug_add_test_binary`'s `SOURCES`.

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[roms]"
```

Expected: all three cases pass. If a hash differs, the generator's bytes moved: recompute the hash from the corrected
bytes and commit both in one edit (a hash is a reading of the generator, so a changed byte and an unchanged hash is the
failure this case exists for), and if the write-table case fails the table's order moved, which the FM and PSG register
semantics do not allow.

- [ ] **Step 4: Commit**

```bash
git add packages/blastem/native/test/SyntheticRom.hpp packages/blastem/native/test/SyntheticRom.test.cpp \
        packages/blastem/native/CMakeLists.txt
git commit -m "test(blastem): hand-assembled Mega Drive images for the native cases"
```

---

### Task 4: `BlastemMdSystem` boots, and owns its ROM the way BlastEm does

**Files:**
- Create: `packages/blastem/native/src/system/blastem/BlastemMdConfig.hpp`,
  `packages/blastem/native/src/system/blastem/BlastemRom.hpp`,
  `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemBoot.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt` (sources and the test case)

**Interfaces:**
- Consumes: `BlastemCore.hpp` (Task 2b), `rp::blastem::test::*` (Task 3).
- Produces: `rp::blastem::BlastemMdConfig` (`region`, `gainDb`, `romName`, `sram`, `savestate`), `rp::blastem::MdRegion`
  (`{auto, ntscUs, ntscJp, pal}` in the settings' spelling), `rp::blastem::isMdRom(std::span<const std::uint8_t>)`, and
  `rp::blastem::BlastemMdSystem final : public SystemBase` with the constructor
  `BlastemMdSystem(SystemId, BlastemMdConfig, std::vector<std::uint8_t> romBytes)` and the seams `config()`,
  `masterClock()`, `activated()`, `coreFrameWidth()`, `coreFrameHeight()`, and the public `mclkNow()` every later
  case reads the stop against.

**Two facts about BlastEm that this task's code depends on, both read rather than guessed:** `aligned_calloc` stores
its alignment in ONE byte before the pointer (`ret[-1] = (uint8_t)align`, util.c:44), so an alignment of 4096 truncates to
0 and `aligned_free` then frees a misaligned pointer; and `resume_context` only returns once `should_return` is set
(the frontends set it from `render_framebuffer_updated`), so nothing in this task calls it.

- [ ] **Step 1: The ROM gate and the config**

`BlastemRom.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace rp::blastem {

// The Mega Drive header's magic: "SEGA" at $100, and NOT "SEGA 32X". ONE rule on both sides (packages/blastem/ts/
// consoles.ts's detect is the same four bytes), because two rules that disagree classify a file the shell accepts and
// this gate refuses: a Master System `.bin` has no such string (its header is "TMR SEGA" at the end of a bank) and stays
// Mesen's, a 32X image is a different console this core does not run, and a `.gen`/`.md` file that is neither is refused
// rather than booted as noise.
inline bool isMdRom(std::span<const std::uint8_t> rom) {
    if (rom.size() < 0x108) return false;
    if (std::memcmp(rom.data() + 0x100, "SEGA", 4) != 0) return false;
    return std::memcmp(rom.data() + 0x100, "SEGA 32X", 8) != 0;
}

}  // namespace rp::blastem
```

`BlastemMdConfig.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

namespace rp::blastem {

// Which machine BlastEm boots (genesis.c's set_region): the header's own region, or one forced. Construct-time: a
// region fixes the master clock (53,693,175 NTSC against 53,203,395 PAL), the video standard and the frame height
// (224 against 240), so a change is a rebuild, exactly as the NES's region is a reset.
//
// The enumerators ARE the settings' labels: knobs::Label<E> is built from their names with the first letter
// lowercased (SystemNode.hpp), so `NtscUs` reads and writes as "ntscUs" and a label array beside it would be a second
// spelling of one fact (and would go stale). packages/blastem/ts/enums.ts carries the same four words.
enum class MdRegion : std::uint32_t { Auto = 0, NtscUs = 1, NtscJp = 2, Pal = 3 };

// Plain-data config for a Mega Drive system slot, in the shape of MesenSmsConfig.
struct BlastemMdConfig {
    using Tag = rfl::Literal<"md">;

    std::uint32_t region = 0;  // an MdRegion ordinal
    float         gainDb = 0.0f;
    // The file stem a core built outside a plan names its ROM by (BlastemMdSystem stages nothing: BlastEm takes its
    // bytes, so this only names the image in messages and in `event_system_start`).
    std::string              romName;
    std::vector<std::uint8_t> sram;       // battery seed (may be empty)
    std::vector<std::uint8_t> savestate;  // savestate seed (may be empty; wins over the battery)
};

}  // namespace rp::blastem
```

- [ ] **Step 2: The system's header**

`BlastemMdSystem.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "system/SystemBase.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "util/ExpSmoother.hpp"
#include "util/Gain.hpp"   // dbToLin

struct genesis_context;

namespace rp::blastem {

class PortDevice;

// A Sega Mega Drive (or Genesis), run by BlastEm 1.0. One instance owns one `genesis_context`; every call into the
// core sets `currentInstance` first, because BlastEm's frontend callbacks take no instance of their own.
//
// The clock model is section 2 of the design: a 64-bit absolute master clock, host sample n at rate R being master
// clock floor(n * MCLK / R). MCLK is the core's own `gen->master_clock`, read after boot.
class BlastemMdSystem final : public SystemBase {
public:
    BlastemMdSystem(SystemId id, BlastemMdConfig config, std::vector<std::uint8_t> romBytes);
    ~BlastemMdSystem() override;

    void onActivate(double sampleRate) override;
    void onDeactivate() override;
    void onSampleRateChanged(double sampleRate) override;

    void prepareForBlock(const AudioBlockInfo& info) override;
    bool stepIfBelowTarget(std::uint32_t framesNeeded) override;
    void finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) override;
    void reserveAudio(std::uint32_t maxBlockFrames) override;

    // The wire-byte table's button, as InputTypes.hpp orders it.
    void pressButton(std::uint8_t button, bool down) override;
    void setGainDb(float dB, bool immediate = false) override;

    // No framebuffer override yet: the video sink is where framebuffer() appears, and until it does the base's null is
    // the honest answer and no case asks for one.

    // True once onActivate has booted a live core: a ROM the gate accepted can still fail to boot, and the backend
    // then rejects the build rather than adopting a dead system (as MesenSmsSystem::activated).
    bool activated() const { return activated_; }

    const BlastemMdConfig& config() const { return config_; }
    // The core's master clock after boot (53,693,175 NTSC, 53,203,395 PAL). 0 before activation.
    std::uint32_t masterClock() const;

    // The picture's frame size: 320 wide (the widest mode) by the region's 224 or 240 lines. The frame is allocated
    // once because the shell sizes its frame slot at claim and never resizes it, so a mode change moves the
    // picture inside the frame rather than the frame.
    std::uint32_t coreFrameWidth() const;
    std::uint32_t coreFrameHeight() const;

    // The core, for this package's own tests only: the sibling screens (audio, the port device) read BlastEm's state
    // directly rather than growing a seam apiece.
    genesis_context* core() const { return gen_; }

    // The absolute master clock of host sample `sample`, in this instance's time base.
    std::uint64_t masterClockOf(std::uint64_t sample) const;

    // The absolute number of master clocks the machine has run: gen->cycle_base + the 68k's 32-bit cycle count. A
    // test seam (a stop is checked against masterClockOf), and what the port device stamps from.
    std::uint64_t mclkNow() const;

private:
    void boot();
    void destroyCore();
    void run_until(std::uint64_t mclk);

    BlastemMdConfig           config_;
    // The ROM as a capability view; BlastEm gets its own aligned copy (see boot()) and owns it from then on.
    std::vector<std::uint8_t> rom_;
    genesis_context*          gen_ = nullptr;
    bool                      activated_ = false;
    bool                      stateRefused_ = false;  // a seed state the serializer refused
    double                    sampleRate_ = 44100.0;
    ExpSmoother               gainSmoother_;

    // The absolute host sample the next block's first frame is, and the master clock that sample was mapped from. A
    // rate change re-anchors here, so every host sample already scheduled keeps the master clock it was given.
    std::uint64_t sampleBase_ = 0;
    std::uint64_t sampleAnchor_ = 0;
    std::uint64_t mclkAnchor_ = 0;
    std::uint32_t blockFrames_ = 0;  // the block prepareForBlock was told about
};
```

- [ ] **Step 3: Write the failing test**

`packages/blastem/native/test/BlastemBoot.test.cpp` (three cases, all this task's: the triad's own cases are Task 5's
and the video's Task 6's):

```cpp
#include <cstdint>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, std::uint32_t region = 1) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = region;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

}  // namespace

TEST_CASE("a Mega Drive image boots into a live core", "[blastem][boot]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    CHECK(sys->activated());
    CHECK(sys->masterClock() == 53693175u);
    CHECK(sys->coreFrameWidth() == 320);
    CHECK(sys->coreFrameHeight() == 224);
}

TEST_CASE("a PAL region changes the clock and the picture's height", "[blastem][boot]") {
    auto sys = boot(rp::blastem::test::fmPsgRom(), 3 /* pal */);
    CHECK(sys->masterClock() == 53203395u);
    CHECK(sys->coreFrameHeight() == 240);
}

TEST_CASE("the gate refuses what is not this console", "[blastem][boot]") {
    std::vector<std::uint8_t> rom = rp::blastem::test::fmPsgRom();
    rom[0x100] = 'X';
    CHECK_FALSE(rp::blastem::isMdRom(rom));                      // no "SEGA" at all
    std::vector<std::uint8_t> mars = rp::blastem::test::fmPsgRom();
    std::memcpy(mars.data() + 0x100, "SEGA 32X        ", 16);
    CHECK_FALSE(rp::blastem::isMdRom(mars));                     // the 32X's own magic: another console
    CHECK(rp::blastem::isMdRom(rp::blastem::test::fmPsgRom()));   // and the real thing still passes
}
```

- [ ] **Step 4: Run it and see it fail**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[boot]"
```

Expected: FAIL, with a link error for `BlastemMdSystem` (the test include path is Task 2b's `INCLUDES` line, so
`SyntheticRom.hpp` resolves already).

- [ ] **Step 5: Implement the boot**

`BlastemMdSystem.cpp`'s boot half (the triad's bodies arrive in Task 5):

```cpp
#include "system/blastem/BlastemMdSystem.hpp"

#include <algorithm>
#include <cstring>

#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace rp::blastem {

// The ROM must be BlastEm's own allocation: alloc_config_genesis takes the pointer, byteswaps it in place over
// nearest_pow2(size) bytes (shared_init_gen), and free_genesis releases it with aligned_free, which reads the byte
// BEFORE the pointer to find the offset (util.c:51). A std::vector's buffer is not that allocation, and handing it
// over is a double free at teardown. So the image is copied into aligned_calloc's buffer, padded to the power of two
// BlastEm byteswaps, and BlastEm owns it from boot to free.
//
// The alignment is 16, BlastEm's own (romdb.c:274), and never more: aligned_calloc keeps it in ONE byte
// (`ret[-1] = (uint8_t)align`, util.c:44), so 4096 truncates to 0 and aligned_free then frees a misaligned pointer.
static void* blastemRomCopy(const std::vector<std::uint8_t>& rom) {
    std::size_t size = 1;
    while (size < rom.size()) size <<= 1;
    void* copy = aligned_calloc(1, size, 16);
    if (!copy) return nullptr;
    std::memcpy(copy, rom.data(), rom.size());
    return copy;
}

static std::uint8_t blastemRegion(std::uint32_t region) {
    switch (static_cast<MdRegion>(region)) {
        case MdRegion::NtscUs: return REGION_U;
        case MdRegion::NtscJp: return REGION_J;
        case MdRegion::Pal:    return REGION_E;
        case MdRegion::Auto:
        default:               return 0;  // the header's own region (set_region's region == 0)
    }
}

BlastemMdSystem::BlastemMdSystem(SystemId id, BlastemMdConfig config, std::vector<std::uint8_t> romBytes)
    : SystemBase(id), config_(std::move(config)), rom_(std::move(romBytes)) {}

BlastemMdSystem::~BlastemMdSystem() { destroyCore(); }

std::uint32_t BlastemMdSystem::masterClock() const { return gen_ ? gen_->master_clock : 0; }

std::uint64_t BlastemMdSystem::masterClockOf(std::uint64_t sample) const {
    if (sample <= sampleAnchor_) return mclkAnchor_;
    const std::uint64_t mclk = gen_ ? gen_->master_clock : 53693175u;
    // floor(n * MCLK / R) in exact integer arithmetic, from the anchor rather than from zero, so a rate change only
    // re-anchors and never introduces a rounding step (design section 2).
    return mclkAnchor_ + ((sample - sampleAnchor_) * mclk) / static_cast<std::uint64_t>(sampleRate_ + 0.5);
}

void BlastemMdSystem::onActivate(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    boot();
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();  // the first value is the node's, not a ramp from unity
}

void BlastemMdSystem::boot() {
    if (!isMdRom(rom_)) return;
    // The callbacks BlastEm makes while allocating (render_audio_source, render_set_video_standard,
    // render_get_framebuffer) belong to this instance: the shim and the sinks read this thread's pointer.
    currentInstance = this;
    void* cart = blastemRomCopy(rom_);
    if (!cart) {
        // An allocation failure is a refused build, not a crash: alloc_config_genesis byteswaps the cart in place.
        return;
    }
    gen_ = alloc_config_genesis(cart, static_cast<std::uint32_t>(rom_.size()), nullptr, 0, 0,
                                blastemRegion(config_.region));
    if (!gen_) {
        aligned_free(cart);
        return;
    }
    m68k_reset(gen_->m68k);
    sampleBase_ = 0;
    sampleAnchor_ = 0;
    mclkAnchor_ = 0;
    activated_ = true;
    setGainDb(config_.gainDb, true);
}

std::uint32_t BlastemMdSystem::coreFrameWidth() const { return 320; }

std::uint32_t BlastemMdSystem::coreFrameHeight() const {
    // The region's visible lines (vdp.c's NTSC_INACTIVE_START and PAL_INACTIVE_START). version_reg's 0x40 is HZ50,
    // set by set_region for a PAL machine; Task 6's sink allocates the frame from the same expression.
    if (!gen_) return 0;
    return (gen_->version_reg & 0x40) ? 240u : 224u;
}

std::uint64_t BlastemMdSystem::mclkNow() const {
    return gen_ ? static_cast<std::uint64_t>(gen_->m68k->cycles) : 0u;  // the 64-bit base arrives in Task 5
}

void BlastemMdSystem::destroyCore() {
    if (!gen_) return;
    currentInstance = this;
    gen_->header.free_context(&gen_->header);  // frees the aligned ROM copy too
    gen_ = nullptr;
    activated_ = false;
}

void BlastemMdSystem::onDeactivate() { destroyCore(); }

void BlastemMdSystem::onSampleRateChanged(double sampleRate) {
    if (sampleRate <= 0.0) return;
    // Re-anchor at the CURRENT sample, and move the anchor rather than mapping the anchor to itself (which is a no-op
    // and the bug this body exists for): masterClockOf reads sampleAnchor_, so the new anchor's clock has to be taken
    // BEFORE the rate changes, and the sample anchor moves with it so the next sample maps from here at the new rate.
    // The sink's own rate change is Task 7's line (audio_.setHostRate), added there beside this one.
    const std::uint64_t at = masterClockOf(sampleBase_);
    sampleAnchor_ = sampleBase_;
    mclkAnchor_ = at;
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
}

void BlastemMdSystem::setGainDb(float dB, bool immediate) {
    // ExpSmoother's own shape (util/ExpSmoother.hpp), as MesenSmsSystem::setGainDb uses it: the smoother holds a linear
    // gain, so the dB is converted once, and `immediate` snaps rather than ramps.
    gainSmoother_.setTargetValue(dbToLin(dB));
    if (immediate) gainSmoother_.clearToTargetValue();
}

}  // namespace rp::blastem
```

`coreFrameWidth()/coreFrameHeight()` and `mclkNow()` are above; the triad's bodies land with the block loop. Until
then the class needs the four triad methods to compile, so declare them in the header (as above) and give them these
bodies **in this task**, each with a one-line comment saying what it will do (not a task number: the tree carries no
such references):

```cpp
void BlastemMdSystem::prepareForBlock(const AudioBlockInfo& info) { blockFrames_ = info.frames; }
bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t) { return false; }
void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const*, std::size_t) { sampleBase_ += info.frames; }
void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) { SystemBase::reserveAudio(maxBlockFrames); }
void BlastemMdSystem::run_until(std::uint64_t) {}  // later: the stop, and the 64-bit base it is measured from
```

- [ ] **Step 6: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[boot]"
```

Expected: all three cases pass, the PAL one included: `coreFrameHeight()` reads the core's own `version_reg`, so the
region's 240 lines are known the moment the core is up, before any video sink exists.

- [ ] **Step 7: Commit**

```bash
git add packages/blastem/native/src/system/blastem/BlastemMdConfig.hpp \
        packages/blastem/native/src/system/blastem/BlastemRom.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemBoot.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): the Mega Drive system boots an image BlastEm owns"
```

---

### Task 5: `run_until(mclk)`: the block triad stops where the block ends

**Files:**
- Mirror (`deps/blastem`, branch `retroplug`): `genesis.c`, `genesis.h` (the stop cycle and the cycle base)
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp`
- Create: `packages/blastem/native/test/BlastemStop.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4's `BlastemMdSystem`, `masterClockOf`, `core()`.
- Produces: `BlastemMdSystem`'s triad: `prepareForBlock` applies queued pad transitions (Task 16), `stepIfBelowTarget`
  calls `run_until(masterClockOf(blockEndSample))` and returns `false`, `finishBlock` drains exactly the block's
  frames; `std::uint64_t BlastemMdSystem::mclkNow() const`, the absolute clock including `gen->cycle_base`; and the
  mirror's `gen->stop_cycle` / `gen->cycle_base`.

- [ ] **Step 1: The mirror commit: a stop cycle and a cycle base**

In `deps/blastem/genesis.h`, add to `struct genesis_context`, beside `frame_end`:

```c
	/* RetroPlug: the absolute master clock this run stops at (0 = only the frame end), as a 32-bit offset from
	 * cycle_base, and the 64-bit base itself. A 32-bit cycle count cannot address a session's whole timeline, and a
	 * host that runs a block at a time needs to stop at the block's end rather than at a frame boundary.
	 *
	 * cycle_base MUST be 64-bit: the 32-bit 68k count is rebased about every 80 s (MAX_NO_ADJUST, genesis.c:561), so a
	 * 32-bit base wraps on the second rebase and the session's clock jumps backwards. */
	uint64_t        cycle_base;
	uint32_t        stop_cycle;
```

In `deps/blastem/genesis.c`'s `sync_components`, after `vdp_run_context(v_context, mclks);` and the three `io_run`
calls (so every component has caught up first), and before the frame-end branch:

```c
	/* RetroPlug: stop at the requested master clock. A sync is only reached because sync_cycle was clamped to it
	 * below, so this is the first one at or past stop_cycle; adjust_int_cycle then ends the run at this instruction
	 * boundary, and resume_genesis' loop sees should_return. */
	if (gen->stop_cycle && mclks >= gen->stop_cycle) {
		context->should_return = 1;
	}
```

and where `context->sync_cycle = gen->frame_end;` is set:

```c
	gen->frame_end = vdp_cycles_to_frame_end(v_context);
	/* RetroPlug: the block's end is the next sync when it is sooner than the frame's, which is what makes the stop
	 * above reachable at all (a sync only happens once cycles >= sync_cycle). */
	context->sync_cycle = gen->stop_cycle && gen->stop_cycle < gen->frame_end ? gen->stop_cycle : gen->frame_end;
```

and in the rebase branch (`if (context->cycles > MAX_NO_ADJUST ...)`, `uint32_t deduction = mclks - ADJUST_BUFFER;`),
first line after `deduction` is computed:

```c
			/* RetroPlug: the rebase is the one place the 32-bit cycle count is re-based, so the 64-bit base moves
			 * with it and cycle_base + cycles stays the absolute master clock. */
			gen->cycle_base += deduction;
			/* RetroPlug: and the stop moves with it too. A stop_cycle that stayed put would make one run span the
			 * whole rebased window (about 4e9 clocks, 80 s of emulation) inside a single block, because the sync
			 * above would not fire until the 32-bit count caught up with a target that is now 80 s away. */
			if (gen->stop_cycle) {
				gen->stop_cycle = gen->stop_cycle > deduction ? gen->stop_cycle - deduction : 0;
			}
```

and NOTHING ELSE in `genesis.c`. In particular there is no "is this stop too far away" guard: `stop_cycle` is a 32-bit
offset from `cycle_base`, so comparing it with a 64-bit clock cannot be done in that domain, and a guard that cleared the
stop when the arithmetic looked large would clear it on every rebased run and leave `resume_genesis` looping forever (a
32-bit `mclks` minus a 64-bit base wraps as soon as the base is nonzero). The API contract is therefore a 32-bit offset,
and `run_until` in Step 4 is the only place that keeps it one.

Commit in the submodule:

```bash
cd deps/blastem
git add genesis.c genesis.h
git commit -m "genesis: stop a run at an absolute master clock

A library host that runs a block at a time needs the 68k to stop at the
block's end, not at a frame boundary, and a 32-bit cycle count needs a 64-bit
base to name a session's timeline. stop_cycle is an offset from cycle_base;
the rebase moves the base and the stop with it.

RetroPlug: this is the mirror's, used by packages/blastem's BlastemMdSystem."
git -c protocol.file.allow=always push \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
cd ../..
```

Expected: `git -C deps/blastem log --oneline -1` and the mirror's own `log --oneline -1` print that subject, and the
outer repo's `deps/blastem` is modified (the pointer moves in this task's last step).

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemStop.test.cpp`:

```cpp
// The triad stops the 68k at the block's end, and the stop lands within one instruction of the clock asked for. This
// is what the whole sample-accurate design stands on: a port level staged at frame f must be applied at f's clock, so
// the core must be able to stop there and nowhere coarser. The third case is the rebase: a session's clock is 64-bit
// even though the 68k's count is not.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

}  // namespace

TEST_CASE("a block stops the 68k at the block's end", "[blastem][stop]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    AudioBlockInfo info{};
    info.frames = 256;
    sys->prepareForBlock(info);
    CHECK_FALSE(sys->stepIfBelowTarget(256));
    const std::uint64_t target = sys->masterClockOf(256);
    const std::uint64_t at = sys->mclkNow();
    // At or past the target, and within one 68k instruction of it: the longest 68k instruction is under 1200 master
    // clocks, so 2000 is "one instruction" with room.
    CHECK(at >= target);
    CHECK(at - target < 2000);
}

TEST_CASE("a second block continues from where the first stopped", "[blastem][stop]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    AudioBlockInfo info{};
    info.frames = 64;
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(64);
    const std::uint64_t first = sys->mclkNow();
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(64);
    CHECK(sys->mclkNow() > first);
    CHECK(sys->mclkNow() - first >= sys->masterClockOf(64) - sys->masterClockOf(0));
}

TEST_CASE("a session past the 32-bit rebase keeps one monotonic clock", "[blastem][stop]") {
    // MAX_NO_ADJUST is UINT32_MAX - 8*MCLKS_LINE*313 (genesis.c:561), so the 68k's own 32-bit count is rebased after
    // about 80 s of emulated time: a 32-bit base would wrap there and the session's clock would jump backwards, and a
    // stop left un-rebased would run the whole window inside one call. This is the suite's slowest case (a few seconds
    // on a host that runs the core at 22x) and it is the only one that can see either.
    auto sys = boot(rp::blastem::test::portPollRom());
    constexpr std::uint32_t kBlock = 8192;
    AudioBlockInfo info{};
    info.frames = kBlock;
    std::uint64_t previous = sys->mclkNow();
    std::uint64_t worst = 0;
    for (std::uint64_t s = 0; s < 90ull * 48000; s += kBlock) {  // 90 s: past the first rebase with margin
        sys->prepareForBlock(info);
        sys->stepIfBelowTarget(kBlock);
        const std::uint64_t now = sys->mclkNow();
        REQUIRE(now >= previous);  // monotonic: a 32-bit base fails here, at the rebase
        const std::uint64_t target = sys->masterClockOf(s + kBlock);
        const std::uint64_t over = now > target ? now - target : target - now;
        if (over > worst) worst = over;
        previous = now;
        sys->finishBlock(info, nullptr, 0);  // no lanes: the clock still advances
    }
    CHECK(worst < 2000);
    CHECK(previous > 0xFFFFFFFFull);  // the run really crossed the 32-bit boundary
}
```

`AudioBlockInfo` (`system/SystemTypes.hpp`) carries `frames`, `sampleRate`, `tempo`, `ppqPosBlockStart` and
`transportPlaying`; this case sets `frames` only. The system takes the rate from `info.sampleRate` when it is positive
and falls back to `sampleRate_` otherwise, so a case that sets only `frames` runs at the activation rate.

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[stop]"
```

Expected: FAIL: `stepIfBelowTarget` returns false but the core has not run (`mclkNow()` is 0), so the first CHECK
fails.

- [ ] **Step 4: Implement the triad**

In `BlastemMdSystem.cpp`, replace the empty triad bodies with:

```cpp
std::uint64_t BlastemMdSystem::mclkNow() const {
    return gen_ ? static_cast<std::uint64_t>(gen_->cycle_base) + gen_->m68k->cycles : 0;
}

void BlastemMdSystem::run_until(std::uint64_t mclk) {
    if (!gen_ || mclk <= mclkNow()) return;
    currentInstance = this;
    // The stop is a 32-bit OFFSET from gen->m68k->cycles (cycle_base + cycles is the absolute clock), so it is computed
    // in that domain, never as `mclk - cycle_base` truncated to 32 bits: that difference is a 64-bit quantity and a
    // `uint32_t` stop would wrap at about 20 s of emulation, putting the target in the past and stopping after one
    // instruction forever. Three terms cap the step: the clocks still wanted, one quarter of the 32-bit space (which
    // keeps the arithmetic clear of the top), and UINT32_MAX minus the current count (which is what the rebase's own
    // window guarantees: a frame end lands within ADJUST_BUFFER of the top, so this third term never binds and the
    // rebase always happens before the count can wrap).
    constexpr std::uint64_t kWindow = 0x40000000u;
    while (mclkNow() < mclk) {
        const std::uint32_t cyc = gen_->m68k->cycles;
        const std::uint64_t step = std::min<std::uint64_t>({mclk - mclkNow(), kWindow, UINT32_MAX - cyc - 1});
        gen_->stop_cycle = cyc + static_cast<std::uint32_t>(step);
        // resume_context returns at stop_cycle (or at the frame end if that is sooner, and a frame end is where the
        // rebase happens); the loop then recomputes the step from the clock it just read, so a rebase inside a pass
        // costs nothing: the rebase moves cycle_base, cycles and stop_cycle together.
        gen_->header.resume_context(&gen_->header);
        gen_->stop_cycle = 0;
    }
}

void BlastemMdSystem::prepareForBlock(const AudioBlockInfo& info) {
    blockFrames_ = info.frames;
    // The pad's transitions are applied as they arrive (BlastEm's gamepad state is a level its port read samples
    // live), so nothing is queued here: a press that precedes a block is what that block's polls see.
}

bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t /*framesNeeded*/) {
    // One stop a block: the port device answers from a queue stamped in master clocks, so nothing else needs the 68k
    // to pause mid-block. Always false: an unlinked system is the 1-member case of the round-robin.
    run_until(masterClockOf(sampleBase_ + blockFrames_));
    return false;
}
```

The header already carries `sampleBase_`, `blockFrames_` and `run_until` from Task 4 (they are what the triad's
stubs and `onSampleRateChanged` were written against); this step fills the bodies in.

`resume_context` is `genesis_context::header.resume_context` (genesis.c's `resume_genesis`), which loops
`sync_components` / `m68k_execute` until `should_return`, then clears it: exactly the stop this task adds. It also
calls `handle_reset_requests`, so a cart that requests a reset does not wedge the block loop. Note that it NEVER
returns without a stop: the frontends end a frame from `render_framebuffer_updated`, and this host does not, which is
why every call above sets `stop_cycle` first (the rules block carries the same rule).

- [ ] **Step 5: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[stop]"
```

Expected: every case passes. The overshoot assertion is the sensitive one: if `mclkNow()` is far past the target, the
clamp in Step 1 is not being reached (check that `gen->stop_cycle` is set before `resume_context`, and that
`sync_components`' clamp is before `adjust_int_cycle`), and the rebase case is the one that catches a stop left in the
wrong domain: a `uint32_t(mclk - cycle_base)` stop passes 1G once and then stops after a single instruction on every
pass, and the run either crawls or (with a "too far" guard) never returns at all, both of which the monotonic-clock
CHECK catches at about 20 s.

**Three cases in that file are the deliverable**, and the third is the expensive one:

- `[blastem][stop]`'s two cases above, per block.
- `[blastem][stop]`'s rebase case (added in Step 2 above), which is the suite's slowest: about 85 s of emulated time,
  roughly 4 s on a host that runs this core at 22x. It asserts `mclkNow()` is monotonic across the rebase and that
every stop in the run lands within 2000 clocks of its target (the two-instruction bound below).

The 2000-clock bound is not one instruction, it is two: with NEW_CORE the sync that sets `should_return` then runs one
more instruction (`target_cycle++`, genesis.c:725-727), so a stop lands at or past the target by up to two of the
longest 68k instructions. The poll ROMs' loops are tens of clocks, so 2000 is a loose bound that still fails a coarse
stop.

- [ ] **Step 6: Commit (outer repo only, plus the pointer)**

```bash
git add deps/blastem packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemStop.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): run_until(mclk) stops the 68k at the block's end

The mirror's commit adds the stop cycle and the 64-bit cycle base; the
system's stepIfBelowTarget runs to the block's end clock and the triad
advances its sample base."
```

Expected: the commit carries both the mirror commit (as a gitlink move) and the system's, and
`git -C /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm log --oneline -1` already shows the mirror
commit (the push in Step 1 put it there). **Ask the user to push the mirror before the next CI run.**

---

### Task 6: The VDP renders into the framebuffer the shell reads

**Files:**
- Create: `packages/blastem/native/src/system/blastem/BlastemVideo.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemVideo.test.cpp`
- Modify: `packages/blastem/native/src/system/blastem/BlastemShim.cpp` (drop the video stubs),
  `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}`, `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: `BlastemMdSystem` (Task 4), `currentInstance` (Task 2b).
- Produces: `rp::blastem::VideoSink` with `prepareBuffers()`, `prepareFrame(regionLines)`, `triple()`, `width()`,
  `height()`, `framesPublished()`, `buffer(which, pitch)` and `publish(which, width, vdp)`; the shim's
  `render_get_framebuffer` / `render_framebuffer_updated` / `render_overscan_*` become real; and
  `BlastemMdSystem::framebuffer()` returns the sink's triple.

- [ ] **Step 1: The pixel facts, proved**

BlastEm's `pixel_t` is `uint32_t` in `0xFFRRGGBB` when neither `USE_RGB565` nor `USE_GLES` is defined (pixel.h,
render_sdl.c:351), which is exactly the format `FrameBufferTriple` documents. The VDP draws a full bordered frame into
one buffer: `LINEBUF_SIZE` (347) pixels wide, `inactive_start + border_bot + border_top` rows tall, with the active
picture at x = `BORDER_LEFT` (13) and y = `border_top`, `h40_lines ? 320 : 256` wide and `output_lines` tall. Add this
check to `BlastemVideo.cpp` (a compile-time fact, not a runtime one):

```cpp
static_assert(sizeof(pixel_t) == 4, "the frame triple is XRGB8888: BlastEm must be built without USE_RGB565");
```

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemVideo.test.cpp`:

```cpp
// The picture: a Mega Drive's visible area in the triple the shell reads, in the format the triple documents. A frame
// that is not 320 wide, or not the region's height, is a silent failure a min-clamping copy would hide; and a triple
// nobody publishes is the failure the first draft had (framebuffer() returned a member the VDP never drew into).
#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "transport/FrameBufferTriple.hpp"

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, std::uint32_t region = 1) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = region;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

// Run `blocks` blocks of 512 frames, which is what publishes a frame.
void run(rp::blastem::BlastemMdSystem& sys, int blocks) {
    AudioBlockInfo info{};
    info.frames = 512;
    for (int i = 0; i < blocks; ++i) {
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(512)) {}
        sys.finishBlock(info, nullptr, 0);
    }
}

}  // namespace

TEST_CASE("a Mega Drive frame reaches the triple at the core's size", "[blastem][video]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 4);  // 2048 frames is more than a frame at 60 Hz
    FrameBufferTriple* fb = sys->framebuffer();
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == sys->coreFrameWidth());
    CHECK(fb->height() == sys->coreFrameHeight());
    CHECK(fb->width() == 320);
    CHECK(fb->height() == 224);
    std::vector<std::uint32_t> pixels(fb->width() * fb->height());
    REQUIRE(fb->readInto(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
    // Every pixel is an opaque colour (the backdrop of a ROM that never enables a plane), never a zero slot.
    CHECK((pixels[0] & 0xFF000000u) == 0xFF000000u);
}

TEST_CASE("a PAL machine's frame is 240 lines", "[blastem][video]") {
    auto sys = boot(rp::blastem::test::fmPsgRom(), 3);
    run(*sys, 4);
    CHECK(sys->coreFrameHeight() == 240);
    CHECK(sys->framebuffer()->height() == 240);
}
```

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[video]"
```

Expected: FAIL: `framebuffer()` is null (no sink yet), so the REQUIRE fails before the size checks.

- [ ] **Step 4: Implement the sink**

`BlastemVideo.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "system/blastem/BlastemCore.hpp"  // pixel_t, FRAMEBUFFER_*, LINEBUF_SIZE, BORDER_LEFT, vdp_context

// The shell's frame triple is a GLOBAL class (transport/FrameBufferTriple.hpp), so the forward declaration has to be at
// global scope too: one declared inside rp::blastem names a different, unrelated type, and the sink's unique_ptr and
// BlastemMdSystem::framebuffer()'s override would both bind to it instead of the real one.
class FrameBufferTriple;

namespace rp::blastem {

// BlastEm's framebuffer seam: two buffers the VDP draws into (FRAMEBUFFER_ODD / FRAMEBUFFER_EVEN alternate per frame),
// and a publish that places the active picture inside the FrameBufferTriple the engine reads. BlastEm keeps its own
// bordered frame (LINEBUF_SIZE wide, the border rows around the picture), so the crop is the whole conversion.
class VideoSink {
public:
    VideoSink() = default;
    ~VideoSink();            // out of line: triple_ is an incomplete type here

    // [control, before the core allocates] Allocate the two buffers the VDP draws into. This has to happen BEFORE
    // alloc_config_genesis, not after: the VDP asks render_get_framebuffer for a buffer at init (vdp.c:524) and
    // dereferences it immediately (context->output = fb + pitch * border_top, vdp.c:529-530).
    void prepareBuffers();

    // [control, boot only, after the core exists] Allocate the frame once: 320 wide (the widest mode) by `regionLines`
    // (224 NTSC, 240 PAL), from the region the core just chose. The shell's frame slot is sized at claim and never
    // resized, so this is the only place a size is decided; publish() returns early until it has run.
    void prepareFrame(std::uint32_t regionLines);

    FrameBufferTriple* triple() { return triple_.get(); }
    std::uint32_t      width() const;
    std::uint32_t      height() const;
    std::uint64_t      framesPublished() const { return published_; }

    // The buffer `which` names, at BlastEm's pitch. Stable until the instance is destroyed: the VDP asks for one at
    // init and again after every published frame.
    pixel_t* buffer(std::uint8_t which, int* pitch);
    // `width` is the pitch the VDP used (LINEBUF_SIZE or 256 + HORIZ_BORDER); it is not the picture's width, which
    // comes from the VDP's own mode register.
    void publish(std::uint8_t which, int width, const vdp_context& vdp);

private:
    // 512 rows, the size the VDP's own headless allocation uses (vdp.c:525): the bordered frame's picture plus border
    // is under 280 rows, but the VDP writes at row `output_line`, and 512 is the bound its own code assumes.
    static constexpr std::uint32_t kRows = 512;
    std::vector<pixel_t>              odd_;
    std::vector<pixel_t>              even_;
    std::unique_ptr<FrameBufferTriple> triple_;
    std::uint64_t                     published_ = 0;
};

}  // namespace rp::blastem
```

`BlastemVideo.cpp`:

```cpp
#include "system/blastem/BlastemVideo.hpp"

#include <algorithm>
#include <cstring>

#include "system/blastem/BlastemMdSystem.hpp"
#include "transport/FrameBufferTriple.hpp"

namespace rp::blastem {

static_assert(sizeof(pixel_t) == 4, "the frame triple is XRGB8888: BlastEm must be built without USE_RGB565");

VideoSink::~VideoSink() = default;

void VideoSink::prepareBuffers() {
    // The VDP's bordered frame is LINEBUF_SIZE wide and under 280 rows tall; kRows is 512, the size the VDP's headless
    // allocation uses, and it is a CEILING on the row its own code can write, not a mode's height.
    odd_.assign(static_cast<std::size_t>(LINEBUF_SIZE) * kRows, 0u);
    even_.assign(static_cast<std::size_t>(LINEBUF_SIZE) * kRows, 0u);
    published_ = 0;
}

void VideoSink::prepareFrame(std::uint32_t regionLines) {
    triple_ = std::make_unique<FrameBufferTriple>(320u, regionLines);
}

std::uint32_t VideoSink::width() const { return triple_ ? triple_->width() : 0u; }
std::uint32_t VideoSink::height() const { return triple_ ? triple_->height() : 0u; }

pixel_t* VideoSink::buffer(std::uint8_t which, int* pitch) {
    *pitch = LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t));
    return (which == FRAMEBUFFER_EVEN ? even_ : odd_).data();
}

void VideoSink::publish(std::uint8_t which, int /*width*/, const vdp_context& vdp) {
    if (!triple_ || odd_.empty()) return;
    // The picture is the VDP's own mode, not the pitch BlastEm hands over: 320 in H40, 256 in H32, and the mode's own
    // visible lines (inactive_start: 224 or 240 in mode 5, 192 in mode 4). It is CENTRED in the fixed frame, and the
    // rest of the frame keeps the backdrop the buffer was cleared to, so a mode change moves the picture instead of
    // resizing the frame.
    const std::uint32_t w = (vdp.regs[REG_MODE_4] & BIT_H40) ? 320u : 256u;
    const std::uint32_t h = vdp.inactive_start;
    const std::uint32_t frameW = triple_->width();
    const std::uint32_t frameH = triple_->height();
    const std::uint32_t left = (frameW - std::min(w, frameW)) / 2;
    const std::uint32_t top = (frameH - std::min(h, frameH)) / 2;
    const pixel_t* src = (which == FRAMEBUFFER_EVEN ? even_ : odd_).data();
    std::uint32_t* dst = triple_->writeSlot();
    const std::uint32_t rows = std::min(h, frameH - top);
    const std::uint32_t cols = std::min(w, frameW - left);
    // The bars a narrower picture leaves are the console's own BACKDROP, not the shell's zeroed slot: BlastEm paints the
    // border around the active picture, so the pixel just left of BORDER_LEFT is the backdrop colour, and a frame whose
    // every pixel is opaque is what FrameBufferTriple's XRGB8888 contract means (a zero pixel renders as transparent).
    const std::uint32_t borderCol = BORDER_LEFT ? BORDER_LEFT - 1 : 0;
    const pixel_t backdrop = 0xFF000000u | (src[std::uint32_t(vdp.border_top) * LINEBUF_SIZE + borderCol] & 0x00FFFFFFu);
    for (std::uint32_t y = 0; y < frameH; ++y) {
        for (std::uint32_t x = 0; x < frameW; ++x) dst[y * frameW + x] = backdrop;
    }
    for (std::uint32_t y = 0; y < rows; ++y) {
        const pixel_t* row = src + std::uint32_t(vdp.border_top + y) * LINEBUF_SIZE + BORDER_LEFT;
        for (std::uint32_t x = 0; x < cols; ++x) dst[(top + y) * frameW + left + x] = 0xFF000000u | (row[x] & 0x00FFFFFFu);
    }
    triple_->publish();
    ++published_;
}

}  // namespace rp::blastem
```

Then in `BlastemShim.cpp`, replace the stub definitions from Task 2b with the real ones. They stay in the shim (they are
`extern "C"` and live in exactly one file), and the two that reach an instance read `currentInstance`:

```cpp
pixel_t* render_get_framebuffer(uint8_t which, int* pitch) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (sys) return sys->video().buffer(which, pitch);
    // The no-instance fallback STAYS (it is the shim's stub): a context built outside any system, which is exactly what
    // the link test does, still gets a buffer because the VDP at init points context->output into this pointer and
    // draws into it. 512 rows is the VDP's own headless allocation (vdp.c:525).
    static pixel_t kScratch[512 * LINEBUF_SIZE];
    *pitch = LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t));
    return kScratch;
}

void render_framebuffer_updated(uint8_t which, int width) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (!sys) return;  // a context outside any system has no frame to publish, and the VDP does not read one back
    sys->video().publish(which, width, *sys->vdp());
    // NO frame end here. A frontend ends the frame from this callback (render_sdl.c and libblastem.c call
    // system_request_exit), and this host must not: a block is longer than a frame, so a stop at the frame boundary
    // would leave the rest of the block undrained and starve its audio. The stop is run_until's, once a block.
}

vid_std video_standard = VID_NTSC;
void render_set_video_standard(vid_std std) { video_standard = std; }
uint8_t render_get_active_framebuffer(void) { return FRAMEBUFFER_ODD; }
uint8_t render_fullscreen(void) { return 0; }
uint32_t render_overscan_top(void) { return 0; }   // the full picture: the sink places it, no overscan crop
uint32_t render_overscan_bot(void) { return 0; }
uint8_t render_create_window(char*, uint32_t, uint32_t, window_close_handler) { return 0; }
void render_destroy_window(uint8_t) {}
```

`BlastemMdSystem` needs `video()`, `vdp()` and `framebuffer()` (add to the header: `VideoSink& video() { return video_; }`,
`vdp_context* vdp() const { return gen_ ? gen_->vdp : nullptr; }`, and
`FrameBufferTriple* framebuffer() override { return video_.triple(); }`), a `VideoSink video_;` member, and the buffer
allocation BEFORE the allocation (the VDP asks for a buffer inside `alloc_config_genesis` and writes through it
straight away), the frame after it:

```cpp
void BlastemMdSystem::boot() {
    if (!isMdRom(rom_)) return;
    currentInstance = this;
    // BEFORE alloc_config_genesis: the VDP asks render_get_framebuffer for its buffer at init (vdp.c:524) and points
    // context->output into it immediately (vdp.c:529-530), so a sink with no buffers yet is a null write on the first
    // line. `headless` stays 0 and is never written anywhere: vdp.c reads it at init, at free and at every frame end, so
    // a value that changed between those three would leak or free a buffer the sink still owns.
    video_.prepareBuffers();
    ...
    m68k_reset(gen_->m68k);
    // The frame, once, for the region the core just chose: 240 lines on a PAL machine, 224 on an NTSC one. The shell's
    // frame slot is sized at claim and never resized, so this is the only place its size is decided.
    video_.prepareFrame((gen_->version_reg & 0x40) ? 240u : 224u);
    ...
}
```

and `coreFrameWidth()/coreFrameHeight()` return the sink's frame once it exists, and 320 x 224 before boot (the region
is unknown until the core is up), which is what Task 4's cases see:

```cpp
std::uint32_t BlastemMdSystem::coreFrameWidth() const { return video_.triple() ? video_.width() : 320u; }
std::uint32_t BlastemMdSystem::coreFrameHeight() const { return video_.triple() ? video_.height() : 224u; }
```

**The shim's audio sources and the video sink both read `currentInstance`, so the only global the video half touches is
`headless`, which nobody writes**: vdp.c reads it at three points and every instance must see the same value, which is
why one copy set to 0 is the whole story.

- [ ] **Step 5: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[video]"
build/bin/retroplug-blastem-test "[boot]"
```

Expected: the `[video]` cases pass at 320x224 and 320x240, and the `[boot]` cases still do. From here
`coreFrameWidth()/coreFrameHeight()` return the sink's frame (the triple's), 320 x 224 before a core exists.

- [ ] **Step 6: Commit**

```bash
git add packages/blastem/native/src/system/blastem/BlastemVideo.hpp \
        packages/blastem/native/src/system/blastem/BlastemVideo.cpp \
        packages/blastem/native/src/system/blastem/BlastemShim.cpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemVideo.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): the VDP draws into the shell's framebuffer triple"
```

---

### Task 7: The audio sink: blip_buf at the host rate, BlastEm's filter at the native rates

**Files:**
- Mirror (`deps/blastem`, branch `retroplug`): `blip_buf.c`, `blip_buf.h` (added: stock blip_buf 1.1.0, copied from
  `deps/mesen/Utilities/Audio/` and stripped to plain C)
- Create: `packages/blastem/native/src/system/blastem/BlastemAudio.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemAudio.test.cpp`
- Modify: `cmake/blastem.cmake` (drop `render_audio.c` from the file set), `BlastemShim.cpp` (drop the audio backend
  stubs), `BlastemMdSystem.{hpp,cpp}`, `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: `BlastemMdSystem` (Task 4/5), the sample mapping (`masterClockOf`).
- Produces: `rp::blastem::AudioSink` implementing `render_audio.h`'s **source** API (`render_audio_source`,
  `render_audio_adjust_clock`, `render_put_mono_sample`, `render_put_stereo_sample`, `render_pause_source`,
  `render_resume_source`, `render_audio_source_gaindb`, `render_free_source`, `render_end_audio`,
  `render_save_audio`), plus `drain(frames, left, right)`, `prepare(maxBlockFrames)`, `setHostRate(rate)`,
  `starvedSamples()`, `reset()` and `clear()`; the system's `finishBlock` sums the sink into the block's lanes and
  applies the gain.
- Produces also, as documented test seams: `BlastemMdSystem::audio()`, `BlastemMdSystem::starvedSamples()` (forwarding to
  the sink), `AudioSink::sourceNativeRate(const char* name)` (the rate a chip's own `render_audio_source` /
  `render_audio_adjust_clock` calls gave it, so a case can see a divider change) and `BlastemMdSystem::switchPsgClock(bool
  fast)`, which runs the two lines genesis.c runs around `vdp_test_port_write` when the VDP test register's bit 0
  changes (`gen->psg->clock_inc = fast ? MCLKS_PER_YM * 16 : MCLKS_PER_PSG; psg_adjust_master_clock(gen->psg,
  gen->master_clock);`).

**The sink's latency budget, which is what makes the block partition irrelevant.** A chip that has emitted
`floor((T - phase) / divider)` native samples by the clock `T` gives blip `floor(c * R / native)` host samples: at a
block end that is `n` or `n - 1`, because the remainder is a fraction of one host sample and it stays in blip's own
accumulator. A sink that drains `n` frames from a ring with nothing in reserve therefore starves by a sample, and the
zeros land wherever the partition puts the boundary. So every ring is PRIMED with `kPrime = 16` host samples of silence
at activation, after a rate change and after a state load, and the block's output is the blip stream delayed by 16
samples: a function of absolute time alone. `drain` counts a starvation as a bug (`starvedSamples()`), and the audio,
invariance and parity cases assert it is 0.

**A block is also longer than blip can take in ONE time frame, which is why the sink flushes mid-block.** blip's time
base is `time_unit = 2^52` in a 64-bit accumulator, so `t * factor` overflows once a frame would produce more than 4096
output samples (that is what `blip_max_frame = 4000` encodes). An 8192-frame block at 48 kHz from a 223,721 Hz source is
8192 output samples' worth of clocks, so one `blip_end_frame` per block wraps the accumulator and blip then reports
nonsense: a scratch run of exactly this shape starved 90,097 of its 96,000 samples at 4,096- and 8,192-frame blocks (and
was fine at 1, 64 and 1,024, which is why the small-block cases would never have caught it). So `putStereo` ends the frame
whenever `pendingClocks` reaches `flushClocks` (3,900 output samples' worth, under the 4,096 cliff) and moves what blip
produced into the ring, and `drain` flushes once more before it consumes the block. The frame boundary is then an
internal detail: the concatenated stream is the same at every block size (checked at 1, 64, 1024, 4096 and 8192 frames
with nothing starved), which is exactly what block-size invariance needs.

The native rates are BlastEm's own, from the `render_audio_source`/`render_audio_adjust_clock` arguments, never a
constant: the YM2612's divider is `MCLKS_PER_YM * 6 * OPN2_NUM_OPERATORS` = 7 * 6 * 24 = 1008 master clocks per stereo
sample (`ym2612.c:144-146`), which is 53,267 Hz NTSC, the chip's own sample rate; the PSG's is 240 (223,722 Hz), and
its Z80-clock-select mode changes it to 112 (479,403 Hz). Both 44.1 and 48 kHz hosts therefore resample downward, and
blip handles the other ratios too (it interpolates up when a host rate is above the source's, as an 88.2 or 96 kHz
host is for the YM).

- [ ] **Step 1: The mirror commit: blip_buf 1.1.0**

BlastEm has no band-limited resampler of its own (its `render_audio.c` interpolates linearly). Take the copy this
repository already carries: **`deps/mesen/Utilities/Audio/blip_buf.{cpp,h}`**, whose header is `blip_buf 1.1.0` and
whose API is the stock mono one this task's code calls (`blip_add_delta(blip, time, delta)` and
`blip_read_samples(blip, out, count, stereo)`, the last argument selecting interleaved output). Do NOT take Genesis
Plus GX's fork from the spike tree: its `blip_add_delta` is STEREO (`delta_l, delta_r`), its `blip_read_samples` takes
three arguments, and it clamps a read to what is available only under `BLIP_ASSERT`, so the calls below would either
not compile or read past the samples it has.

Copy the two files into `deps/blastem` as `blip_buf.c` and `blip_buf.h`, with three edits, all mechanical and all
named in the marker:

- drop `#include "pch.h"` from each (Mesen's precompiled header, which BlastEm does not have);
- define `EXPORT` empty instead of `__declspec(dllexport)` (the library is static; a dllexport on a static symbol is a
  warning MSVC turns into an error at /W4 /WX);
- keep Mesen's added `blip_reset(blip_t*, int sample_count)` (its own in-place re-initialisation, which the sink's
  `reset()` uses so a state load re-primes without allocating); it is NOT stock 1.1.0, and the marker says so.

At the top of each file:

```c
/* RetroPlug: blip_buf 1.1.0 (Shay Green, LGPL-2.1 or later), copied from deps/mesen/Utilities/Audio/blip_buf.{cpp,h}
 * in this repository. Three edits, mechanical: pch.h dropped (BlastEm has no precompiled header), EXPORT defined empty
 * (this is a static library, not a DLL), and Mesen's own blip_reset() kept, which is not stock 1.1.0. BlastEm has no
 * band-limited resampler of its own: its render_audio.c interpolates linearly. packages/blastem links this instead. */
```

Then check the API the sink is written against, before committing:

```bash
grep -n "blip_add_delta(\|blip_read_samples(\|blip_reset(\|blip_max_frame" deps/blastem/blip_buf.h
```

Expected: `blip_add_delta( blip_t*, unsigned int clock_time, int delta )` (three arguments, mono),
`blip_read_samples( blip_t*, short out [], int count, int stereo )` (four), `blip_reset( blip_t*, int sample_count )`,
and `blip_max_frame = 4000`, whose comment says it bounds ONE time frame's output, not a buffer's. Both matter: the
sink ends a frame every 3,900 output samples' worth of clocks (under that bound, because the accumulator overflows past
4,096), and the BUFFER is sized in output samples, so an 8192-frame block at 48 kHz needs 8192 + prime + slack entries,
not 4000. Then commit:

```bash
cd deps/blastem
git add blip_buf.c blip_buf.h
git commit -m "blip_buf: band-limited step synthesis, 1.1.0

BlastEm resamples linearly in render_audio.c. A host that wants Mesen's
audio quality needs blip_buf, which both Mesen and Genesis Plus GX use."
git -c protocol.file.allow=always push \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
cd ../..
```

Expected: the commit's subject is in both `deps/blastem` and the mirror, and
`diff deps/blastem/blip_buf.c deps/mesen/Utilities/Audio/blip_buf.cpp` shows the `pch.h` line and nothing else.

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemAudio.test.cpp`:

```cpp
// The audio path, at the three points that fail silently: silence (a ROM that plays must not render zeros), the FM
// chip's contribution (a YM2612 write must change the output), and run-to-run determinism (the same input must give the
// same bytes, or nothing downstream can be compared).
#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

// `seconds` of interleaved stereo at the host rate, one block of kFrames at a time.
std::vector<float> render(rp::blastem::BlastemMdSystem& sys, double seconds) {
    std::vector<float> out;
    std::vector<float> left(kFrames), right(kFrames);
    float* lanes[2] = {left.data(), right.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    const int blocks = static_cast<int>(seconds * kRate / kFrames);
    for (int i = 0; i < blocks; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(kFrames)) {}
        sys.finishBlock(info, lanes, 2);
        out.insert(out.end(), left.begin(), left.end());
        out.insert(out.end(), right.begin(), right.end());
    }
    return out;
}

double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

}  // namespace

TEST_CASE("a ROM playing PSG and FM is not silent", "[blastem][audio]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    const std::vector<float> out = render(*sys, 0.5);
    CHECK(out.size() == static_cast<std::size_t>(0.5 * kRate) * 2);
    CHECK(peak(out) > 0.01);   // about -40 dBFS: a full-volume PSG channel is far louder, the check only rejects silence
    // The sink must never have run dry: a starved sample is a zero nobody's ROM asked for, and its position would be
    // a function of the block partition.
    CHECK(sys->starvedSamples() == 0);
}

TEST_CASE("the YM2612's writes change the output", "[blastem][audio]") {
    auto psg = boot(rp::blastem::test::psgOnlyRom());
    auto both = boot(rp::blastem::test::fmPsgRom());
    const std::vector<float> a = render(*psg, 0.25);
    const std::vector<float> b = render(*both, 0.25);
    REQUIRE(a.size() == b.size());
    // Both halves must be audible on their own, or the inequality below could hold with one of them silent: the
    // prototype measured the FM patch's own peak at about 435 in the 16-bit source ring against the PSG's 2190, so a
    // quiet patch is expected and silence is not.
    CHECK(peak(a) > 0.01);
    CHECK(peak(b) > 0.01);
    CHECK(a != b);  // the FM patch is audible: the two renders differ
    CHECK(psg->starvedSamples() == 0);
    CHECK(both->starvedSamples() == 0);
}

TEST_CASE("the same input renders the same bytes twice", "[blastem][audio]") {
    auto first = boot(rp::blastem::test::fmPsgRom());
    auto second = boot(rp::blastem::test::fmPsgRom());
    CHECK(render(*first, 0.25) == render(*second, 0.25));
}

TEST_CASE("a chip's rate change ends its frame first", "[blastem][audio]") {
    // The VDP test register's bit 0 moves the PSG's divider from 240 to 112 and calls psg_adjust_master_clock
    // (genesis.c:1128-1132), which is the sink's adjustClock. Writing the test port's register is NOT the same thing:
    // the comparison before and after the write is genesis.c's, so vdp_test_port_write alone changes no clock and this
    // case would pass without exercising a line of the sink. switchPsgClock runs those two lines, and the native rate
    // the sink recorded is what the case reads, so a sink that ignored the call fails here.
    auto sys = boot(rp::blastem::test::psgOnlyRom());
    REQUIRE(sys->audio().sourceNativeRate("PSG") == 53693175.0 / 240.0);
    const std::vector<float> before = render(*sys, 0.1);
    sys->switchPsgClock(true);
    // 53,693,175 / 112 = 479,403.35: the divider really moved, and the sink took the new one.
    CHECK(sys->audio().sourceNativeRate("PSG") == 53693175.0 / 112.0);
    const std::vector<float> after = render(*sys, 0.1);
    CHECK(peak(before) > 0.01);
    CHECK(peak(after) > 0.01);
    CHECK(sys->starvedSamples() == 0);
}
```

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[audio]"
```

Expected: FAIL: `finishBlock` writes nothing, so the first case's peak is 0 (and the `size` check fails first).

- [ ] **Step 4: `cmake/blastem.cmake` drops BlastEm's own audio file**

Remove `render_audio.c` from `_BLASTEM_SOURCES` and add `blip_buf.c`. The defines stay `PUBLIC` as Task 2a wrote them:
the package's own translation units include BlastEm's headers and must see the same `NEW_CORE` and `IS_LIB`.

- [ ] **Step 5: Implement the sink**

`BlastemAudio.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "blip_buf.h"

namespace rp::blastem {

// One audio_source (one chip) as this host drives it: BlastEm's own 3390 Hz one-pole low-pass and gain at the chip's
// NATIVE sample rate, then blip_buf's band-limited step synthesis to the host rate, then a small ring the block drains
// exactly. BlastEm's render_audio.c is not compiled: it is a process-global registry capped at 8 sources (a ninth
// aborts), and it resamples linearly.
//
// The native rate is whatever the chip told us: `master_clock / sample_divider` from its render_audio_source call, never
// a constant in this file.
class AudioSink {
public:
    // [control, at activation] Size every source's ring and every blip for the plan's largest block, and prime the
    // ring with kPrime host samples of silence. Growth only.
    void prepare(std::uint32_t maxBlockFrames);

    // [block] The block's `frames` host samples, summed from every live source into `left`/`right` (added, not
    // overwritten, as SystemBase's contract says), after each source's own gain. Fills with silence and counts a
    // starvation if a source is short, which is a bug and the cases assert it never happens.
    void drain(std::uint32_t frames, float* left, float* right);

    // [any] Host samples the sink has starved since activation: 0 on a driven core.
    std::uint64_t starvedSamples() const { return starved_; }

    // The silence every ring carries ahead of its first real sample, in host frames. The fractional part of a
    // native-to-host resample leaves a block one host sample short now and then, and this is what absorbs it.
    static constexpr std::uint32_t kPrime = 16;

    // The native rate the named source's own render_audio_source / render_audio_adjust_clock call gave it, or 0. A test
    // seam: it is how a case sees that a divider change really reached the sink. Double, not integer: the chip emits one
    // sample every `divider` master clocks, and 53,693,175 / 240 is 223,721.5625.
    double sourceNativeRate(const char* name) const;

    // Called by the render_audio.h entry points below.
    struct Source;
    Source* add(const char* name, std::uint64_t masterClock, std::uint64_t sampleDivider, std::uint8_t channels);
    void     remove(Source* source);
    void     adjustClock(Source* source, std::uint64_t masterClock, std::uint64_t sampleDivider);
    void     putStereo(Source* source, std::int16_t left, std::int16_t right);
    void     putMono(Source* source, std::int16_t value);
    void     setGainDb(Source* source, float gainDb);
    // [control/block] The host sample rate every ring reads at (the same for all sources). Re-primes every ring.
    void setHostRate(double sampleRate);
    // Drops every source (a core being destroyed).
    void clear();
    // Drops and re-primes every source's ring and blip, keeping the sources and their rates: what a state load needs
    // (the ring, the blips and the filter are the host's state and a savestate does not carry them).
    void reset();

private:
    void prime(Source& source);
    // Ends the current blip frame and moves what it produced into the ring. The ONLY place a frame is ended: putStereo
    // calls it when the frame's clock budget is spent (putStereo has no other way to stay inside blip's 64-bit
    // accumulator), and drain calls it for the block's tail.
    void flush(Source& source);
    // The ring's scratch for one blip read: a MEMBER, not a local, because a block runs on the audio thread and 16 KiB
    // of stack there is not a thing to spend (drain's scratch was a local array in the first draft).
    std::vector<std::int16_t> scratchLeft_;
    std::vector<std::int16_t> scratchRight_;
    std::vector<Source*>      sources_;
    std::uint32_t             capacity_ = 0;
    std::uint32_t             maxBlockFrames_ = 0;
    double                    hostRate_ = 48000.0;
    std::uint64_t             starved_ = 0;
};

}  // namespace rp::blastem
```

The source's state and the per-sample arithmetic, which is the whole design of this file:

```cpp
struct AudioSink::Source {
    const char*   name = nullptr;    // the chip's own label from render_audio_source ("PSG", "YM2612"); the seam's key
    blip_t*       left = nullptr;    // one blip per side, both clocked at the chip's native rate
    blip_t*       right = nullptr;
    std::vector<std::int16_t> ringLeft;   // host-rate samples read from blip, drained by the block
    std::vector<std::int16_t> ringRight;
    std::uint32_t ringRead = 0, ringFill = 0;
    double        nativeRate = 0.0;  // master clocks / sample_divider, from the chip's own call, as a real rate
    double        dt = 0.0;          // seconds per native sample: 1 / nativeRate
    std::uint32_t lowpassAlpha = 0;  // BlastEm's one-pole coefficient, 16.16
    std::int16_t  lastLeft = 0, lastRight = 0;  // the low-pass state, as render_audio.c kept it
    float         gain = 1.0f;       // the chip's own gain (audio.fm_gain / audio.psg_gain, 0 dB by default)
    std::uint32_t pendingClocks = 0; // native samples added since the last end_frame: blip's time base
    std::uint32_t flushClocks = 0;   // the most native clocks ONE blip frame may hold: 3900 * nativeRate / hostRate
};

// BlastEm's filter, from render_audio.c:339: alpha = dt / (dt + rc) with rc = (1 / cutoff) / 2pi and cutoff 3390 Hz,
// held as 16.16. It runs at the chip's native rate, before the resampler, exactly as BlastEm's own sink does.
static std::int16_t lowpass(std::uint32_t alpha, std::int16_t last, std::int16_t current) {
    const std::int32_t tmp = current * std::int32_t(alpha) + last * std::int32_t(0x10000 - alpha);
    return static_cast<std::int16_t>(tmp >> 16);
}

void AudioSink::putStereo(Source* s, std::int16_t left, std::int16_t right) {
    if (!s) return;
    // A frame's worth of clocks is a fixed budget (see flushClocks), so a block spans several blip frames and every one
    // of them is moved into the ring as soon as it closes. That is why the ring, not blip, is what the block drains.
    if (s->pendingClocks >= s->flushClocks) flush(*s);
    left = lowpass(s->lowpassAlpha, s->lastLeft, left);
    right = lowpass(s->lowpassAlpha, s->lastRight, right);
    // A band-limited step per native sample: the delta from the previous filtered sample, at this sample's clock.
    blip_add_delta(s->left, s->pendingClocks, static_cast<int>(left) - s->lastLeft);
    blip_add_delta(s->right, s->pendingClocks, static_cast<int>(right) - s->lastRight);
    s->lastLeft = left;
    s->lastRight = right;
    ++s->pendingClocks;
}

// Ends the current blip frame and moves everything it produced into the ring. Called from putStereo (a block is more
// than one blip frame can hold) and from drain (the block's tail), so it is the only place a frame is ended.
void AudioSink::flush(Source& s) {
    blip_end_frame(s.left, s.pendingClocks);
    blip_end_frame(s.right, s.pendingClocks);
    s.pendingClocks = 0;
    std::uint32_t room = capacity_ - s.ringFill;
    std::uint32_t done = 0;
    while (done < room) {
        const std::uint32_t chunk = std::min<std::uint32_t>(room - done, static_cast<std::uint32_t>(scratchLeft_.size()));
        const int got = blip_read_samples(s.left, scratchLeft_.data(), int(chunk), 0);
        const int gotRight = blip_read_samples(s.right, scratchRight_.data(), int(chunk), 0);
        // Both sides are fed the same clocks at the same rates, so their avail is identical; a mismatch is a bug (and
        // counting it beats writing one side's zeros into the other's slot).
        if (got != gotRight) { ++starved_; return; }
        if (got <= 0) return;
        for (int k = 0; k < got; ++k) {
            const std::uint32_t at = (s.ringRead + s.ringFill + std::uint32_t(k)) % capacity_;
            s.ringLeft[at] = scratchLeft_[std::size_t(k)];
            s.ringRight[at] = scratchRight_[std::size_t(k)];
        }
        s.ringFill += std::uint32_t(got);
        done += std::uint32_t(got);
    }
}
```

`render_audio_adjust_clock` is the one place a chip's rate changes, and it has to END the frame first: a divider that
moves mid-run (the VDP test register switches the PSG from 240 to 112, genesis.c:1129-1130) would otherwise re-rate a
blip that still holds a partial frame, which blip does not allow. So:

```cpp
void AudioSink::adjustClock(Source* s, std::uint64_t masterClock, std::uint64_t sampleDivider) {
    if (!s) return;
    // The samples taken at the OLD rate belong to a frame of their own: close it before the rates change.
    blip_end_frame(s->left, s->pendingClocks);
    blip_end_frame(s->right, s->pendingClocks);
    s->pendingClocks = 0;
    // The chip emits one sample every `sampleDivider` master clocks, so its rate is a rational number and it is kept as
    // one: 53,693,175 / 240 is 223,721.5625, and a truncated integer would put the resampler a hair out, which the ring
    // would collect as latency (about 7 host samples a minute).
    const double native = static_cast<double>(masterClock) / static_cast<double>(sampleDivider ? sampleDivider : 1u);
    s->nativeRate = native;
    s->dt = 1.0 / native;
    // blip's time base is a 64-bit accumulator with `time_unit` at 2^52, so ONE frame can hold at most 4096 output
    // samples' worth of clocks before t*factor overflows (`blip_max_frame = 4000` is the same cliff). 3900 leaves a
    // margin, and the count is in native clocks: 3900 host samples = 3900 * native / host clocks.
    s->flushClocks = static_cast<std::uint32_t>(3900.0 * native / hostRate_);
    if (!s->flushClocks) s->flushClocks = 1;
    // BlastEm's cutoff, from config (audio.lowpass_cutoff, DEFAULT_LOWPASS_CUTOFF 3390 in genesis.c):
    // alpha = dt / (dt + rc), rc = (1 / cutoff) / (2 pi), held as 16.16.
    const double rc = (1.0 / 3390.0) / (2.0 * 3.14159265358979323846);
    s->lowpassAlpha = static_cast<std::uint32_t>(0x10000 * (s->dt / (s->dt + rc)));
    blip_set_rates(s->left, native, hostRate_);
    blip_set_rates(s->right, native, hostRate_);
}
```

`drain` (the ring, primed, and the starvation count):

```cpp
void AudioSink::drain(std::uint32_t frames, float* left, float* right) {
    for (std::uint32_t i = 0; i < frames; ++i) { left[i] = 0.0f; right[i] = 0.0f; }
    for (Source* s : sources_) {
        // The block's tail: putStereo has already flushed every full blip frame, so this closes the last partial one.
        flush(*s);
        const float scale = s->gain / 32768.0f;
        for (std::uint32_t i = 0; i < frames; ++i) {
            if (s->ringFill == 0) { ++starved_; continue; }
            left[i] += static_cast<float>(s->ringLeft[s->ringRead]) * scale;
            right[i] += static_cast<float>(s->ringRight[s->ringRead]) * scale;
            s->ringRead = (s->ringRead + 1) % capacity_;
            --s->ringFill;
        }
    }
}
```

The ring is what decouples the fractional native-to-host ratio (`floor(c * R / native)` at a block end is `n` or `n - 1`)
from the block size, so the stream depends on absolute time alone, and it is sized for a block PLUS the prime and slack,
so `flush` cannot overrun it.

`prepare(maxBlockFrames)` sizes `capacity_ = maxBlockFrames + kPrime + 64` (the prime, the block, and the slack a
resample can deliver in one drain), allocates `scratchLeft_/scratchRight_` at 4096 entries, gives every source a ring of
that capacity and two blips of `maxBlockFrames + kPrime + 64` samples (`blip_new(int(capacity_))`, `blip_clear`), and
calls `prime()`/`adjustClock` on each. `prime(Source&)` fills the ring's first `kPrime` entries with zeros and
`ringFill = kPrime`, once at activation, once after every `setHostRate`, and once after a state load. `setHostRate`
stores the rate, re-primes every ring and re-runs `adjustClock` with each source's last clock (which is also what
recomputes `flushClocks` for the new host rate). `add` and `remove` are
the registry (a `std::vector<Source*>`, 8 is no longer a limit), and **`add` sizes the new source at the current
`capacity_` and primes it**: the chips call `render_audio_source` inside `alloc_config_genesis`, which is after
`prepare` and after `setHostRate`, so a source that waited for the next `prepare` would have no ring for the first
block. `prepare` growing `capacity_` re-primes every source, which the boot path does not depend on (it runs before any
source exists) but a runtime growth does; that is a quiescent control-thread operation, and the one audible cost is that
re-prime. `putMono` duplicates into both sides. `reset()` calls `blip_reset(blip, int(capacity_))` on both sides of
every source and re-primes, which is `clear()`'s ring reset without dropping the registry.

`render_audio_source`/`render_free_source`/... in `BlastemShim.cpp` forward to the instance's sink. **Every one of them
tolerates a null instance**, because the sink's entry points are also called by a context built outside a system (Task
2b's link test allocates one with no system at all, and a thread that ran another case first has a destroyed
`currentInstance` in the pointer):

```cpp
audio_source* render_audio_source(const char* name, uint64_t masterClock, uint64_t sampleDivider, uint8_t channels) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    return sys ? sys->audio().add(name, masterClock, sampleDivider, channels) : nullptr;  // a source-less chip
}
void render_free_source(audio_source* src) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (sys && src) sys->audio().remove(static_cast<rp::blastem::AudioSink::Source*>(src));
}
void render_put_stereo_sample(audio_source* src, int16_t left, int16_t right) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (sys) sys->audio().putStereo(static_cast<rp::blastem::AudioSink::Source*>(src), left, right);
}
// render_audio_adjust_clock, render_put_mono_sample, render_pause_source, render_resume_source and
// render_audio_source_gaindb are the same shape: a null system or a null source is a no-op.
```

(the sink's `add` returns `nullptr` when it has no capacity yet, and every sink entry point is already null-safe, so a
chip that got a null source still runs, silently). `render_end_audio` is a no-op and `render_save_audio` warns: a WAV
dump is not a host feature. **`destroyCore()` and the destructor end with `currentInstance = nullptr`**, or a callback
that runs after a teardown (a context built on this thread with no system) would dereference a destroyed system; Task
2b's `BlastemShim.cpp` therefore starts with a null pointer and every entry point above reads it as "no audio".

Then `BlastemMdSystem` (whose `onSampleRateChanged` gains its audio line here, which is why Task 4's body does not
name `audio_`). Its header gains, in the public section beside `core()`: `AudioSink& audio() { return audio_; }`,
`std::uint64_t starvedSamples() const { return audio_.starvedSamples(); }` and a declared (not inline) test seam,
`void switchPsgClock(bool fast);`, whose body goes in `BlastemMdSystem.cpp` because it names `psg_adjust_master_clock`
and `gen_->psg`, and the header deliberately includes no BlastEm header (it forward-declares `genesis_context`):

```cpp
void BlastemMdSystem::switchPsgClock(bool fast) {
    // The two lines genesis.c runs around vdp_test_port_write when the VDP test register's bit 0 changes its mind about
    // the Z80's clock source: the PSG's divider becomes MCLKS_PER_YM * 16 (112) or MCLKS_PER_PSG (240), and
    // psg_adjust_master_clock tells the sink. Writing the test register directly does NOT do this (the before/after
    // comparison is genesis.c's), which is why a case calls this instead.
    if (!gen_ || !gen_->psg) return;
    gen_->psg->clock_inc = fast ? 112u : 240u;
    psg_adjust_master_clock(gen_->psg, gen_->master_clock);
}
```

and a private `AudioSink audio_;` member (so the header includes `system/blastem/BlastemAudio.hpp`).

- `onActivate` sizes the mix and the sink FIRST, at `blockFrames(SystemBase::kDefaultBlockFrames)` (8192 when no node has
  reserved anything, which is the case for every core a case builds directly), then calls `audio_.setHostRate(sampleRate_)`
  **before** `alloc_config_genesis` (the chips ask for their source during it), then boots, and calls
  `SystemBase::reserveAudio(blockFrames(SystemBase::kDefaultBlockFrames))` LAST (its contract: a sizing that throws
  leaves the recorded maximum as it was).
- `onSampleRateChanged` gains one line after Task 4's re-anchor: `audio_.setHostRate(sampleRate_)` (which re-primes every
  ring, so the prime's 16 samples are silence at the new rate).
- `reserveAudio(maxBlockFrames)` sizes `mixLeft_`/`mixRight_` and the sink for the larger of the new maximum and what is
  already recorded, then calls `SystemBase::reserveAudio(maxBlockFrames)` LAST.
- `finishBlock` drains into the lanes and applies the smoothed gain:

```cpp
void BlastemMdSystem::onActivate(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    // Sized for a core built outside a plan (a test, a tool), which reserves nothing: the engine's default plan block.
    const std::uint32_t frames = blockFrames(SystemBase::kDefaultBlockFrames);
    mixLeft_.resize(frames);
    mixRight_.resize(frames);
    audio_.prepare(frames);
    audio_.setHostRate(sampleRate_);  // before the chips exist: they ask for their source during alloc_config_genesis
    boot();
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();  // the first value is the node's, not a ramp from unity
    SystemBase::reserveAudio(frames);    // LAST, per its contract
}

void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) {
    const std::uint32_t frames = std::max(maxBlockFrames, blockFrames(0));
    if (frames > mixLeft_.size()) {
        mixLeft_.resize(frames);
        mixRight_.resize(frames);
        audio_.prepare(frames);
    }
    SystemBase::reserveAudio(maxBlockFrames);  // LAST: the maximum moves only once the sizing has succeeded
}

void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) {
    if (!gen_) {
        sampleBase_ += info.frames;  // not activated: nothing to drain
        return;
    }
    // Drained EVERY time, lanes or none. A lane-less block still ends the sink's frame: blip's buffer is sized for one
    // block plus the prime, so a `return` before the drain would leave the second block's deltas past its end and
    // either corrupt memory or trip blip's own assert (the rebase case and the video case both pass no lanes).
    audio_.drain(info.frames, mixLeft_.data(), mixRight_.data());
    const bool wired = laneCount >= 2 && outs && outs[0] && outs[1];
    if (wired) {
        // The block's samples are sized at activation and at every growth (reserveAudio), never here: the engine's
        // [alloc] cases count a block's allocations, emulation included.
        for (std::uint32_t i = 0; i < info.frames; ++i) {
            const float gain = gainSmoother_.next();
            outs[0][i] += mixLeft_[i] * gain;   // added, not written: SystemBase's contract
            outs[1][i] += mixRight_[i] * gain;
        }
    }
    sampleBase_ += info.frames;
}
```

with `std::vector<float> mixLeft_, mixRight_;` members sized here (never reallocated on the block thread).

- [ ] **Step 6: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[audio]"
build/bin/retroplug-blastem-test "[stop]"   # the triad changed
```

Expected: all pass. If the FM/PSG comparison case fails (the renders are equal), the ROM's FM writes are not reaching
the chip: check `fmPsgRom`'s register order against the YM2612's (a data byte needs its address byte first on the same
port), and remember the ROM runs to the `bra .` after them, so the key-on is at $28 = $F0 set last.

- [ ] **Step 7: Commit**

```bash
git add deps/blastem cmake/blastem.cmake \
        packages/blastem/native/src/system/blastem/BlastemAudio.hpp \
        packages/blastem/native/src/system/blastem/BlastemAudio.cpp \
        packages/blastem/native/src/system/blastem/BlastemShim.cpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemAudio.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): band-limited audio at the host rate, BlastEm's filter at the native rates

render_audio.c is dropped for our own sink: blip_buf per chip and per side,
the 3390 Hz one-pole and the chip gains at the native rates, and a ring the
block drains exactly. The mirror carries blip_buf 1.1.0."
```

Expected: the commit carries the mirror move too. **Ask the user to push the mirror before CI.**

---

### Task 8: The port-2 device: levels at their cycles, and the ROM's own output pins

**Files:**
- Mirror (`deps/blastem`, branch `retroplug`): `io.h`, `io.c` (`IO_CUSTOM`)
- Create: `packages/blastem/native/src/system/blastem/BlastemPortDevice.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemPort.test.cpp`
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}` (`pushCoreBytes`,
  `hasCoreBytesIn`, `levelsAccepted`), `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: the master-clock mapping (Task 5), `BlastemMdSystem::core()`.
- Produces: `rp::blastem::PortDevice` with `void enqueue(std::uint64_t mclk, std::uint8_t level)`, `void clear()`,
  `std::uint64_t accepted()`, `std::uint64_t dropped()`, `std::uint8_t lastApplied()`, and `std::uint8_t
  level(std::uint64_t mclk)` (the level in force at that clock, having applied everything due); the C entry point
  `io_custom_input` the mirror declares; `BlastemMdSystem::pushCoreBytes(frame, data, size, flush)` (frame is the
  intra-block sample offset), `hasCoreBytesIn()` (true, as `MesenSmsSystem`'s is: the queue exists whether or not a role
  is wired, and an unwired port simply never has anything in it), `levelsAccepted()`, and test seams
  `writePortControl(value)` / `writePortData(value)` (BlastEm's own `io_control_write` / `io_data_write` on
  `gen->io.ports[1]` at `mclkNow()`). (spec/13's port-2 and clock-model paragraphs are the docs task's, with the rest of
  the spec text: the spec describes the finished shape, and this task's own comments are where the device's timing rule
  lives until then.)

- [ ] **Step 1: The mirror commit: `IO_CUSTOM`**

BlastEm's `io_data_read` already has the direction rule and the pull-ups, in its own `switch` and then a common tail
(`io.c:1533-1843`): `control = port->control | 0x80`, `value = (input & ~control & device_driven) | (port->output &
control)`, and pins not driven by the device float high. A host device therefore supplies only `input` and says which
bits it drives. That is the whole patch: one enum value, one entry point, one switch case, one name.

In `deps/blastem/io.h`, add `IO_CUSTOM` to the device enum (at the end, so no existing value moves) and the entry point,
with the marker:

```c
	IO_HEARTBEAT_TRAINER,
	/* RetroPlug: a device the host drives. The port's reads are handed to the host with their exact master clock (the
	 * same cycle every other device gets), which is what lets a host hold a level on a pin from a queued schedule
	 * instead of at a block boundary. It supplies the pins it DRIVES; the direction rule and the pull-ups on the rest
	 * are io_data_read's own tail, unchanged. */
	IO_CUSTOM
};

/* RetroPlug: the host device's input byte for this read (bit 5 TR, bit 6 TH for the Mega Drive's port 2). */
uint8_t io_custom_input(io_port *port, uint32_t current_cycle);
```

In `io.c`:
- `device_type_names[]` gains `"Host (RetroPlug)"` at IO_CUSTOM's index, so a debug message that names a port's device
  names this one.
- `io_data_read`'s switch gains, before `default:`:

  ```c
  	case IO_CUSTOM:
  		input = io_custom_input(port, current_cycle);
  		/* RetroPlug: only the two sync pins are the host's. The tail then mixes in this port's own data latch on the
  		 * pins the ROM made outputs ($A1000B), and pulls the undriven bits high, exactly as it does for a pad. */
  		device_driven = 0x60;
  		break;
  ```

- Nothing else. There is no `io_data_write` case and no `io_control_write` hook: `io_data_write` stores `port->output`
  before its switch and `io_control_write` stores `port->control` at io.c:1289, so both are already there for the tail to
  read. There is no `io_run` hook either: `io_run` (io.c:1208) only services the serial device types, and the
  `process_events()` switch the first draft's comment referred to is in `io_data_read` (io.c:1540), where it already
  runs on every poll.

Commit in the submodule:

```bash
cd deps/blastem
git add io.h io.c
git commit -m "io: a device the host drives, with each access's master clock

Every other device answers from its own state. A host-driven device answers
from a schedule, so it needs the read's exact cycle. It supplies only the pins
it drives: io_data_read's tail keeps the direction rule, the ROM's own output
latch and the pull-ups on everything else."
git -c protocol.file.allow=always push \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
cd ../..
```

Expected: the commit's subject in both `deps/blastem` and the mirror, and `grep -n IO_CUSTOM deps/blastem/io.c` shows the
name table's entry and the one switch case.

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemPort.test.cpp`:

```cpp
// The port-2 device (design section 3): a level lands at its scheduled master clock, not at the block boundary; a level
// staged past a block's end keeps its timing into the next; a pin the ROM drives as an output reads the ROM's own
// latch; and the ROM's own poll count pins the sample mapping across a mid-session rate change.
//
// Every case boots portPollLoopRom (a read and a store, forever): portPollRom reads ONCE, so after the boot blocks it
// has nothing left to show and a case that used it would fail whatever the device did (the design's first draft did).
#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;
constexpr std::uint8_t  kIdle   = 0x60;  // TR and TH released: the level a disconnected pad leaves

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

std::uint8_t ramByte(rp::blastem::BlastemMdSystem& sys, std::uint32_t addr) {
    // BlastEm stores work RAM as 16-bit words: the 68k's byte at an even address is the word's high half, so the byte
    // view is swapped. getMemory presents address order; here the raw view is what this case reads.
    return static_cast<std::uint8_t*>(static_cast<void*>(sys.core()->work_ram))[addr ^ 1];
}

std::uint16_t ramWord(rp::blastem::BlastemMdSystem& sys, std::uint32_t addr) {
    return static_cast<std::uint16_t>((ramByte(sys, addr) << 8) | ramByte(sys, addr + 1));
}

// The largest |sample| in a render: the audio case's helper, which is how a case tells "the tone is playing" from
// "the sink gave silence" (and so keeps a search for an edge from passing on a silent render).
double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

// A level on the host's side of port 2: 0x40 is TR low, TH high.
constexpr std::uint8_t kLevel = 0x40;

}  // namespace

TEST_CASE("a level reaches the ROM's port read at its own sample", "[blastem][port]") {
    auto sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    // Let the ROM boot and settle on its poll loop; the idle level reads $60, or $7F with the pull-ups' bits counted.
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == kIdle);
    sys->pushCoreBytes(100, &kLevel, 1);  // inside this block, mid-block
    CHECK(sys->levelsAccepted() == 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == (kLevel & 0x60));
    CHECK(sys->starvedSamples() == 0);
}

TEST_CASE("a level staged past the block's end keeps its timing", "[blastem][port]") {
    auto sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    sys->pushCoreBytes(0, &kIdle, 1);
    // Four frames INTO the next block, not at its first frame: a stop lands at or past its target by up to two 68k
    // instructions (about 2000 master clocks, under half a host sample here), so a level due exactly at the boundary
    // could be applied by the last poll the first block runs past the target. Four frames (about 4,474 clocks) is past
    // that overshoot by design, which is what makes "this block still sees idle" an assertion about the queue's timing
    // and not about where the instruction boundary fell.
    sys->pushCoreBytes(kFrames + 4, &kLevel, 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == kIdle);          // this block still sees the idle level
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == (kLevel & 0x60));  // the next one sees it
}

TEST_CASE("a pin the ROM drives as an output reads the ROM's latch", "[blastem][port]") {
    // The direction rule is BlastEm's own tail ($A1000B's bits, then the port's data latch), so this asks the port
    // directly: the host holds both lines low, the ROM makes both outputs and drives them high, and the read is the
    // ROM's latch.
    auto sys = boot(rp::blastem::test::portPollLoopRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    const std::uint8_t low = 0x00;  // the host drives both lines low
    sys->pushCoreBytes(0, &low, 1);
    sys->writePortControl(0x60);    // $A1000B = $60: TR and TH are outputs
    sys->writePortData(0x60);       // $A10005 = $60: the ROM's own latch is high
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x60);  // the ROM's latch, not the host's level
}
```

`writePortControl`/`writePortData` are test seams on the system (see Step 4): they call BlastEm's own
`io_control_write` / `io_data_write` on `gen->io.ports[1]` at `mclkNow()`.

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[port]"
```

Expected: FAIL: `pushCoreBytes` is `SystemBase`'s no-op, so nothing reaches the port.

- [ ] **Step 4: Implement the device**

`BlastemPortDevice.hpp`:

```cpp
#pragma once

#include <cstdint>

#include "system/InputCapacity.hpp"
#include "util/FixedQueue.hpp"

namespace rp::blastem {

// Controller port 2 as the host device (design section 3). A queue of (master clock, level byte) entries; each access
// applies everything due at or before its own clock and then answers. The level byte's bits are the pins: bit 5 (TR)
// carries genmddj's counter bit 0, bit 6 (TH) its bit 1, and the rest idle high, which is what a disconnected pad
// leaves. `pushCoreBytes` is the only door in, so an idle port is a port with an empty queue.
class PortDevice {
public:
    // Sized to what one block's doors can deliver, as SmsSyncRole's is: the bytes input's 1 KiB, or a message the
    // midi input carries (72 KiB) through pushCoreBytes. A level is one byte.
    static constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;

    void enqueue(std::uint64_t mclk, std::uint8_t level);
    void clear() { pending_.clear(); needsSort_ = false; }
    // [device] Applies every entry due at `mclk` and returns the LEVEL BYTE in force: bits 5 and 6 are the host's TR
    // and TH, and nothing else is the host's to decide (io_data_read's tail mixes in the ROM's latch and pulls the
    // rest high).
    std::uint8_t level(std::uint64_t mclk);
    // [device] Whether anything is queued (a test's read of whether a block drained its schedule).
    bool empty() const { return pending_.empty(); }

    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t dropped() const { return pending_.dropped(); }
    std::uint8_t  lastApplied() const { return lastApplied_; }

private:
    struct Entry {
        std::uint64_t mclk;
        std::uint8_t  level;
    };
    rp::FixedQueue<Entry>      pending_{kCapacity};
    rp::SortScratch<Entry>     sortScratch_{kCapacity};
    bool                       needsSort_ = false;
    std::uint64_t              accepted_ = 0;
    std::uint8_t               lastApplied_ = 0x60;  // idle: TR and TH released (a pad's pull-ups)
};

}  // namespace rp::blastem
```

`BlastemPortDevice.cpp`:

```cpp
#include "system/blastem/BlastemPortDevice.hpp"

#include <algorithm>

namespace rp::blastem {

void PortDevice::enqueue(std::uint64_t mclk, std::uint8_t level) {
    if (pending_.push({mclk, level})) ++accepted_;
    needsSort_ = true;  // a later push can carry an earlier clock: sort once before draining, as SmsSyncRole does
}

std::uint8_t PortDevice::level(std::uint64_t mclk) {
    if (needsSort_) {
        sortScratch_.sort(pending_.begin(), pending_.end(),
                          [](const Entry& a, const Entry& b) { return a.mclk < b.mclk; });
        needsSort_ = false;
    }
    while (!pending_.empty() && pending_.front().mclk <= mclk) {
        lastApplied_ = pending_.front().level;
        pending_.pop();
    }
    // The two pins this device drives, and nothing more: the direction rule on $A1000B, the ROM's own output latch and
    // the pull-ups on every other bit are io_data_read's tail, which is why this answer is a level and not a byte.
    return std::uint8_t(lastApplied_ & 0x60);
}

}  // namespace rp::blastem
```

Then the C entry point at the bottom of the same file:

```cpp
extern "C" {
uint8_t io_custom_input(io_port* port, uint32_t cycle) {
    (void)port;
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    // No instance (a context built outside this package) drives nothing: io_data_read's tail then reads the pull-ups.
    if (!sys) return 0;
    return sys->portDevice().level(sys->mclkOfCoreCycle(cycle));
}
}
```

`mclkOfCoreCycle(cycle)` is `gen->cycle_base + cycle`: add to the system's header
`std::uint64_t mclkOfCoreCycle(std::uint32_t cycle) const { return gen_ ? std::uint64_t(gen_->cycle_base) + cycle : 0; }`,
`PortDevice& portDevice() { return port_; }`, `std::uint64_t levelsAccepted() const { return port_.accepted(); }`, and a
`PortDevice port_;` member. A write needs no handler at all: `io_data_write` stores `port->output` and
`io_control_write` stores `port->control`, which ARE the ROM's latch and direction register, so the tail applies the
rule from them without the host seeing either.

`BlastemMdSystem` gains:

```cpp
void BlastemMdSystem::pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool) {
    if (!gen_ || !data || size == 0) return;
    const std::uint64_t at = masterClockOf(sampleBase_ + frame);
    for (std::size_t i = 0; i < size; ++i) port_.enqueue(at, data[i]);
}
```

and after `alloc_config_genesis` succeeds, in `boot()`:

```cpp
    // Port 2 is the host's device, not a pad: a game that reads player 2 sees the pins' idle state (which is a pad's
    // nothing-pressed state minus the pad's response), and a role claims the lines by pushing levels.
    gen_->io.ports[1].device_type = IO_CUSTOM;
```

- [ ] **Step 5: The sample mapping: the ROM's own poll count, and the audio edge it predicts**

Two cases, because they answer two questions the design asks separately: WHERE the level's sample is (the audio edge,
in host samples) and WHICH master clock the mapping gave it (the poll ROM's count, in the core's own units, across a
mid-session rate change).

```cpp
// A render of syncEdgeRom's left lane at `blocks` blocks of `kFrames`, with a level staged at absolute frame `at` when
// `staged`, and nothing staged when not. Two runs of the same ROM differ only in whether the level reached the port, so
// their first differing sample IS the level's own sample, which is what the edge case measures.
std::vector<float> renderEdge(std::uint32_t at, bool staged, int blocks) {
    auto sys = boot(rp::blastem::test::syncEdgeRom());
    std::vector<float> l(kFrames), r(kFrames), out;
    std::uint32_t base = 0;
    for (int i = 0; i < blocks; ++i) {
        if (staged && base <= at && at < base + kFrames) {
            const std::uint8_t level = 0x00;
            sys->pushCoreBytes(at - base, &level, 1);
        }
        runBlock(*sys, l, r);
        out.insert(out.end(), l.begin(), l.end());
        base += kFrames;
    }
    REQUIRE(sys->starvedSamples() == 0);
    return out;
}

TEST_CASE("a level's audio edge lands at the frame it was staged for", "[blastem][port]") {
    // The detector is a DIFF against the same ROM with no level staged, not a magnitude threshold: blip high-passes its
    // output (bass_shift 9, a time constant of about 512 samples) and BlastEm's PSG is unipolar (0 or the volume), so
    // silencing the tone leaves a decaying DC tail of about 0.035 for thousands of samples. A "two samples under
    // 0.001" search therefore either fires thousands of samples late, or never.
    const std::uint32_t at = 9000;  // inside the 18th 512-frame block (9000 / 512 = 17.6)
    const std::vector<float> reference = renderEdge(at, false, 40);
    const std::vector<float> staged = renderEdge(at, true, 40);
    REQUIRE(reference.size() == staged.size());
    REQUIRE(peak(reference) > 0.01);  // the tone plays: silence would make the search vacuous
    std::size_t edge = staged.size();
    for (std::size_t i = 0; i < staged.size(); ++i) {
        if (staged[i] != reference[i]) { edge = i; break; }
    }
    INFO("staged at " << at << ", first difference at " << edge);
    REQUIRE(edge < staged.size());       // found at all, never `edge == at` by default
    // The sink delays every sample by its 16-sample prime, and blip's kernel reaches up to 8 samples AHEAD of the delta
    // it belongs to (`half_width`, blip_buf.c), so the first sample the level can touch is at or after `at` (the prime
    // more than covers the lead) and its own sample is around at + 16. The ROM's poll (~0.34 of a host sample) and the
    // PSG's next 240-clock sample add under one sample. A level applied at the block boundary instead would land up to
    // 511 samples late.
    CHECK(edge >= at);
    CHECK(edge <= at + rp::blastem::AudioSink::kPrime + 16u);
}

TEST_CASE("a mid-session rate change maps the same sample to the clock the anchor predicts", "[blastem][port]") {
    // portTimestampRom counts its own polls and stores the count when the level changes, so the count IS the master
    // clock at which the level was applied, in units of one poll loop (~191.6 master clocks, measured on this core).
    // The prediction is built from the core, not from a constant: a run measures the poll period at one rate, and a run
    // that changes rate mid-session must move the count by exactly the clocks the change adds. A mapping that ignored
    // the change (or re-anchored wrongly) moves it by nothing, or by the wrong sign, which no tolerance hides.
    //
    // Both stage points are early on purpose: the ROM counts into a WORD, and 300-8000 host samples keep every count
    // under 65536 polls (one poll is about 191.6 master clocks, so the word wraps after about 11,000 samples at 48 kHz).
    constexpr std::uint64_t kMclk = 53693175ull;
    const auto count = [](std::uint64_t stageAt, std::uint64_t changeAt, double newRate) {
        auto sys = boot(rp::blastem::test::portTimestampRom());
        std::vector<float> l(kFrames), r(kFrames);
        float* lanes[2] = {l.data(), r.data()};
        std::uint64_t base = 0;
        std::uint16_t last = 0;
        AudioBlockInfo info{};
        info.frames = kFrames;
        while (base <= stageAt + kFrames) {
            if (changeAt && base <= changeAt && changeAt < base + kFrames) {
                sys->onSampleRateChanged(newRate);  // between blocks, as a host does
            }
            if (base <= stageAt && stageAt < base + kFrames) {
                const std::uint8_t level = kLevel;
                sys->pushCoreBytes(static_cast<std::uint32_t>(stageAt - base), &level, 1);
            }
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            sys->prepareForBlock(info);
            while (sys->stepIfBelowTarget(kFrames)) {}
            sys->finishBlock(info, lanes, 2);
            last = ramWord(*sys, 0x0000);
            base += kFrames;
        }
        return std::uint32_t(last);
    };
    const std::uint64_t s1 = 3000, s2 = 8000;  // two staged levels, 5000 samples apart, at 48 kHz throughout
    const std::uint32_t c1 = count(s1, 0, 0.0);
    const std::uint32_t c2 = count(s2, 0, 0.0);
    REQUIRE(c1 > 0);
    REQUIRE(c2 > c1);
    const double perPoll = (double(s2 - s1) * double(kMclk) / kRate) / double(c2 - c1);
    const std::uint64_t changeAt = 5000;  // between s1 and s2
    const std::uint32_t c3 = count(s2, changeAt, 44100.0);
    // The clocks the change adds over the samples between it and s2, in polls. Same staged sample both times, so the
    // comparison is c3 against c2, never against c1 (which staged a different sample).
    const double expected = double(s2 - changeAt) * (double(kMclk) / 44100.0 - double(kMclk) / kRate) / perPoll;
    INFO("polls " << c1 << " " << c2 << " " << c3 << ", per poll " << perPoll << ", expected shift " << expected);
    CHECK(std::llabs((std::int64_t(c3) - std::int64_t(c2)) - std::int64_t(std::llround(expected))) <= 3);
}
```

The second case's tolerance is three polls, which is about half a host sample at 48 kHz: a mapping error smaller than
that cannot be resolved by any ROM, and the failure this catches (a no-op or a wrongly anchored rate change) moves the
count by the whole shift, about 1500 polls. `ramWord` is the helper above; `peak` is the audio case's own.

Note the includes these two cases add to the file: `<cmath>` (`fabs`) and `<cstdlib>` (`llabs`, `llround` is `<cmath>`).

- [ ] **Step 6: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[port]"
```

Expected: all five cases pass. What each proves, and what it does not: the first two prove delivery and the queue's
timing (a level staged mid-block, and one staged past the block's end), the third the direction rule, the fourth the
level's own sample (the first sample that differs from a no-level render, inside a 32-sample window after the staged
frame, and it FAILS when no sample differs instead of passing with `edge == at`), and the fifth the master clock the
mapping gives a sample across a rate change, in the core's own poll units. Only the fourth and fifth are sample-level,
and Task 9 then holds them to a partition.

- [ ] **Step 7: Commit**

```bash
git add deps/blastem \
        packages/blastem/native/src/system/blastem/BlastemPortDevice.hpp \
        packages/blastem/native/src/system/blastem/BlastemPortDevice.cpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemPort.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): controller port 2 is the host device, one cycle-exact level at a time"
```

---

### Task 9: Block-size invariance

**Files:**
- Create: `packages/blastem/native/test/BlastemInvariance.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: everything in Tasks 4 to 8.
- Produces: the proof the design calls a hard requirement (section 2), and the decision Task 10 acts on if it fails.

- [ ] **Step 1: Write the test**

```cpp
// Block-size invariance (design section 2): the same input rendered with host blocks of 1, 64, 1024 and 4096 frames is
// byte-identical. Every host hands a plugin a different partition of the same 2 seconds, and the parity baseline
// compares bytes: an output that moves with the partition makes both meaningless.
//
// The stimulus is a sync level staged at a fixed absolute frame, so the level's sample lands inside blocks of every
// size, and the ROM answers it with an audio edge. That the stimulus really produces the edge (a diff against a run
// with no level) is the second case's job.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate       = 48000.0;
constexpr std::uint64_t kTotal      = 96000;             // 2 s
constexpr std::uint64_t kLevelFrame = 41000;             // an odd frame, inside every partition differently

// The largest |sample| in a render: a silent render is invariant for the wrong reason, so the case checks this first.
double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

// A render of the whole 2 s, as two lanes INDEXED BY FRAME (not interleaved, and not concatenated a block at a time:
// block = 1 would make a per-block `left`-then-`right` vector frame-interleaved, so index kLevelFrame would address
// frame 20500 and the "after" window would still contain the tone). `staged` writes the level at kLevelFrame.
struct Render {
    std::vector<float> left, right;
    bool               starved = false;
};

Render renderAt(std::uint32_t block, bool staged = true) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::syncEdgeRom());
    sys->onActivate(kRate);
    Render out;
    out.left.reserve(kTotal);
    out.right.reserve(kTotal);
    std::vector<float> left(block), right(block);
    float* lanes[2] = {left.data(), right.data()};
    std::uint64_t at = 0;
    bool sent = false;
    AudioBlockInfo info{};
    while (at < kTotal) {
        info.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, kTotal - at));
        if (staged && !sent && at + info.frames > kLevelFrame) {
            const std::uint8_t level = 0x00;
            sys->pushCoreBytes(static_cast<std::uint32_t>(kLevelFrame - at), &level, 1);
            sent = true;
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys->prepareForBlock(info);
        while (sys->stepIfBelowTarget(info.frames)) {}
        sys->finishBlock(info, lanes, 2);
        out.left.insert(out.left.end(), left.begin(), left.begin() + info.frames);
        out.right.insert(out.right.end(), right.begin(), right.begin() + info.frames);
        at += info.frames;
    }
    out.starved = sys->starvedSamples() != 0;
    return out;
}

}  // namespace

TEST_CASE("1, 64, 1024 and 4096 frame blocks render identical bytes", "[blastem][invariance]") {
    const Render one = renderAt(1);
    const Render sixtyFour = renderAt(64);
    const Render thousand = renderAt(1024);
    const Render fourThousand = renderAt(4096);
    REQUIRE(one.left.size() == kTotal);
    REQUIRE(one.right.size() == kTotal);
    // Before the comparison, the case has to be ABOUT something: a render that is silent (a broken ROM, a starved
    // sink) is identical at every partition and would pass this test while proving nothing.
    REQUIRE(peak(one.left) > 0.01);
    // A starved sink's zeros are placed by the partition, so it is the sink failing this case, not the stop model: the
    // audio and invariance cases assert it is 0, and the parity group's recorded bytes are where a stray zero would show
    // up as a digest change.
    CHECK_FALSE(one.starved);
    CHECK_FALSE(sixtyFour.starved);
    CHECK_FALSE(thousand.starved);
    CHECK_FALSE(fourThousand.starved);
    CHECK(one.left == sixtyFour.left);
    CHECK(one.left == thousand.left);
    CHECK(one.left == fourThousand.left);
    CHECK(one.right == sixtyFour.right);
    CHECK(one.right == thousand.right);
    CHECK(one.right == fourThousand.right);
}

TEST_CASE("the invariance case's stimulus really produces its edge", "[blastem][invariance]") {
    // The level staged at kLevelFrame silences syncEdgeRom's tone. The proof is a DIFF against the same render with no
    // level, not a magnitude threshold: blip's high-pass leaves a decaying DC tail (about 0.035 for thousands of
    // samples) after the tone stops, so "the peak after the level is under 0.001" is false for thousands of samples and
    // a window that skipped straight to a quiet stretch would be a different, weaker claim.
    const Render staged = renderAt(1);
    const Render reference = renderAt(1, false);
    REQUIRE(peak(reference.left) > 0.01);  // the tone really plays all the way through when nothing is staged
    std::size_t edge = staged.left.size();
    for (std::size_t i = 0; i < staged.left.size(); ++i) {
        if (staged.left[i] != reference.left[i]) { edge = i; break; }
    }
    INFO("level staged at " << kLevelFrame << ", first difference at " << edge);
    REQUIRE(edge < staged.left.size());
    // The same window the port case uses: at or after the staged frame (the 16-sample prime covers the kernel's
    // 8-sample lead), and no later than the prime plus 16. Anything later is a level applied at a block boundary (up to
    // 511 samples at 512-frame blocks).
    CHECK(edge >= kLevelFrame);
    CHECK(edge <= kLevelFrame + rp::blastem::AudioSink::kPrime + 16u);
}
```

- [ ] **Step 2: Run it and record what it says**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[invariance]"
```

Expected, and both outcomes are a result:

- **PASS**: the design's stop model is transparent. Commit it (Step 3) and delete Task 10's steps from this plan when
  you reach it, with a note in the commit that the fallback was not needed.
- **FAIL**: the LIKELY outcome, and neither a surprise nor a defect in this test. BlastEm charges refresh delays and
  interrupt-latency syncs at each `sync_components` (genesis.c:519-524, 425-434), and `resume_context` plus
  `m68k_execute`'s own entry sync (m68k_util.c:82-86) put two syncs in every `run_until`, so the stops themselves move
  the emulation by a few cycles and a block end is a partition-dependent stop. Task 10 is the fix, and this test is how
  it is proved. Record the numbers before doing anything else, because they are what Task 10's decision is made on:

```bash
build/bin/retroplug-blastem-test "[invariance]" -s 2>&1 | tail -30
```

  Note *where* the first difference is (a sample index, and how far into the render), and whether the 64-frame render
  agrees with the 1024 one (a shared cause) or not (a partition-dependent one). Write those numbers into the commit
  message of the next commit, whether that is Step 3 or Task 10. Do not weaken this test to make it pass.

- [ ] **Step 3: Commit (on a pass)**

```bash
git add packages/blastem/native/test/BlastemInvariance.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "test(blastem): 1, 64, 1024 and 4096 frame blocks render identical bytes

The stop model is transparent: the stop points the host's partition creates
change no output byte, so the parity baseline can compare a Mega Drive."
```

If it failed, commit the test itself first (`test(blastem): the block-size invariance case, failing`, with the numbers
in the body) so the next task's fix has a failing case to turn green.

---

### Task 10: If Task 9 failed: stop on a fixed sample grid

**Files:**
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}`
- Modify: `packages/blastem/native/test/BlastemInvariance.test.cpp`

**Interfaces:**
- Consumes: Task 9's failing case.
- Produces: `BlastemMdSystem`'s stop schedule: one stop per host sample, independent of the host's block partition.
  **Do this task only if Task 9 failed.** If it passed, skip it entirely and say so in Task 24's report.

- [ ] **Step 1: Change the stop schedule**

The study's warning is the reason this exists (`STEPPING-STUDY.md`, BlastEm section 3): BlastEm charges refresh delays
and interrupt-latency syncs at the moment it syncs, so *where* the stops fall moves the emulation by a few cycles.
A host block's end is a partition-dependent stop. The fix is to stop on a grid that depends only on absolute time: one
stop per host sample, so 1-frame and 4096-frame blocks hit exactly the same set of stops.

In `BlastemMdSystem`, replace `stepIfBelowTarget`'s single `run_until` with a loop over the block's samples:

```cpp
bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t) {
    // One stop per host sample: the grid is absolute time, so a block's partition cannot move a stop. About 8% of the
    // block's cost is the extra syncs (the study's estimate for SameBoy's model, which this is).
    const std::uint64_t end = sampleBase_ + blockFrames_;
    for (std::uint64_t s = nextGridSample_; s < end; ++s) {
        run_until(masterClockOf(s + 1));
        nextGridSample_ = s + 1;
    }
    return false;
}
```

with `std::uint64_t nextGridSample_ = 0;` in the header, and `blockFrames_`/`sampleBase_` as Task 5 left them. Note
`run_until` is already a no-op when the clock has passed, so a partial sample costs nothing.

- [ ] **Step 2: Run the case**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[invariance]"
```

Expected: PASS. If a single sample grid still differs, the cause is a component that is not caught up at the stop
(`sync_components` runs `psg_run`/`ym_run`/`vdp_run_context`/`io_run` before the stop check, which is what makes the
grid's own partition irrelevant); report the numbers rather than adding a second grid.

- [ ] **Step 3: Note the cost and the reason in the code**

The block's cost with this grid is one `resume_context` per sample. Verify it is not absurd on this host:

```bash
build/bin/retroplug-blastem-test "[audio]" --durations yes 2>&1 | tail -8
```

Expected: the audio cases' total stays in the tens of milliseconds for a quarter second of audio (that is, the render
still runs at tens of times real time). Record the number in Task 23's bench as the reference, and add one line to
`spec/13`'s Mega Drive section in Task 24 saying the stop grid is one host sample and why.

- [ ] **Step 4: Commit**

```bash
git add packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemInvariance.test.cpp
git commit -m "fix(blastem): stop on one host sample's grid, so a partition cannot move a stop

BlastEm charges refresh and interrupt-latency syncs at each sync, so the
block's end as a stop point made the output depend on the host's block size.
The grid is absolute time now."
```

---

### Task 11: Two instances are two consoles

**Files:**
- Mirror (`deps/blastem`, branch `retroplug`): `genesis.c`, `genesis.h` (`vdp->system` instead of `current_system`, the
  per-instance `z80_map`, `MCLKS_PER_68K`, the allocation mutex), `vdp.c`, `vdp.h`
- Modify: `packages/blastem/native/src/system/blastem/BlastemShim.cpp` (the instance pointer on every entry)
- Create: `packages/blastem/native/test/BlastemInstances.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: the system and its triad, and the mirrors' own entry points (`read_dma_value`, `z80_invalidate_code_range`).
- Produces: nothing new to call; it changes what two instances do to each other, and it is the task that stops BlastEm's
  allocation path being shared state.

- [ ] **Step 1: The mirror commit: the DMA reads its own console's RAM**

The study's first and worst global: `read_dma_value` (genesis.c:309) and `vdp_dma_started` (:345) cast the process
global `current_system`, so with two instances a VDP DMA reads the *other* console's work RAM. Both are called from
`vdp.c` with a `vdp_context` in hand, and `shared_init` already sets `gen->vdp->system = &gen->header`.

In `deps/blastem/vdp.h`, change the two declarations:

```c
/* RetroPlug: the DMA source read takes the VDP it belongs to. The old form read the process-global current_system,
 * so two consoles in one process would DMA out of each other's RAM. */
uint16_t read_dma_value(vdp_context *vdp, uint32_t address);
void vdp_dma_started(vdp_context *vdp);
```

In `genesis.c`, the two definitions take the vdp and reach their console through it:

```c
uint16_t read_dma_value(vdp_context *vdp, uint32_t address)
{
	genesis_context *genesis = (genesis_context *)vdp->system;
	...unchanged body...
}
void vdp_dma_started(vdp_context *vdp)
{
	genesis_context *genesis = (genesis_context *)vdp->system;
	...unchanged body...
}
```

In `vdp.c`, `read_dma_value((context->regs[...] ...))` becomes `read_dma_value(context, ...)`, and the
`vdp_dma_started()` call site passes its own context. `genesis.c`'s one other caller (`get_open_bus_value`) passes
`genesis->vdp`, and `gdb_remote.c`'s calls pass `context->get_vdp(&context->header)` (it is not compiled here, but the
mirror must build on its own Makefile too).

- [ ] **Step 2: The same commit: the 68k's clock divider is not a variable**

`MCLKS_PER_68K` is a file-scope `uint32_t` in `genesis.c` (:29) written by every allocation from the config file
(`shared_init`, :2840), and read on the hot path (`gen_update_refresh*`). It is always 7 for a Mega Drive, and as a
variable it is shared state between instances. Replace it:

```c
/* RetroPlug: the 68k's clock divider is fixed at 7 on a Mega Drive (the M68K runs at MCLK/7). It was a writable
 * file-scope variable that every allocation set from the config tree, which is shared state no second instance can
 * own; there is no Mega Drive configuration in which it is anything else. */
#define MCLKS_PER_68K 7
```

and delete the reads of `clocks\0m68k_divider\0` in `shared_init`, `shared_init_gen` and `alloc_config_pico` (:3397: a
`#define` written to is a compile error, so the file would stop building). Leave the config key itself alone: nothing
reads it now, and removing it from `blastem.cfg` is not this plan's business.

- [ ] **Step 3: The same commit: the Z80's map is per instance, and allocation is serialised**

Two more globals, both worse than the DMA one because neither is a one-line signature change:

- **`z80_map`.** `shared_init` declares `static memmap_chunk z80_map[]` as a function-local static (genesis.c:2828),
  writes `z80_map[0].buffer = gen->zram` on every allocation (:2905), and hands the array to `init_z80_opts`, which keeps
  the pointer (z80_util.c:65). Every `z80_invalidate_code_range` re-resolves the Z80 through it (the zram and state
  deserialisers, genesis.c:191/201/296, and the VDP test register, :1132), so after a savestate load instance A's Z80
  runs on whichever instance was allocated LAST, and on freed memory once that one is destroyed. genmddj drives its
  sound from the Z80, so this is not theoretical. Give each console its own copy:

  ```c
  /* RetroPlug: the Z80's memory map carries this console's zram pointer, and init_z80_opts keeps the array it is handed
   * (z80_util.c:65) for every later z80_invalidate_code_range. It was a function-local static, so the second console
   * overwrote the first one's map and a state load in one console re-resolved the other's Z80 into this one's RAM. */
  gen->z80_map = malloc(sizeof(memmap_chunk) * 5);
  memcpy(gen->z80_map, kZ80MapTemplate, sizeof(kZ80MapTemplate));
  gen->z80_map[0].buffer = gen->zram = calloc(1, Z80_RAM_BYTES);
  ...
  init_z80_opts(z_opts, gen->z80_map, 5, NULL, 0, MCLKS_PER_Z80, 0xFFFF);
  ```

  with `memmap_chunk *z80_map;` in `genesis_context`, the template (the five entries, unchanged) a file-scope `static
  const memmap_chunk kZ80MapTemplate[5]`, and `free(gen->z80_map)` in `free_genesis` beside `free(gen->zram)`.

- **Allocation is not reentrant, and this plan does not touch that.** `m68k_util.c:222-254` passes `sync_components` and
  `int_ack` through the file statics `sync_comp_tmp`/`int_ack_tmp` and NULLs them after, `ym_common.c:52-57` sets
  `did_tbl_init` BEFORE filling the tables, and `vdp.c:174`'s table init is the same shape. Two instances building
  CONCURRENTLY can get a NULL `sync_components` or half-built YM tables. The fix this plan takes is the mutex below, not
  the three internals: with every `alloc_config_genesis` and `free_context` under one lock, no two allocations overlap,
  so the statics are not shared state in practice. Editing those three would mean three more mirror hunks that the plan
  could not test beyond "the mutex already serialised them", which is why they stay out (and why the commit below
  touches only `genesis.c`, `genesis.h`, `vdp.c`, `vdp.h` and `gdb_remote.c`).

  ```cpp
  // BlastemShim.cpp. BlastEm's allocation path (alloc_config_genesis / free_context) still touches file-scope state this
  // package has not audited end to end, so it runs under one process-wide lock. Control thread only: a block never
  // allocates a context, so the audio path never takes it.
  std::mutex& coreLifecycleMutex() {
      static std::mutex mutex;
      return mutex;
  }
  ```

  and `boot()`/`destroyCore()` take `std::lock_guard<std::mutex> lock(coreLifecycleMutex());` around
  `alloc_config_genesis` / `free_context` ONLY (never around the rest of `boot()`, so the seed-and-retry path below can
  reallocate without deadlocking on a lock the outer call still holds).

Commit in the submodule:

```bash
cd deps/blastem
git add genesis.c genesis.h vdp.c vdp.h gdb_remote.c
git commit -m "genesis: a console's state is its own, not the process's

read_dma_value and vdp_dma_started went through the current_system global, so
a second console in one process would DMA out of the first one's RAM; the VDP
the call belongs to is the console. The Z80's memory map was a function-local
static holding one console's zram pointer, so a state load re-resolved the
wrong console's Z80. MCLKS_PER_68K was a writable global every allocation set;
it is 7 on every Mega Drive. Allocation is serialised by the host instead
(see the plan's coreLifecycleMutex), so the file statics that path uses are
not shared between two builds.

RetroPlug runs several consoles in one process."
git -c protocol.file.allow=always push \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
cd ../..
```

- [ ] **Step 4: Write the failing test**

`packages/blastem/native/test/BlastemInstances.test.cpp`:

```cpp
// Two consoles in one process (design section 2): interleaved on one thread and run on two, each bit-identical to a
// solo render. The DMA case is the one that caught a real global: a second instance used to read its VDP DMA out of
// the first instance's work RAM.
#include <cstdint>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"   // the C headers: io.h, vdp.h and Genesis's prototypes
#include "system/blastem/BlastemMdSystem.hpp"

extern "C" {
#include "z80.h"   // z80_invalidate_code_range and z80_context: BlastemCore.hpp does not include it
}

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom, SystemId id) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(id, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

std::vector<float> solo(std::vector<std::uint8_t> rom, int blocks) {
    auto sys = boot(std::move(rom), 1);
    std::vector<float> l(kFrames), r(kFrames), out;
    for (int i = 0; i < blocks; ++i) {
        runBlock(*sys, l, r);
        out.insert(out.end(), l.begin(), l.end());
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

}  // namespace

TEST_CASE("two instances interleaved on one thread render what each renders alone", "[blastem][instances]") {
    const std::vector<float> alone = solo(rp::blastem::test::fmPsgRom(), 40);
    auto a = boot(rp::blastem::test::fmPsgRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);  // a different ROM: its DMA and ROM reads would cross
    std::vector<float> la(kFrames), ra(kFrames), lb(kFrames), rb(kFrames), out;
    for (int i = 0; i < 40; ++i) {
        runBlock(*a, la, ra);
        runBlock(*b, lb, rb);
        out.insert(out.end(), la.begin(), la.end());
        out.insert(out.end(), ra.begin(), ra.end());
    }
    CHECK(out == alone);
}

TEST_CASE("two instances on two threads render what each renders alone", "[blastem][instances]") {
    const std::vector<float> alone = solo(rp::blastem::test::fmPsgRom(), 40);
    const std::vector<float> aloneB = solo(rp::blastem::test::psgOnlyRom(), 40);
    auto a = boot(rp::blastem::test::fmPsgRom(), 1);
    auto b = boot(rp::blastem::test::psgOnlyRom(), 2);  // a different ROM, and one that also makes sound
    std::vector<float> outA, outB;
    // Creation is serialised (the host builds cores on one thread); only the blocks run concurrently, which is the
    // engine's contract for a system that migrates between threads.
    std::thread first([&] {
        std::vector<float> l(kFrames), r(kFrames);
        for (int i = 0; i < 40; ++i) {
            runBlock(*a, l, r);
            outA.insert(outA.end(), l.begin(), l.end());
            outA.insert(outA.end(), r.begin(), r.end());
        }
    });
    std::thread second([&] {
        std::vector<float> l(kFrames), r(kFrames);
        for (int i = 0; i < 40; ++i) {
            runBlock(*b, l, r);
            outB.insert(outB.end(), l.begin(), l.end());
            outB.insert(outB.end(), r.begin(), r.end());
        }
    });
    first.join();
    second.join();
    CHECK(outA == alone);
    // B against ITS OWN solo render, which is what makes the pair a test: a comparison against A's render would pass
    // with both instances broken in the same way, and a cross-read (A's globals reaching B's audio) shows up here as
    // B's tone in A's lanes or A's in B's.
    CHECK(outB == aloneB);
}
```

- [ ] **Step 5: Run it, then make it pass**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[instances]"
```

What to expect, honestly: the two render cases above can PASS before the fix, because no audio image in this plan runs a
68k DMA or touches the Z80, so they exercise the instance pointer and not the mirror's globals. Two more cases reach
the globals directly, and they are the ones that fail (or crash) before it:

```cpp
TEST_CASE("a console's DMA reads its own work RAM", "[blastem][instances]") {
    // read_dma_value and vdp_dma_started reached their console through the process global current_system, which the
    // shim left NULL: with two instances the read belongs to whichever console was allocated last (or to nothing at
    // all, which dereferences NULL here). Each instance's RAM holds a different word, so the read cannot be right twice.
    // The address is a WORD address: read_dma_value multiplies by two (genesis.c:312), so reading 68k $FF0000 is
    // 0xFF0000 >> 1. Passing the byte address only works by mirror accident.
    auto a = boot(rp::blastem::test::portPollRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);
    static_cast<std::uint16_t*>(static_cast<void*>(a->core()->work_ram))[0] = 0xAAAA;
    static_cast<std::uint16_t*>(static_cast<void*>(b->core()->work_ram))[0] = 0x5555;
    CHECK(read_dma_value(a->core()->vdp, 0xFF0000 >> 1) == 0xAAAA);
    CHECK(read_dma_value(b->core()->vdp, 0xFF0000 >> 1) == 0x5555);
}

TEST_CASE("a console's Z80 re-resolves through its own RAM", "[blastem][instances]") {
    // z80_map was a function-local static whose [0].buffer every allocation overwrote (and init_z80_opts keeps it), so
    // the FIRST console's Z80 re-resolved through the LAST one's zram on every invalidate: a savestate load in one
    // console pointed the other's Z80 at freed memory once that one was gone. fastread/fastwrite are what the
    // re-resolution writes (z80_util.c:180-198), and [0] covers Z80 addresses 0..1023.
    auto a = boot(rp::blastem::test::portPollRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);  // allocated LAST: the instance the static map would point at
    z80_invalidate_code_range(a->core()->z80, 0, 0x4000);  // what a state load and a Z80 code write do
    CHECK(a->core()->z80->fastwrite[0] == a->core()->zram);
    CHECK(b->core()->z80->fastwrite[0] == b->core()->zram);
    CHECK(a->core()->zram != b->core()->zram);
}
```

- The render cases are the regression guard: each instance's render is compared against ITS OWN solo render, which is
  what makes "bit-identical" mean the right thing (a comparison against the other instance's render would pass with
  both of them broken).

The only remaining per-call global is the instance pointer: `BlastemShim.cpp`'s `thread_local void* currentInstance`.
With the mirror's fix in place a threaded run has each thread's pointer its own; an *interleaved* run has one thread's
pointer set by whichever system called last, so every entry point must set it. Audit the system's public methods for
that (each one a `currentInstance = this;` first line): `onActivate`, `onDeactivate`, `onSampleRateChanged`,
`prepareForBlock`, `stepIfBelowTarget` (via `run_until`), `finishBlock`, `pressButton`, `pushCoreBytes`, `setGainDb`,
`saveSramBytes`, `saveStateBytes`, `captureStateSnapshot`, `getMemory`, and the device's read (`io_custom_input`, which
runs inside a block and therefore under the system's own call).

- [ ] **Step 6: Run all the native cases**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test
```

Expected: every case passes, the invariance one included (a grid, if Task 10 applied, is per instance).

- [ ] **Step 7: Commit**

```bash
git add deps/blastem \
        packages/blastem/native/src/system/blastem/BlastemShim.cpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemInstances.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "fix(blastem): two instances are two consoles

The DMA reads its own VDP's console rather than the process global, the 68k
divider is a constant, and every entry point sets the instance pointer the
frontend callbacks read. Interleaved and threaded pairs render what each
renders alone."
```

**Ask the user to push the mirror before CI.**

---

### Task 12: Battery, work RAM and the Z80's RAM through the capabilities

**Files:**
- Create: `packages/blastem/native/src/system/blastem/BlastemMemory.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemMemory.test.cpp`
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}`,
  `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: `BlastemMdSystem`, `gen_->work_ram` / `gen_->zram` / `gen_->save_storage` / `gen_->save_size`.
- Produces: `BlastemMdSystem::getMemory(type, access)` (Ram = the 68k's 64 KiB in address order, Sram = the battery,
  ExtWorkRam = the Z80's 8 KiB), `saveSramBytes()`, and `memory()` / `sram()` / `rom()`, the three capability objects
  the control plane's jobs use (with `rp::blastem::MdMemory`, a `MemoryCapability` whose write maps the swap).

- [ ] **Step 1: Write the failing test**

`packages/blastem/native/test/BlastemMemory.test.cpp`:

```cpp
// What the control plane reads and writes (design section 5): the 68k's work RAM at 68k addresses (BlastEm stores it as
// 16-bit words, so a naive view is byte-swapped), the Z80's own 8 KiB, and the cartridge battery in BlastEm's own
// odd-byte layout (byte i is address $200001 + 2i), which is what a real odd-byte cart holds and what genmddj's sync
// variables are read through.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/SystemCapabilities.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

void runBlock(rp::blastem::BlastemMdSystem& sys) {
    std::vector<float> l(kFrames), r(kFrames);
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    sys.prepareForBlock(info);
    while (sys.stepIfBelowTarget(kFrames)) {}
    sys.finishBlock(info, lanes, 2);
}

// A cart with 64 KiB of odd-byte SRAM, as genmddj's header declares it ($1B0 "RA", $F8, $20; start $200001, end
// $21FFFF), and a program that writes one byte into it at $200001 and reads it back into work RAM at $FF0000.
std::vector<std::uint8_t> sramRom() {
    std::vector<std::uint8_t> rom = rp::blastem::test::image(
        {0x13, 0xFC, 0x00, 0x5A, 0x00, 0x20, 0x00, 0x01,             // move.b #$5A,$00200001
         0x10, 0x39, 0x00, 0x20, 0x00, 0x01,                         // move.b $00200001,d0
         0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00},                        // move.b d0,$FF0000
        {});
    rom[0x1B0] = 'R'; rom[0x1B1] = 'A';
    rom[0x1B2] = 0xF8;  // SRAM present, odd bytes
    rom[0x1B3] = 0x20;
    rom[0x1B4] = 0x00; rom[0x1B5] = 0x20; rom[0x1B6] = 0x00; rom[0x1B7] = 0x01;  // start $00200001
    rom[0x1B8] = 0x00; rom[0x1B9] = 0x21; rom[0x1BA] = 0xFF; rom[0x1BB] = 0xFF;  // end $0021FFFF
    return rom;
}

}  // namespace

TEST_CASE("work RAM reads at 68k addresses, not in BlastEm's word order", "[blastem][memory]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    rp::MemoryAccessor ram = sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    REQUIRE(ram.valid());
    CHECK(ram.size() == 0x10000);
    // portPollRom stores what it read from $A10005 at $FF0000; work RAM's offset 0 IS $FF0000's byte.
    CHECK(ram[0] != 0x00);  // the pad's idle $7F (0x7F & 0xFF), not the unused odd byte
    CHECK((ram[0] & 0x40) != 0);
}

TEST_CASE("the Z80's own RAM is a region of its own", "[blastem][memory]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    rp::MemoryAccessor z80 = sys->getMemory(rp::MemoryType::ExtWorkRam, rp::AccessType::Read);
    CHECK(z80.valid());
    CHECK(z80.size() == 8192);
}

TEST_CASE("a capability write lands at the 68k's address", "[blastem][memory]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    rp::MemoryCapability* memory = sys->memory();
    REQUIRE(memory != nullptr);
    const std::uint8_t value = 0x5A;
    CHECK(memory->write(0, 0x0100, &value, 1));  // $FF0100
    CHECK(sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read)[0x0100] == 0x5A);
}

TEST_CASE("the battery keeps BlastEm's odd-byte layout", "[blastem][memory]") {
    auto sys = boot(sramRom());
    for (int i = 0; i < 4; ++i) runBlock(*sys);
    const std::vector<std::uint8_t> sram = sys->saveSramBytes();
    REQUIRE(sram.size() == 0x10000);  // (end - start + 1) / 2: the compacted odd-byte cart
    CHECK(sram[0] == 0x5A);           // byte 0 is $200001, which the ROM wrote
    CHECK(sram[1] == 0x00);           // byte 1 is $200003
    // And the ROM read its own byte back through the odd lane.
    CHECK((sys->getMemory(rp::MemoryType::Ram, rp::AccessType::Read)[0] & 0x5A) == 0x5A);
}

TEST_CASE("a battery seed reaches the cart", "[blastem][memory]") {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    cfg.sram.assign(0x10000, 0x00);
    cfg.sram[0] = 0xA5;  // $200001
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, sramRom());
    sys->onActivate(kRate);
    CHECK(sys->saveSramBytes()[0] == 0xA5);
}
```

- [ ] **Step 2: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[memory]"
```

Expected: FAIL: `getMemory` returns an invalid accessor (the base's default), `saveSramBytes` is empty.

- [ ] **Step 3: Implement**

`BlastemMemory.hpp` / `.cpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "system/SystemCapabilities.hpp"

namespace rp::blastem {

// The system, forward-declared INSIDE this namespace: a global-scope `class BlastemMdSystem;` declares a different,
// unrelated type, and MdMemory would then bind to an incomplete one.
class BlastemMdSystem;

// The 68k's work RAM in the 68k's own address order. BlastEm keeps it as 16-bit words, so its byte view swaps each
// pair; a view in address order is a per-call copy, refreshed on every getMemory (the registry copies it into its own
// triple the same block, so the copy's lifetime is the call).
class WorkRamView {
public:
    explicit WorkRamView(std::vector<std::uint8_t>& scratch) : scratch_(scratch) {}
    // Fills the scratch from `source` and returns the view: byte `i` of the result is address `i`.
    std::span<const std::uint8_t> refresh(const std::uint8_t* source, std::size_t size);

private:
    std::vector<std::uint8_t>& scratch_;
};

// rp.system.memory/1's write half: an offset in 68k address order, poked into BlastEm's word-ordered storage. Region 0
// is the 68k's work RAM and region 1 the Z80's own memory, the same order and names regions() lists.
class MdMemory final : public MemoryCapability {
public:
    explicit MdMemory(BlastemMdSystem& sys) : sys_(sys) {}
    std::span<const Region> regions() const override;
    bool write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) override;

private:
    BlastemMdSystem& sys_;
};

}  // namespace rp::blastem
```

The two mapping rules, written once each:

```cpp
// work RAM, 68k address -> BlastEm's byte view
static std::size_t wordOrder(std::size_t address) { return address ^ 1u; }

std::span<const std::uint8_t> WorkRamView::refresh(const std::uint8_t* source, std::size_t size) {
    scratch_.resize(size);
    for (std::size_t i = 0; i < size; ++i) scratch_[i] = source[wordOrder(i)];
    return scratch_;
}

bool MdMemory::write(std::size_t region, std::uint32_t offset, const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return false;
    if (region == 0) {  // work RAM: address order in, word order out
        auto live = sys_.getMemory(rp::MemoryType::Ram, rp::AccessType::ReadWrite);
        if (!live.valid() || offset + size > live.size()) return false;
        std::uint8_t* raw = sys_.core()->work_ram ? reinterpret_cast<std::uint8_t*>(sys_.core()->work_ram) : nullptr;
        if (!raw) return false;
        for (std::size_t i = 0; i < size; ++i) raw[wordOrder(offset + i)] = data[i];
        return true;
    }
    if (region == 1) {  // the Z80's RAM: a plain byte array
        auto live = sys_.getMemory(rp::MemoryType::ExtWorkRam, rp::AccessType::ReadWrite);
        if (!live.valid() || offset + size > live.size()) return false;
        std::memcpy(live.data() + offset, data, size);
        return true;
    }
    return false;
}
```

`BlastemMdSystem` adds `std::vector<std::uint8_t> ramView_;` and:

```cpp
rp::MemoryAccessor BlastemMdSystem::getMemory(rp::MemoryType type, rp::AccessType access) {
    if (!gen_) return {};
    switch (type) {
        case rp::MemoryType::Ram:
            return {type, access, const_cast<std::uint8_t*>(workRamView().data()), workRamView().size()};
        case rp::MemoryType::ExtWorkRam:  // the Z80's own 8 KiB
            return {type, access, gen_->zram, 8192};
        case rp::MemoryType::Sram:
            if (gen_->save_type == SAVE_NONE || !gen_->save_storage) return {};
            return {type, access, gen_->save_storage, gen_->save_size};
        default:
            return {};
    }
}
```

(`workRamView()` refreshes `ramView_` and returns it. `MemoryCapability::regions()` returns a static table:
`{{"work", rp::MemoryType::Ram}, {"z80", rp::MemoryType::ExtWorkRam}}`.)

`saveSramBytes()`:

```cpp
std::vector<std::uint8_t> BlastemMdSystem::saveSramBytes() const {
    if (!gen_ || gen_->save_type == SAVE_NONE || !gen_->save_storage || gen_->save_size == 0) return {};
    currentInstance = const_cast<BlastemMdSystem*>(this);  // nothing in it calls out; kept for the const path's clarity
    return {gen_->save_storage, gen_->save_storage + gen_->save_size};
}
```

and the three capability accessors plus a `rp::CoreRom romCap_{rom_, 0x120, 0x30}` (a Mega Drive's domestic name at
`$120`, 48 bytes) so `rom()` answers. The battery seed is already handled: `seedSramAndState` (SystemBase) writes into
`getMemory(Sram, ReadWrite)`, which is BlastEm's own buffer, so no extra code.

- [ ] **Step 4: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[memory]"
```

Expected: all five pass. If the odd-byte case fails with an empty battery, the ROM's header is what romdb reads: check
`$1B0`-`$1BF` against genmddj's (`RA`, `$F8`, `$20`, then the start and end longwords), and that `save_size` is
`(mask + 1) / 2` for `RAM_FLAG_ODD` (romdb.c:252). If the work-RAM case fails, the swap is the wrong way round for the
address you read: byte 0 of `work_ram` as a word is address 0's *high* half, so `ram[0]` must be `raw[1]`.

- [ ] **Step 5: Commit**

```bash
git add packages/blastem/native/src/system/blastem/BlastemMemory.hpp \
        packages/blastem/native/src/system/blastem/BlastemMemory.cpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemMemory.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): work RAM in the 68k's address order, the Z80's RAM, and the odd-byte battery"
```

---

### Task 13: Savestates, and a foreign state refused

**Files:**
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}` (the state, the refusals, the sink's
  re-prime)
- Mirror (`deps/blastem`, branch `retroplug`): `serialize.c`, `serialize.h`, `genesis.c`, `vdp.c` (a bad state is
  refused, not fatal)
- Create: `packages/blastem/native/test/BlastemSaves.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above.
- Produces: `saveStateBytes()`, `stateSnapshotSize()`, `captureStateSnapshot()`, `loadStateBytes()` (the seed path) and
  `stateRefused()`; and a `deserialize_buffer` that reports a short read instead of exiting the process.

**The refusal rule, which is the whole point of this task.** BlastEm's serializer aborts (`fatal_error`, util.c:474,
which `exit(1)`s) on a section bigger than the space left (serialize.c:216), on a short field (:144-197), and on a state
RAM size it does not expect (genesis.c:177, 188). A plugin handed a truncated or foreign state must return an error, so
a mirror commit turns every one of those into a latch on the buffer, and the host refuses the state and keeps running.

- [ ] **Step 1: Write the failing test**

`packages/blastem/native/test/BlastemSaves.test.cpp`:

```cpp
// Savestates (design section 2): two instances seeded with one state render identical bytes, a restored render is not
// silent, and a state whose serializer identifier is not BlastEm's 1.0 one (or whose body is truncated) is refused
// rather than misread.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

// The largest |sample| in a render: a restored render has to be audible, or "the state reached the core" is unproven.
double peak(const std::vector<float>& samples) {
    double p = 0.0;
    for (float s : samples) p = std::max(p, std::fabs(static_cast<double>(s)));
    return p;
}

std::unique_ptr<rp::blastem::BlastemMdSystem> boot(std::vector<std::uint8_t> rom,
                                                   std::vector<std::uint8_t> state = {}) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    cfg.savestate = std::move(state);
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, std::move(rom));
    sys->onActivate(kRate);
    return sys;
}

std::vector<float> run(rp::blastem::BlastemMdSystem& sys, int blocks) {
    std::vector<float> l(kFrames), r(kFrames), out;
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    for (int i = 0; i < blocks; ++i) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys.prepareForBlock(info);
        while (sys.stepIfBelowTarget(kFrames)) {}
        sys.finishBlock(info, lanes, 2);
        out.insert(out.end(), l.begin(), l.end());
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

}  // namespace

TEST_CASE("two instances seeded with one state render identical bytes", "[blastem][saves]") {
    // The round trip as a sink can honestly prove it: the sink's ring, its blips and its filter are the HOST's state,
    // not the console's, so a restored run is not byte-identical to the original run. Two FRESH instances seeded with
    // the same bytes each run through the same sink, so their renders are the comparison that means something, and a
    // restored render that is not silent is the check that the state reached the core at all.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 20);  // boot, and let the note sound
    const std::vector<std::uint8_t> state = sys->saveStateBytes();
    REQUIRE(state.size() > 8);
    CHECK(std::memcmp(state.data(), "BLSTSZ\x01\x07", 8) == 0);  // BlastEm's own serializer identifier
    auto first = boot(rp::blastem::test::fmPsgRom(), state);
    auto second = boot(rp::blastem::test::fmPsgRom(), state);
    const std::vector<float> a = run(*first, 40);
    const std::vector<float> b = run(*second, 40);
    CHECK(a == b);
    CHECK(peak(a) > 0.01);  // the restored core is playing, not frozen at the state's first sample
}

TEST_CASE("a state from another serializer version is refused", "[blastem][saves]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    state[7] = 0x08;  // "BLSTSZ\x01\x08": another version
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    // A refused state leaves the core at power-on: the ROM's first block still renders, and the state's bytes never
    // reached the emulator.
    CHECK(restored->activated());
    CHECK(restored->stateRefused());
}

TEST_CASE("a truncated state is refused, and the core still runs", "[blastem][saves]") {
    // The case is the RETURN, not the message: BlastEm's own serializer exits the process on a short read
    // (fatal_error, serialize.c:216 and its load_* helpers), so reaching the next CHECK at all is the proof, and the
    // render after it is the proof that the refusal left a live core behind.
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    state.resize(state.size() / 2);
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    REQUIRE(restored->stateRefused());
    CHECK(restored->activated());
    CHECK(peak(run(*restored, 4)) > 0.01);  // from power-on, not a half-loaded state
}
```

- [ ] **Step 2: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[saves]"
```

Expected: FAIL: `saveStateBytes()` is empty.

- [ ] **Step 3: The mirror commit: a bad state is refused, not fatal**

Five edits in `deps/blastem`, each with the marker:

1. `serialize.h`'s `deserialize_buffer` gains `uint8_t failed;` ("RetroPlug: a short read is a REFUSAL a caller can see,
   not an exit: a plugin handed a truncated state must return an error"), and `init_deserialize` zeroes it; `serialize_buffer`
   gains the same field for the growth latch below, zeroed by `init_serialize`.
2. `serialize.c`'s `load_int8/16/32`, `load_buffer8/16/32` and `load_section` replace each `fatal_error(...)` with
   `buf->failed = 1; buf->cur_pos = buf->size; return ...;` (cur_pos at the end so every later load in the same buffer
   fails immediately too), and `load_section` propagates a section's latch: after the handler call,
   `if (section.failed) buf->failed = 1;`.
3. `genesis.c`'s `ram_deserialize` and `zram_deserialize` (:177, :188) replace their `fatal_error` with the same latch
   (`buf->failed = 1; return;`), and `genesis_deserialize`'s caller can then see it.
4. `genesis.c`'s 68k write to an unhandled Z80 address (:1327, `fatal_error`) becomes a `warning` and a dropped write: a
   malformed ROM must not take the host process with it.
5. `vdp.c`'s `vdp_deserialize` (:6617) refuses an out-of-range FIFO size out of a loaded state the same way
   (`buf->failed = 1; return;`), which is another field a corrupt state controls.

and, because a state can also fail to *grow* (`serialize.c`'s `reserve` reallocs without a check, so an out-of-memory
capture writes through NULL): `reserve` returns a `uint8_t` that latches `buf->failed` on a failed `realloc`, and every
`save_*` checks it and returns without writing.

**The rest of the `fatal_error` calls, classified rather than guessed.** Find them with the file set the library
compiles:

```bash
cd deps/blastem && grep -n fatal_error system.c genesis.c vdp.c io.c romdb.c i2c.c serialize.c gst.c saves.c \
    ym2612.c psg.c config.c tern.c util.c paths.c gen.c event_log.c | grep -v "void fatal_error"
```

(Run it before the edits above; afterwards, only the entries marked "not reachable" should remain in those files.) Each
one falls in one of three groups:

- **Converted here** (a state or a ROM can reach it): `serialize.c:102, 144, 157, 168, 177, 186, 197, 211, 216`,
  `genesis.c:177, 188` (a state's RAM sizes), `genesis.c:1327` (a 68k write to an unhandled Z80 address, which a
  cart's own code can cause) and `vdp.c:6617` (a state's FIFO size, in `vdp_deserialize`).
- **File loading only, and this host never loads a file**: `system.c:104, 219` (the frontend's `load_media`), and
  `genesis.c:2147` (`load_state`'s file path; this plan calls `genesis_serialize`/`genesis_deserialize` directly).
  The host hands `alloc_config_genesis` a memory buffer and its own state bytes, so none of those three is on any path a
  plugin takes.
- **Built-in data only, which is ours and static**: `romdb.c`'s fifteen and `i2c.c`'s four fire on a malformed ROM DATABASE
  or a missing EEPROM map, and the database is compiled in from `deps/blastem/rom.db` (a bad row fails Task 3's build
  test, not a user's session); `config.c`, `tern.c`, `paths.c`, `gen.c` and `event_log.c` are the frontend's
  configuration, asset and code-generation paths, which a library build never enters; `genesis.c:3035,
  3042, 3069, 3117` are the TMSS model's ROM and remap, reachable only when the config tree selects a TMSS model, and
  this shim builds no config tree (`config` stays NULL, and `get_model` returns the default model, which has no TMSS).

Any one of the last two groups that a later task finds reachable gets converted in that task and deleted here; the ones
that stay are recorded as known process-exits in Task 24's spec/07 entry, so the knowledge lives in the spec and not in a
comment.

Commit in the submodule, and push to the local mirror (the rules block has the form):

```bash
cd deps/blastem
git add serialize.c serialize.h genesis.c vdp.c
git commit -m "serialize: a bad state is refused, not fatal

load_section and the load_* helpers called fatal_error, which exits the
process, so a plugin handed a truncated or foreign state could only die.
They latch the buffer instead, a section propagates its own latch, the RAM
size checks do the same, and a growth failure is a latch too.

RetroPlug: a host must be able to refuse a state and keep running."
git -c protocol.file.allow=always push \
    /workspaces/Megadrive/.superpowers/megadrive/RetroPlug-BlastEm retroplug
cd ../..
```

Expected: the subject in both `deps/blastem` and the mirror.

- [ ] **Step 4: Implement the host side**

Before any byte reaches the emulator, walk the section headers; then hand it over and check the latch:

```cpp
// BlastEm's own serializer, behind its own identifier. save_to_file writes the 8 bytes "BLSTSZ\x01\x07" ahead of the
// state (serialize.c:229); a library build writes the state with genesis_serialize directly, so this prefixes the same
// bytes and loadStateBytes demands them. A state from another serializer version, another console or another
// emulator is then refused before a byte reaches the emulator.
static constexpr char kStateIdent[] = "BLSTSZ\x01\x07";

std::vector<std::uint8_t> BlastemMdSystem::saveStateBytes() const {
    if (!gen_) return {};
    currentInstance = const_cast<BlastemMdSystem*>(this);
    serialize_buffer buf;
    init_serialize(&buf);
    // The pc the 68k will resume at: the address sync_components is handed at a block boundary (resume_genesis passes
    // gen->m68k->pc), so a state saved between blocks resumes on the next instruction.
    genesis_serialize(gen_, &buf, gen_->m68k->pc, 1);
    if (buf.failed) {  // the capture could not grow: no state is better than half a state
        free(buf.data);
        return {};
    }
    std::vector<std::uint8_t> out(sizeof(kStateIdent) - 1 + buf.size);
    std::memcpy(out.data(), kStateIdent, sizeof(kStateIdent) - 1);
    std::memcpy(out.data() + sizeof(kStateIdent) - 1, buf.data, buf.size);
    free(buf.data);
    return out;
}

// A section is a u16 id and a u32 size followed by exactly that many bytes (start_section/end_section, serialize.c:88-
// 108), so a truncated or foreign body is visible before the serializer is asked to read it. The walk is what makes
// "refused" mean refused: the mirror's latch catches a short FIELD inside a well-formed section, this catches the rest.
static bool sectionsFit(const std::uint8_t* bytes, std::size_t size) {
    std::size_t at = 0;
    while (at < size) {
        if (size - at < 6) return false;
        const std::uint32_t len = (std::uint32_t(bytes[at + 2]) << 24) | (std::uint32_t(bytes[at + 3]) << 16) |
                                  (std::uint32_t(bytes[at + 4]) << 8) | std::uint32_t(bytes[at + 5]);
        at += 6;
        if (len > size - at) return false;
        at += len;
    }
    return at == size;
}

bool BlastemMdSystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kIdent = sizeof(kStateIdent) - 1;
    if (!gen_ || bytes.size() <= kIdent || std::memcmp(bytes.data(), kStateIdent, kIdent) != 0) {
        stateRefused_ = !bytes.empty();
        return false;
    }
    currentInstance = this;
    if (!sectionsFit(bytes.data() + kIdent, bytes.size() - kIdent)) {
        stateRefused_ = true;  // truncated or sized wrong: nothing has been touched
        return false;
    }
    deserialize_buffer buf;
    init_deserialize(&buf, const_cast<std::uint8_t*>(bytes.data()) + kIdent, bytes.size() - kIdent);
    genesis_deserialize(&buf, gen_);
    if (buf.failed) {
        // A well-formed section whose payload is short or impossible: the core is now PARTLY loaded, so it has to be
        // rebuilt. That is boot()'s job, not this function's: loadStateBytes runs inside seedSramAndState, which boot()
        // calls as its LAST act, so rebooting from here would recurse through a half-built boot. It only flags the
        // refusal and drops the seed; boot()'s tail sees the flag, destroys the core and boots once more from power-on
        // (and the second boot has no state to seed, so it cannot recurse again).
        stateRefused_ = true;
        config_.savestate.clear();
        return false;
    }
    // The state carries the 68k's 32-bit cycle count, not this host's 64-bit clock: re-anchor so masterClockOf maps the
    // NEXT sample from the restored core rather than from a clock that ran past it, and re-prime the sink (its ring and
    // its blips are the host's state and the state did not carry them).
    sampleAnchor_ = sampleBase_;
    mclkAnchor_ = mclkNow();
    audio_.reset();
    return true;
}

std::size_t BlastemMdSystem::stateSnapshotSize() const {
    // A Mega Drive state is about 50 KB (VDP VRAM 64 KB dominates) and grows with a cart's battery; a ceiling with
    // headroom keeps the snapshot triple from resizing mid-life, as the base class requires.
    return 256 * 1024;
}

bool BlastemMdSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    // Allocates on the DSP thread, which is Mesen's precedent too (MesenSmsSystem.cpp:651 captures a state the same
    // way every kStateSnapshotIntervalSec); the triple it writes into is pre-sized, so the state's own buffer is the
    // only allocation.
    dst = saveStateBytes();
    return !dst.empty();
}
```

`loadStateBytes` is a private virtual in `SystemBase` (SystemBase.hpp:388), so the override goes in the class's private
section beside `boot()`. Its caller is `seedSramAndState` (SystemBase), and **`boot()` ends with it**, after
`m68k_reset`, the anchors and `setGainDb`, as its LAST act:

```cpp
void BlastemMdSystem::boot() {
    ...  // gate, currentInstance, video_.prepareBuffers(), the alloc under the mutex, m68k_reset, video_.prepareFrame,
         // the anchors, the smoothed gain
    stateRefused_ = false;
    seedSramAndState(config_.sram, config_.savestate);  // LAST: the battery, then the state over it
    if (stateRefused_ && !config_.savestate.empty()) {
        // A refused state: either it was thrown out before it touched the core (the identifier, the section walk) or it
        // latched after writing part of itself in (the serializer's short-field latch). Dropping the seed and rebuilding
        // ONCE from power-on is the one path that leaves no half-loaded core behind in either case.
        // Safe only because this is boot()'s last act and the mutex is not held here (the retry's alloc takes it itself),
        // and because the second boot has an empty savestate, so it cannot recurse again.
        config_.savestate.clear();
        destroyCore();
        boot();
        stateRefused_ = true;  // the refusal is still this instance's answer to the build request
    }
}
```

(`boot()`'s `stateRefused_ = false` before the seed is what makes the retry terminate: the nested boot clears it, seeds
nothing, and returns. `destroyCore()` and the allocated core's `free_context` both run under the lifecycle mutex, in
separate scopes, never nested. `genesis_deserialize` calls `adjust_int_cycle` itself (genesis.c:279), so the next
`run_until` resumes from the loaded pc.)

The class also gains a public accessor for that field, beside `activated()`: `bool stateRefused() const { return
stateRefused_; }`, and a public `std::vector<std::uint8_t> saveSramBytes() const` (Task 12) is the other half of the
build-request contract. `saveStateBytes()` is `const` because the snapshot capture and the control plane both call it on
a const system; `genesis_serialize` takes the context's non-const pointer, not the member, so no cast of the member is
needed.

`AudioSink::reset()` (a new member beside `clear()`) drops every source's ring and blip and re-primes, which is what a
state load needs and what a rate change needs: it is `clear()` plus `prime()`.

- [ ] **Step 5: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc)
build/bin/retroplug-blastem-test "[saves]"
build/bin/retroplug-blastem-test
```

Expected: the `[saves]` cases pass and the whole binary still does. If the refusal case aborts the process, the mirror
commit is missing a `fatal_error` (grep for it in serialize.c and the two deserializers); if the seeded pair's renders
differ, the state was taken mid-instruction or the sink was not re-primed on the load.

- [ ] **Step 6: Commit**

```bash
git add deps/blastem \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/src/system/blastem/BlastemAudio.hpp \
        packages/blastem/native/src/system/blastem/BlastemAudio.cpp \
        packages/blastem/native/test/BlastemSaves.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): savestates through BlastEm's serializer, and a bad one refused

The mirror's serializer latches a short read instead of exiting, so a
truncated or foreign state is refused with a reason and the host keeps
running. A load re-anchors the sample mapping at the restored core and
re-primes the audio sink, which the state does not carry."
```

---

### Task 14: `rp.blastem.md`, the node, and the build function

**Files:**
- Create: `packages/blastem/native/src/system/blastem/BlastemBackend.{hpp,cpp}`
- Modify: `packages/blastem/native/src/system/blastem/BlastemNode.cpp` (the node type),
  `packages/blastem/native/CMakeLists.txt`
- Modify: `packages/native/package.json` (devDependency `@retroplug/blastem`), `packages/native/cmake/tests.cmake`
  (the include path), `packages/native/test/engine-graph/GraphDocs.hpp` (`romFor`), 
  `packages/native/test/engine-graph/SystemNode.test.cpp` (three cases),
  `packages/native/test/engine-graph/golden/product-registrations.json` (re-recorded),
  `packages/retroplug/test/systems/store-graph.test.ts` (`NATIVE_TYPES` gains `rp.blastem.md`, the first list this
  node type breaks; its `be.seed("/roms/genmddj.bin", ...)` fixture is Task 20's, when the node it builds lands)

**Interfaces:**
- Consumes: `BlastemMdSystem`, `BlastemMdConfig`, `rp::blastem::MdRegion`'s labels, `rp::blastem::test::*`.
- Produces: `rp::blastem::buildMdCore(SystemId, SystemBuildRequest&&, double, std::string*)`, the node type
  `rp.blastem.md` with settings `{system, rom, sram, state, romName, region}` and `kBuild = rp::blastem::buildMdCore`;
  `RP_MD_ROM_PATH` is **not** needed (the image is generated in the test).

- [ ] **Step 1: The build function**

`BlastemBackend.hpp`:

```cpp
#pragma once

#include "system/SystemBuild.hpp"

// The blastem package's build function, which rp.blastem.md's request carries to rp.host.systems/2 (so the host that
// builds the core names no console). It gates the ROM, builds the system, activates it at the plan's rate, and refuses
// a core that did not come up.
namespace rp::blastem {

std::unique_ptr<SystemBase> buildMdCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                        std::string* why);

}  // namespace rp::blastem
```

`BlastemBackend.cpp`:

```cpp
#include "system/blastem/BlastemBackend.hpp"

#include <utility>

#include "system/SystemBase.hpp"
#include "system/blastem/BlastemMdConfig.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace rp::blastem {
namespace {

void refuse(std::string* why, const char* reason) {
    if (why) *why = reason;
}

}  // namespace

std::unique_ptr<SystemBase> buildMdCore(SystemId id, SystemBuildRequest&& request, double sampleRate,
                                        std::string* why) {
    if (!request.config) {
        refuse(why, "no Mega Drive config in the build request");
        return nullptr;
    }
    if (!isMdRom(request.rom)) {
        refuse(why, "not a Mega Drive ROM: no \"SEGA\" header at $100 (or a 32X image, which this core does not run)");
        return nullptr;
    }
    BlastemMdConfig cfg = *static_cast<const BlastemMdConfig*>(request.config);
    cfg.romName = request.romName;
    cfg.sram = std::move(request.sram);
    cfg.savestate = std::move(request.savestate);
    auto sys = std::make_unique<BlastemMdSystem>(id, std::move(cfg), std::move(request.rom));
    if (request.maxBlockFrames) sys->reserveAudio(request.maxBlockFrames);
    sys->onActivate(sampleRate);
    if (!sys->activated()) {
        refuse(why, "BlastEm refused the ROM");
        return nullptr;
    }
    sys->enableStateSnapshot();
    return sys;
}

}  // namespace rp::blastem
```

- [ ] **Step 2: The node**

In `BlastemNode.cpp`, replace the empty type table with the node (the shape is `MesenNode.cpp`'s; the knobs are
`packages/system/native/src/system/SystemNode.hpp`'s):

```cpp
namespace rp::host {
namespace {

using namespace rp::host::knobs;

// The region's labels ARE MdRegion's enumerator names (knobs::Label<MdRegion>), so packages/blastem/ts/enums.ts's
// REGION_VALUES spells the same four words: "auto", "ntscUs", "ntscJp", "pal".
struct MdSettings {
    std::uint64_t              system = 0;
    rp::resource_ref<"rom">    rom;
    rp::resource_ref<"sram">   sram;
    rp::resource_ref<"state">  state;
    std::optional<std::string> romName;
    // Construct-time: a region fixes the master clock and the frame height, so a change is a rebuild (Task 15's
    // descriptor lists it under constructFields, and the store rebuilds on one).
    std::optional<Label<MdRegion>> region;

    struct Key {
        std::uint64_t              system = 0;
        std::optional<std::string> romName;
        std::optional<int>         region;
        bool operator==(const Key&) const = default;
    };
    Key key() const {
        return {system, romName,
                region ? std::optional<int>(static_cast<int>(region->value())) : std::nullopt};
    }
    struct Applied {};
    Applied liveKnobs() const { return {}; }
};

struct Md : SystemNode<Md, rp::blastem::BlastemMdSystem, MdSettings> {
    static constexpr const char* id = "rp.blastem.md";
    static constexpr const char* name = "Mega Drive";
    static constexpr const char* category = "system";
    static constexpr SystemBuildFn kBuild = rp::blastem::buildMdCore;
    // All four, as Sms does (MesenNode.cpp:326-331): SystemNode::applyInputs reads serialIn, midi, bytes and buttons
    // unconditionally, so a node that declares three of them does not compile.
    struct {
        rp::bytes_in  serialIn;
        rp::events_in midi;
        rp::bytes_in  bytes;    // the sync levels (Task 20's node writes them)
        rp::events_in buttons;
    } inputs;
    struct { rp::audio_out<rp::stereo> mix; } outputs;
    struct { GainParam gain; } params;
    std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
    static rp::blastem::BlastemMdConfig config(const Settings& s) {
        rp::blastem::BlastemMdConfig c;
        if (s.region) c.region = static_cast<std::uint32_t>(valueOf<rp::blastem::MdRegion>(*s.region));
        return c;
    }
};

const rp_package* blastemPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Md>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.blastem","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace
}  // namespace rp::host

extern "C" const rp_package* rp_blastem_package(void) { return rp::host::blastemPackage(); }
```

`knobs::valueOf<MdRegion>` and `knobs::Label<MdRegion>` need NOTHING beside the enum: `Label<E>` is built from
`rfl::get_enumerator_array<E>()`, so the enumerator names are the labels and `valueOf` maps a label back by position
(SystemNode.hpp:50-80, which is exactly how `NesRegion` is used in MesenNode.cpp:254). There is no label array to add and
no `labelOf` to write: a `kMdRegionLabels` beside the enum would be a second spelling of one fact, and the first thing
to go stale.

- [ ] **Step 3: The shell's tests (engine-graph)**

In `packages/native/package.json`, add `"@retroplug/blastem": "workspace:*"` to `devDependencies`, then `pnpm install`.
In `packages/native/cmake/tests.cmake`, `retroplug-engine-graph-test`'s `INCLUDES` list gains one entry, beside the
`packages/n8/native/test` one it already has (a test target may include a devDependency's tree, which is what makes
this a devDependency and not a dependency):

```cmake
             ${CMAKE_SOURCE_DIR}/packages/blastem/native/test   # SyntheticRom.hpp (the MD cases' images)
```

In `packages/native/test/engine-graph/GraphDocs.hpp`, `romFor`'s table gains:

```cpp
    if (type == "rp.blastem.md") return rp::blastem::test::fmPsgRom();
```

with `#include "SyntheticRom.hpp"` at the top of the file.

In `packages/native/test/engine-graph/SystemNode.test.cpp`, add the helper beside `sms()` and `smsNode()`, and three
cases in the file's own shape (`Direct`, `compare`, `sounds`, `rig.boot`):

```cpp
std::unique_ptr<BlastemMdSystem> md(SystemId id) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;  // ntscUs: the same region the node's settings below carry
    auto sys = std::make_unique<BlastemMdSystem>(id, cfg, rp::blastem::test::fmPsgRom());
    sys->onActivate(kRate);
    return sys;
}

std::string mdNode(NodeRig& rig, SystemId id) { return rig.boot("rp.blastem.md", id, R"("region":"ntscUs")"); }

TEST_CASE("a lone Mega Drive node builds its core from the ROM resource", "[engine-graph][system-node][blastem-md]") {
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" + mdNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    const auto* core = static_cast<BlastemMdSystem*>(graphCore);
    CHECK(core->activated());
    CHECK(core->masterClock() == 53693175u);       // ntscUs, from the settings' label
    CHECK(core->getMemory(rp::MemoryType::Ram, rp::AccessType::Read).valid());
}

TEST_CASE("a lone Mega Drive node renders its core's mix bit for bit", "[engine-graph][system-node][blastem-md]") {
    NodeRig rig(kRate);
    auto direct = md(1);
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" + mdNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    Direct d({direct.get()}, 2, {{0, 1}});           // stereo: the node's two mix lanes are the host's two
    CHECK(compare(rig, d, 1.0) == "");               // one second, in the suite's ragged block pattern
    CHECK(sounds(d));
}

TEST_CASE("a warmed Mega Drive block allocates nothing", "[alloc][engine-graph][system-node][blastem-md]") {
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" + mdNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    Lanes out(2);
    const rp_transport t{kRate, 120.0, 0.0, 0};
    // Warm: the boot's savestate capture, the audio rings and the blip buffers are all first-block costs.
    for (int b = 0; b < 8; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    // A level staged for the block, so the port device's queue and its sort are in the counted window too.
    const std::uint8_t level = 0x00;
    REQUIRE(rig.builtCore(1) != nullptr);
    rig.builtCore(1)->pushCoreBytes(0, &level, 1);
    rp::test::beginCountingAllocations();
    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    // The level was accepted and the block that applied it allocated nothing: the port device's queue is part of the
    // same no-allocation window as the rest of the block.
    CHECK(static_cast<BlastemMdSystem*>(rig.builtCore(1))->levelsAccepted() == 1);
}
```

(`rp::test::beginCountingAllocations`/`endCountingAllocations` come from `graph/CountingAllocator.hpp`, which the file
already includes, and count because `retroplug-engine-graph-test` already links `retroplug-testing-allocator`;
`Lanes` and `rp_transport` are the file's own.)

And the store's list of the C++ node types, which is the first place the new node type is missing
(`packages/retroplug/test/systems/store-graph.test.ts`'s `NATIVE_TYPES`, "The C++ node types buildGraph places"):

```ts
const NATIVE_TYPES = new Set([
  "rp.host.midi-in", "rp.host.midi-out", "rp.host.audio-out", "rp.core.split", "rp.core.add",
  "rp.launchpad.surface-in", "rp.launchpad.surface-out", "rp.n8.nes",
  "rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms", "rp.blastem.md",
]);
```

Its failure mode is the one this list exists for: a document that builds `rp.blastem.md` is refused as an unknown type,
so `pnpm test systems/store-graph` fails until the type is named here. (The store-graph's `be.seed` for a Mega Drive
ROM comes with the script node in Task 20, which is the first case that boots one through the store.)

Re-record the registration golden:

```bash
RP_PRODUCT_REGISTRATIONS_UPDATE=1 build/bin/retroplug-engine-graph-test "[packages-dump]"
git diff packages/native/test/engine-graph/golden/product-registrations.json
```

Expected: `rp.blastem.md` added to EVERY product row that lists the package (up to nine rows, all of them built from the
desktop's package list: Packages.test.cpp's `product-registrations.json` is per product, not one line), in the position
the product list puts it (after `rp.mesen.sms`). The switch is `RP_PRODUCT_REGISTRATIONS_UPDATE`, and the case is
`[packages-dump]` (Packages.test.cpp:68-84), not `RP_GOLDEN_UPDATE` and `[packages]`. `rp.sms.sync` is a protocol node
and is not in this golden at all.

- [ ] **Step 4: Run it**

```bash
pnpm install
cmake --build build --target retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-engine-graph-test "[blastem-md]"
build/bin/retroplug-engine-graph-test "[packages]"
```

Expected: the three new cases pass, `[packages]` passes against the re-recorded golden, and no other case changed.
`pnpm test systems/store-graph` exits 0 too (its `NATIVE_TYPES` list now names the type).

- [ ] **Step 5: Commit**

```bash
git add packages/blastem/native packages/native/package.json packages/native/cmake/tests.cmake \
        packages/native/test/engine-graph/GraphDocs.hpp \
        packages/native/test/engine-graph/SystemNode.test.cpp \
        packages/native/test/engine-graph/golden/product-registrations.json \
        packages/retroplug/test/systems/store-graph.test.ts pnpm-lock.yaml
git commit -m "feat(blastem): rp.blastem.md, its build function, and the engine's Mega Drive cases"
```

---

### Task 15: The TS console descriptor, the `blastem` core role, and the products

**Files:**
- Create: `packages/blastem/ts/consoles.ts`, `packages/blastem/ts/app.ts`, `packages/blastem/ts/enums.ts`,
  `packages/blastem/tsconfig.json` (and add `typecheck` to its manifest)
- Modify: `packages/blastem/package.json` (exports, app, consoles, typecheck),
  `packages/retroplug/src/systemsStore.ts` (the battery prefix), `packages/retroplug/test/paths/rom-format.test.ts`,
  `packages/retroplug/test/systems/battery.test.ts`, `packages/retroplug/test/consoles/registry.test.ts`,
  `packages/retroplug/test/system-settings/registry.test.ts` (its role-kind list gains `blastem`, the first list the
  new role breaks), `packages/retroplug/package.json` (already declares the package from Task 2b). The recorded
  `test/consoles/{cases.ts,recorded.ts}` tables are NOT touched (this plan adds no case to them).

**Interfaces:**
- Consumes: `@retroplug/core/consoles`'s contract, `@retroplug/core/systemRoles`.
- Produces: `MD` (`ConsoleDescriptor` id `blastem.md`, one platform `md` for the extensions `.md`, `.bin` and `.gen`),
  `BLASTEM_ROLE` (kind `blastem`, category `system`), and the generated registries' entries for both.

- [ ] **Step 1: The enums and the descriptor**

`packages/blastem/ts/enums.ts`:

```ts
// The Mega Drive's settings labels. A native enum knob's labels are its ENUMERATOR NAMES with the first letter
// lowercased (SystemNode.hpp's knobs::Label<E>), so MdRegion's {Auto, NtscUs, NtscJp, Pal} spell exactly these four
// words and this tuple is their TS side; a hyphenated "ntsc-us" is refused by the native settings parser.
export const REGION_VALUES = ["auto", "ntscUs", "ntscJp", "pal"] as const;
```

`packages/blastem/ts/consoles.ts`:

```ts
// The Mega Drive's descriptor (docs/megadrive-blastem-design.md section 1): what the shell needs to classify a Mega
// Drive ROM, place the node that runs it, and wire its knobs, without naming the console. Detection is the header at
// $100, never the extension: a Master System `.bin` has no "SEGA" there and is not this platform, and a `.md` file that
// is not one is refused rather than booted as noise.
import { REGION_VALUES } from "./enums";
import { label, type ConsolesContribution, type ConsoleDescriptor } from "@retroplug/core/consoles";
// enumField is the role/console contract's clamping enum field (packages/core/ts/configFields.ts:58): a junk value snaps
// to its default, as every other enum knob's does, instead of throwing the parse.

// ONE rule, the native gate's: "SEGA" at $100, and not "SEGA 32X" (a different console this core does not run). A
// real header's next twelve bytes name the machine ("SEGA MEGA DRIVE ", "SEGA GENESIS    ", or a homebrew's own words),
// so matching the whole 16 refuses carts the native gate accepts; the four bytes are what both sides read.
const MAGIC = "SEGA";
const NOT_THIS_CONSOLE = "SEGA 32X";
const MAGIC_OFFSET = 0x100;

function matchesAt(bytes: Uint8Array, offset: number, text: string): boolean {
  if (bytes.length < offset + text.length) return false;
  for (let i = 0; i < text.length; i++) if (bytes[offset + i] !== text.charCodeAt(i)) return false;
  return true;
}

/** The Mega Drive's descriptor. One node type, one platform: the machine is a Mega Drive whether its header says
 *  MEGA DRIVE or GENESIS. */
export const MD: ConsoleDescriptor = {
  id: "blastem.md",
  core: "blastem",
  nodeType: "rp.blastem.md",
  platforms: [{ id: "md", label: "Mega Drive", extensions: [".md", ".bin", ".gen"] }],
  // The Mega Drive's header sits at $100, inside the cheap tier every Nintendo console is read at, so this adds no
  // deeper read (0x110 would be a third sniff length, and registry.test.ts pins the tiers to [0x134, 0x8200]).
  detect: {
    prefixBytes: 0x134,
    // Rank 4: LAST, so a crafted file carrying the GB logo or the Sega 8-bit header as well is that console and not
    // this one. Ranks are unique per detector (the same rank ties, and a tie is broken by platform id, which is not a
    // decision anyone wrote down), which is why this is not 2 (the Game Boy's).
    rank: 4,
    run: (bytes) =>
      matchesAt(bytes, MAGIC_OFFSET, MAGIC) && !matchesAt(bytes, MAGIC_OFFSET, NOT_THIS_CONSOLE) ? "md" : null,
  },
  // The whole prefix, not the short header: genmddj's product code is at $180 and its version string at $D1B1 in the
  // built ROM, and `hasBattery` reads $1B0. The store's detectBattery reads this same prefix (src/systemsStore.ts),
  // which is what makes the battery rule below see the byte it names.
  providerHeaderBytes: 0x10000,
  // The header's $1B0 region: "RA" and a ROM-DB RAM flag byte mean a battery. $1B0 alone is not enough (a cart may
  // declare RAM it does not keep), so the flag's low nibble must be one of the ROM DB's values (0, 8 or 0x10|0).
  // A header TOO SHORT to hold the field answers TRUE, the policy romHasBattery documents in src/consoles.ts ("only ever
  // returns false when we're certain there's no save", so a working cart is never wrongly greyed): the store reads
  // providerHeaderBytes (0x10000), so this only triggers on a truncated read, and refusing a save the user has is worse
  // than offering one they do not.
  hasBattery: (header) =>
    header.length <= 0x1B2 ||
    (header[0x1B0] === 0x52 && // 'R'
     header[0x1B1] === 0x41 && // 'A'
     (header[0x1B2] & 0x0F) !== 0x0F),
  settingFields: { region: label(REGION_VALUES) },
  // A region fixes the master clock and the frame height, so a change is a rebuild, not a live knob (the native
  // config is read once at boot).
  constructFields: { region: { takes: label(REGION_VALUES), fallback: "auto" } },
  configParams: [],
};

const BLASTEM_CONSOLES: ConsolesContribution = { consoles: [MD] };
export default BLASTEM_CONSOLES;
```

`packages/blastem/ts/app.ts`:

```ts
// The blastem package's app contribution: the `blastem` system role, keyed by the core (platform.ts's Core value), so
// it attaches to any BlastEm system the way `mesen` does to Mesen's. Its schema is the console's knobs; `region` is the
// one the node reads (as a construct-time field, so the store rebuilds on a change).
import type { RoleType } from "@retroplug/core/systemRoles";
import { definePackage } from "@retroplug/core/definePackage";
import { enumField } from "@retroplug/core/configFields";
import { z } from "zod";
import { REGION_VALUES } from "./enums";

export const BLASTEM_ROLE: RoleType = {
  kind: "blastem",
  category: "system",
  // enumField, not z.enum(...).default(...): every role schema CLAMPS a junk value to its default rather than throwing
  // (configFields.ts:58), so a project written by a newer build still parses.
  schema: z.object({ region: enumField(REGION_VALUES, "auto") }),
};

export default definePackage({ roles: [BLASTEM_ROLE] });
```

`packages/blastem/tsconfig.json`:

```json
{
  "extends": "../../tsconfig.base.json",
  "include": ["ts"]
}
```

and the manifest gains its TS members (its `native` member and dependencies are Task 2b's; add the rest):

```json
  "scripts": { "typecheck": "node ../retroplug/scripts/lib/packages.mjs && tsc -p tsconfig.json" },
  "exports": {
    "./app": "./ts/app.ts",
    "./consoles": "./ts/consoles.ts",
    "./enums": "./ts/enums.ts"
  },
  "dependencies": {
    "@retroplug/core": "workspace:*",
    "@retroplug/node-sdk": "workspace:*",
    "@retroplug/system": "workspace:*",
    "zod": "^4.4.3"
  },
  "devDependencies": {
    "@retroplug/testing": "workspace:*",
    "typescript": "^7.0.2"
  },
  "retroplug": {
    "app": "ts/app.ts",
    "consoles": "ts/consoles.ts",
    "native": { "target": "retroplug-blastem", "entry": "rp_blastem_package", "device": false }
  }
```

- [ ] **Step 2: Write the failing tests, in the files that RUN**

Two real test files, not the recorded tables. `packages/retroplug/test/consoles/cases.ts` and `recorded.ts` are DATA
(`recorded.ts` is the pre-descriptor answers, "RECORDED, not computed", with no re-record switch), so a test written
there never runs and appending to it breaks the registry's one-answer-per-entry comparison. The classification cases
belong in `test/paths/rom-format.test.ts` (`detectPlatform`, the pure sniffer) and the battery's in
`test/systems/battery.test.ts` (`romHasBattery`, also pure):

```ts
// test/paths/rom-format.test.ts, beside the other magic tests: the Mega Drive's header at $100, in either of its two
// names, and the two negatives that matter (a headerless .bin, and a 32X image, which is not this console).
const mdRom = (magic: string, len = 0x4000) => {
  const b = new Uint8Array(len);
  for (let i = 0; i < magic.length; i++) b[0x100 + i] = magic.charCodeAt(i);
  return b;
};

test("md: the SEGA header at $100, in either name", () => {
  expect(detectPlatform(mdRom("SEGA MEGA DRIVE "))).toBe("md");
  expect(detectPlatform(mdRom("SEGA GENESIS    "))).toBe("md");
  expect(detectPlatform(mdRom("SEGA GENESIS    "), ".bin")).toBe("md");
  expect(detectPlatform(mdRom("SEGA HOMEBREW  "))).toBe("md");  // the four bytes are the rule, not the machine's name
});

test("md: a headerless .bin is not a Mega Drive", () => {
  // Nothing claims a headerless `.bin`: the SMS descriptor's `extensionOnly` platforms list only `.sms` and `.gg`
  // (mesen/ts/consoles.ts:161), and `extensionOnly()` matches only a listed extension (src/consoles.ts:228-234), so the
  // answer is "unknown", never "md". The design's "an SMS .bin still goes to Mesen" is moot: no console claims `.bin`
  // by extension, and a `.bin` with the Mega Drive's header is the case above.
  expect(detectPlatform(new Uint8Array(0x4000), ".bin")).toBe("unknown");
  expect(detectPlatform(new Uint8Array(0x4000))).toBe("unknown");
});

test("md: a 32X image is not a Mega Drive", () => {
  expect(detectPlatform(mdRom("SEGA 32X        "))).not.toBe("md");
});
```

```ts
// test/systems/battery.test.ts, beside the NES and GB rules.
test("romHasBattery: MD reads $1B0's RA block", () => {
  const mdHeader = (flag: number, len = 0x200) => {
    const b = new Uint8Array(len);
    b[0x1B0] = 0x52; // 'R'
    b[0x1B1] = 0x41; // 'A'
    b[0x1B2] = flag;
    return b;
  };
  expect(romHasBattery(mdHeader(0xF8), "md")).toBe(true);   // odd-byte SRAM, genmddj's own header
  expect(romHasBattery(mdHeader(0x20), "md")).toBe(true);   // even-byte SRAM
  expect(romHasBattery(mdHeader(0x0F), "md")).toBe(false);  // "no RAM" in the ROM DB's nibble
  expect(romHasBattery(new Uint8Array(0x150), "md")).toBe(true);  // too short to see the field: never wrongly greyed
});
```

and the store's read length, which is what lets that field be seen at all:

```ts
// packages/retroplug/src/systemsStore.ts, detectBattery: the console's own prefix where it declares one (a Sega
// header sits deep, and the Mega Drive's battery byte is at $1B2, past the short header). Import providerHeaderBytes
// from "./consoles" beside platformCapabilities.
const header = this.backend.readFilePrefix(romPath, providerHeaderBytes(platform) ?? ROLE_HEADER_LEN) ?? new Uint8Array();
return romHasBattery(header, platform);
```

The edit names no platform, core or cart, so the shell's boundary rules and source-literal scan accept it. Its one side
effect is on the SEGA 8-bit consoles, which declare `providerHeaderBytes` too (`SEGA_SNIFF_LEN`): their `detectBattery`
now reads that many bytes instead of `ROLE_HEADER_LEN`. The ANSWER is unchanged (the SMS descriptor declares no
`hasBattery`, so `romHasBattery` answers `true` for it either way, `src/consoles.ts:158`); the cost is one longer file
read per SMS load. No other package's provider runs on `md`, and nothing on the block path touches this.

- [ ] **Step 3: Update the pins the new descriptor moves, then wire the package in**

`packages/retroplug/test/consoles/registry.test.ts` pins three facts that a new console changes, and each is an
expectation written by hand (the recorded CLASSIFY/BATTERY tables are NOT touched: this plan adds no case to `cases.ts`,
which is why they stay valid):

- `sniffLengths()` stays `[0x134, 0x8200]` (the descriptor reads $100 inside the cheap tier), so that line does not move.
- `registeredCores()` becomes `["sameboy", "mesen", "blastem"]`, in product order.
- `registeredPlatforms()` gains `"md"` after `"gg"` (product order), and `projectConfig`'s two error texts name the new
  core and platform (`unknown core "gambatte" (known: sameboy, mesen, blastem)`).

And `packages/retroplug/test/system-settings/registry.test.ts:75` pins every role KIND, so `"blastem"` joins that list
here, in the commit that registers the role (a later task's `"genmddj-sync"` joins it there). Nothing else in that file
moves: the role has no feature-role entry and its schema is one enum field.

Then `package.json` (root): `"blastem"` is already in both product lists from Task 2b, and
`packages/retroplug/package.json` already declares the package, so the generator finds the module:

```bash
pnpm install
pnpm typecheck
pnpm test paths/rom-format
pnpm test systems/battery
pnpm test consoles/registry
pnpm test system-settings
pnpm test:ui bindings
```

Expected: every command exits 0. `pnpm test:ui` is `test:ui` (there is no `test-ui` script) and its `bindings` slug is
the input-matrix case a new platform could move; if no row appears, it is unchanged by design. `pnpm test system-settings`
is the role-kind list's case, which fails before the `"blastem"` edit and passes after it.

- [ ] **Step 4: The `.bin` case the design names**

The design says "an SMS `.bin`, which has no such header, still goes to Mesen". In this tree no console claims `.bin` by
extension at all (SMS lists `.sms` and `.gg`, Step 2's test asserts `"unknown"`), so the sentence's intent is the one that
holds: a `.bin` whose bytes are not a Mega Drive ROM is not classified as one. Task 18's provider test holds the other
half (an SMS ROM gets no `genmddj-sync` even when it carries genmddj's marker).

- [ ] **Step 5: Commit**

```bash
git add packages/blastem package.json pnpm-lock.yaml packages/retroplug/src/systemsStore.ts \
        packages/retroplug/test/paths/rom-format.test.ts packages/retroplug/test/systems/battery.test.ts \
        packages/retroplug/test/consoles/registry.test.ts packages/retroplug/test/system-settings/registry.test.ts
git commit -m "feat(blastem): the Mega Drive console descriptor and the blastem core role"
```

---

### Task 16: Z, X, Y and Mode on the pad's wire bytes

**Files:**
- Modify: `packages/retroplug/src/keyCodes.ts`, `packages/retroplug/src/bindingMap.ts`
- Create: `packages/blastem/native/src/system/blastem/InputTypes.hpp`
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}` (`pressButton`),
  `packages/blastem/native/test/BlastemPad.test.cpp` (new), `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: the shared press-byte table (`InputTypes.hpp`'s peers in Mesen and SameBoy: `Right=0 ... Start=7`).
- Produces: `rp::blastem::MdButton` (`Right=0, Left=1, Up=2, Down=3, A=4, B=5, C=6, Start=7, X=8, Y=9, Z=10, Mode=11`),
  `BlastemMdSystem::pressButton` applying a transition through BlastEm's `io_gamepad_down/up`, and
  `BUTTON_VALUE` extended with `X`, `Y`, `Z`, `Mode`.

- [ ] **Step 1: The wire table**

`packages/blastem/native/src/system/blastem/InputTypes.hpp`:

```cpp
#pragma once

#include <cstdint>

// The Mega Drive's button identifiers, keyed by the wire byte SystemBase::pressButton takes. Position-aligned with every
// other console's table (Right=0 ... Start=7), and the 6-button pad's extra buttons after them, exactly as the GBA's L
// and R are.
//
//   Select (6) is the Mega Drive's C button: the pad has A, B and C, and the shared table's sixth position is a face
//   button on every console whose pad has a third one. That is a choice, and it is the one the JS bridge's name table
//   makes unavoidable; the UI labels it per platform.
//
// BlastEm's own enum is {BUTTON_INVALID, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, BUTTON_A, BUTTON_B, BUTTON_C,
// BUTTON_START, BUTTON_X, BUTTON_Y, BUTTON_Z, BUTTON_MODE} (io.h:117-130, NUM_GAMEPAD_BUTTONS last), and
// io_gamepad_down takes that ordering, so
// BlastemMdSystem maps this table onto it explicitly rather than casting.
enum class MdButton : std::uint8_t {
    Right  = 0,
    Left   = 1,
    Up     = 2,
    Down   = 3,
    A      = 4,
    B      = 5,
    C      = 6,   // the shared table's Select slot
    Start  = 7,
    X      = 8,
    Y      = 9,
    Z      = 10,
    Mode   = 11,
    Count  = 12,
};
```

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemPad.test.cpp`:

```cpp
// The pad reaches the emulated 6-button controller: press A, read the port-1 pad back through BlastEm's device state.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"
#include "system/blastem/InputTypes.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

}  // namespace

TEST_CASE("a pressed button is held on port 1's pad", "[blastem][pad]") {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::portPollRom());
    sys->onActivate(kRate);
    sys->pressButton(static_cast<std::uint8_t>(rp::blastem::MdButton::A), true);
    std::vector<float> l(kFrames), r(kFrames);
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info{};
    info.frames = kFrames;
    sys->prepareForBlock(info);
    while (sys->stepIfBelowTarget(kFrames)) {}
    sys->finishBlock(info, lanes, 2);
    // BlastEm's gamepad state: THE pad's input bits are inverted at the port, so `input[GAMEPAD_TH1]` bit 4 (BUTTON_A)
    // set means pressed. Read it through the port rather than the device's struct, so the mapping is what is tested.
    CHECK(sys->padDown(static_cast<std::uint8_t>(rp::blastem::MdButton::A)));
    sys->pressButton(static_cast<std::uint8_t>(rp::blastem::MdButton::A), false);
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(kFrames);
    sys->finishBlock(info, lanes, 2);
    CHECK_FALSE(sys->padDown(static_cast<std::uint8_t>(rp::blastem::MdButton::A)));
}
```

with a test seam `bool padDown(std::uint8_t button) const;` on the system, documented as such the way
`MesenSmsSystem::buttonDown` is, and implemented from the same tables in the other direction: the port
`find_gamepad(&gen_->io, 1)` reaches holds each button's bit set in `input[state]` while pressed (a pad's input is
inverted at the port, so set means pressed). Read `io.h`'s `button_defs` for the two tables and write the inverse from
them:

```cpp
bool BlastemMdSystem::padDown(std::uint8_t button) const {
    if (!gen_ || button >= 12) return false;
    io_port* port = find_gamepad(const_cast<sega_io*>(&gen_->io), 1);
    if (!port) return false;
    // io.c's own button_defs table (io.c:77-90), restated here because it is `static` in io.c and no header exposes it:
    // each button is one bit of one of the pad's three input registers, set while the button is held (the port inverts
    // it on the wire). Keep this table and pressButton's in one edit, or the seam stops describing what it drives.
    static constexpr std::uint8_t kState[12] = {GAMEPAD_TH1, GAMEPAD_TH1, GAMEPAD_TH0, GAMEPAD_TH0,
                                                GAMEPAD_TH0, GAMEPAD_TH1, GAMEPAD_TH1, GAMEPAD_TH0,
                                                GAMEPAD_EXTRA, GAMEPAD_EXTRA, GAMEPAD_EXTRA, GAMEPAD_EXTRA};
    static constexpr std::uint8_t kBit[12] = {0x08, 0x04, 0x01, 0x02,
                                              0x10, 0x10, 0x20, 0x20,
                                              0x04, 0x02, 0x01, 0x08};
    return (port->input[kState[button]] & kBit[button]) != 0;
}
```

(`kState`/`kBit` are `button_defs`' `states[0]`/`value` for each of `MdButton`'s twelve, in its order: Right, Left, Up,
Down, A, B, C, Start, X, Y, Z, Mode. The test asks the port, so it holds the mapping rather than restating `pressButton`'s
array.)

- [ ] **Step 3: Run it, then implement**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[pad]"
```

Expected: FAIL: `pressButton` is the base's no-op, `padDown` does not exist.

`BlastemMdSystem::pressButton`:

```cpp
void BlastemMdSystem::pressButton(std::uint8_t button, bool down) {
    if (!gen_) return;
    currentInstance = this;
    // The byte is the shared table's; BlastEm's is its own ordering, so the map is explicit (io.h:117-130).
    static constexpr std::uint8_t kBlastemButton[] = {
        DPAD_RIGHT, DPAD_LEFT, DPAD_UP, DPAD_DOWN, BUTTON_A, BUTTON_B, BUTTON_C,
        BUTTON_START, BUTTON_X, BUTTON_Y, BUTTON_Z, BUTTON_MODE,
    };
    if (button >= std::size(kBlastemButton)) return;  // a byte that names no button drives none
    // The player-1 pad is gamepad number 1, not 0: setup_io_devices connects "gamepad6.1" to port 1 and "gamepad6.2" to
    // port 2 (io.c:386-390), find_gamepad matches on that number (io.c:92-110), and number 0 matches the EXT port
    // instead (IO_NONE's zeroed gamepad_num), so a press would go nowhere.
    if (down) {
        io_gamepad_down(&gen_->io, 1, kBlastemButton[button]);
    } else {
        io_gamepad_up(&gen_->io, 1, kBlastemButton[button]);
    }
}
```

`prepareForBlock` does not need a press queue for the MD: BlastEm's gamepad state is a level that io_data_read samples
live, so a press applied before the block is what the block's polls see, which is the same contract Mesen's apply
gives (the engine's press ordering per block is preserved because presses are applied before the core runs).

- [ ] **Step 4: The JS side's bytes**

In `packages/retroplug/src/keyCodes.ts`, extend `BUTTON_VALUE` (the file's comment says the values must not drift from
native's `InputTypes.hpp`; update both):

```ts
export const BUTTON_VALUE: Record<string, number> = {
  Right: 0, Left: 1, Up: 2, Down: 3, A: 4, B: 5, Select: 6, Start: 7,
  // The 6-button pad's extras, and every console whose pad has more than eight (the GBA's L/R are 8/9 of ITS table,
  // the Mega Drive's X/Y/Z/Mode are 8..11 of its own: a byte is only ever read by the console it was sent to).
  X: 8, Y: 9, Z: 10, Mode: 11,
};
```

and in `bindingMap.ts`, the gamepad channel's defaults gain X/Y/Z/Mode only if a host gamepad's buttons should drive
them: leave them unbound (a missing entry reads `-`), so no default binding changes and no recorded keyboard/gamepad
matrix moves. Note that in the comment above `BUTTON_VALUE`.

- [ ] **Step 5: Run the suites a byte change can move**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[pad]"
pnpm test config/bindings
pnpm test:ui bindings
```

Expected: all exit 0 (no default binding moved, so the UI's input cases are untouched). `pnpm test config/bindings` is
the TS suite's slug for `test/config/bindings.test.ts`, and `pnpm test:ui bindings` is `test-ui/bindings.test.ts`: there
is no `test-ui` script, and neither `bindings` nor `input` is a slug under `pnpm test`.

- [ ] **Step 6: Commit**

```bash
git add packages/blastem/native packages/retroplug/src/keyCodes.ts packages/retroplug/src/bindingMap.ts
git commit -m "feat(blastem): the 6-button pad on port 1, and Z/X/Y/Mode on the wire"
```

---

### Task 17: The parity baseline's Mega Drive group

**Files:**
- Create: `packages/blastem/ts/fixtures.ts`, `packages/blastem/ts/test/fixtures.test.ts`,
  `packages/retroplug/test-native/parity/md.test.ts`, `packages/retroplug/test-native/parity/baseline/md.json`
- Modify: `packages/blastem/package.json` (`./testFixtures` export),
  `packages/retroplug/test-native/parity/scenes.ts` (`mdSynthetic`)

**Interfaces:**
- Consumes: the descriptor (Task 15), the core (all native tasks).
- Produces: `packages/blastem/ts/fixtures.ts`'s `fmPsgRom(): Uint8Array`, `psgOnlyRom(): Uint8Array` and `fnv1a()`
  (the TS twins of the C++ images: same bytes, generated independently for the TS tier), and the parity group `md`.

- [ ] **Step 1: The TS image generator and its test**

`packages/blastem/ts/fixtures.ts` builds the same two images the native cases boot, from the same write tables, in
TypeScript (the parity tier cannot include a C++ header). It is published as `./testFixtures`, per spec/06's rule for
a cart's or console's synthetic images (`ts/fixtures.ts`, never `./fixtures`).

```ts
// Synthetic Mega Drive images for the TS tiers, the twins of packages/blastem/native/test/SyntheticRom.hpp: the same
// layout (header at $100, vectors, `move.b #imm,(xxx).L` writes at $200, then a spin). Both files carry a hash of each
// image and the native header pins the same bytes through its own cases, so a drift on either side fails a test.
export const MD_ROM_SIZE = 0x10000;

function image(program: number[], writes: [number, number][]): Uint8Array {
  const rom = new Uint8Array(MD_ROM_SIZE);
  const vectors = [0x00, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x02, 0x00];
  rom.set(vectors, 0);
  const magic = "SEGA MEGA DRIVE ";
  for (let i = 0; i < magic.length; i++) rom[0x100 + i] = magic.charCodeAt(i);
  let at = 0x200;
  for (const [address, value] of writes) {
    rom.set([0x13, 0xfc, 0x00, value, (address >>> 24) & 0xff, (address >>> 16) & 0xff, (address >>> 8) & 0xff, address & 0xff], at);
    at += 8;
  }
  rom.set(program, at);
  at += program.length;
  rom[at] = 0x60;
  rom[at + 1] = 0xfe;
  return rom;
}

const PSG_WRITES: [number, number][] = [
  [0xc00011, 0x8e],
  [0xc00011, 0x01],
  [0xc00011, 0x90],
];

/** The PSG tone plus a keyed YM2612 channel: the image the audio and parity cases boot. */
export function fmPsgRom(): Uint8Array {
  return image([0x60, 0xfe], [
    [0xa04000, 0x22], [0xa04001, 0x00], [0xa04000, 0x27], [0xa04001, 0x00],
    [0xa04000, 0x2b], [0xa04001, 0x80], [0xa04000, 0x30], [0xa04001, 0x01],
    [0xa04000, 0x40], [0xa04001, 0x00], [0xa04000, 0x50], [0xa04001, 0x1f],
    [0xa04000, 0x60], [0xa04001, 0x00], [0xa04000, 0x70], [0xa04001, 0x00],
    [0xa04000, 0x80], [0xa04001, 0x0f], [0xa04000, 0x90], [0xa04001, 0x00],
    [0xa04000, 0xa4], [0xa04001, 0x22], [0xa04000, 0xa0], [0xa04001, 0x69],
    [0xa04000, 0xb0], [0xa04001, 0x06], [0xa04000, 0xb4], [0xa04001, 0xc0],
    [0xa04000, 0x28], [0xa04001, 0xf0],
    ...PSG_WRITES,
  ]);
}

/** The same tone with the PSG alone: the FM half's own control in the audio tier. */
export function psgOnlyRom(): Uint8Array {
  return image([0x60, 0xfe], PSG_WRITES);
}

/** A hash of an image, so a drift between this file and the native header is a failing case on both sides. */
export function fnv1a(bytes: Uint8Array): number {
  let h = 2166136261 >>> 0;
  for (const b of bytes) {
    h ^= b;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}
```

`packages/blastem/ts/test/fixtures.test.ts` pins the hashes to the native ones (0x6397D2DF and 0x0DCF5FCA in
`SyntheticRom.test.cpp`; the same bytes, so the same two numbers, and a drift on either side fails one of the two
files):

```ts
import { test, expect } from "@retroplug/testing";
import { fmPsgRom, psgOnlyRom, fnv1a } from "../fixtures";

test("the synthetic images are the bytes the native cases boot", () => {
  expect(fnv1a(psgOnlyRom())).toBe(0x6397d2df);
  expect(fnv1a(fmPsgRom())).toBe(0x0dcf5fca);
});
```

Add `"./testFixtures": "./ts/fixtures.ts"` to the package's `exports`, and `ts` to `tsconfig.json`'s `include`
(already covers `ts`).

- [ ] **Step 2: The parity scene**

In `packages/retroplug/test-native/parity/scenes.ts`:

```ts
import { fmPsgRom } from "@retroplug/blastem/testFixtures";

/** A synthetic Mega Drive image as a new system: written into the run's directory and adopted, so the store's own
 *  detection and construct path are what runs. */
export function mdSynthetic(rig: Rig): number {
  // `rig.dir()` does not exist (the parity directory is a PRIVATE romDir()), so the case writes into its own config dir
  // (__CONFIG_DIR__, the temp dir run-native-tests.mjs gives each file) and lets the rig copy it, exactly as rig.add()
  // does for a real ROM. Nothing is added to rig.ts.
  const src = __CONFIG_DIR__ + "/md-synthetic.md";
  rig.writeFile(src, fmPsgRom());
  return rig.add(src);
}
```

(`declare const __CONFIG_DIR__: string;` at the top of the file, as the other test-native files do.)

- [ ] **Step 3: The group**

`packages/retroplug/test-native/parity/md.test.ts`:

```ts
// The Mega Drive group: the synthetic image playing PSG and FM, and the port-2 device answering a staged level at its
// sample. Cases are compared byte for byte against baseline/md.json on linux-x64-gnu13 (spec/06's parity rule).
import { runGroup } from "./group";
import { mdSynthetic } from "./scenes";

runGroup("md", [
  {
    name: "boot",
    outputs: 2,
    audible: [0, 1],  // the PSG tone and the keyed FM channel, in both lanes
    setup: (rig) => { mdSynthetic(rig); },
    // The partitions the baseline is held at: a mixed cycle whose sizes straddle the one-sample grid (1 frame) and any
    // block the engine runs, then the machine's own 512-frame blocks. Everything the group records is a function of
    // absolute time, so every partition must give the same lanes.
    segments: [{ blocks: [1, 64, 1024, 4096, 7, 512, 512, 512, 512, 512] }],
  },
]);
```

One case, one segment: the image plays from power-on and the group's job is its audio bytes at a partition-independent
time base (the invariance itself is Task 9's native case, and the parity baseline's contribution is that a future audio
change fails here). The port device's timing is proved natively (Task 8) and end to end (Task 22). If the recording
refuses the 1-frame blocks for a case that is silent in its first segment, the boot is what needs the shorter
partitions dropped, and the segments become `[64, 1024, 4096, 512, 512, 512, 512]`.

- [ ] **Step 4: Record the baseline**

```bash
RP_PARITY_UPDATE=1 pnpm test:native parity/md
```

Expected: on `linux-x64-gnu13` only, the run writes `md.json` with the group's target and digests, and its alone-run
check passes. On another host the recording is refused by design; record it on the devcontainer (which is that target)
and commit the file.

- [ ] **Step 5: Check it, then commit**

```bash
pnpm test:native parity/md
```

Expected: PASS, comparing against the file just written. Then:

```bash
git add packages/blastem packages/retroplug/test-native/parity/md.test.ts \
        packages/retroplug/test-native/parity/baseline/md.json \
        packages/retroplug/test-native/parity/scenes.ts packages/retroplug/test-native/parity/rig.ts
git commit -m "test(blastem): the parity baseline's Mega Drive group"
```

---

### Task 18: `packages/genmddj`: identity and the provider

**Files:**
- Create: `packages/genmddj/package.json`, `packages/genmddj/tsconfig.json`, `packages/genmddj/ts/romDetect.ts`,
  `packages/genmddj/ts/romProvider.ts`, `packages/genmddj/ts/fixtures.ts`,
  `packages/genmddj/ts/test/romDetect.test.ts`
- Modify: root `package.json` (both product lists), `packages/retroplug/package.json` (dependency)

**Interfaces:**
- Consumes: `@retroplug/core/systemRoles`'s `RomProvider`.
- Produces: `isGenmddjRom(rom: Uint8Array): boolean`, `identifyGenmddjVersion(rom: Uint8Array): string | null`,
  `GENMDDJ_ROM_PROVIDER: RomProvider` (attaching `genmddj-sync` with `{}`), and `packages/genmddj/ts/fixtures.ts`'s
  header builders.

- [ ] **Step 1: The package and its identity**

`packages/genmddj/package.json`:

```json
{
  "name": "@retroplug/genmddj",
  "version": "0.1.0",
  "private": true,
  "scripts": { "typecheck": "tsc -p tsconfig.json" },
  "type": "module",
  "description": "genmddj's half of the app: the ROM identity, the IN24 clock levels (a 2-bit counter on controller port 2's TR and TH), the genmddj-sync role, its ROM provider, and the rp.genmddj.sync protocol node (span label 24).",
  "exports": {
    "./romDetect": "./ts/romDetect.ts",
    "./romProvider": "./ts/romProvider.ts",
    "./testFixtures": "./ts/fixtures.ts"
  },
  "retroplug": {},
  "dependencies": {
    "@retroplug/core": "workspace:*",
    "@retroplug/smsggdj": "workspace:*",
    "zod": "^4.4.3"
  },
  "devDependencies": {
    "@retroplug/testing": "workspace:*",
    "typescript": "^7.0.2"
  }
}
```

`tsconfig.json` is the two-line file every package has (`extends` `../../tsconfig.base.json`, `include: ["ts"]`).

`ts/romDetect.ts`:

```ts
// genmddj's identity, from its ROM header: the product code "GM GENMDDJ-00" at $180 (src/main.asm:577-600), and the
// build's own version string ("V0.27") in the first 64 KiB. Both, because either alone is a guess: a homebrew could
// fill the product-code field, and a version string is four kinds of other data.
//
// 64 KiB, not 32: in the built ROM the version string is at $D1B1 (53681, proved with `grep -boa 'V[0-9][0-9]*\.[0-9][0-9]*'
// build/genmddj.bin`, whose FIRST match is that one), which is past the halfway point of a 32 KiB scan and past the
// 0x150-byte short header the role providers otherwise see. The descriptor's providerHeaderBytes is 0x10000 for exactly
// this reason, so the scan and the prefix agree.
//
// The gate is REQUIRED, not tidy (design section 4): the sync drives controller port 2's TR and TH, which every Mega
// Drive game reads as player 2, so attaching it to any ROM would inject phantom button presses.
const PRODUCT_CODE = "GM GENMDDJ-";
const PRODUCT_CODE_OFFSET = 0x180;
const VERSION_SCAN_LEN = 0x10000;

function matches(rom: Uint8Array, offset: number, text: string): boolean {
  if (rom.length < offset + text.length) return false;
  for (let i = 0; i < text.length; i++) if (rom[offset + i] !== text.charCodeAt(i)) return false;
  return true;
}

// The scan itself, without the product-code gate: the public version function and the identity predicate both use it,
// and if it called isGenmddjRom they would recurse.
function identifyGenmddjVersionUnchecked(rom: Uint8Array): string | null {
  const end = Math.min(rom.length, VERSION_SCAN_LEN);
  for (let i = 0; i < end - 4; i++) {
    if (rom[i] !== 0x56) continue; // 'V'
    let text = "";
    for (let k = i + 1; k < end && text.length < 8; k++) {
      const c = rom[k];
      if ((c >= 0x30 && c <= 0x39) || c === 0x2e) text += String.fromCharCode(c);
      else break;
    }
    const m = /^(\d+\.\d+)$/.exec(text);
    if (m) return m[1];
  }
  return null;
}

/** The version string ("0.27") a genmddj build carries, or null. Gated first, so a ROM that is not genmddj never
 *  reports a version. */
export function identifyGenmddjVersion(rom: Uint8Array): string | null {
  return isGenmddjRom(rom) ? identifyGenmddjVersionUnchecked(rom) : null;
}

/** True when `rom` is a genmddj build: its product code and one of its version strings. */
export function isGenmddjRom(rom: Uint8Array): boolean {
  if (!matches(rom, PRODUCT_CODE_OFFSET, PRODUCT_CODE)) return false;
  return identifyGenmddjVersionUnchecked(rom) !== null;
}
```

The two functions share one scan (`identifyGenmddjVersionUnchecked`), so the identity predicate and the version report
can never disagree about where a version string is; the public version function gates on the product code first, which
is what makes `identifyGenmddjVersion` safe to call on any ROM.

`ts/romProvider.ts`:

```ts
// genmddj's ROM provider: the cart gets the `genmddj-sync` role that clocks it from the DAW transport. Matched on the
// ROM's identity, not the platform: the transport rides controller port 2's TR and TH, which every Mega Drive game
// reads as player 2.
import type { RomProvider } from "@retroplug/core/systemRoles";
import { isGenmddjRom } from "./romDetect";

export const GENMDDJ_ROM_PROVIDER: RomProvider = (rom) => {
  if (rom.platform !== "md" || !isGenmddjRom(rom.header)) return [];
  return [{ kind: "genmddj-sync", config: {} }];
};
```

`ts/fixtures.ts` (the synthetic headers the tests build; published as `./testFixtures` per spec/06):

```ts
/** A Mega Drive image whose header is a genmddj build's, and whose body carries `version` at `versionOffset`, which
 *  defaults to the built ROM's own $D1B1. A case can pass another offset, which is how the scan's "not position-pinned"
 *  claim is tested. */
export function genmddjHeader(version = "V0.27", productCode = "GM GENMDDJ-00", versionOffset = 0xd1b1): Uint8Array {
  const rom = new Uint8Array(0x10000);  // the size the provider is shown (providerHeaderBytes): 0xD1B1 fits inside it
  const set = (offset: number, text: string) => {
    for (let i = 0; i < text.length; i++) rom[offset + i] = text.charCodeAt(i);
  };
  set(0x100, "SEGA MEGA DRIVE ");
  set(0x180, productCode);
  set(versionOffset, version);
  return rom;
}
```

- [ ] **Step 2: Write the failing test**

`packages/genmddj/ts/test/romDetect.test.ts`:

```ts
// genmddj's identity and the provider that gives it its role: the product code AND a version string, and nothing else,
// because the sync's pins are player 2's buttons to every other game.
import { test, expect } from "@retroplug/testing";
import { isGenmddjRom, identifyGenmddjVersion } from "../romDetect";
import { GENMDDJ_ROM_PROVIDER } from "../romProvider";
import { genmddjHeader } from "../fixtures";

test("genmddj is its product code and its version string", () => {
  expect(isGenmddjRom(genmddjHeader())).toBe(true);
  expect(identifyGenmddjVersion(genmddjHeader())).toBe("0.27");
  expect(identifyGenmddjVersion(genmddjHeader("V1.10"))).toBe("1.10");
});

test("the version string is found wherever the build puts it, not at one offset", () => {
  // The real ROM has it at $D1B1, past a 32 KiB scan and past the 0x150-byte short header; a build could move it, and
  // the scan is the whole reason the identity is not pinned to an offset. The old $400 position is the case that would
  // fail if VERSION_SCAN_LEN went back to 0x8000.
  expect(identifyGenmddjVersion(genmddjHeader("V0.27", "GM GENMDDJ-00", 0xd1b1))).toBe("0.27");
  expect(identifyGenmddjVersion(genmddjHeader("V0.27", "GM GENMDDJ-00", 0x400))).toBe("0.27");
});

test("either half alone is not genmddj", () => {
  expect(isGenmddjRom(genmddjHeader("V0.27", "GM OTHER  -00"))).toBe(false);
  const noVersion = genmddjHeader();
  noVersion.fill(0x00, 0xd1b1, 0xd1c0);  // the version string's own bytes, where this fixture puts it
  expect(isGenmddjRom(noVersion)).toBe(false);
});

test("a short prefix is not genmddj rather than a throw", () => {
  expect(isGenmddjRom(new Uint8Array(0x100))).toBe(false);
  // The role provider's default prefix is 0x150, which ends before the product code at $180: an md ROM read at that
  // length is not genmddj, which is why the descriptor declares providerHeaderBytes 0x10000.
  expect(isGenmddjRom(genmddjHeader().slice(0, 0x150))).toBe(false);
});

test("the provider attaches genmddj-sync on md and nothing to anything else", () => {
  const rom = genmddjHeader();
  expect(GENMDDJ_ROM_PROVIDER({ platform: "md", header: rom } as never)).toEqual([{ kind: "genmddj-sync", config: {} }]);
  expect(GENMDDJ_ROM_PROVIDER({ platform: "sms", header: rom } as never)).toEqual([]);
  expect(GENMDDJ_ROM_PROVIDER({ platform: "md", header: genmddjHeader("V0.27", "GM OTHER  -00") } as never)).toEqual([]);
});
```

(Read `RomProvider`'s real argument type in `packages/core/ts/systemRoles.ts` and build the argument with its field
names; smsggdj's `romProvider.ts` reads `rom.platform` and `rom.header`, and the cast is only to keep the case short.)

- [ ] **Step 3: Run it, then wire the package in**

```bash
pnpm test genmddj/romDetect
```

Expected: FAIL (module not found). Then create the package files, add `"genmddj"` AFTER `"smsggdj"` in both product
lists (its only contributing dependency is `@retroplug/smsggdj`, and the generator refuses a package listed before a
dependency it declares, which is what "after blastem" did: blastem sits before smsggdj), add
`"@retroplug/genmddj": "workspace:*"` to `packages/retroplug/package.json`, `pnpm install`, and run again:

```bash
pnpm test genmddj/romDetect && pnpm --filter @retroplug/genmddj typecheck && pnpm check:boundaries
```

Expected: all exit 0. The generator refuses a package with no `app`/`protocol` module only if the manifest names one,
so a manifest naming just its exports and native-free members is valid; Task 20 adds `app`, `protocol` and the role.

- [ ] **Step 4: Commit**

```bash
git add packages/genmddj package.json pnpm-lock.yaml packages/retroplug/package.json
git commit -m "feat(genmddj): the genmddj package, its ROM identity and its provider"
```

---

### Task 19: The counter state machine takes its encoder

**Files:**
- Modify: `packages/smsggdj/ts/smsSync.ts` (extract the state machine), `packages/smsggdj/ts/smsSyncNode.ts`
- Create: `packages/genmddj/ts/mdSync.ts`, `packages/genmddj/ts/test/mdSync.test.ts`
- Modify: `packages/smsggdj/package.json` (export the extracted module's new names if they live in a new file)

**Interfaces:**
- Consumes: `smsggdj`'s IN24 counter semantics (`START` 0, each clock +1 while running, `STOP` freeze, `mod 4`).
- Produces: `@retroplug/smsggdj/smsSync`'s `CounterSync` (the encoder-parameterised state machine: `advance()`,
  `levels()`), `smsSyncNode` built on it, and `@retroplug/genmddj/mdSync`'s `mdSyncLevels(counter)` and
  `GENMDDJ_SYNC_PPQN` / `GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL`.

- [ ] **Step 1: Read what the state machine is, exactly**

`smsSyncNode.ts`'s `process` is the whole of it: a free-running counter advanced by `clock.walk(24, tick)`, where each
tick does `counter = (counter + 1) % 4` and sends `levels(counter)` at the tick's offset. The counter's absolute value
is irrelevant (the ROM diffs it), there is no arm and no locate, and `TickClock.walk` is the DAW-transport clock. That
is the state machine genmddj shares; only `levels` differs.

- [ ] **Step 2: Write the failing test for the MD encoder**

`packages/genmddj/ts/test/mdSync.test.ts`:

```ts
// genmddj's IN24 level word: counter bit 0 on TR ($A10005 bit 5), bit 1 on TH (bit 6), every other pin released
// (src/main.asm:9438 sync_read: `move.b $A10005,d0; lsr.b #5,d0; andi.b #3,d0`). Active high at the port: a set bit in
// the counter is a HIGH line, which is the same polarity as smsggdj's (active high, idle = counter 3); only the pin bits
// differ, because the two ports are different ports.
import { test, expect } from "@retroplug/testing";
import { mdSyncLevels, GENMDDJ_SYNC_COUNTER_MOD, GENMDDJ_SYNC_IDLE_LEVELS } from "../mdSync";

test("the counter's bits are TR and TH, high for a one", () => {
  expect(mdSyncLevels(0)).toBe(0x00);        // both lines low
  expect(mdSyncLevels(1)).toBe(0x20);        // TR high, TH low
  expect(mdSyncLevels(2)).toBe(0x40);        // TH high, TR low
  expect(mdSyncLevels(3)).toBe(0x60);        // both high: the idle value the ROM's arming latches
  expect(GENMDDJ_SYNC_IDLE_LEVELS).toBe(0x60);
});

test("the counter wraps every four clocks", () => {
  expect(GENMDDJ_SYNC_COUNTER_MOD).toBe(4);
  expect(mdSyncLevels(4)).toBe(mdSyncLevels(0));
  expect(mdSyncLevels(-1)).toBe(mdSyncLevels(3));
});
```

- [ ] **Step 3: Run it**

```bash
pnpm test genmddj/mdSync
```

Expected: FAIL (module not found).

- [ ] **Step 4: Extract the state machine and add the encoder**

In `packages/smsggdj/ts/smsSync.ts`, add (the file already holds `smsSyncLevels`, `ggSyncLevels`, `syncLevelsFor`):

```ts
/** A 2-bit free-running counter at a tracker's IN24 rate, and the level word it puts on the wire. The state machine
 *  smsggdj and genmddj share: the ROM reads deltas, so the counter's absolute value never matters, there is no arm and
 *  no locate, and every tick sends the encoder's word. `encode` is the only difference between the two carts. */
export class CounterSync {
  constructor(
    private encode: (counter: number) => number,
    private readonly mod = SMS_SYNC_COUNTER_MOD,
  ) {}
  private counter = 0;
  private walking = 0;
  /** The counter value to advance to, before a walk: the node keeps it across blocks so a mid-block walk resumes. */
  get value(): number { return this.counter; }
  set value(v: number) { this.counter = v; this.walking = v; }
  begin(): void { this.walking = this.counter; }
  /** Swaps the encoder (a machine change) and keeps the counter, which is the state the ROM diffs. */
  use(encode: (counter: number) => number): void { this.encode = encode; }
  /** One clock: advances the counter and returns the level word for it. */
  tick(): number { this.walking = (this.walking + 1) % this.mod; return this.encode(this.walking); }
  end(): void { this.counter = this.walking; }
}

/** The encoder for this machine's wire, as `syncLevelsFor` returns it, behind a CounterSync. */
export function counterSyncFor(machine: SmsSyncMachine): CounterSync {
  return new CounterSync(syncLevelsFor(machine));
}
```

and `smsSyncNode.ts`'s `create` uses it (its `process` keeps its shape):

```ts
import { SMS_SYNC_PPQN, counterSyncFor, syncLevelsFor } from "./smsSync";

  create: ({ settings: initial }) => {
    let machine = initial.machine;
    const clock = new TickClock();
    // The CounterSync lives in the closure and is NEVER rebuilt on a settings change: its counter is the state the ROM
    // reads deltas of, and a rebuilt machine would restart it at 0 and hand the cart a spurious delta (the existing
    // node keeps it, and smsSync.test.ts's 24-tick case only agreed because 24 is 0 mod 4).
    const sync = counterSyncFor(machine);
    let block: Block | null = null;
    const level = new Uint8Array(1);
    const tick = (_t: number, off: number) => {
      level[0] = sync.tick();
      block!.send("bytes", off, level);
    };
    return {
      process(b) {
        block = b;
        clock.begin(b);
        if (!clock.block.transport) return;   // a stopped transport sends nothing and keeps the counter
        sync.use(syncLevelsFor(machine));     // a machine change encodes the next step on the new pins
        sync.begin();
        clock.walk(SMS_SYNC_PPQN, tick);
        sync.end();
      },
      onSettings(next) { machine = next.machine; },
    };
  },
```

Keep the `CounterSync` instance in the closure and do not rebuild it per block (its counter is the state): it is
created once in `create`, its `use(syncLevelsFor(machine))` swaps only the ENCODER and keeps the counter, and
`onSettings` therefore only records the new machine. The import of `SMS_SYNC_IDLE_LEVELS` is not needed by the node;
drop it from the import line. The node's behaviour must be identical to before, which
`packages/smsggdj/ts/test/smsSync.test.ts` holds without an edit:

```bash
pnpm test smsggdj
```

Expected: exit 0 with no edit to `packages/smsggdj/ts/test/smsSync.test.ts`. If a case fails, the extraction changed
behaviour (the most likely spot: `sync.value` across a stopped block) and the fix is in the new class, not the test.

Now `packages/genmddj/ts/mdSync.ts`:

```ts
// genmddj's IN24 level word (design section 4): the same 2-bit counter smsggdj uses, on this console's pins.
//
// genmddj reads it with `move.b $A10005,d0; lsr.b #5,d0; andi.b #3,d0` (src/main.asm:9438): bit 5 is TR, bit 6 is TH,
// and the bit is HIGH when the counter's bit is set. As on every controller port, the ROM reads what the pads drive
// plus its own output latch, so a level word only decides the pins the ROM leaves as inputs ($A1000B's direction).
//
// The other lines idle high, which is what a disconnected pad leaves and what the ROM's arming latches as its base.
export const GENMDDJ_SYNC_COUNTER_MOD = 4;
export const GENMDDJ_SYNC_IDLE_LEVELS = 0x60;
export const GENMDDJ_SYNC_PPQN = 24;
/** At most 3 clocks survive one of the ROM's frame polls: a 4th aliases to 0 (sync_in_delta's `(read - last) & 3`). */
export const GENMDDJ_SYNC_MAX_CLOCKS_PER_POLL = 3;

const TR_BIT = 0x20; // $A10005 bit 5, the pad's B / the sync's counter bit 0
const TH_BIT = 0x40; // $A10005 bit 6, the pad's C / the sync's counter bit 1

/** The port-2 level word for 2-bit counter value `counter`. */
export function mdSyncLevels(counter: number): number {
  const c = ((counter % GENMDDJ_SYNC_COUNTER_MOD) + GENMDDJ_SYNC_COUNTER_MOD) % GENMDDJ_SYNC_COUNTER_MOD;
  let levels = 0;
  if (c & 1) levels |= TR_BIT;
  if (c & 2) levels |= TH_BIT;
  return levels;
}
```

- [ ] **Step 5: Run**

```bash
pnpm test genmddj/mdSync && pnpm test smsggdj && pnpm check:boundaries
```

Expected: all exit 0, and `packages/smsggdj/ts/test/smsSync.test.ts` is unchanged in the commit.

- [ ] **Step 6: Commit**

```bash
git add packages/smsggdj packages/genmddj
git commit -m "refactor(smsggdj): the IN24 counter machine takes its level encoder

genmddj's wire is the same 2-bit counter on this console's pins, so the state
machine is shared and the encoder is the parameter. genmddj's own words are
in mdSync.ts; smsggdj's tests are unchanged."
```

---

### Task 20: The `genmddj-sync` role and the `rp.genmddj.sync` node

**Files:**
- Create: `packages/genmddj/ts/genmddjSyncNode.ts`, `packages/genmddj/ts/protocol.ts`, `packages/genmddj/ts/app.ts`,
  `packages/genmddj/ts/test/genmddjSyncNode.test.ts`
- Modify: `packages/genmddj/package.json` (app, protocol), `packages/retroplug/test/_featureRoles.ts`,
  `packages/retroplug/test/system-settings/registry.test.ts` (the `genmddj-sync` kind and the genmddj case, on top of
  the `blastem` kind the descriptor's task added), `packages/retroplug/test/systems/store-graph.test.ts` (its Mega Drive
  `be.seed` fixture; `NATIVE_TYPES` gained `rp.blastem.md` in the node's own task),
  `packages/native/test/engine-graph/ScriptNodes.test.cpp` (the bundle's node list and the span labels)

**Interfaces:**
- Consumes: `@retroplug/smsggdj/smsSync`'s `CounterSync` (Task 19), `mdSyncLevels`, the `blastem` role (Task 15).
- Produces: `GENMDDJ_SYNC_ROLE` (kind `genmddj-sync`, category `feature`, schema `z.object({})`), the node type
  `rp.genmddj.sync` (span label **24**; 16 to 23 are taken), and its `bytes` output of level words.

- [ ] **Step 1: Write the failing node test**

`packages/genmddj/ts/test/genmddjSyncNode.test.ts` (the mock tier's `NodeRig`, as `smsggdj/ts/test/smsSync.test.ts`
drives its node: `new NodeRig(node, settings)`, `rig.run({ frames, playing, ppq, tempo })`, and each `Sent` has
`data`, `frame` and `flags`):

```ts
// rp.genmddj.sync: the DAW transport at 24 PPQN, one level word per clock on the `bytes` output, at the clock's own
// frame, with the counter surviving a stopped transport. Both carts are active-high with idle = counter 3; the pins are
// what differ (genmddj's TR and TH are bits 5 and 6 of $A10005, smsggdj's are other bits of another port), so these
// cases also say the shared machine is fed the right encoder.
import { test, expect } from "@retroplug/testing";
import { NodeRig, type Sent } from "@retroplug/testing/rig";
import { GENMDDJ_SYNC_NODE_TYPE, genmddjSyncNode } from "../genmddjSyncNode";
import { mdSyncLevels, GENMDDJ_SYNC_PPQN } from "../mdSync";

const rig = () => new NodeRig(genmddjSyncNode, {});
// 22050 frames at 44100 / 120 bpm is exactly one beat: 24 ticks at 24 PPQN.
const beat = (over: Partial<Parameters<NodeRig["run"]>[0]> = {}) => ({ frames: 22050, ...over });
const levels = (sent: Sent[]): number[] => sent.map((s) => s.data[0]);

test("one output, the system's core-bytes input, and a descriptor with no fields", () => {
  expect(rig().ports()).toEqual(["out bytes bytes"]);
  expect(genmddjSyncNode.descriptor().id).toBe(GENMDDJ_SYNC_NODE_TYPE);
  expect(genmddjSyncNode.parseSettings({})).toEqual({});
});

test("a stopped transport sends nothing", () => {
  expect(rig().run(beat())).toEqual([]);
});

test("a playing transport sends one level per 24-PPQN clock, at the clock's own frame", () => {
  const out = rig().run(beat({ playing: true }));
  expect(out.length).toBe(GENMDDJ_SYNC_PPQN);
  for (const s of out) expect(s.data.length).toBe(1);
  expect(out.map((s) => s.frame).filter((f, i, all) => i > 0 && f <= all[i - 1])).toEqual([]);  // strictly ascending
  expect(out.filter((s) => s.frame < 0 || s.frame >= 22050)).toEqual([]);                        // inside the block
});

test("the levels are genmddj's words, not smsggdj's", () => {
  const want: number[] = [];
  for (let i = 1; i <= GENMDDJ_SYNC_PPQN; i++) want.push(mdSyncLevels(i));
  expect(levels(rig().run(beat({ playing: true })))).toEqual(want);
});

test("the counter continues across a stopped transport", () => {
  const r = rig();
  const first = levels(r.run(beat({ playing: true })));
  expect(r.run(beat({ playing: false, ppq: 1 }))).toEqual([]);  // nothing at all, not a stop message
  const next = levels(r.run(beat({ playing: true, ppq: 2 })));
  expect(next[0]).toBe(mdSyncLevels(GENMDDJ_SYNC_PPQN + 1));
});
```

Each case's exact expected sequence follows from `mdSyncLevels` and the counter's start at 0 (`CounterSync`'s first
tick is counter 1). If a case's frame assertions fail, the rig's `BlockSpec` field for the transport is not
`playing` (read `@retroplug/testing/rig`'s type and use its name).
- [ ] **Step 2: Run it**

```bash
pnpm test genmddj/genmddjSyncNode
```

Expected: FAIL (module not found).

- [ ] **Step 3: Implement the node, protocol module and role**

`packages/genmddj/ts/genmddjSyncNode.ts`:

```ts
// genmddj's host sync (`genmddj-sync`'s audio-thread work): while the DAW plays, a 2-bit counter advanced one step a
// 24-PPQN tick, each step a LEVEL WORD at the tick's own frame, which the system holds on controller port 2's TR and TH
// until the next (mdSync.ts). The state machine is smsggdj's, with genmddj's encoder: no arm, no locate, no start or
// stop message, because the ROM reads deltas and latches the lines itself when it arms.
//
// A block allocates nothing (the engine's [alloc] cases hold it).
import { bytes, defineNode, type Block } from "@retroplug/core/defineNode";
import { s, type Infer } from "@retroplug/core/settingsSchema";
import { TickClock } from "@retroplug/core/clock";
import { CORE_BYTES } from "@retroplug/core/ports";
import { CounterSync } from "@retroplug/smsggdj/smsSync";
import { GENMDDJ_SYNC_COUNTER_MOD, GENMDDJ_SYNC_PPQN, mdSyncLevels } from "./mdSync";

export const GENMDDJ_SYNC_NODE_TYPE = "rp.genmddj.sync";

const settings = s.object({});

export type GenmddjSyncSettings = Infer<typeof settings>;

export const genmddjSyncNode = defineNode<GenmddjSyncSettings>({
  id: GENMDDJ_SYNC_NODE_TYPE,
  name: "genmddj Host Sync",
  category: "protocol",
  settings,
  reshapes: true,
  ports: { bytes: bytes.out(CORE_BYTES) },
  create: () => {
    const clock = new TickClock();
    const sync = new CounterSync(mdSyncLevels, GENMDDJ_SYNC_COUNTER_MOD);
    let block: Block | null = null;
    const level = new Uint8Array(1);
    const tick = (_t: number, off: number) => {
      level[0] = sync.tick();
      block!.send("bytes", off, level);
    };
    return {
      process(b) {
        block = b;
        clock.begin(b);
        if (!clock.block.transport) return;   // a stopped transport sends nothing and keeps the counter
        sync.begin();
        clock.walk(GENMDDJ_SYNC_PPQN, tick);
        sync.end();
      },
      onSettings() {},
    };
  },
});
```

`packages/genmddj/ts/protocol.ts`:

```ts
// genmddj's protocol contribution: the host-sync node, span label 24 (16 to 23 are taken in the desktop's list).
import type { DefinedNode } from "@retroplug/core/defineNode";
import { genmddjSyncNode } from "./genmddjSyncNode";

const protocolNodes: readonly DefinedNode<any>[] = [genmddjSyncNode];
export default protocolNodes;
```

`packages/genmddj/ts/app.ts`:

```ts
// genmddj's app contribution: the `genmddj-sync` role whose work is rp.genmddj.sync, and the ROM provider that attaches
// it by the cart's identity. The role's schema has no fields yet (spec 2 adds the tracker integration); an empty object
// is additive, so a project written before it parses as {}.
import { definePackage } from "@retroplug/core/definePackage";
import type { RoleType } from "@retroplug/core/systemRoles";
import { z } from "zod";
import { GENMDDJ_ROM_PROVIDER } from "./romProvider";

export const GENMDDJ_SYNC_ROLE: RoleType = {
  kind: "genmddj-sync",
  category: "feature",
  schema: z.object({}),
};

export default definePackage({ roles: [GENMDDJ_SYNC_ROLE], romProviders: [GENMDDJ_ROM_PROVIDER] });
```

and the manifest's `retroplug` member gains:

```json
    "app": "ts/app.ts",
    "protocol": {
      "module": "ts/protocol.ts",
      "nodes": [{ "type": "rp.genmddj.sync", "label": 24, "midiIn": null, "writes": ["bytes"] }],
      "roles": { "genmddj-sync": "rp.genmddj.sync" }
    }
```

- [ ] **Step 4: The lists that pin every role, node and label**

Each edit is in this task because this task is what breaks it; the two that break EARLIER were made in their own tasks
(`system-settings/registry.test.ts`'s `"blastem"` kind in the descriptor's task, `store-graph.test.ts`'s `NATIVE_TYPES`
in the node's task), and an implementer reads only its own task, so neither is a to-do here.

- `packages/retroplug/test/system-settings/registry.test.ts:75` pins every role KIND: `"genmddj-sync"` joins the list,
  plus a case that a genmddj ROM on `md` carries `["blastem", "genmddj-sync"]` (built from
  `@retroplug/genmddj/testFixtures`'s `genmddjHeader`, as the smsggdj cases use their own fixture).
- `packages/retroplug/test/systems/store-graph.test.ts`'s fixture table needs
  `be.seed("/roms/genmddj.bin", genmddjHeader())`, because this task's role is the first that makes a store case boot a
  Mega Drive ROM (the `genmddjHeader` import comes from `@retroplug/genmddj/testFixtures`).
- `packages/native/test/engine-graph/ScriptNodes.test.cpp:2563-2565` and `:2852-2856` pin the script bundle's node
  types and the span labels 16..23: `rp.genmddj.sync` at label 24 appends `"rp.genmddj.sync"` to both lists and adds
  `CHECK(names[24] == "rp.genmddj.sync")`. That is this task's edit, and `pnpm test:plugin` fails until it is made.
- `spec/08-profiling.md:268` and `:338` say "16 to 23" where they describe the label range: the docs task updates both
  to "16 to 24".

`packages/retroplug/test/_featureRoles.ts`: add `"genmddj-sync"` to the app-order list, after `"sms-sync"`.

- [ ] **Step 5: Run**

```bash
pnpm test genmddj && pnpm test system-settings && pnpm test systems/store-graph && pnpm typecheck && pnpm check:boundaries
```

Expected: all exit 0. The protocol-node port test
(`test/graph/protocol-nodes.test.ts`) holds `writes: ["bytes"]` to a system input of that name: `rp.blastem.md` has
one (Task 14), so it passes without an edit; if it does not, the MD node's input is missing.

- [ ] **Step 6: Commit**

```bash
git add packages/genmddj packages/retroplug/test/_featureRoles.ts \
        packages/retroplug/test/system-settings/registry.test.ts packages/retroplug/test/systems/store-graph.test.ts \
        packages/native/test/engine-graph/ScriptNodes.test.cpp
git commit -m "feat(genmddj): the genmddj-sync role and rp.genmddj.sync, span label 24"
```

---

### Task 21: `tools/fetch-genmddj-fixture.sh`

**Files:**
- Create: `tools/fetch-genmddj-fixture.sh` (executable)

**Interfaces:**
- Produces: `$RETROPLUG_RESOURCES_DIR/roms/genmddj_v0_27.bin` (the name from the checkout's own `ver_str`), which
  Task 22's test looks for.

- [ ] **Step 1: The script**

```bash
#!/usr/bin/env bash
# Builds genmddj's ROM from a checkout and copies it into the resources tree, where the fixture-gated cases look for it
# (RETROPLUG_RESOURCES_DIR). Nothing vendors the ROM and nothing in CI builds it (spec 1's design section 5): the
# resources tree is a sibling checkout, and a case that needs the ROM skips, counted, when it is absent.
#
#   tools/fetch-genmddj-fixture.sh [checkout]        (default /workspaces/genmddj)
#
# Needs vasmm68k_mot (vasm), wla-z80 and wlalink (wla-dx) on PATH, and python3 for the cart's own generators. The cart
# builds its own ROM with `make all`: src/main.asm plus the Z80 driver, the font and wave banks and the sample table.
set -euo pipefail

SRC=${1:-/workspaces/genmddj}
DEST=${RETROPLUG_RESOURCES_DIR:-/workspaces/resources}/roms

for tool in make python3 vasmm68k_mot wla-z80 wlalink; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "error: $tool is not on PATH (vasm and wla-dx are the cart's assemblers)" >&2
		exit 2
	fi
done
if [ ! -f "$SRC/src/main.asm" ]; then
	echo "error: no genmddj checkout at $SRC" >&2
	exit 2
fi

cd "$SRC"
if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
	echo "warning: $SRC is dirty; the ROM is stamped with a '+' and its name is not reproducible" >&2
fi
make all

# The version, read from the ROM the cart just built (the identity the app itself matches on) rather than re-parsed out
# of src/main.asm: the Makefile already stamps the string, and a second derivation is a second thing to keep right.
VER=$(grep -boa 'V[0-9][0-9]*\.[0-9][0-9]*' build/genmddj.bin | head -1 | sed 's/^[0-9]*://' |
	tr 'A-Z' 'a-z' | tr '.' '_')
if [ -z "$VER" ]; then
	echo "error: no version string in build/genmddj.bin" >&2
	exit 1
fi
mkdir -p "$DEST"
OUT="$DEST/genmddj_${VER}.bin"
cp build/genmddj.bin "$OUT"
echo "wrote $OUT ($(wc -c <"$OUT") bytes, $(sha256sum "$OUT" | cut -d' ' -f1))"
echo "set RETROPLUG_RESOURCES_DIR to the tree this is in, or the genmddj cases will skip"
```

- [ ] **Step 2: Run it (a checkout is at /workspaces/genmddj)**

```bash
chmod +x tools/fetch-genmddj-fixture.sh
tools/fetch-genmddj-fixture.sh
```

Expected: `wrote .../roms/genmddj_v0_27.bin (...) bytes, <sha256>` (`v` plus the ROM's own `V0.27`, so the name is the
cart's identity and not the Makefile's variable). If the tools are missing, the script says which and exits 2: that is
the CI picture, and the reason the case skips. `make all` builds four ROMs and needs `wla-z80`/`wlalink` only for the
Z80 driver inside the cart; `src/main.asm` alone is the 68k half.

- [ ] **Step 3: Commit**

```bash
git add tools/fetch-genmddj-fixture.sh
git commit -m "tools: fetch-genmddj-fixture.sh builds the cart's ROM into the resources tree"
```

---

### Task 22: The genmddj end-to-end sync proof (fixture-gated)

**Files:**
- Create: `packages/retroplug/test-native/genmddj-sync.test.ts`
- Modify: `packages/retroplug/scripts/skip-baseline.json` (the new file's skips)

**Interfaces:**
- Consumes: everything: the console, the port device, the role, the node, and `readMemory`.
- Produces: the one case that proves a real genmddj ROM is clocked by the DAW through port 2.

- [ ] **Step 1: The addresses, from the cart's own source**

genmddj's sync variables are fixed work-RAM addresses (its `src/main.asm` equs), so the case reads them by offset from
`$FF0000`:

| Address | Equ | Meaning |
|---|---|---|
| `$FFD425` | `sync_last` | the counter value the ROM read last frame |
| `$FFD426` | `sync_wait` | 1 while armed and waiting for the first clock, 0 once one arrived |
| `$FFE20A` | `playing` | 0 stopped, 1 playing |
| `$FFE20E` | `play_row` | the playhead's row |
| `$FFE3E5` | `opt_sync` | the SYNC option: 5 is IN24 |
| `$FFE3E7` | `proj_tmpo` | the project tempo, BPM |

`getMemory(Ram)` is in 68k address order (Task 12), so `$FFD425` is offset `0xD425` of the region.

- [ ] **Step 2: The case**

```ts
// genmddj, the real ROM, clocked by the DAW through controller port 2 (design section 5). Fixture-gated: the ROM is not
// vendored (tools/fetch-genmddj-fixture.sh builds it from a checkout), so CI skips this case and the skip baseline
// counts it.
//
// The cart's SYNC option is set by writing its OPTIONS byte in work RAM rather than by seeding a battery: genmddj's
// save format is spec 2's (the design defers its SRAM codec), so a battery the cart would accept cannot be written
// here. The option is read by its engine every tick (opt_sync), so a poke is what a user's menu pick ends up as.
import { test, expect, skip } from "@retroplug/testing";

declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;  // the temp dir run-native-tests.mjs gives each file (the case's own ROM copy)

const GENMDDJ_ROM = __RESOURCES_DIR__ + "/roms/genmddj_v0_27.bin";
const SYNC_LAST = 0xd425;
const SYNC_WAIT = 0xd426;
const PLAY_ROW = 0xe20e;
const OPT_SYNC = 0xe3e5;
const PROJ_TEMPO = 0xe3e7;
const SYNC_IN24 = 5;
const BUTTON_START = 7;

test("the DAW clocks genmddj through port 2", async () => {
  // ... the session below
});
```

The session, in the shape `test-native/app-play-nes.test.ts` builds one (`createRealBackend`, `ProjectStore`,
`createAudioDriver`):

```ts
  const be = createRealBackend();
  if (!be.fileExists(GENMDDJ_ROM)) skip(`genmddj-sync: no ROM at ${GENMDDJ_ROM}`);
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver();
  // The ROM is copied into the case's own directory first, so its .sav and its battery slot are this case's. (There is no
  // be.copyFile; read and write, as Rig.rom does.)
  const local = __CONFIG_DIR__ + "/genmddj_v0_27.bin";
  expect(be.writeFile(local, be.readFile(GENMDDJ_ROM)!)).toBeTruthy();
  const id = (project.systems.loadRom(local) as { system: number }).system;
  expect(project.systems.view()[0].platform).toBe("md");
  expect(project.systems.view()[0].roles.map((r) => r.kind)).toContain("genmddj-sync");
  audio.renderAudio(2000);  // boot: the cart clears RAM, sets up the VDP and draws its UI
  const ram = () => project.systems.readRam(id)!;
  // IN24 and a tempo whose 24-PPQN clocks are inside the cart's 3-per-frame cap. The option is read by the engine
  // every tick, so a poke is what the OPTIONS menu writes; genmddj's save codec is spec 2's, so no battery carries it.
  // The store's offsets ARE the 68k addresses minus $FF0000, which is what these constants already are: the first draft
  // subtracted 0x10000 again and writeRam returned false.
  expect(project.systems.writeRam(id, OPT_SYNC, Uint8Array.of(SYNC_IN24))).toBe(true);
  expect(project.systems.writeRam(id, PROJ_TEMPO, Uint8Array.of(120))).toBe(true);
  // Play: the pad's Start, so the cart's own play-start runs and arms the sync (a RAM poke of `playing` would skip the
  // arm, which is the state this case is about).
  audio.pressButton(id, BUTTON_START, true);
  audio.renderAudio(100);
  audio.pressButton(id, BUTTON_START, false);
  audio.renderAudio(500);
  expect(ram()[SYNC_WAIT]).toBe(1);         // armed, showing WAIT: no clock has arrived yet
  audio.setTransport(true);
  audio.setBpm(120);
  audio.renderAudio(1000);
  // The clocks are derived from the TRANSPORT, not from the ROM's counter: 120 BPM at 24 PPQN for one second of audio is
  // 48 clocks (bpm / 60 * 24 * seconds). The ROM counts `(read - last) & 3`, and 48 is 0 mod 4, so its counter value
  // cannot carry the count at all; a residue assertion would be about 0 and would prove nothing. What the case can
  // assert is the ROW the clocks advanced to, to within a row: IN24's head start plays row 0 on the first clock,
  // then one row per 6 clocks (groove 6), so 48 clocks land on row 1 + floor(47 / 6) = 8, plus or minus one for the
  // frames at the transport's edges.
  expect(ram()[SYNC_WAIT]).toBe(0);
  const renderedSeconds = 1000 / 1000;  // renderAudio takes milliseconds
  const ticks = Math.round((120 / 60) * 24 * renderedSeconds);  // 48
  const row = ram()[PLAY_ROW];
  expect(row).toBeGreaterThan(0);  // the playhead moved: the clocks advanced the song
  expect(Math.abs(row - (1 + Math.floor((ticks - 1) / 6)))).toBeLessThanOrEqual(1);
  audio.setTransport(false);
  const frozenLast = ram()[SYNC_LAST];
  const frozenRow = ram()[PLAY_ROW];
  audio.renderAudio(500);
  expect(ram()[SYNC_LAST]).toBe(frozenLast);  // stopped: no clock was counted, so the counter froze
  expect(ram()[PLAY_ROW]).toBe(frozenRow);    // and so did the playhead
  project.systems.removeSystem(id);
```

`SystemsStore.readRam(id)`/`writeRam(id, offset, bytes)` are the store's own, and their offsets are the 68k addresses
minus `$FF0000` (Task 12), which is why `$FFD425` is `0xD425` and the constants above are used as they stand. The
counter's residue is deliberately NOT an assertion: `(sync_last - armed) & 3` is the clock count mod 4, and 48 clocks
is 0, so a residue check would compare 0 with 0.

- [ ] **Step 3: Run it, with and without the ROM**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native genmddj-sync
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm test:native genmddj-sync
```

Expected: with the ROM (built by Task 21), every assertion passes. Without it, the one case reports
`# SKIP` and the file exits 0.

- [ ] **Step 4: Add the skip to the baseline**

```bash
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm test:native --update-skip-baseline
git diff packages/retroplug/scripts/skip-baseline.json
```

Expected: one new entry, `"genmddj-sync": { "skipped": 1, "total": 1 }`. The baseline MUST be recorded with the
fixtures absent (`RETROPLUG_RESOURCES_DIR=/nonexistent`, which is CI's worst case): recording it on a box that has the
ROM writes the *passing* reading, and CI then fails against a baseline it can never meet. Task 23's baseline is the same
rule for the bench. If the run reports a *failure* without the ROM, the
case skipped from the wrong place: `skip()` throws, so no `catch` may stand between it and the harness.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/test-native/genmddj-sync.test.ts packages/retroplug/scripts/skip-baseline.json
git commit -m "test(genmddj): the DAW clocks the real cart through port 2, end to end"
```

---

### Task 23: The profile bench

**Files:**
- Create: `packages/retroplug/test-native/md-bench.test.ts`
- Modify: `packages/retroplug/scripts/skip-baseline.json`, `spec/08-profiling.md` (a prose bullet beside `nes-bench`'s:
  that file has no bench table, its benches are bullets in the metrics section)

**Interfaces:**
- Consumes: the console and the port device.
- Produces: an xRT figure per case, printed, in a profile host only.

- [ ] **Step 1: The bench**

```ts
// A Mega Drive's speed on this host (design section 5, "Performance"): the synthetic image and, from the fixture tree,
// the genmddj ROM, rendered and timed through the real engine at 48 kHz. Profile hosts only (the counters are
// RETROPLUG_PROFILE's), so `pnpm test:native` sweeps it as a counted skip and `pnpm profile md-bench` runs it.
import { test, expect, skip } from "@retroplug/testing";

test("a Mega Drive's xRT", () => {
  // The same shape as nes-bench.test.ts: skip unless the profile host's counters are enabled, warm the system, then
  // time N seconds of emulated audio and print `md: <xRT>x realtime (synthetic)`.
});
```

Fill it from `packages/retroplug/test-native/nes-bench.test.ts` (its guard, its warm-up and its print format), with
the MD's image from `@retroplug/blastem/testFixtures` and a second, fixture-gated window on
`__RESOURCES_DIR__ + "/roms/genmddj_v0_27.bin"` with the `blastem` and `genmddj-sync` roles and a playing transport.

- [ ] **Step 2: Run it both ways**

```bash
pnpm test:native md-bench                        # skip, counted
pnpm profile md-bench                            # the numbers, in a build-prof/ host
```

Expected: the first exits 0 with a skip; the second prints one xRT line per window. The root `package.json`'s
`"profile"` script is already `tools/run-profile.sh stats`, so `run-profile.sh`'s first argument (its MODE, which
defaults to `stats`) is supplied by the script and everything typed after `pnpm profile` is its second argument, the
SLUG: `pnpm profile md-bench` is `run-profile.sh stats md-bench`. A word typed before the slug is read AS the slug, so
only the file's name goes there. Record the synthetic figure in the docs task's spec/13 section (one number, with the
host it was measured on).

- [ ] **Step 3: Baseline and commit**

The bench's skip is this file's, so the baseline is recorded WITH THE FIXTURES ABSENT, exactly as the end-to-end case's
is, and in the same command (one recording covers every file's skips, so run it once, from either task):

```bash
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm test:native --update-skip-baseline
git diff packages/retroplug/scripts/skip-baseline.json
git add packages/retroplug/test-native/md-bench.test.ts packages/retroplug/scripts/skip-baseline.json
git commit -m "test(blastem): a profile-host bench for a Mega Drive's speed"
```

Expected: the bench's entry appears beside `genmddj-sync`'s if the end-to-end case was committed first, and the diff
shows no reading taken on a box that owns the fixtures. A second `--update-skip-baseline` without
`RETROPLUG_RESOURCES_DIR=/nonexistent` is what undoes that, so there is only one here and it carries the variable.

---

### Task 24: The docs, and the final verification

**Files:**
- Modify: `spec/00-overview.md` (the package table), `spec/06-build-test.md` (Python, the generated cores, the fixture
  script, `test:plugin`'s list, the `.bin` rule), `spec/13-subsystems.md` (a Mega Drive section), `AGENTS.md` (the
  existing vendored-core rule, widened), `spec/08-profiling.md` (the label range and the bench bullet),
  `spec/07-remaining-work.md` (four entries)

**Interfaces:**
- Produces: the documentation the design's section 6 lists, in ONE docs commit at the end. The design asks for each doc
  beside its code, and this plan deliberately does not: the spec files describe the FINISHED shape (a spec/06 paragraph
  written in the build task would be rewritten by the audio, port and savestate tasks), and AGENTS.md's own cap (a new
  rule cuts or moves one) is a single edit that has to be made once. So no code task stages a spec file, and this task
  lands them all: the package table, spec/06's build notes and test list, spec/13's section, the label range, the
  AGENTS.md rule and spec/07's entries.

- [ ] **Step 1: spec/00's package table**

Add two rows beside the console and cart rows:

- `packages/blastem/` beside `packages/sameboy/` and `packages/mesen/`: "the Mega Drive, with **BlastEm 1.0** vendored
  as `deps/blastem` (our mirror's `retroplug` branch), the portable interpreter only: the 68k and Z80 cores generated by
  `cpu_dsl.py` at build time (Python 3), `BlastemMdSystem`, its port-2 host device, its blip_buf audio sink, the build
  function and `rp.blastem.md`. Its TS is the console descriptor and the `blastem` core role."
- `packages/genmddj/` in the carts row (beside `smsggdj`): "genmddj's identity, its IN24 clock levels, the
  `genmddj-sync` role, its ROM provider and `rp.genmddj.sync` (span label 24)."

Update the overview's first paragraph if it names the consoles (it lists "Game Boy (SameBoy) and NES/GBA (Mesen)"):
add the Mega Drive and its core.

- [ ] **Step 2: spec/06's build section**

In "The packages", the console-package paragraph gains BlastEm: its target is `retroplug-blastem` over the `blastem`
library, which `cmake/blastem.cmake` builds from `deps/blastem` with `cpu_dsl.py` (Python 3, found at configure) into
`build/blastem-gen`; the generated cores are derived artifacts and are never committed; the file set is the Mega Drive's
alone (no SDL, Nuklear, 32X, Sega CD, SMS, Coleco, LaserActive), and `BlastemShim.cpp` provides the functions the
remaining sources reference from branches a Mega Drive cannot reach. Add `retroplug-blastem-test` to the `test:plugin`
row's binary list and to the `test` script's list, and one line to "Hosts -> test tiers" naming the MD cases.

And the design's "`cmake/packages.cmake`'s boundary admits it only to `packages/blastem/native`" line, which the tree
does not have: `packages.cmake` treats a root target as vendored (there is no per-package allowlist, `packages.cmake:715,
750, 985`), so nothing enforces that sentence. Say what IS true instead: the `blastem` library is defined in
`cmake/blastem.cmake` at the root and linked by exactly one target, `retroplug-blastem`; the TS side is held by
`pnpm check:boundaries`, and the native side by review (a second `target_link_libraries(... blastem)` would be visible in
one file). Record the deviation from the design's sentence here, and in the commit body.

- [ ] **Step 3: spec/13's Mega Drive section**

A `### Mega Drive (BlastEm)` section under Trackers or a new Hardware subsection (whichever the file's shape puts it
in; it is a console, so beside the other consoles' notes):

- the clock model (`run_until(mclk)`, the 64-bit base, the sample mapping, the stop grid if Task 10 applied and its
  cost);
- the port-2 device: `IO_CUSTOM`, the `(mclk, level)` queue, and the `$A1000B` direction rule, with genmddj's pins;
- the audio path: `render_audio.c` not compiled, blip_buf per chip and side, the 3390 Hz one-pole and the chip gains at
  the native rates, the ring the block drains, and the measured xRT;
- the video path: the bordered frame cropped into `FrameBufferTriple`;
- the battery's odd-byte layout and the savestate's identifier;
- the two "not ours" notes: `deps/blastem` is a mirror with `RetroPlug:` markers, and the threaded renderer and the
  event log are compiled out.

- [ ] **Step 4: one AGENTS.md rule, folded into the existing one**

AGENTS.md's own rule is that a new rule CUTS or MOVES one (the file is capped), so BlastEm joins the vendored-core line
`deps/mesen` already has rather than adding a line beside it: rewrite that bullet to name both trees, the markers both
carry, and the mirror's push:

```
- **Every edit to the vendored `deps/mesen` or `deps/blastem` carries a `RetroPlug:` marker comment**, and a BlastEm
  edit is a commit on the mirror's `retroplug` branch (pushed to the local mirror, which only the user pushes on) with
  the pointer bumped beside it ([Build gotchas](spec/06-build-test.md#build-gotchas)).
```

and one line to Build gotchas in spec/06: the generated `m68k.c`/`z80.c` live in the build tree only, Python 3 is a
configure requirement, and the mirror's push is the one CI dependency this branch cannot satisfy locally.

- [ ] **Step 5: spec/07's four entries**

Add four checklist lines and their detail entries (the file's own rule: open work only, each line linking its
entry):

- `[ ] [genmddj's tracker integration and MIDI takeover (spec 2)](#genmddjs-tracker-integration-and-midi-takeover-spec-2)`
  with the detail: the SRAM codec, Songs and live load, and the 10-voice MIDI takeover over the emulated port-2 bridge,
  as `docs/megadrive-blastem-design.md`'s spec 2.
- `[ ] [Mega Drive audio fidelity (spec 3)](#mega-drive-audio-fidelity-spec-3)`: Nuked-OPN2 as an option, the
  YM2612/YM3438 models, the output filter's choice, per-channel stems, and the LFO question the spike found (N against
  N+1 steps).
- `[ ] [The A53's Mega Drive speed](#the-a53s-mega-drive-speed)`: the design's estimate is weak (about 1.2x realtime,
  0.7x with Nuked); the spike's arm64 builds and its unrun `device-bench.sh` are the start
  (`docs/megadrive-blastem-design.md` section 7).
- `[ ] [BlastEm's remaining process-exits](#blastems-remaining-process-exits)`: the `fatal_error` calls the savestate
  task does not convert, listed by file and line with the condition that makes each one reachable
  (`system.c:104, 219`, `genesis.c:2147, 3035, 3042, 3069, 3117`, `romdb.c`'s fifteen, `i2c.c`'s four, `tern.c:277`,
  the config/paths/gen/event_log ones). The task that converts the last of them deletes this entry, per this file's own
  rule; until then it is the record that a corrupt payload from a path nobody has taken can still exit the DAW.

And `spec/08-profiling.md:268` and `:338`: both say the span label range is "16 to 23", and `rp.genmddj.sync` is 24, so
both become "16 to 24". Its bench section is prose bullets (no table), so the bench's bullet goes beside `nes-bench`'s
with the synthetic figure the profile task measured and the host it was measured on.

- [ ] **Step 6: The documentation checks, then the whole suite**

```bash
pnpm check:docs
./build.sh
pnpm typecheck
pnpm test
pnpm test:plugin
pnpm test:native
pnpm test:ui
```

Expected: every command exits 0. `pnpm test:native` includes `parity/md` (Task 17) and the two new fixture-gated files'
skips; run `RETROPLUG_RESOURCES_DIR=<tree> RP_FAIL_ON_SKIP=1 pnpm test:native` on a box that owns the fixtures for the
stronger reading, and name the result in the commit message.

- [ ] **Step 7: Commit**

```bash
git add spec/00-overview.md spec/06-build-test.md spec/07-remaining-work.md spec/08-profiling.md \
        spec/13-subsystems.md AGENTS.md
git commit -F - <<'EOF'
docs(megadrive): the console, its device, its audio path, and the work spec 1 leaves

spec/00's package table, spec/06's build notes and test list, spec/13's
Mega Drive section, one AGENTS.md rule for the mirror, and spec/07's entries
for spec 2, spec 3, the A53 measurement and BlastEm's remaining process-exits.
EOF
```

- [ ] **Step 8: Read the design once more, against this plan**

Walk `docs/megadrive-blastem-design.md` section by section and check each sentence against a task: the packages,
products, clock, triad, block-size invariance, audio, video, input, battery and savestates, instances and threads, the
port-2 device, detection, the sync, the surface, persistence, testing (native, goldens, TS, end to end, performance),
build and platforms, error handling, and docs. Report (in the final summary, not in a file) every sentence no task
implements, with the reason: this plan's own review found the e2e test's battery route (Task 22 pokes the option in RAM
because the SRAM codec is spec 2's) and the design's "A retroplug-cli TS test" (the case lives in `test-native`, where
skips are counted, which is what the design's own parenthetical asks for).
