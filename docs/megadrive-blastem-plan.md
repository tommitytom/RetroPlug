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
- **Every edit under `deps/blastem` is a commit on the mirror's `retroplug` branch**, made inside `deps/blastem`:
  subject line, blank line, body, and a `RetroPlug:` marker comment at the edit site. The outer repo then gets a
  commit that moves the pointer, naming both. The mirror is `https://github.com/tommitytom/RetroPlug-BlastEm`; **only
  the user pushes**, so ask them to push `upstream` and `retroplug` after the first BlastEm commit (Task 1's last
  step) and again whenever a CI job has to fetch a newer pointer. Until a push, CI cannot resolve `deps/blastem` and
  that is expected; it is the one red CI cause this plan tolerates, and only until the push.
- Never bump `deps/mesen` or `deps/sameboy`: both pointers are managed deliberately. `deps/sameboy` shows as modified
  after a configure (its patches are applied); that is expected, and it is never added, reset or stashed.
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

## Global constraints

Copied from the design (section 2, 3, 5); every task's requirements include them.

- **Clock.** Each instance keeps a 64-bit absolute master-clock count. Host sample `n` at rate `R` is master clock
  `floor(n * MCLK / R)`, in exact integer arithmetic, and a sample-rate change re-anchors at the current sample.
  `MCLK` is the *core's* `gen->master_clock` after boot: NTSC 53,693,175 and PAL 53,203,395 (`genesis.c:26-27`). The
  design's PAL figure (53,203,424) is the hardware nominal; the tree's constant is what the emulated chips actually
  run at, so the tree wins and the mapping reads the field, never a constant.
- **Sample rates at the chips.** The YM2612 emits one stereo sample per 1008 master clocks (53,267 Hz NTSC); the PSG
  one per 240 (223,722 Hz). BlastEm's 3390 Hz one-pole low-pass and its YM/PSG gains (0 dB each by default) apply at
  those native rates, before resampling.
- **Video.** 320 or 256 pixels wide by 224 lines (NTSC) or 240 (PAL); BlastEm's pixel format is `0xFFRRGGBB`, which is
  exactly `FrameBufferTriple`'s.
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
| `packages/blastem/native/test/BlastemBuild.test.cpp` | new | the link test: a context builds and frees |
| `packages/blastem/native/test/BlastemBoot.test.cpp` | new | boot, region, `run_until`, teardown |
| `packages/blastem/native/test/BlastemVideo.test.cpp` | new | geometry and a rendered frame |
| `packages/blastem/native/test/BlastemAudio.test.cpp` | new | non-silence, determinism, the low-pass and the mix |
| `packages/blastem/native/test/BlastemPort.test.cpp` | new | offsets, late levels, direction, block carry |
| `packages/blastem/native/test/BlastemInvariance.test.cpp` | new | 1 / 64 / 1024 / 4096 frames identical |
| `packages/blastem/native/test/BlastemInstances.test.cpp` | new | two instances, interleaved and on two threads |
| `packages/blastem/native/test/BlastemSaves.test.cpp` | new | state and battery round trips, the refusals |
| `packages/blastem/ts/app.ts`, `ts/enums.ts` | new | the `blastem` core role (the core's system role) |
| `packages/blastem/ts/consoles.ts` | new | the Mega Drive descriptor: detection, battery, region, gain |
| `packages/blastem/ts/fixtures.ts` | new | synthetic ROM images for the parity group, published as `./testFixtures` |
| `packages/blastem/ts/test/consoles.test.ts`, `fixtures.test.ts` | new | detection and image identity |
| `packages/retroplug/src/keyCodes.ts`, `bindingMap.ts` | changed | X / Y / Z / Mode on the pad's wire bytes 8 to 11 |
| `packages/retroplug/test/consoles/{cases.ts,recorded.ts}` | changed / re-recorded | the MD descriptor's knob rows |
| `packages/native/cmake/warnings.cmake` | changed | `blastem` and the console test binaries stay off `/W4 /WX` |
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
| `spec/00-overview.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/13-subsystems.md`, `AGENTS.md` | changed | docs |
| `.github/workflows/build.yml` | changed | Python 3 in the four jobs |

---

### Task 1: `deps/blastem`, the mirror's submodule

**Files:** create `.gitmodules` entry and the `deps/blastem` submodule (no source change yet).

**Interfaces:**
- Produces: `deps/blastem` checked out on branch `retroplug` at commit `6d42bbf` ("BlastEm 1.0.0 (hg
  2b2a182f4522), upstream snapshot"), with `.gitmodules` naming the GitHub mirror. Every later BlastEm task commits
  inside `deps/blastem` and then moves this pointer.

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

Then make the local clone agree (it keeps the local path in `.git/config`, which is the only place it may live):

```bash
git submodule sync deps/blastem
```

Expected: `synchronizing submodule url for 'deps/blastem'`.

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
`https://github.com/tommitytom/RetroPlug-BlastEm`, which is empty until the user pushes. **Ask the user, in this
step, to push `upstream` and `retroplug` from `.superpowers/megadrive/RetroPlug-BlastEm` to the GitHub remote.** Do not
push it yourself. Every later BlastEm commit needs the same ask before a CI run is expected to be green; this plan
marks those points.

---

### Task 2: BlastEm's Mega Drive file set builds and links

The riskiest unknown in the design (MSVC compiling the 56.7 MB generated `m68k.c`, and the trimmed file set's
missing pieces) is settled here, before anything builds on it. This task's link test is the whole deliverable: a
`genesis_context` is allocated from a synthetic image, reset, and freed.

**Files:**
- Create: `cmake/blastem.cmake`, `packages/blastem/package.json`, `packages/blastem/native/CMakeLists.txt`,
  `packages/blastem/native/src/system/blastem/BlastemCore.hpp`,
  `packages/blastem/native/src/system/blastem/BlastemShim.cpp`,
  `packages/blastem/native/src/system/blastem/BlastemNode.cpp`,
  `packages/blastem/native/test/BlastemBuild.test.cpp`
- Modify: `CMakeLists.txt` (include the new file), `package.json` (the `blastem` product entry),
  `packages/retroplug/package.json` and `packages/retroplug/scripts/run-plugin-tests.mjs` (`test:plugin`),
  `packages/native/cmake/warnings.cmake` (the `/W4 /WX` exemption), `.github/workflows/build.yml` (Python 3)
- Mirror (`deps/blastem`, branch `retroplug`): `vdp.c` (the threaded-renderer guard), `blip_buf.{c,h}` is **not** added
  yet (Task 7 adds it)

**Interfaces:**
- Consumes: nothing.
- Produces: the CMake target `blastem` (a static library, include root `deps/blastem` plus
  `${CMAKE_BINARY_DIR}/blastem-gen`), the target `retroplug-blastem` (the package's library) and
  `retroplug-blastem-test` (its Catch2 binary); `rp_blastem_package`, the package's `extern "C"` entry, which this
  task defines with no node types and Task 14 fills in; `rp::blastem::currentInstance()`, the thread-local instance
  pointer the frontend callbacks read (Task 6 onwards).

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
cd ../..
```

Expected: `git -C deps/blastem log --oneline -1` shows that subject.

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

# One generator run per core, in the build tree: cpu_dsl.py writes the core's .h beside its working directory and its
# .c to stdout (cpu_dsl.py's writeHeader + main), so both land in _BLASTEM_GEN. The dispatch is upstream's: a call per
# 68k instruction (the handlers are too large for one function on MSVC) and a goto per Z80 instruction
# (Z80_DISPATCH:=goto, Makefile:13, 93).
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
    "${_BLASTEM_GEN}/m68k.c" "${_BLASTEM_GEN}/z80.c" "${_BLASTEM_GEN}/rom.db.c")
set_source_files_properties("${_BLASTEM_GEN}/m68k.c" "${_BLASTEM_GEN}/z80.c" "${_BLASTEM_GEN}/rom.db.c"
                            PROPERTIES GENERATED TRUE)

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

- [ ] **Step 4: `BlastemShim.cpp`, the frontend BlastEm expects**

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

namespace rp::blastem {

// The instance the frontend callbacks belong to. BlastEm's callbacks take no instance argument, so a call that has
// none of its own (render_get_framebuffer, process_events, the globals below) reads this. One instance runs one call
// at a time, and the system sets this on entry to every one of its own methods.
thread_local void* currentInstance = nullptr;

}  // namespace rp::blastem

extern "C" {

// -- The globals libblastem.c would define ---------------------------------------------------------------
// The config tree, built once by the first system that activates and never written after (every instance reads the
// same settings: the region is a build parameter, not a config value, so no per-instance tree is needed).
tern_node* config = nullptr;
system_header* current_system = nullptr;
int headless = 1;  // 1 until BlastemVideo.cpp supplies a framebuffer: vdp.c then renders into its own line buffer
int exit_after = 0;
int z80_enabled = 1;
char* save_filename = nullptr;
uint8_t use_native_states = 1;

static const system_media kNoMedia = {};
const system_media* current_media(void) { return &kNoMedia; }

void process_events(void) {}

// BlastEm's ROM database is compiled in (rom.db.c), as libblastem.c does it: romdb.c is the only caller.
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

// -- The render seam's defaults (Tasks 6 and 7 replace the video and audio halves) -------------------------
pixel_t render_map_color(uint8_t r, uint8_t g, uint8_t b) { return 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | b; }
void render_set_video_standard(vid_std) {}
uint8_t render_create_window(char*, uint32_t, uint32_t, window_close_handler) { return 0; }
void render_destroy_window(uint8_t) {}
void render_framebuffer_updated(uint8_t, int) {}
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
sms_context* alloc_configure_sms(system_media*, uint32_t, uint8_t) { return nullptr; }
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

- [ ] **Step 5: `BlastemNode.cpp`, the package entry with no node types yet**

```cpp
// The blastem package's entry (packages/blastem/package.json names it): a product registers the node types this
// returns. Empty until the console's node lands, so the package can be built and linked while its core is proved.
#include <cstdint>

#include "graph/node.hpp"

namespace rp::host {
namespace {

const rp_node_type* const kTypes[] = {};
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.blastem","version":"1.0.0"})", 0, kTypes};

}  // namespace
}  // namespace rp::host

extern "C" const rp_package* rp_blastem_package(void) { return &rp::host::kPackage; }
```

- [ ] **Step 6: The package's `CMakeLists.txt` and manifest**

`packages/blastem/package.json` (the TS members arrive in Tasks 14 and 15):

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
            blastem)            # deps/blastem: the emulator
if(WIN32)
    # The core's interpreter is C99 with POSIX-flavoured assumptions in places, and the shim includes its headers; the
    # library is not ours to fix, so its warning policy is cmake/blastem.cmake's and warnings.cmake's.
    target_link_libraries(retroplug-blastem PUBLIC m)
endif()

retroplug_add_test_binary(
    NAME     retroplug-blastem-test
    SOURCES  test/BlastemBuild.test.cpp
    LIBS     retroplug-blastem)
```

The link test, `packages/blastem/native/test/BlastemBuild.test.cpp`:

```cpp
// The library's link proof: the whole Mega Drive file set, the generated 68k and Z80 interpreters, the shim and the
// ROM database compile and link, and a context builds from a real image and frees. Everything else in this package
// builds on this case, and it is what a new CI platform fails first.
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/blastem/BlastemCore.hpp"

namespace {

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
    std::vector<std::uint8_t> rom = minimalRom();
    genesis_context* gen = alloc_config_genesis(rom.data(), static_cast<std::uint32_t>(rom.size()), nullptr, 0, 0,
                                                REGION_U);
    REQUIRE(gen != nullptr);
    CHECK(gen->master_clock == 53693175u);
    CHECK(gen->work_ram != nullptr);
    CHECK(gen->vdp != nullptr);
    free_genesis(&gen->header);
}
```

`free_genesis` is static in `genesis.c`; use `gen->header.free_context(&gen->header)` instead. Fix that line in the
case: `gen->header.free_context(&gen->header);`.

- [ ] **Step 7: Wire it into the build**

In `CMakeLists.txt`, after `include(cmake/sameboy.cmake)`:

```cmake
# --- BlastEm: Sega Mega Drive emulator core (submodule at deps/blastem, our mirror's retroplug branch) ---
include(cmake/blastem.cmake)
```

In root `package.json`, add `"blastem"` to `retroplug.products.desktop` and `retroplug.products.plugin`, after
`"mesen"` in both (its dependencies precede it; `genmddj` follows it in Task 18):

```json
      "desktop": ["core", "audio", "audio-portaudio", "audio-sdl", "midi", "launchpad", "system", "sameboy", "dsp",
                  "mgb", "lsdj", "lsdj-launchpad", "graph-runtime", "n8-wire", "n8", "mesen", "blastem", "smsggdj",
                  "risa", "host-io", "native", "bliptoaster"],
      "plugin": ["core", "system", "sameboy", "dsp", "mgb", "lsdj", "graph-runtime", "n8-wire", "n8", "mesen",
                 "blastem", "smsggdj", "risa", "host-io", "native", "bliptoaster"]
```

In `packages/retroplug/package.json`'s `dependencies` add `"@retroplug/blastem": "workspace:*"`, then `pnpm install`.
In `packages/native/cmake/warnings.cmake`, add `blastem` and `retroplug-blastem-test` to the list of targets that stay
off `/W4 /WX` (find `_rp_werror_targets`; add both names there, with the one-line reason the surrounding comment
already uses: a vendored core and a console test binary).

- [ ] **Step 8: Build and see the link test pass**

```bash
pnpm install
cmake -S . -B build
cmake --build build --target retroplug-blastem-test -j$(nproc)
build/bin/retroplug-blastem-test
```

Expected: the configure reports the Python 3 it found and no `deps/blastem is empty`; the build compiles
`build/blastem-gen/m68k.c` (about 2 minutes at -O2, a 20 MB object) once, then links; the binary exits 0 with
`All tests passed (4 assertions in 1 test case)`. If the linker names a symbol the shim lacks, add it there and
rebuild: every name must come from the shim, never from linking an excluded system's source.

- [ ] **Step 9: Add the test binary to `test:plugin`, and Python 3 to CI**

In `packages/retroplug/package.json`, `test:plugin`'s `cmake-build.js` argument list gains `retroplug-blastem-test`
after `retroplug-mesen-test`; `packages/retroplug/scripts/run-plugin-tests.mjs`'s binary list gains
`["retroplug-blastem-test", ...]` in the same place. In `.github/workflows/build.yml`, add to all four jobs (after the
submodule step):

```yaml
      - name: Python 3 for BlastEm's core generator
        uses: actions/setup-python@v5
        with:
          python-version: "3.12"
```

- [ ] **Step 10: The Windows job's portability fixes (only when it fails)**

The CI job on `windows-2025-vs2026` is the one build this task cannot run locally. When it first reaches this commit,
its errors are BlastEm's GNU C meeting cl.exe, and **each fix is a mirror commit with a `RetroPlug:` marker**, not a
change to the outer tree: an include cl.exe needs (`<stdint.h>` where a source assumed a POSIX header), a function
MSVC has under another name, or a warning that is a build error there. Suppress a warning only by NUMBER
(`target_compile_options(blastem PRIVATE /wdNNNN)` in `cmake/blastem.cmake`, one line each with the reason), never by
lowering the warning level, and never for a target of ours. If the generated `m68k.c` is the only obstacle, clang-cl for
the `blastem` target alone is the design's fallback; do not reach for it until a mirror fix has failed. Commit the
mirror fixes as Tasks 1 and 2 do, and ask the user to push before re-running CI.

- [ ] **Step 11: Commit, then ask the user to push**

```bash
git add packages/blastem deps/blastem cmake/blastem.cmake CMakeLists.txt package.json \
        packages/retroplug/package.json packages/retroplug/scripts/run-plugin-tests.mjs \
        packages/native/cmake/warnings.cmake .github/workflows/build.yml pnpm-lock.yaml
git commit -F - <<'EOF'
build(blastem): BlastEm's Mega Drive file set compiles and links

The library is the portable interpreter (NEW_CORE) only, with cpu_dsl.py
generating the 68k and Z80 cores into the build tree and the ROM database
compiled in. The shim provides BlastEm's frontend globals and the functions
its uncompiled systems leave behind; a link test allocates, resets and frees
a context.

The submodule moves to the mirror's vdp commit, so CI needs the mirror pushed.
EOF
```

Expected: exit 0, and `deps/blastem`'s pointer in the commit is the new `vdp` commit. **Ask the user to push the
mirror's `retroplug` branch now**; until they do, CI's submodule step fails and no other CI result means anything.

### Task 3: The synthetic Mega Drive ROMs

Every native case in this plan boots an image built here: hand-assembled 68k bytes, no assembler, so CI needs none.

**Files:**
- Create: `packages/blastem/native/test/SyntheticRom.hpp`, `packages/blastem/native/test/SyntheticRom.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt` (the test source)

**Interfaces:**
- Consumes: nothing (the header is C++ only; the cases that boot an image are later tasks).
- Produces: `rp::blastem::test::Write` (`{std::uint32_t address; std::uint8_t value;}`),
  `rp::blastem::test::image(const std::vector<std::uint8_t>& program, const std::vector<Write>& writes = {})`,
  `fmPsgRom()`, `psgOnlyRom()`, `portPollRom()`, `portTimestampRom()`, `syncEdgeRom()`, all returning a 64 KiB
  `std::vector<std::uint8_t>`, and `fnv1a(const std::vector<std::uint8_t>&)`.

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
//   67dd                beq.s  (dd = signed displacement from the byte after it)
//   60FE                bra.s  -2 (a spin)
namespace rp::blastem::test {

struct Write {
    std::uint32_t address;
    std::uint8_t  value;
};

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
inline std::vector<std::uint8_t> portPollRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05, 0x13, 0xC0, 0x00, 0xFF, 0x00, 0x00});
}

// Polls port 2 in a tight loop and counts its own polls until TR/TH leave their idle level ($60), then stores the count
// at $FF0000 and the value read at $FF0002. The count is the poll loop's own clock, so a case reads WHEN a level
// arrived in units the ROM can see, not in host samples.
inline std::vector<std::uint8_t> portTimestampRom() {
    return image({0x72, 0x00,                                     // moveq #0,d1
                  0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                         // andi.b #$60,d0
                  0x0C, 0x00, 0x00, 0x60,                         // cmpi.b #$60,d0
                  0x67, 0xF2,                                     // beq.s  loop
                  0x33, 0xC1, 0x00, 0xFF, 0x00, 0x00,             // move.w d1,$FF0000
                  0x13, 0xC0, 0x00, 0xFF, 0x00, 0x02});           // move.b d0,$FF0002
}

// Holds PSG channel 0 at full volume, then silences it the first time TR/TH leave their idle level, and records that
// it happened at $FF0001. An audio edge a case can time: the level's sample is the sample the volume drops on.
inline std::vector<std::uint8_t> syncEdgeRom() {
    return image({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                 {{0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}

}  // namespace rp::blastem::test
```

`syncEdgeRom` needs byte code (it has a loop), so its body replaces the placeholder above with:

```cpp
inline std::vector<std::uint8_t> syncEdgeRom() {
    return image({0x10, 0x39, 0x00, 0xA1, 0x00, 0x05,             // move.b $A10005,d0   <- loop
                  0x02, 0x00, 0x00, 0x60,                         // andi.b #$60,d0
                  0x0C, 0x00, 0x00, 0x60,                         // cmpi.b #$60,d0
                  0x67, 0xF2,                                     // beq.s  loop
                  0x13, 0xFC, 0x00, 0x9F, 0x00, 0xC0, 0x00, 0x11, // move.b #$9F,$C00011 (channel 0 level 15)
                  0x13, 0xFC, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x01},// move.b #$01,$FF0001 (the flag)
                {{0xC00011, 0x8E}, {0xC00011, 0x01}, {0xC00011, 0x90}});
}
```

and `image`/`fnv1a` are defined once, at the top of the same header **above the ROMs** (a later-defined inline function
is not visible to an earlier one, so the order is load-bearing):

```cpp
inline void push32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

inline std::vector<std::uint8_t> image(const std::vector<std::uint8_t>& program,
                                       const std::vector<Write>& writes) {
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
```

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/SyntheticRom.test.cpp`:

```cpp
// The images are code, so this pins them: a hash per image (any byte that moves changes it), the header and vectors
// a Mega Drive needs, and the one property a later case depends on and a hash cannot state (the write table's order).
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"

TEST_CASE("the synthetic images are the bytes later cases boot", "[blastem][roms]") {
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::psgOnlyRom()) == 0x6397D2DFu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::fmPsgRom()) == 0x82A7496Fu);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portPollRom()) == 0xB7DE6771u);
    CHECK(rp::blastem::test::fnv1a(rp::blastem::test::portTimestampRom()) == 0x4B99B6BFu);
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
```

Add `test/SyntheticRom.test.cpp` to `retroplug_add_test_binary`'s `SOURCES`.

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[roms]"
```

Expected: both cases pass. If a hash differs, the generator's bytes moved: fix the generator, never the hash, unless
the change is the point of the edit.

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
- Consumes: `BlastemCore.hpp` (Task 2), `rp::blastem::test::*` (Task 3).
- Produces: `BlastemMdConfig` (`region`, `gainDb`, `romName`, `sram`, `savestate`), `enum class MdRegion : std::uint32_t
  {Auto, NtscUs, NtscJp, Pal}`, `rp::blastem::mdRegionFromConfig`, `rp::blastem::isMdRom(std::span<const
  std::uint8_t>)`, and `class BlastemMdSystem final : public SystemBase` with the constructor
  `BlastemMdSystem(SystemId, BlastemMdConfig, std::vector<std::uint8_t> romBytes)` and the seams
  `config()`, `masterClock()`, `activated()`, `coreFrameWidth()`, `coreFrameHeight()` (the last two from Task 6).

- [ ] **Step 1: The ROM gate and the config**

`BlastemRom.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rp::blastem {

// The Mega Drive header's magic. A Sega cart names its console in the first 16 bytes of its header at $100, and this
// is the whole test: a Master System `.bin` has no such string (its header is "TMR SEGA" at the end of a bank) and
// stays Mesen's, and a `.gen`/`.md` file that is not one is refused rather than booted as noise.
inline bool isMdRom(std::span<const std::uint8_t> rom) {
    static constexpr char kMagic[] = "SEGA";
    if (rom.size() < 0x104) return false;
    for (std::size_t i = 0; i < sizeof(kMagic) - 1; ++i) {
        if (rom[0x100 + i] != static_cast<std::uint8_t>(kMagic[i])) return false;
    }
    return true;
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

// Which machine BlastEm boots (genesis.c's set_region): the header's own region, or one forced. Construct-time: a
// region fixes the master clock (53,693,175 NTSC against 53,203,395 PAL), the video standard and the frame height
// (224 against 240), so a change is a rebuild, exactly as the NES's region is a reset.
enum class MdRegion : std::uint32_t { Auto = 0, NtscUs = 1, NtscJp = 2, Pal = 3 };

// The labels in packages/blastem/ts/enums.ts's order: a settings label is an ordinal into this list, and the two are
// one fact (the descriptor test holds them together).
inline constexpr const char* kMdRegionLabels[] = {"auto", "ntsc-us", "ntsc-jp", "pal"};

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

    void pressButton(std::uint8_t button, bool down) override;   // the wire byte table, in InputTypes.hpp
    void setGainDb(float dB, bool immediate = false) override;

    FrameBufferTriple* framebuffer() override { return &frames_; }

    // True once onActivate has booted a live core: a ROM the gate accepted can still fail to boot, and the backend
    // then rejects the build rather than adopting a dead system (as MesenSmsSystem::activated).
    bool activated() const { return activated_; }

    const BlastemMdConfig& config() const { return config_; }
    // The core's master clock after boot (53,693,175 NTSC, 53,203,395 PAL). 0 before activation.
    std::uint32_t masterClock() const;

    // Diagnostics a case reads: the core's own frame size, which must equal framebuffer()'s.
    std::uint32_t coreFrameWidth() const;
    std::uint32_t coreFrameHeight() const;

    // The core, for this package's own tests only: the sibling screens (audio, the port device) read BlastEm's state
    // directly rather than growing a seam apiece.
    genesis_context* core() const { return gen_; }

    // The absolute master clock of host sample `sample`, in this instance's time base.
    std::uint64_t masterClockOf(std::uint64_t sample) const;

private:
    void boot();
    void destroyCore();

    BlastemMdConfig           config_;
    // The ROM as a capability view; BlastEm gets its own aligned copy (see boot()) and owns it from then on.
    std::vector<std::uint8_t> rom_;
    genesis_context*          gen_ = nullptr;
    FrameBufferTriple         frames_{320, 224};  // resized at activation for the region
    bool                      activated_ = false;
    double                    sampleRate_ = 44100.0;
    ExpSmoother               gainSmoother_;

    // The absolute master clock at host sample `sampleAnchor_`, so a sample-rate change re-anchors instead of
    // invalidating every queued offset.
    std::uint64_t sampleAnchor_ = 0;
    std::uint64_t mclkAnchor_ = 0;
    // The absolute number of master clocks the machine has run: cycle_base + the 68k's 32-bit cycle count.
    std::uint64_t mclkNow() const;
    void          run_until(std::uint64_t mclk);
};
```

- [ ] **Step 3: Write the failing test**

`packages/blastem/native/test/BlastemBoot.test.cpp` (the last two cases in it arrive with Tasks 5 and 6; write all of
it now and mark nothing pending: this task's checks are the first two):

```cpp
#include <cstdint>
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

TEST_CASE("an image without the SEGA header is refused by the gate", "[blastem][boot]") {
    std::vector<std::uint8_t> rom = rp::blastem::test::fmPsgRom();
    rom[0x100] = 'X';
    CHECK_FALSE(rp::blastem::isMdRom(rom));
}
```

- [ ] **Step 4: Run it and see it fail**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[boot]"
```

Expected: FAIL, first with `SyntheticRom.hpp: No such file` unless the test's include path reaches the test
directory (add `target_include_directories(retroplug-blastem-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)` to the
test binary in `packages/blastem/native/CMakeLists.txt`), then with a link error for `BlastemMdSystem`.

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
static void* blastemRomCopy(const std::vector<std::uint8_t>& rom) {
    std::size_t size = 1;
    while (size < rom.size()) size <<= 1;
    void* copy = aligned_calloc(1, size, 4096);
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
    // The callbacks BlastEm makes while allocating (render_audio_source, render_set_video_standard) belong to this
    // instance: the shim and the sinks read this thread's pointer.
    currentInstance = this;
    gen_ = alloc_config_genesis(blastemRomCopy(rom_), static_cast<std::uint32_t>(rom_.size()), nullptr, 0, 0,
                                blastemRegion(config_.region));
    if (!gen_) return;
    m68k_reset(gen_->m68k);
    mclkAnchor_ = 0;
    sampleAnchor_ = 0;
    activated_ = true;
    setGainDb(config_.gainDb, true);
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
    // Re-anchor at the current sample: every queued offset keeps the master clock it was given.
    mclkAnchor_ = masterClockOf(sampleAnchor_);
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    audio_.setHostRate(sampleRate_);  // the sink's rings read at the new rate
}

void BlastemMdSystem::setGainDb(float dB, bool immediate) {
    // ExpSmoother's own shape (util/ExpSmoother.hpp), as MesenSmsSystem::setGainDb uses it: the smoother holds a linear
    // gain, so the dB is converted once, and `immediate` snaps rather than ramps.
    gainSmoother_.setTargetValue(dbToLin(dB));
    if (immediate) gainSmoother_.clearToTargetValue();
}

}  // namespace rp::blastem
```

`coreFrameWidth()/coreFrameHeight()` and the triad's bodies land with the video and the block loop; until then the
class needs the three triad methods to compile, so declare them in the header (as above) and give them these bodies
**in this task**, each with a one-line comment saying what it will do (not a task number: the tree carries no such
references):

```cpp
void BlastemMdSystem::prepareForBlock(const AudioBlockInfo&) {}
bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t) { return false; }
void BlastemMdSystem::finishBlock(const AudioBlockInfo&, float* const*, std::size_t) {}
void BlastemMdSystem::reserveAudio(std::uint32_t) {}
```

- [ ] **Step 6: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[boot]"
```

Expected: all three cases pass. The frame-size checks pass only because `frames_` is constructed 320x224 and the PAL
case's `coreFrameHeight` reports the core's own `output_lines` after a frame; if the PAL case fails here, it is Task
6's measurement to fix, not a reason to loosen this check: leave it failing and carry it into Task 6 (its `[video]`
tag runs it).

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
	 * host that runs a block at a time needs to stop at the block's end rather than at a frame boundary. */
	uint32_t        cycle_base;
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
```

Commit in the submodule:

```bash
cd deps/blastem
git add genesis.c genesis.h
git commit -m "genesis: stop a run at an absolute master clock

A library host that runs a block at a time needs the 68k to stop at the
block's end, not at a frame boundary, and a 32-bit cycle count needs a 64-bit
base to name a session's timeline. stop_cycle is an offset from cycle_base;
the rebase moves the base with the count.

RetroPlug: this is the mirror's, used by packages/blastem's BlastemMdSystem."
cd ../..
```

Expected: `git -C deps/blastem log --oneline -1` shows that subject; the outer repo's `deps/blastem` is modified.

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemStop.test.cpp`:

```cpp
// The triad stops the 68k at the block's end, and the stop lands within one instruction of the clock asked for. This
// is what the whole sample-accurate design stands on: a port level staged at frame f must be applied at f's clock, so
// the core must be able to stop there and nowhere coarser.
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
    AudioBlockInfo info;
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
    AudioBlockInfo info;
    info.frames = 64;
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(64);
    const std::uint64_t first = sys->mclkNow();
    sys->prepareForBlock(info);
    sys->stepIfBelowTarget(64);
    CHECK(sys->mclkNow() > first);
    CHECK(sys->mclkNow() - first >= sys->masterClockOf(64) - sys->masterClockOf(0));
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
    // The stop is an offset from the 32-bit base. A block longer than the addressable window cannot arrive: a block is
    // at most kDefaultBlockFrames (8192 frames, about 9.2 million clocks), far inside it.
    gen_->stop_cycle = static_cast<std::uint32_t>(mclk - gen_->cycle_base);
    gen_->header.resume_context(&gen_->header);  // returns at stop_cycle, or at the frame end if that is sooner
    gen_->stop_cycle = 0;
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

`sampleBase_` is the absolute host-sample index of the block's first frame: add `std::uint64_t sampleBase_ = 0;` to the
header, advance it by the block's frames at the end of `finishBlock`, and re-anchor `mclkAnchor_`/`sampleAnchor_` in
`onSampleRateChanged` at `sampleBase_` rather than at 0. Add `run_until` and `blockFrames_` to the header's private
section (`void run_until(std::uint64_t mclk); std::uint32_t blockFrames_ = 0;`).

`resume_context` is `genesis_context::header.resume_context` (genesis.c's `resume_genesis`), which loops
`sync_components` / `m68k_execute` until `should_return`, then clears it: exactly the stop this task adds. It also
calls `handle_reset_requests`, so a cart that requests a reset does not wedge the block loop.

- [ ] **Step 5: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[stop]"
```

Expected: both cases pass. The overshoot assertion is the sensitive one: if `mclkNow()` is far past the target, the
clamp in Step 1 is not being reached (check that `gen->stop_cycle` is set before `resume_context`, and that
`sync_components`' clamp is before `adjust_int_cycle`).

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

Expected: the commit carries both the mirror commit (as a gitlink move) and the system's. **Ask the user to push the
mirror before the next CI run.**

---

### Task 6: The VDP renders into the framebuffer the shell reads

**Files:**
- Create: `packages/blastem/native/src/system/blastem/BlastemVideo.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemVideo.test.cpp`
- Modify: `packages/blastem/native/src/system/blastem/BlastemShim.cpp` (drop the video stubs),
  `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}`, `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: `BlastemMdSystem` (Task 4), `currentInstance` (Task 2).
- Produces: `rp::blastem::VideoSink` with `frameWidth()`, `frameHeight()`, `triple()` and `resize(std::uint32_t width,
  std::uint32_t height)`; the shim's `render_get_framebuffer` / `render_framebuffer_updated` /
  `render_map_color` / `render_set_video_standard` / `render_overscan_*` become real; `BlastemMdSystem` exposes
  `coreFrameWidth/Height`.

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
// The picture: a Mega Drive's visible area in the triple the shell reads, at the core's own size, in the format the
// triple documents. A 320-wide mode must not come out 256 wide, and a PAL machine's 240 lines must not be cropped to
// 224, which are the two silent failures a min-clamping copy would hide.
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
    AudioBlockInfo info;
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

Expected: FAIL: `coreFrameWidth()` returns 0 (not implemented) and the triple never receives a frame.

- [ ] **Step 4: Implement the sink**

`BlastemVideo.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "pixel.h"

namespace rp::blastem {

// BlastEm's framebuffer seam: two buffers the VDP draws into (FRAMEBUFFER_ODD / FRAMEBUFFER_EVEN alternate per frame),
// and a publish that crops the active picture into the FrameBufferTriple the engine reads. BlastEm keeps its own
// bordered frame (LINEBUF_SIZE wide, the border rows around the picture), so the crop is the whole conversion.
class VideoSink {
public:
    VideoSink() = default;

    FrameBufferTriple* triple() { return &triple_; }
    std::uint32_t      frameWidth() const { return triple_.width(); }
    std::uint32_t      frameHeight() const { return triple_.height(); }
    void               resize(std::uint32_t width, std::uint32_t height) { triple_ = FrameBufferTriple(width, height); }

    // The buffer `which` names, at BlastEm's pitch. Stable until the instance is destroyed: the VDP asks for one at
    // init and again after every published frame.
    pixel_t* buffer(std::uint8_t which, int* pitch);
    // `width` is the pitch the VDP used (LINEBUF_SIZE or 256 + HORIZ_BORDER); it is not the picture's width, which
    // comes from the VDP's own mode register through the instance.
    void publish(std::uint8_t which, int width, const vdp_context& vdp);

private:
    static constexpr std::uint32_t kRows = 320;  // more than any mode's bordered height (PAL's 313 lines)
    std::vector<pixel_t> odd_ = std::vector<pixel_t>(LINEBUF_SIZE * kRows, 0u);
    std::vector<pixel_t> even_ = std::vector<pixel_t>(LINEBUF_SIZE * kRows, 0u);
    FrameBufferTriple triple_{320, 224};
};

}  // namespace rp::blastem
```

`BlastemVideo.cpp`:

```cpp
#include "system/blastem/BlastemVideo.hpp"

#include <cstring>

#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace rp::blastem {

static_assert(sizeof(pixel_t) == 4, "the frame triple is XRGB8888: BlastEm must be built without USE_RGB565");

pixel_t* VideoSink::buffer(std::uint8_t which, int* pitch) {
    *pitch = LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t));
    return (which == FRAMEBUFFER_EVEN ? even_ : odd_).data();
}

void VideoSink::publish(std::uint8_t which, int /*width*/, const vdp_context& vdp) {
    // The picture's width is the VDP's own mode, not the pitch BlastEm hands over (which is the bordered buffer's
    // width): 320 in H40, 256 in H32. A mode switch resizes the triple, which drops the one frame in flight, as every
    // console's does.
    const std::uint32_t w = (vdp.regs[REG_MODE_4] & BIT_H40) ? 320u : 256u;
    if (w != triple_.width()) triple_ = FrameBufferTriple(w, triple_.height());
    // The height is the region's (240 lines on a PAL machine, 224 on an NTSC one), fixed at activation.
    const std::uint32_t h = triple_.height();
    const pixel_t* src = (which == FRAMEBUFFER_EVEN ? even_ : odd_).data();
    std::uint32_t* dst = triple_.writeSlot();
    for (std::uint32_t y = 0; y < h; ++y) {
        const pixel_t* row = src + (vdp.border_top + y) * LINEBUF_SIZE + BORDER_LEFT;
        for (std::uint32_t x = 0; x < w; ++x) dst[y * w + x] = 0xFF000000u | (row[x] & 0x00FFFFFFu);
    }
    triple_.publish();
}

}  // namespace rp::blastem
```

Then in `BlastemShim.cpp`, delete the stub definitions of `render_get_framebuffer` (if Step 4 of Task 2 added one),
`render_framebuffer_updated`, `render_set_video_standard`, `render_overscan_top`, `render_overscan_bot`,
`render_get_active_framebuffer`, `render_fullscreen`, `render_create_window`, `render_destroy_window`, and define
them for real there (they are `extern "C"` and must stay in one place):

```cpp
pixel_t* render_get_framebuffer(uint8_t which, int* pitch) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    return sys ? sys->video().buffer(which, pitch) : nullptr;
}

void render_framebuffer_updated(uint8_t which, int width) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (sys) sys->video().publish(which, width, *sys->vdp());
}

vid_std video_standard = VID_NTSC;
void render_set_video_standard(vid_std std) { video_standard = std; }
uint8_t render_get_active_framebuffer(void) { return FRAMEBUFFER_ODD; }
uint8_t render_fullscreen(void) { return 0; }
uint32_t render_overscan_top(void) { return 0; }   // the full picture, borders cropped by the sink
uint32_t render_overscan_bot(void) { return 0; }
uint8_t render_create_window(char*, uint32_t, uint32_t, window_close_handler) { return 0; }
void render_destroy_window(uint8_t) {}
```

`BlastemMdSystem` needs `video()` and `vdp()` accessors (add to the header: `VideoSink& video() { return video_; }` and
`vdp_context* vdp() const { return gen_ ? gen_->vdp : nullptr; }`), a `VideoSink video_;` member, and this in `boot()`
after `m68k_reset`:

```cpp
    // From here the VDP asks the sink for its buffer instead of allocating its own (vdp.c's headless branch), and the
    // triple is sized for the region: 240 lines on a PAL machine, 224 on an NTSC one. The width follows the mode
    // register at the first published frame (320 in H40, 256 in H32).
    headless = 0;
    video_.resize(320, (gen_->version_reg & 0x40) ? 240u : 224u);
```

and `coreFrameWidth()/coreFrameHeight()` return `video_.frameWidth()/frameHeight()`.

**The shim's `render_audio_source` and the sink both read `currentInstance`, so `headless` is the only global the video
half still touches**: it is BlastEm's own, set by the instance that activates, and every instance sets it to the same
value, which is why one copy is enough.

- [ ] **Step 5: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[video]"
build/bin/retroplug-blastem-test "[boot]"
```

Expected: the `[video]` cases pass at 320x224 and 320x240; the `[boot]` PAL case from Task 4 (`coreFrameHeight() ==
240`) now passes too. `coreFrameWidth/Height` return `video_.frameWidth()/frameHeight()`.

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
- Mirror (`deps/blastem`, branch `retroplug`): `blip_buf.c`, `blip_buf.h` (added, upstream 1.1.0)
- Create: `packages/blastem/native/src/system/blastem/BlastemAudio.{hpp,cpp}`,
  `packages/blastem/native/test/BlastemAudio.test.cpp`
- Modify: `cmake/blastem.cmake` (drop `render_audio.c` from the file set), `BlastemShim.cpp` (drop the audio backend
  stubs), `BlastemMdSystem.{hpp,cpp}`, `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: `BlastemMdSystem` (Task 4/5), the sample mapping (`masterClockOf`).
- Produces: `rp::blastem::AudioSink` implementing `render_audio.h`'s **source** API (`render_audio_source`,
  `render_audio_adjust_clock`, `render_put_mono_sample`, `render_put_stereo_sample`, `render_pause_source`,
  `render_resume_source`, `render_audio_source_gaindb`, `render_free_source`, `render_end_audio`,
  `render_save_audio`), plus `drain(std::uint32_t frames)` and `prepare(std::uint32_t maxBlockFrames)`; the system's
  `finishBlock` sums the sink into the block's lanes and applies the gain.

- [ ] **Step 1: The mirror commit: blip_buf 1.1.0**

BlastEm has no band-limited resampler of its own. Take blip_buf 1.1.0 (Shay Green, LGPL-2.1+, the version Mesen and
Genesis Plus GX carry) from the copy this repository's own test tree already has,
`deps/mesen/Utilities/Audio/blip_buf.cpp` is C++ and patched, so take the plain C one from the spike's GPGX tree:
`/workspaces/Megadrive/.superpowers/mdspike/arm/gpgx/core/sound/blip_buf.{c,h}`. Copy both into `deps/blastem`, and
add at the top of each:

```c
/* RetroPlug: blip_buf 1.1.0 (Shay Green, LGPL-2.1 or later), copied verbatim from Genesis Plus GX's
 * core/sound/blip_buf.c at the same version. BlastEm has no band-limited resampler of its own: its render_audio.c
 * interpolates linearly. packages/blastem links this instead. */
```

Verify both files are unmodified apart from that header, then commit:

```bash
cd deps/blastem
git add blip_buf.c blip_buf.h
git commit -m "blip_buf: band-limited step synthesis, 1.1.0

BlastEm resamples linearly in render_audio.c. A host that wants Mesen's
audio quality needs blip_buf, which both Mesen and Genesis Plus GX use."
git log --oneline -1
cd ../..
```

Expected: the log shows that subject, and `diff` against the spike's copies shows only the added header.

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
    AudioBlockInfo info;
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
}

TEST_CASE("the YM2612's writes change the output", "[blastem][audio]") {
    auto psg = boot(rp::blastem::test::psgOnlyRom());
    auto both = boot(rp::blastem::test::fmPsgRom());
    const std::vector<float> a = render(*psg, 0.25);
    const std::vector<float> b = render(*both, 0.25);
    REQUIRE(a.size() == b.size());
    CHECK(a != b);  // the FM patch is audible: the two renders differ
}

TEST_CASE("the same input renders the same bytes twice", "[blastem][audio]") {
    auto first = boot(rp::blastem::test::fmPsgRom());
    auto second = boot(rp::blastem::test::fmPsgRom());
    CHECK(render(*first, 0.25) == render(*second, 0.25));
}
```

- [ ] **Step 3: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[audio]"
```

Expected: FAIL: `finishBlock` writes nothing, so the first case's peak is 0 (and the `size` check fails first).

- [ ] **Step 4: `cmake/blastem.cmake` drops BlastEm's own audio file**

Remove `render_audio.c` from `_BLASTEM_SOURCES` and add `blip_buf.c`. The defines stay `PUBLIC` as Task 2 wrote them:
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
// exactly. BlastEm's render_audio.c is not compiled: it is a process-global registry capped at 8 sources, and it
// resamples linearly.
//
// The native rates are the chips' own: the YM2612 emits one stereo sample per 1008 master clocks and the PSG one per
// 240, so a source's `clockRate` is master_clock / sample_divider and blip is told exactly that.
class AudioSink {
public:
    // [control, at activation] Size every source's ring for the plan's largest block. Growth only.
    void prepare(std::uint32_t maxBlockFrames);

    // [block] The block's `frames` host samples, summed from every live source into `left`/`right` (added, not
    // overwritten, as SystemBase's contract says), after each source's own gain. Fills with silence and counts a
    // starvation if a source is short, which is a bug and a test asserts it never happens.
    void drain(std::uint32_t frames, float* left, float* right);

    // [any] Sources the sink has starved since activation: 0 on a driven core.
    std::uint64_t starvedSamples() const { return starved_; }

    // Called by the render_audio.h entry points below.
    struct Source;
    Source* add(const char* name, std::uint64_t masterClock, std::uint64_t sampleDivider, std::uint8_t channels);
    void     remove(Source* source);
    void     adjustClock(Source* source, std::uint64_t masterClock, std::uint64_t sampleDivider);
    void     putStereo(Source* source, std::int16_t left, std::int16_t right);
    void     putMono(Source* source, std::int16_t value);
    void     setGainDb(Source* source, float gainDb);
    // [control/block] The host sample rate every ring reads at (the same for all sources).
    void setHostRate(double sampleRate);
    // Drops every source (a core being destroyed).
    void clear();

private:
    struct Impl;
    std::vector<Source*> sources_;
    std::uint32_t        maxBlockFrames_ = 1024;
    double               hostRate_ = 48000.0;
    std::uint64_t        starved_ = 0;
};

}  // namespace rp::blastem
```

The source's state and the per-sample arithmetic, which is the whole design of this file:

```cpp
struct AudioSink::Source {
    blip_t*       left = nullptr;    // one blip per side, both clocked at the chip's native rate
    blip_t*       right = nullptr;
    std::vector<std::int16_t> ringLeft;   // host-rate samples read from blip, drained by the block
    std::vector<std::int16_t> ringRight;
    std::uint32_t ringRead = 0, ringFill = 0;
    std::uint32_t capacity = 0;
    double        dt = 0.0;          // seconds per native sample: 1 / (masterClock / sampleDivider)
    std::uint32_t lowpassAlpha = 0;  // BlastEm's one-pole coefficient, 16.16
    std::int16_t  lastLeft = 0, lastRight = 0;  // the low-pass state, as render_audio.c kept it
    float         gain = 1.0f;       // the chip's own gain (audio.fm_gain / audio.psg_gain, 0 dB by default)
    std::uint32_t pendingClocks = 0; // native samples added since the last end_frame: blip's time base
};

// BlastEm's filter, from render_audio.c:339: alpha = dt / (dt + rc) with rc = (1 / cutoff) / 2pi and cutoff 3390 Hz,
// held as 16.16. It runs at the chip's native rate, before the resampler, exactly as BlastEm's own sink does.
static std::int16_t lowpass(std::uint32_t alpha, std::int16_t last, std::int16_t current) {
    const std::int32_t tmp = current * std::int32_t(alpha) + last * std::int32_t(0x10000 - alpha);
    return static_cast<std::int16_t>(tmp >> 16);
}

void AudioSink::putStereo(Source* s, std::int16_t left, std::int16_t right) {
    if (!s) return;
    left = lowpass(s->lowpassAlpha, s->lastLeft, left);
    right = lowpass(s->lowpassAlpha, s->lastRight, right);
    // A band-limited step per native sample: the delta from the previous filtered sample, at this sample's clock.
    blip_add_delta(s->left, s->pendingClocks, static_cast<int>(left) - s->lastLeft);
    blip_add_delta(s->right, s->pendingClocks, static_cast<int>(right) - s->lastRight);
    s->lastLeft = left;
    s->lastRight = right;
    ++s->pendingClocks;
}
```

`render_audio_adjust_clock` recomputes `dt`, `lowpassAlpha` and calls `blip_set_rates(s->left, nativeRate,
hostRate_)` on both buffers (nativeRate = `masterClock / sampleDivider`). `drain`:

```cpp
void AudioSink::drain(std::uint32_t frames, float* left, float* right) {
    for (std::uint32_t i = 0; i < frames; ++i) { left[i] = 0.0f; right[i] = 0.0f; }
    for (Source* s : sources_) {
        blip_end_frame(s->left, s->pendingClocks);
        blip_end_frame(s->right, s->pendingClocks);
        s->pendingClocks = 0;
        // Everything blip has, into the ring: the ring decouples the fractional native-to-host ratio from the block
        // size, so the sample stream depends on absolute time alone.
        const std::uint32_t room = s->capacity - s->ringFill;
        if (room) {
            std::int16_t scratchL[4096];
            std::int16_t scratchR[4096];
            std::uint32_t done = 0;
            while (done < room) {
                const std::uint32_t chunk = std::min<std::uint32_t>(room - done, 4096);
                const int got = blip_read_samples(s->left, scratchL, int(chunk), 0);
                blip_read_samples(s->right, scratchR, int(chunk), 0);
                if (got <= 0) break;
                for (int k = 0; k < got; ++k) {
                    const std::uint32_t at = (s->ringRead + s->ringFill + std::uint32_t(k)) % s->capacity;
                    s->ringLeft[at] = scratchL[k];
                    s->ringRight[at] = scratchR[k];
                }
                s->ringFill += std::uint32_t(got);
                done += std::uint32_t(got);
            }
        }
        const float scale = s->gain / 32768.0f;
        for (std::uint32_t i = 0; i < frames; ++i) {
            if (s->ringFill == 0) { ++starved_; continue; }
            left[i] += static_cast<float>(s->ringLeft[s->ringRead]) * scale;
            right[i] += static_cast<float>(s->ringRight[s->ringRead]) * scale;
            s->ringRead = (s->ringRead + 1) % s->capacity;
            --s->ringFill;
        }
    }
}
```

`add` allocates the two blips with `nearest_pow2(4096)`-sample buffers (`blip_new(4096)` and `blip_clear`), sizes the
ring `capacity = 2 * maxBlockFrames_ + 1024`, and calls `adjustClock`. `putMono` duplicates the sample into both sides.
`render_audio_source`/`render_audio_free_source`/... in `BlastemShim.cpp` forward to the instance's sink:
`currentInstance` → `static_cast<BlastemMdSystem*>(currentInstance)->audio()` (add `AudioSink& audio() { return audio_; }`
and an `AudioSink audio_;` member to the system). `render_end_audio` and
`render_save_audio` are no-ops (`warning()` for the latter: a WAV dump is not a host feature).

Then `BlastemMdSystem`:
- `onActivate` calls `audio_.setHostRate(sampleRate_)` **before** `alloc_config_genesis` (the chips ask for their
  source during it) and `audio_.prepare(blockFrames(1024))` before the chips are created.
- `reserveAudio(maxBlockFrames)` calls `audio_.prepare(maxBlockFrames)` (growth only).
- `finishBlock` drains into the lanes and applies the smoothed gain:

```cpp
void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) {
    if (!gen_ || laneCount < 2 || !outs || !outs[0] || !outs[1]) {
        sampleBase_ += info.frames;
        return;  // a plan that wires no audio still advances the clock
    }
    // The block's samples are sized at activation and at every growth (reserveAudio), never here: the engine's [alloc]
    // cases count a block's allocations, emulation included.
    audio_.drain(info.frames, mixLeft_.data(), mixRight_.data());
    for (std::uint32_t i = 0; i < info.frames; ++i) {
        const float gain = gainSmoother_.next();
        outs[0][i] += mixLeft_[i] * gain;   // added, not written: SystemBase's contract
        outs[1][i] += mixRight_[i] * gain;
    }
    sampleBase_ += info.frames;
}

void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) {
    SystemBase::reserveAudio(maxBlockFrames);
    const std::uint32_t frames = blockFrames(1024);
    if (frames > mixLeft_.size()) {
        mixLeft_.resize(frames);
        mixRight_.resize(frames);
        audio_.prepare(frames);
    }
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
  `std::uint64_t accepted()`, `std::uint64_t dropped()`, `std::uint8_t lastApplied()`, and the C entry points
  `io_custom_data_read/write/control_write` the mirror declares; `BlastemMdSystem::pushCoreBytes(frame, data, size,
  flush)` (frame is the intra-block sample offset), `hasCoreBytesIn()` (true, as `MesenSmsSystem`'s is: the queue
  exists whether or not a role is wired, and an unwired port simply never has anything in it), `levelsAccepted()`.

- [ ] **Step 1: The mirror commit: `IO_CUSTOM`**

In `deps/blastem/io.h`, add `IO_CUSTOM` to the device enum (at the end, so no existing value moves) and the four entry
points, with the marker:

```c
	IO_HEARTBEAT_TRAINER,
	/* RetroPlug: a device the host drives. The port's reads and writes are handed to the host with their exact
	 * master clock (the same cycle every other device gets), which is what lets a host hold a level on a pin from a
	 * queued schedule instead of at a block boundary. */
	IO_CUSTOM
};

/* RetroPlug: the host device's callbacks (IO_CUSTOM). io_data_read returns the whole byte the 68k sees, so the host
 * applies the $A1000B direction rule itself: a pin the ROM has configured as an output reads its own data latch. */
void io_custom_data_write(io_port *port, uint8_t value, uint32_t current_cycle);
void io_custom_control_write(io_port *port, uint8_t value, uint32_t current_cycle);
uint8_t io_custom_data_read(io_port *port, uint32_t current_cycle);
void io_custom_run(io_port *port, uint32_t current_cycle);
```

In `io.c`:
- `io_control_write`, inside `if (changes)` after `port->control = value;`: `if (port->device_type == IO_CUSTOM) io_custom_control_write(port, value, current_cycle);`
- `io_data_write`'s switch: `case IO_CUSTOM: io_custom_data_write(port, value, current_cycle); break;` (the common tail still stores `port->output = value`, which is the latch the direction rule reads)
- `io_data_read`'s switch, first case: `case IO_CUSTOM: return io_custom_data_read(port, current_cycle);`
- `io_run`'s device switch (where `process_events()` is called): `case IO_CUSTOM: io_custom_run(port, current_cycle); break;`

Commit in the submodule:

```bash
cd deps/blastem
git add io.h io.c
git commit -m "io: a device the host drives, with each access's master clock

Every other device answers from its own state. A host-driven device answers
from a schedule, so it needs the read's exact cycle and the whole byte, which
is where the emulated ROM's own output pins have to win over the host's
levels."
cd ../..
```

- [ ] **Step 2: Write the failing test**

`packages/blastem/native/test/BlastemPort.test.cpp`:

```cpp
// The port-2 device (design section 3): a level lands at its scheduled master clock, not at the block boundary; a
// level staged for a frame already past applies immediately; a pin the ROM drives as an output reads the ROM's own
// latch; and a level past the block's end keeps its timing into the next block.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemCore.hpp"
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

void runBlock(rp::blastem::BlastemMdSystem& sys, std::vector<float>& l, std::vector<float>& r) {
    float* lanes[2] = {l.data(), r.data()};
    AudioBlockInfo info;
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

}  // namespace

TEST_CASE("a level reaches the ROM's port read at its own sample", "[blastem][port]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    std::vector<float> l(kFrames), r(kFrames);
    // Let the ROM boot and settle on its poll loop.
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    const std::uint8_t level = 0x40;  // TH low, TR high: not the idle $60
    sys->pushCoreBytes(100, &level, 1);
    CHECK(sys->levelsAccepted() == 1);
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x40);
}

TEST_CASE("a level staged past the block's end keeps its timing", "[blastem][port]") {
    auto sys = boot(rp::blastem::test::portPollRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    const std::uint8_t idle = 0x60;
    const std::uint8_t level = 0x00;
    sys->pushCoreBytes(0, &idle, 1);
    sys->pushCoreBytes(4096, &level, 1);  // exactly one block further on
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x60);  // this block still sees idle
    runBlock(*sys, l, r);
    CHECK((ramByte(*sys, 0x0000) & 0x60) == 0x00);  // the next one sees the level
}

TEST_CASE("a pin the ROM drives as an output reads the ROM's latch", "[blastem][port]") {
    // syncEdgeRom writes $C00011 and polls; the direction rule is proved with a ROM-less case instead: the device is
    // asked what it answers when the control register says TR and TH are outputs.
    auto sys = boot(rp::blastem::test::portPollRom());
    std::vector<float> l(kFrames), r(kFrames);
    for (int i = 0; i < 4; ++i) runBlock(*sys, l, r);
    const std::uint8_t level = 0x00;  // the host holds both lines low
    sys->pushCoreBytes(0, &level, 1);
    // The ROM makes both pins outputs and writes them high, as genmddj's OUT mode does ($A1000B = $60, $A10005 = $60).
    sys->writePortControl(0x60);
    sys->writePortData(0x60);
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
    // [device] Applies every entry due at `mclk`, then returns the byte the 68k reads: the host's levels on input
    // pins, the ROM's own latch on pins it drives (control bit set).
    std::uint8_t read(std::uint64_t mclk, std::uint8_t output, std::uint8_t control);
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

std::uint8_t PortDevice::read(std::uint64_t mclk, std::uint8_t output, std::uint8_t control) {
    if (needsSort_) {
        sortScratch_.sort(pending_.begin(), pending_.end(),
                          [](const Entry& a, const Entry& b) { return a.mclk < b.mclk; });
        needsSort_ = false;
    }
    while (!pending_.empty() && pending_.front().mclk <= mclk) {
        lastApplied_ = pending_.front().level;
        pending_.pop();
    }
    // The direction rule, and the whole reason the device answers with a byte rather than a device input: a pin the
    // ROM configured as an output (a set bit in $A1000B's control) reads back the ROM's own data latch, and only the
    // ROM's input pins carry the host's level. `idle` is what nothing drives: bits 0 to 4 and 7 sit behind a
    // disconnected pad's pull-ups, which reads as high, and genmddj never looks at them.
    const std::uint8_t idle = 0x9F;
    return std::uint8_t((output & control) | (((lastApplied_ & 0x60) | idle) & std::uint8_t(~control)));
}

}  // namespace rp::blastem
```

Then the C entry points at the bottom of the same file:

```cpp
extern "C" {
void io_custom_data_write(io_port* port, uint8_t value, uint32_t cycle) { (void)port; (void)value; (void)cycle; }
void io_custom_control_write(io_port* port, uint8_t value, uint32_t cycle) { (void)port; (void)value; (void)cycle; }
void io_custom_run(io_port* port, uint32_t cycle) { (void)port; (void)cycle; }
uint8_t io_custom_data_read(io_port* port, uint32_t cycle) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (!sys) return port->output;
    return sys->portDevice().read(sys->mclkOfCoreCycle(cycle), port->output, port->control);
}
}
```

`mclkOfCoreCycle(cycle)` is `gen->cycle_base + cycle`: add to the system's header
`std::uint64_t mclkOfCoreCycle(std::uint32_t cycle) const { return gen_ ? std::uint64_t(gen_->cycle_base) + cycle : 0; }`,
`PortDevice& portDevice() { return port_; }`, `std::uint64_t levelsAccepted() const { return port_.accepted(); }`, and a
`PortDevice port_;` member. The writes need no handler because `io_data_write` already stores `port->output` and
`io_control_write` already stores `port->control`: those *are* the ROM's latch and direction register, which is why the
read can apply the rule from them.

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

- [ ] **Step 5: The sample mapping, across a rate change, and the audio edge it predicts**

Add one case to the same file. This is the design's "a level staged at frame f produces its audio edge at the same known
delay after f at every block size", and its second half, "a ROM reads `$A10005` at known cycles ... including across a
sample-rate change":

```cpp
TEST_CASE("a level's audio edge lands at the frame it was staged for, at 44.1 and 48 kHz", "[blastem][port]") {
    // syncEdgeRom holds PSG channel 0 at full volume and silences it the first time TR/TH leave idle. So the first
    // sample where the mix changes IS the level's own sample, at either host rate.
    for (const double rate : {44100.0, 48000.0}) {
        rp::blastem::BlastemMdConfig cfg;
        cfg.region = 1;
        auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::syncEdgeRom());
        sys->onActivate(rate);
        const std::uint32_t block = 512;
        std::vector<float> l(block), r(block), mixed;
        float* lanes[2] = {l.data(), r.data()};
        AudioBlockInfo info;
        info.frames = block;
        info.sampleRate = rate;
        const std::uint32_t at = 9000;  // inside the fifth block
        std::uint32_t base = 0;
        while (base <= at + block) {
            if (base <= at && at < base + block) {
                const std::uint8_t level = 0x00;
                sys->pushCoreBytes(at - base, &level, 1);
            }
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            sys->prepareForBlock(info);
            while (sys->stepIfBelowTarget(block)) {}
            sys->finishBlock(info, lanes, 2);
            for (std::uint32_t i = 0; i < block; ++i) mixed.push_back(l[i]);
            base += block;
        }
        // The first sample whose magnitude leaves the steady tone is the edge. The PSG's own period is about 100
        // samples at these rates, so a threshold on the sample-to-sample change is what finds the silence, not an
        // amplitude test.
        std::size_t edge = at;
        for (std::size_t i = at + 200; i + 1 < mixed.size(); ++i) {
            if (std::fabs(mixed[i]) < 0.001f && std::fabs(mixed[i + 1]) < 0.001f) { edge = i; break; }
        }
        INFO("rate " << rate << ": staged at " << at << ", edge at " << edge);
        CHECK(edge >= at);
        // A tight poll loop: the ROM reads $A10005 every ~20 master clocks and writes the PSG's attenuation, which the
        // PSG applies at its next 240-clock sample. 64 host samples is far more than that and far less than the block,
        // so the case fails if the level was applied at a block boundary instead of its own cycle.
        CHECK(edge - at < 64);
    }
}
```

Add the two includes the case needs (`<cmath>`, `<memory>`). If the edge moves with the host rate rather than staying at
the staged frame, the mapping is following the rate and not the frame index, which is what the case exists to catch.

- [ ] **Step 6: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[port]"
```

Expected: all four cases pass. The first one's second read (`0x40`) is the case that proves the cycle: if the level
lands at the block boundary instead, the ROM's poll sees it one block later and the check still passes, which is why
Task 9 exists; this case proves delivery and the direction rule, not the sample. The rate case is what pins the sample
mapping.

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
// size, and the ROM answers it with an audio edge.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate       = 48000.0;
constexpr std::uint64_t kTotal      = 96000;             // 2 s
constexpr std::uint64_t kLevelFrame = 41000;             // an odd frame, inside every partition differently

std::vector<float> renderAt(std::uint32_t block) {
    rp::blastem::BlastemMdConfig cfg;
    cfg.region = 1;
    auto sys = std::make_unique<rp::blastem::BlastemMdSystem>(1, cfg, rp::blastem::test::syncEdgeRom());
    sys->onActivate(kRate);
    std::vector<float> out;
    out.reserve(kTotal * 2);
    std::vector<float> left(block), right(block);
    float* lanes[2] = {left.data(), right.data()};
    std::uint64_t at = 0;
    bool staged = false;
    AudioBlockInfo info;
    while (at < kTotal) {
        info.frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(block, kTotal - at));
        if (!staged && at + info.frames > kLevelFrame) {
            const std::uint8_t level = 0x00;
            sys->pushCoreBytes(static_cast<std::uint32_t>(kLevelFrame - at), &level, 1);
            staged = true;
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sys->prepareForBlock(info);
        while (sys->stepIfBelowTarget(info.frames)) {}
        sys->finishBlock(info, lanes, 2);
        out.insert(out.end(), left.begin(), left.begin() + info.frames);
        out.insert(out.end(), right.begin(), right.begin() + info.frames);
        at += info.frames;
    }
    return out;
}

}  // namespace

TEST_CASE("1, 64, 1024 and 4096 frame blocks render identical bytes", "[blastem][invariance]") {
    const std::vector<float> one = renderAt(1);
    const std::vector<float> sixtyFour = renderAt(64);
    const std::vector<float> thousand = renderAt(1024);
    const std::vector<float> fourThousand = renderAt(4096);
    REQUIRE(one.size() == kTotal * 2);
    CHECK(one == sixtyFour);
    CHECK(one == thousand);
    CHECK(one == fourThousand);
}
```

- [ ] **Step 2: Run it and record what it says**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[invariance]"
```

Expected, and both outcomes are a result:

- **PASS**: the design's stop model is transparent. Commit it (Step 3) and delete Task 10's steps from this plan when
  you reach it, with a note in the commit that the fallback was not needed.
- **FAIL**: record the numbers before doing anything else, because they are what Task 10's decision is made on:

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
    // block's cost is the extra syncs (STOPPING-STUDY.md's estimate for SameBoy's model, which this is).
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
- Mirror (`deps/blastem`, branch `retroplug`): `genesis.c`, `genesis.h` (`vdp->system` instead of `current_system`,
  `MCLKS_PER_68K`)
- Modify: `packages/blastem/native/src/system/blastem/BlastemShim.cpp` (the instance pointer on every entry)
- Create: `packages/blastem/native/test/BlastemInstances.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: the system and its triad.
- Produces: nothing new to call; it changes what two instances do to each other.

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

and delete the reads of `clocks\0m68k_divider\0` in `shared_init` and `shared_init_gen`.

Commit in the submodule:

```bash
cd deps/blastem
git add genesis.c genesis.h vdp.c vdp.h gdb_remote.c
git commit -m "genesis: the DMA reads its own VDP's console, and the 68k divider is a constant

read_dma_value and vdp_dma_started went through the current_system global,
so a second console in one process would DMA out of the first one's RAM. The
VDP the call belongs to is the console. MCLKS_PER_68K was a writable global
every allocation set; it is 7 on every Mega Drive.

RetroPlug runs several consoles in one process."
cd ../..
```

- [ ] **Step 3: Write the failing test**

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
#include "system/blastem/BlastemMdSystem.hpp"

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
    AudioBlockInfo info;
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
    auto a = boot(rp::blastem::test::fmPsgRom(), 1);
    auto b = boot(rp::blastem::test::portPollRom(), 2);
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
}
```

- [ ] **Step 4: Run it, then make it pass**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[instances]"
```

Expected before the fix: FAIL (the interleaved case differs from the solo render, or the threaded one does, or both).
The only remaining global in the path is the instance pointer: `BlastemShim.cpp`'s `thread_local void*
currentInstance`. With the mirror's fix in place, a threaded run has each thread's pointer its own; an *interleaved*
run has one thread's pointer set by whichever system called last, so every entry point must set it. Audit the system's
public methods for that (each one a `currentInstance = this;` first line): `onActivate`, `onDeactivate`,
`onSampleRateChanged`, `prepareForBlock`, `stepIfBelowTarget` (via `run_until`), `finishBlock`, `pressButton`,
`pushCoreBytes`, `setGainDb`, `saveSramBytes`, `saveStateBytes`, `captureStateSnapshot`, `getMemory`, and the device's
read (`io_custom_data_read`, which runs inside a block and therefore under the system's own call).

- [ ] **Step 5: Run all the native cases**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test
```

Expected: every case passes, the invariance one included (a grid, if Task 10 applied, is per instance).

- [ ] **Step 6: Commit**

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
    AudioBlockInfo info;
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

class BlastemMdSystem;  // forward: the capability reaches the core through the system

namespace rp::blastem {

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
- Modify: `packages/blastem/native/src/system/blastem/BlastemMdSystem.{hpp,cpp}`
- Create: `packages/blastem/native/test/BlastemSaves.test.cpp`
- Modify: `packages/blastem/native/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above.
- Produces: `saveStateBytes()`, `stateSnapshotSize()`, `captureStateSnapshot()`, `loadStateBytes()` (the seed path).

- [ ] **Step 1: Write the failing test**

`packages/blastem/native/test/BlastemSaves.test.cpp`:

```cpp
// Savestates (design section 2): a state saved, run from, loaded and run again renders what the uninterrupted run
// renders; and a state whose serializer identifier is not BlastEm's 1.0 one is refused rather than misread.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SyntheticRom.hpp"
#include "system/blastem/BlastemMdSystem.hpp"

namespace {

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 512;

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
    AudioBlockInfo info;
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

TEST_CASE("a savestate round trip renders what the uninterrupted run renders", "[blastem][saves]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 20);                                   // boot, and let the note sound
    const std::vector<std::uint8_t> state = sys->saveStateBytes();
    REQUIRE(state.size() > 8);
    CHECK(std::memcmp(state.data(), "BLSTSZ\x01\x07", 8) == 0);  // BlastEm's own serializer identifier
    const std::vector<float> afterSave = run(*sys, 40);

    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->saveStateBytes().size() == state.size());
    CHECK(run(*restored, 40) == afterSave);
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

TEST_CASE("a truncated state is refused, not read past its end", "[blastem][saves]") {
    auto sys = boot(rp::blastem::test::fmPsgRom());
    run(*sys, 2);
    std::vector<std::uint8_t> state = sys->saveStateBytes();
    state.resize(state.size() / 2);
    auto restored = boot(rp::blastem::test::fmPsgRom(), state);
    CHECK(restored->stateRefused());
}
```

- [ ] **Step 2: Run it**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test "[saves]"
```

Expected: FAIL: `saveStateBytes()` is empty.

- [ ] **Step 3: Implement**

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
    std::vector<std::uint8_t> out(sizeof(kStateIdent) - 1 + buf.size);
    std::memcpy(out.data(), kStateIdent, sizeof(kStateIdent) - 1);
    std::memcpy(out.data() + sizeof(kStateIdent) - 1, buf.data, buf.size);
    free(buf.data);
    return out;
}

bool BlastemMdSystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kIdent = sizeof(kStateIdent) - 1;
    if (!gen_ || bytes.size() <= kIdent || std::memcmp(bytes.data(), kStateIdent, kIdent) != 0) {
        stateRefused_ = !bytes.empty();
        return false;
    }
    currentInstance = this;
    deserialize_buffer buf;
    init_deserialize(&buf, const_cast<std::uint8_t*>(bytes.data()) + kIdent, bytes.size() - kIdent);
    // genesis_deserialize's own section loader warns and skips a section it does not know (serialize.c:218), so a state
    // that is short of a section is refused here, by a length the serializer wrote, rather than being read past.
    genesis_deserialize(&buf, gen_);
    mclkAnchor_ = 0;
    sampleAnchor_ = 0;
    return true;
}

std::size_t BlastemMdSystem::stateSnapshotSize() const {
    // A Mega Drive state is about 50 KB (VDP VRAM 64 KB dominates), and it grows with a cart's battery; a ceiling with
    // headroom keeps the snapshot triple from resizing mid-life, as the base class requires.
    return 256 * 1024;
}

bool BlastemMdSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    dst = saveStateBytes();
    return !dst.empty();
}
```

`loadStateBytes` runs from `seedSramAndState`, which `onActivate` calls after boot:
`seedSramAndState(config_.sram, config_.savestate);` at the end of `boot()`. Add `bool stateRefused_ = false;` and
`bool stateRefused() const { return stateRefused_; }`.

`genesis_deserialize` calls `adjust_int_cycle` itself (genesis.c:279) so the interrupt cycle is consistent; the next
`run_until` resumes from the loaded pc.

- [ ] **Step 4: Run it, and the whole binary**

```bash
cmake --build build --target retroplug-blastem-test -j$(nproc) && build/bin/retroplug-blastem-test
```

Expected: every case passes. If the round trip's audio differs, the state was taken at a point the 68k is not at an
instruction boundary: `saveStateBytes` is only valid between blocks (the engine calls it there), which the case does.

- [ ] **Step 5: Commit**

```bash
git add packages/blastem/native/src/system/blastem/BlastemMdSystem.hpp \
        packages/blastem/native/src/system/blastem/BlastemMdSystem.cpp \
        packages/blastem/native/test/BlastemSaves.test.cpp packages/blastem/native/CMakeLists.txt
git commit -m "feat(blastem): savestates through BlastEm's serializer, and a foreign one refused"
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
  `packages/native/test/engine-graph/golden/product-registrations.json` (re-recorded)

**Interfaces:**
- Consumes: `BlastemMdSystem`, `BlastemMdConfig`, `BlastemMdRegion` labels, `rp::blastem::test::*`.
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
        refuse(why, "not a Mega Drive ROM: no \"SEGA\" header at $100");
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

// The region labels' order is packages/blastem/ts/enums.ts's REGION_VALUES and BlastemMdConfig.hpp's kMdRegionLabels.
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
    struct {
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

`valueOf<MdRegion>` needs the label traits every label type in tree has: Mesen's enums live in `MesenNode.cpp` beside
their `knobs::valueOf`; `MdRegion` is in `BlastemMdConfig.hpp`, so add there:

```cpp
// The label array the knobs read (`knobs::valueOf<MdRegion>`): an MdRegion's ordinal into kMdRegionLabels.
inline const char* const* mdRegionLabel(MdRegion) { return kMdRegionLabels; }
```

and check `SystemNode.hpp`'s `valueOf` for its exact contract (it may take the array as a template argument or a
`labelOf` free function; follow what `NesRegion` does in `MesenNode.cpp` and put the same shape here).

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
    cfg.region = 1;  // ntsc-us: the same region the node's settings below carry
    auto sys = std::make_unique<BlastemMdSystem>(id, cfg, rp::blastem::test::fmPsgRom());
    sys->onActivate(kRate);
    return sys;
}

std::string mdNode(NodeRig& rig, SystemId id) { return rig.boot("rp.blastem.md", id, R"("region":"ntsc-us")"); }

TEST_CASE("a lone Mega Drive node builds its core from the ROM resource", "[engine-graph][system-node][blastem-md]") {
    NodeRig rig(kRate);
    REQUIRE(rig.set(R"({"nodes":[{"id":"md","type":"rp.blastem.md","settings":{)" + mdNode(rig, 1) +
                    R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                        "wires":[{"from":"md.mix","to":"out.out.1-2"}]})").ok);
    SystemBase* const graphCore = rig.builtCore(1);
    REQUIRE(graphCore != nullptr);
    const auto* core = static_cast<BlastemMdSystem*>(graphCore);
    CHECK(core->activated());
    CHECK(core->masterClock() == 53693175u);       // ntsc-us, from the settings' label
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

Re-record the registration golden:

```bash
RP_GOLDEN_UPDATE=1 build/bin/retroplug-engine-graph-test "[packages]"
git diff packages/native/test/engine-graph/golden/product-registrations.json
```

Expected: one line added, `rp.blastem.md`, in the position the product list puts it (after `rp.mesen.sms`, before
`rp.sms.sync`'s package types).

- [ ] **Step 4: Run it**

```bash
pnpm install
cmake --build build --target retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-engine-graph-test "[blastem-md]"
build/bin/retroplug-engine-graph-test "[packages]"
```

Expected: the three new cases pass, `[packages]` passes against the re-recorded golden, and no other case changed.

- [ ] **Step 5: Commit**

```bash
git add packages/blastem/native packages/native/package.json packages/native/cmake/tests.cmake \
        packages/native/test/engine-graph/GraphDocs.hpp \
        packages/native/test/engine-graph/SystemNode.test.cpp \
        packages/native/test/engine-graph/golden/product-registrations.json pnpm-lock.yaml
git commit -m "feat(blastem): rp.blastem.md, its build function, and the engine's Mega Drive cases"
```

---

### Task 15: The TS console descriptor, the `blastem` core role, and the products

**Files:**
- Create: `packages/blastem/ts/consoles.ts`, `packages/blastem/ts/app.ts`, `packages/blastem/ts/enums.ts`,
  `packages/blastem/tsconfig.json` (and add `typecheck` to its manifest)
- Modify: `packages/blastem/package.json` (exports, app, consoles, typecheck),
  `packages/retroplug/test/consoles/{cases.ts,recorded.ts}`,
  `packages/retroplug/package.json` (already declares the package from Task 2)

**Interfaces:**
- Consumes: `@retroplug/core/consoles`'s contract, `@retroplug/core/systemRoles`.
- Produces: `MD` (`ConsoleDescriptor` id `blastem.md`, platforms `[md, gen]`), `BLASTEM_ROLE` (kind `blastem`,
  category `system`), and the generated registries' entries for both.

- [ ] **Step 1: The enums and the descriptor**

`packages/blastem/ts/enums.ts`:

```ts
// The Mega Drive's settings labels, in the order the native enum takes them (BlastemMdConfig.hpp's MdRegion):
// a label is an ordinal into that list, so the two are one fact (a native test holds the array's length and order).
export const REGION_VALUES = ["auto", "ntsc-us", "ntsc-jp", "pal"] as const;
```

`packages/blastem/ts/consoles.ts`:

```ts
// The Mega Drive's descriptor (docs/mega-drive-blastem-design.md section 1): what the shell needs to classify a Mega
// Drive ROM, place the node that runs it, and wire its knobs, without naming the console. Detection is the header at
// $100, never the extension: a Master System `.bin` has no "SEGA MEGA DRIVE" there and stays Mesen's, and a `.md` file
// that is not one is refused rather than booted as noise.
import { REGION_VALUES } from "./enums";
import { label, type ConsolesContribution, type ConsoleDescriptor } from "@retroplug/core/consoles";

const MAGIC = "SEGA MEGA DRIVE ";
const MAGIC_OFFSET = 0x100;
// A "SEGA GENESIS    " header is the same console under its American name, and a Mega Drive ROM's header may name
// either; both are accepted, and the platform id is the same.
const MAGIC_GENESIS = "SEGA GENESIS    ";

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
  detect: {
    prefixBytes: 0x110,
    // Rank 2: after the NES's iNES magic and the Game Boy's logo, before the Sega 8-bit header (rank 3), so a file
    // that carries both a Mega Drive header and a Sega 8-bit one is a Mega Drive. An SMS `.bin` reaches neither.
    rank: 2,
    run: (bytes) =>
      matchesAt(bytes, MAGIC_OFFSET, MAGIC) || matchesAt(bytes, MAGIC_OFFSET, MAGIC_GENESIS) ? "md" : null,
  },
  // The header's $1B0 region: "RA" and a nonzero RAM flag byte means a battery. $1B0 alone is not enough (a cart
  // may declare RAM it does not keep), so the flag's low nibble must be one of the ROM DB's values (0, 8 or 0x10|0).
  hasBattery: (header) =>
    header.length > 0x1B2 &&
    header[0x1B0] === 0x52 && // 'R'
    header[0x1B1] === 0x41 && // 'A'
    (header[0x1B2] & 0x0F) !== 0x0F,
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
import { z } from "zod";
import { REGION_VALUES } from "./enums";

export const BLASTEM_ROLE: RoleType = {
  kind: "blastem",
  category: "system",
  schema: z.object({ region: z.enum(REGION_VALUES).default("auto") }),
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

and the manifest gains its TS members (its `native` member and dependencies are Task 2's; add the rest):

```json
  "scripts": { "typecheck": "node ../retroplug/scripts/lib/packages.mjs && tsc -p tsconfig.json" },
  "exports": {
    "./app": "./ts/app.ts",
    "./consoles": "./ts/consoles.ts",
    "./enums": "./ts/enums.ts",
    "./testFixtures": "./ts/fixtures.ts"
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

- [ ] **Step 2: Write the failing shell test**

In `packages/retroplug/test/consoles/cases.ts`, the registry's table is what classifies ROMs; add MD cases beside the
SMS ones (the file's own helper builds a byte image with a signature at an offset):

```ts
// A Mega Drive header at $100, in either of its two names, and the two negatives that matter: an SMS `.bin` (no such
// header) and a `.md` file whose header is not one.
const mdImage = (magic: string) => {
  const b = new Uint8Array(0x4000);
  for (let i = 0; i < magic.length; i++) b[0x100 + i] = magic.charCodeAt(i);
  return b;
};

test("the Mega Drive is its header, in either name", () => {
  expect(classify(mdImage("SEGA MEGA DRIVE "), ".md")).toBe("md");
  expect(classify(mdImage("SEGA GENESIS    "), ".gen")).toBe("md");
  expect(classify(mdImage("SEGA GENESIS    "), ".bin")).toBe("md");
});

test("an SMS .bin is not a Mega Drive", () => {
  // A headerless Master System image: no $100 magic, and the SMS descriptor's extension-only fallback claims it.
  expect(classify(new Uint8Array(0x4000), ".bin")).toBe("sms");
});
```

Add the MD classification cases to `recorded.ts` (the replayed matrix) with `MD`'s platform id and the battery rule's
rows, and run:

```bash
pnpm test consoles/registry
```

Expected: FAIL until the package is in the product list and its descriptor generated (`classify` returns null for a
Mega Drive image: no descriptor registers it).

- [ ] **Step 3: Wire the package in**

`package.json` (root): `"blastem"` is already in both product lists from Task 2; the generator now finds its `app` and
`consoles` members. `packages/retroplug/package.json` already declares the package. Then:

```bash
pnpm install
pnpm typecheck
pnpm test consoles/registry
pnpm test-ui
```

Expected: the first three exit 0, and the generated registry test's recorded matrix matches (re-record it with the
suite's own `RP_GOLDEN_UPDATE=1` if the file has a recorded table, as `recorded.ts`'s header says). `pnpm test-ui` must
be unchanged: a new platform adds no menu row to a project that has no system of it, so the UI's recorded matrix is
re-recorded only if its test says a row appeared.

- [ ] **Step 4: The `.bin` case the design names**

The design says "an SMS `.bin`, which has no such header, still goes to Mesen". That works only because the SMS
descriptor declares `extensionOnly` platforms, and its fallback runs only when nothing else matched. Hold it with an
explicit case in `consoles.test.ts` (Step 2's second test is that case) and with one in the *provider* test (Task 18:
an SMS `.bin` gets no `genmddj-sync` even if it carries the marker).

- [ ] **Step 5: Commit**

```bash
git add packages/blastem package.json pnpm-lock.yaml packages/retroplug/test/consoles/cases.ts \
        packages/retroplug/test/consoles/recorded.ts
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
// BUTTON_START, BUTTON_X, BUTTON_Y, BUTTON_Z, BUTTON_MODE} (io.h:100), and io_gamepad_down takes that ordering, so
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
    AudioBlockInfo info;
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

with a test seam `bool padDown(std::uint8_t button) const;` on the system (documented as such, as `MesenSmsSystem::
buttonDown` is).

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
    // The byte is the shared table's; BlastEm's is its own ordering, so the map is explicit (io.h:100).
    static constexpr std::uint8_t kBlastemButton[] = {
        DPAD_RIGHT, DPAD_LEFT, DPAD_UP, DPAD_DOWN, BUTTON_A, BUTTON_B, BUTTON_C,
        BUTTON_START, BUTTON_X, BUTTON_Y, BUTTON_Z, BUTTON_MODE,
    };
    if (button >= std::size(kBlastemButton)) return;  // a byte that names no button drives none
    // Port 0 is the player-1 pad; io_gamepad_down finds it by gamepad number.
    if (down) {
        io_gamepad_down(&gen_->io, 0, kBlastemButton[button]);
    } else {
        io_gamepad_up(&gen_->io, 0, kBlastemButton[button]);
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
pnpm test bindings
pnpm test-ui input
```

Expected: all exit 0 (no default binding moved, so the UI's input cases are untouched).

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
- Produces: `packages/blastem/ts/fixtures.ts`'s `fmPsgRom(): Uint8Array` and `syncEdgeRom(): Uint8Array` (the TS twins
  of the C++ images: same bytes, generated independently for the TS tier), and the parity group `md`.

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
    ...PSG_WRITES.slice(0, 0),
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

`packages/blastem/ts/test/fixtures.test.ts` pins the hashes to the native ones:

```ts
import { test, expect } from "@retroplug/testing";
import { fmPsgRom, psgOnlyRom, fnv1a } from "../fixtures";

test("the synthetic images are the bytes the native cases boot", () => {
  expect(fnv1a(psgOnlyRom())).toBe(0x6397d2df);
  expect(fnv1a(fmPsgRom())).toBe(0x82a7496f);
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
  const path = rig.dir() + "/md-synthetic.md";
  rig.writeFile(path, fmPsgRom());
  const id = rig.project.systems.addSystem(path);
  if (id == null) throw new Error("the Mega Drive image did not build");
  return id;
}
```

(`rig.dir()` is whatever the rig exposes for its run directory; `rig.rom` uses one. If it has no accessor, add a
`dir(): string` to `rig.ts` returning the same field.)

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
    "./app": "./ts/app.ts",
    "./mdSync": "./ts/mdSync.ts",
    "./protocol": "./ts/protocol.ts",
    "./romDetect": "./ts/romDetect.ts",
    "./romProvider": "./ts/romProvider.ts",
    "./testFixtures": "./ts/fixtures.ts"
  },
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
// genmddj's identity, from its ROM header (src/main.asm:577-600): the product code "GM GENMDDJ-00" at $180, and the
// build's own version string ("V0.27", src/main.asm:18493) somewhere in the first 32 KiB. Both, because either alone is
// a guess: a homebrew could fill the product-code field, and a version string is four kinds of other data.
//
// The gate is REQUIRED, not tidy (design section 4): the sync drives controller port 2's TR and TH, which every Mega
// Drive game reads as player 2, so attaching it to any ROM would inject phantom button presses.
const PRODUCT_CODE = "GM GENMDDJ-";
const PRODUCT_CODE_OFFSET = 0x180;
const VERSION_SCAN_LEN = 0x8000;

function matches(rom: Uint8Array, offset: number, text: string): boolean {
  if (rom.length < offset + text.length) return false;
  for (let i = 0; i < text.length; i++) if (rom[offset + i] !== text.charCodeAt(i)) return false;
  return true;
}

/** The version string ("0.27") a genmddj build carries, or null. */
export function identifyGenmddjVersion(rom: Uint8Array): string | null {
  if (!isGenmddjRom(rom)) return null;
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

/** True when `rom` is a genmddj build: its product code and one of its version strings. */
export function isGenmddjRom(rom: Uint8Array): boolean {
  if (!matches(rom, PRODUCT_CODE_OFFSET, PRODUCT_CODE)) return false;
  return identifyGenmddjVersionUnchecked(rom) !== null;
}
```

`identifyGenmddjVersionUnchecked` is the scan above without its own `isGenmddjRom` call (the two would recurse);
name it so, and keep `identifyGenmddjVersion` as the public one that gates first.

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
/** A Mega Drive image whose header is a genmddj build's, and whose body carries `version` where a build's would. */
export function genmddjHeader(version = "V0.27", productCode = "GM GENMDDJ-00"): Uint8Array {
  const rom = new Uint8Array(0x8000);
  const set = (offset: number, text: string) => {
    for (let i = 0; i < text.length; i++) rom[offset + i] = text.charCodeAt(i);
  };
  set(0x100, "SEGA MEGA DRIVE ");
  set(0x180, productCode);
  set(0x400, version);
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

test("either half alone is not genmddj", () => {
  expect(isGenmddjRom(genmddjHeader("V0.27", "GM OTHER  -00"))).toBe(false);
  const noVersion = genmddjHeader();
  noVersion.fill(0x00, 0x400, 0x500);
  expect(isGenmddjRom(noVersion)).toBe(false);
});

test("a short prefix is not genmddj rather than a throw", () => {
  expect(isGenmddjRom(new Uint8Array(0x100))).toBe(false);
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

Expected: FAIL (module not found). Then create the package files, add `"genmddj"` after `"blastem"` in both product
lists (its dependency `smsggdj` must precede it, which it does in `desktop`; in `plugin` too), add
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
// the counter is a HIGH line, which is the opposite of smsggdj's mask (its TR/TH are active low at $DD).
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
    private readonly encode: (counter: number) => number,
    private readonly mod = SMS_SYNC_COUNTER_MOD,
  ) {}
  private counter = 0;
  private walking = 0;
  /** The counter value to advance to, before a walk: the node keeps it across blocks so a mid-block walk resumes. */
  get value(): number { return this.counter; }
  set value(v: number) { this.counter = v; this.walking = v; }
  begin(): void { this.walking = this.counter; }
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
import { SMS_SYNC_COUNTER_MOD, SMS_SYNC_PPQN, SMS_SYNC_IDLE_LEVELS, counterSyncFor } from "./smsSync";

  create: ({ settings: initial }) => {
    let machine = initial.machine;
    const clock = new TickClock();
    let sync = counterSyncFor(machine);
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
        clock.walk(SMS_SYNC_PPQN, tick);
        sync.end();
      },
      onSettings(next) { machine = next.machine; sync = counterSyncFor(machine); },
    };
  },
```

Keep the `CounterSync` instance in the closure and do not rebuild it per block (its counter is the state): assign it
once in `create`, and again only on a machine change (`onSettings`). The import of `SMS_SYNC_IDLE_LEVELS` is not needed
by the node; drop it from the import line. The node's behaviour must be identical to before, which
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

genmddj's wire is the same 2-bit counter on the same lines with the opposite
polarity, so the state machine is shared and the encoder is the parameter.
genmddj's own words are in mdSync.ts; smsggdj's tests are unchanged."
```

---

### Task 20: The `genmddj-sync` role and the `rp.genmddj.sync` node

**Files:**
- Create: `packages/genmddj/ts/genmddjSyncNode.ts`, `packages/genmddj/ts/protocol.ts`, `packages/genmddj/ts/app.ts`,
  `packages/genmddj/ts/test/genmddjSyncNode.test.ts`
- Modify: `packages/genmddj/package.json` (app, protocol), `packages/retroplug/test/_featureRoles.ts`,
  `packages/retroplug/test/system-settings/registry.test.ts`, `packages/retroplug/test/systems/store-graph.test.ts`

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
// frame, with the counter surviving a stopped transport. genmddj's words are the opposite polarity to smsggdj's, so
// these cases also say the shared machine is fed the right encoder.
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

- [ ] **Step 4: The shell's lists that pin every role and node**

`packages/retroplug/test/_featureRoles.ts`: add `"genmddj-sync"` to the app-order list, after `"sms-sync"`.
`packages/retroplug/test/system-settings/registry.test.ts`: the kinds list gains `"genmddj-sync"`, and a case that a
genmddj ROM on `md` with the `blastem` role has kinds `["blastem", "genmddj-sync"]` (build the ROM with
`@retroplug/genmddj/testFixtures`'s `genmddjHeader`, as the smsggdj cases use their own fixture).
`packages/retroplug/test/systems/store-graph.test.ts`: the slotted-role table gains
`"genmddj-sync": { romPath: "/roms/genmddj.bin" }`.

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
        packages/retroplug/test/system-settings/registry.test.ts packages/retroplug/test/systems/store-graph.test.ts
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

# The version from the source's own ver_str ("V0.27" -> v0_27), as the cart's Makefile names its stamped ROM.
VER=$(sed -nE 's/^ver_str:.*"([^"]+)".*/\1/p' src/main.asm | tr 'A-Z' 'a-z' | tr -d ' ' | tr '.' '_')
if [ -z "$VER" ]; then
	echo "error: could not read ver_str from src/main.asm" >&2
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

Expected: `wrote .../roms/genmddj_v0_27.bin (...) bytes, <sha256>`. If the tools are missing, the script says which and
exits 2: that is the CI picture, and the reason the case skips.

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
  // The ROM is copied into the case's own directory first, so its .sav and its battery slot are this case's.
  const id = (project.systems.loadRom(GENMDDJ_ROM) as { system: number }).system;
  expect(project.systems.view()[0].platform).toBe("md");
  expect(project.systems.view()[0].roles.map((r) => r.kind)).toContain("genmddj-sync");
  audio.renderAudio(2000);  // boot: the cart clears RAM, sets up the VDP and draws its UI
  const ram = () => project.systems.readRam(id)!;
  // IN24 and a tempo whose 24-PPQN clocks are inside the cart's 3-per-frame cap. The option is read by the engine
  // every tick, so a poke is what the OPTIONS menu writes; genmddj's save codec is spec 2's, so no battery carries it.
  expect(project.systems.writeRam(id, OPT_SYNC - 0x10000, Uint8Array.of(SYNC_IN24))).toBe(true);
  expect(project.systems.writeRam(id, PROJ_TEMPO - 0x10000, Uint8Array.of(120))).toBe(true);
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
  // 120 BPM at 24 PPQN is 48 clocks a second: the first cleared WAIT, and the counter is where the clocks put it.
  expect(ram()[SYNC_WAIT]).toBe(0);
  expect(ram()[SYNC_LAST]).toBe(48 % 4);
  const row = ram()[PLAY_ROW];
  expect(row).toBeGreaterThan(0);           // the playhead moved: the clocks advanced the song
  audio.setTransport(false);
  audio.renderAudio(500);
  expect(ram()[SYNC_LAST]).toBe(48 % 4);    // stopped: no clock was counted, so the counter froze
  expect(ram()[PLAY_ROW]).toBe(row);        // and the playhead did not move
  project.systems.removeSystem(id);
```

`SystemsStore.readRam(id)`/`writeRam(id, offset, bytes)` are the store's own, and `readRam`'s offsets are the 68k's
addresses (Task 12), which is why `$FFD425` is `0xD425`. The case's own copy of the ROM: `rig.rom`-style copying is
`backend.copyFile`; if the store's `loadRom` on the resources path would make the battery shared with another case, copy
the file into the case's temp dir first with `be.copyFile(GENMDDJ_ROM, dir + "/genmddj_v0_27.bin")`.

- [ ] **Step 3: Run it, with and without the ROM**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native genmddj-sync
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm test:native genmddj-sync
```

Expected: with the ROM (built by Task 21), every assertion passes. Without it, the one case reports
`# SKIP` and the file exits 0.

- [ ] **Step 4: Add the skip to the baseline**

```bash
pnpm test:native --update-skip-baseline
git diff packages/retroplug/scripts/skip-baseline.json
```

Expected: one new entry, `"genmddj-sync": { "skipped": 1, "total": 1 }`. (The baseline is generated with the fixtures
absent, which is CI's worst case, so the entry is the CI reading.) If the run reports a *failure* without the ROM, the
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
- Modify: `packages/retroplug/scripts/skip-baseline.json`, `spec/08-profiling.md` (one row, if its table lists benches)

**Interfaces:**
- Consumes: the console and the port device.
- Produces: an xRT figure per case, printed, in a profile host only.

- [ ] **Step 1: The bench**

```ts
// A Mega Drive's speed on this host (design section 5, "Performance"): the synthetic image and, from the fixture tree,
// the genmddj ROM, rendered and timed through the real engine at 48 kHz. Profile hosts only (the counters are
// RETROPLUG_PROFILE's), so `pnpm test:native` sweeps it as a counted skip and `pnpm profile bench` runs it.
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
pnpm test:native md-bench                     # skip, counted
pnpm profile bench                            # the numbers, in a build-prof/ host
```

Expected: the first exits 0 with a skip; the second prints one xRT line per window. Record the synthetic figure in Task
24's spec/13 section (one number, with the host it was measured on).

- [ ] **Step 3: Baseline and commit**

```bash
pnpm test:native --update-skip-baseline
git add packages/retroplug/test-native/md-bench.test.ts packages/retroplug/scripts/skip-baseline.json
git commit -m "test(blastem): a profile-host bench for a Mega Drive's speed"
```

---

### Task 24: The docs, and the final verification

**Files:**
- Modify: `spec/00-overview.md` (the package table), `spec/06-build-test.md` (Python, the generated cores, the fixture
  script, `test:plugin`'s list, the `.bin` rule), `spec/13-subsystems.md` (a Mega Drive section), `AGENTS.md` (one
  rule line), `spec/07-remaining-work.md` (three entries)

**Interfaces:**
- Produces: the documentation the design's section 6 lists, in the same commits as the code they describe (this task
  is where the last of it lands; earlier tasks' commits carried their own spec/06 lines where they belonged).

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

- [ ] **Step 4: one AGENTS.md rule**

Add to the Workflow rules, after the `deps/mesen` line:

```
- **Every edit to the vendored `deps/blastem` is a commit on the mirror's `retroplug` branch with a `RetroPlug:` marker
  comment**, and a pointer bump is committed beside it (the mirror is `https://github.com/tommitytom/RetroPlug-BlastEm`,
  and only the user pushes it) ([Build gotchas](spec/06-build-test.md#build-gotchas)).
```

and one line to Build gotchas in spec/06: the generated `m68k.c`/`z80.c` live in the build tree only, Python 3 is a
configure requirement, and the mirror's push is the one CI dependency this branch cannot satisfy locally.

- [ ] **Step 5: spec/07's three entries**

Add three checklist lines and their detail entries (the file's own rule: open work only, each line linking its
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
git add spec/00-overview.md spec/06-build-test.md spec/07-remaining-work.md spec/13-subsystems.md AGENTS.md
git commit -F - <<'EOF'
docs(megadrive): the console, its device, its audio path, and the work spec 1 leaves

spec/00's package table, spec/06's build notes and test list, spec/13's
Mega Drive section, one AGENTS.md rule for the mirror, and spec/07's entries
for spec 2, spec 3 and the A53 measurement.
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
