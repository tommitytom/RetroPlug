# Phase E1: a portable node core and the package boundary - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** A C++ node's source builds for the RP2350 as it builds for the desktop, with no heap and no exceptions there,
and the package boundary the N8 pack (E2) and any third party's pack will sit behind is drawn. The graph libraries and
the core and DSP nodes become four workspace packages (`node-sdk`, `graph-runtime`, `core`, `dsp`), each with a manifest
in its `package.json`, and every product's node registration is generated from the packages it lists. A portable node's
settings use fixed-capacity types; `graph/node.hpp` is built two ways, the device's (`RP_DEVICE`: no description, no
JSON, settings read from msgpack into the instance's own memory) and the desktop's; and an RP2350 image built on the
Pico SDK links every portable `rp.core` and `rp.dsp` type under a link trap on the heap and on exceptions, in CI. Every
desktop suite is bit-identical and nothing is re-recorded.

**Architecture:** Per [modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md), whose section 12 ("As
built") records the build and which Task 5 lands with its status line, sections 1 to 10 corrected in place. A package
is `packages/<name>/` with a `native/` tree and a `retroplug` field in its `package.json`, which
`cmake/packages.cmake` reads with `string(JSON)`: its targets, its entry functions, and which entries build for the
device. A product (`retroplug_product`) lists its packages and gets a generated `rp_register_packages()`, which
replaces the engine's hard-coded package loop. `node.hpp` is split by file into a portable core (`node_core.hpp`, the
settings types, the field walk, the enum scan, the msgpack decode) built both ways, a device half (`node_device.hpp`)
and a desktop half (`node_desc.hpp`); a device-capable node keeps its desktop hooks inside one `#ifndef RP_DEVICE`, and
the desktop build compiles every device entry's sources in device mode too, so an unguarded hook fails on every
platform. `retroplug-device-mode-test` holds the device build to the desktop's, bit for bit, through the C ABI;
`rp-device-check` is the image, its own CMake project on the Pico SDK, run by `pnpm device:check` and CI.

**Tech stack:** C++20 (desktop GCC 13.3 and the devcontainer's `arm-none-eabi-g++` 13.2.1 with newlib and its
libstdc++), CMake 3.19's `string(JSON)` for the manifests, reflect-cpp (its compile-time field core only, on the
device), the Pico SDK 2.1.1 (no submodules needed), Catch2 for the new `retroplug-device-mode-test` and the
`[packages]`, `[settings-types]`, `[fields]` and `[enums]` cases, pnpm workspaces for the four new members, and the
TypeScript mock backend for the one refusal that moved. No submodule moves and no hardware is touched: nothing is
flashed in E1.

**Spec:** [modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md). The main design's Phase E outline is
[modular-graph-design.md](modular-graph-design.md) section 8. D3's plan, the previous one executed this way, is
[modular-graph-phase-d3-plan.md](modular-graph-phase-d3-plan.md).

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/e1-portable` in this worktree, on top of the design commit a27f53430: five commits, one per task (Task 6 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X, 32 threads). A block that creates a
file is that file as the prototype commit left it; one that changes it is the prototype's diff for it, so `git apply`
it from the step's text (or apply it hunk by hunk with the Edit tool); a move is a `git mv`, followed by the
prototype's diff of the moved file on its new path when the commit also changes it; the one deletion is a `git rm`;
the one generated file a task commits (`pnpm-lock.yaml`, Task 1) is written by its generator and checked by its blob.
The final verification ran on stage 4's tree, whose code is identical to the final tip `d4d960b4e` (stage 5 changed
docs only), and its logs are `/tmp/e1pro/s4f-*.log` with `s4f-summary.txt` listing the legs. Every expected pass, test
count and figure is quoted from the stage reports in /tmp/e1pro, from the stage logs they cite or from the design's
record, which is what the generator checks; a failing step's expected failure is what the missing code produces, as
each step says. When this plan was generated it was replayed in a scratch clone of a27f53430, every block written,
moved or applied in order, and each task committed with the command given: each commit's tree came out exactly as the
prototype's, and each commit message as its. The prototype commits, by task:

- Task 1 `364040f65`: the graph libraries and the core and DSP nodes become workspace packages.
- Task 2 `63c96599a`: fixed-capacity settings types, and the core and DSP nodes use them.
- Task 3 `f34b117a9`: a device build of node.hpp, and settings as msgpack.
- Task 4 `48f3611a9`: the RP2350 device check, with a link trap on the heap and on exceptions.
- Task 5 `d4d960b4e`: the docs. Task 6 is the acceptance run and commits nothing.

**Where this plan departs from the design as committed (a27f53430).** The design was written before the prototype;
this plan follows what was built, and Task 5 lands the design with section 12's record and sections 1 to 10 corrected.
In short:

- **Four packages, and a manifest that is an object or a list.** `packages/node-sdk` (`rp_node.h`, `node.hpp` and
  what it compiles against: the descriptor writer, canonical JSON, the graph document types, `Queue.hpp`),
  `graph-runtime` (the executor, `Plan`, `PlanStorage`, the planner, the registry, the resource table, `GraphHost`,
  `rp.host.audio-out` and the new `graph/Packages.hpp`), `core` (`CoreNodes`) and `dsp` (the kernels with their
  r8brain glue, `util/Hash.hpp`, `DspNodes` and `DspResampleNodes`), all moved by `git mv` with no `#include`
  changed. `retroplug.native` is an object or a list of them, one per `rp_package`: `dsp` holds two, `rp.dsp`
  (`device: true`) and `rp.dsp.resample` (`device: false`, r8brain), so the flag is per entry, and a boolean
  `device` is required wherever there is an entry. `node-sdk` has no entry and no flag (which half of it a build
  gets is the compile mode's choice); `graph-runtime` is `device: false`, since `rp.host.audio-out` keeps a vector
  and the device host implements its own in E3. `packages/native`'s own manifest lists the five not-yet-carved
  entries (`rp_sameboy_package`, `rp_mesen_package`, `rp_host_io_package`, `rp_n8_package`, `rp_launchpad_package`,
  target `retroplug-backend`), so one mechanism registers everything. Each entry is a one-line `extern "C"` wrapper
  beside the C++ function it calls, which OfflineJobs and the tests keep using.
- **`SpscRing` lives in `node-sdk`, not `graph-runtime`**: the N8, Launchpad, script-host and harness-surface links
  include it, and a pack depends on `node-sdk` alone. `node-sdk` gains an include-only target,
  `retroplug-node-sdk-headers`, which the no-heap executor and the Launchpad and N8 tests link instead of a raw
  path. `util/Hash.hpp` sits in `dsp` beside the dither, and `retroplug-core` (whose `SampleCache` uses it) takes
  only the kernels' include directory, not a link edge.
- **Products, `RP_DESKTOP_PACKAGES` and a configure-time check.** `cmake/packages.cmake` holds
  `retroplug_package()`, `retroplug_package_library()` (static, C++20, position-independent, `/bigobj` on MSVC),
  `retroplug_product(<product> PACKAGES ... [TARGETS ...])`, `retroplug_product_table()` and
  `retroplug_check_products()`, each with FATAL configure errors for a missing manifest field, a duplicate or
  non-identifier entry, an unknown package, a product declared twice, an empty `TARGETS` and a manifest target that
  is no target. The root names `RP_DESKTOP_PACKAGES core dsp graph-runtime native`, whose order reproduces the old
  loop (`graph-runtime` follows `dsp` only so that `rp.host` registers after `rp.dsp.resample`). Nine products carry
  it: the plugin, the host, the CLI, the SDL standalone, the Node addon (opt-in) and four test binaries. The plugin
  is one product, `retroplug`, whose targets are every executable, module or shared library DPF built from its DSP
  or UI library, read after `dpf_add_plugin`, so macOS's `retroplug-export` is one and a static format is refused.
  `Engine.cpp` references `rp_register_packages()` and each product's own object defines it; an executable that
  pulls the engine in without being a product fails to link, and since a Linux module would fail only when loaded,
  `retroplug_check_products` refuses at configure time any binary that links the engine's libraries and is no
  product (`retroplug-classid-test` and `-watcher-test` never pull `Engine.o` in and are exempt). The offline pool
  keeps its own `core` and `dsp` subset and the script pack its own path.
- **The `[packages]` golden** (`golden/product-registrations.json`, 26 ids per product, nine rows) was recorded from
  a throwaway program at the design commit that printed the old hard-coded list's ids, not from the new code, so it
  cannot agree with itself by construction. A golden row with no product fails, but for the opt-in Node addon's.
- **Fixed-capacity settings.** `rp::text<N>` (inline bytes and a length, an implicit `string_view`, a consteval
  literal constructor, so a default that does not fit fails to compile) and `rp::list<T, N>` (an inline array, a
  consteval initializer list, trivially copyable elements only, a length byte up to 255 and a `uint16` above) read
  and write the JSON and schema of the `std::string` and `std::vector` they replace, and refuse an over-long value
  naming the field. `join` and `split` hold `rp::list<std::uint32_t, kMaxPorts>`, every `layout`, `from` and `to` an
  `rp::text<kMaxLayoutName>`: the longest layout any table accepts is `discrete(64)`, 12 bytes, and `kMaxLayoutName`
  is 16, beside `kMaxChannels`. `resource_ref` stores its id as a number: a malformed `"res:007"` is refused at the
  settings parse (`settings-invalid`), where it failed at create (`create-failed`), in the same words, and the
  TypeScript mock backend refuses it first the same way. No descriptor golden moves.
- **`node.hpp` built two ways, split by file.** The author still includes `graph/node.hpp`; under `RP_DEVICE` the
  SDK is `node_core.hpp` (the wrappers, `Block`, the reflection of `inputs`, `outputs` and `params`, the hook
  checks, `ResourceHold`, the block-path entries) and `node_device.hpp` (`DeviceTypeCheck`, the in-place instance,
  the msgpack-only read, a vtable with null `prepare`, `commit` and `release`), and on the desktop `node_desc.hpp`
  (the descriptor, `layout_for`, the JSON parse, `validate`, the schema, the heap-settings instance). Both share
  `fields.hpp`, `enums.hpp`, `settings_types.hpp`, `settings_msgpack.hpp`, `msgpack_reader.hpp`, `PortTypes.hpp`,
  `ResourceId.hpp` and `sdk_namespace.hpp`; `settings_json.hpp` holds the desktop's reflect-cpp parsers. The param
  descriptor members became traits (`ParamDescOf`), so the wrappers carry no `#if`. The field walk is reflect-cpp's
  internal compile-time core with our own copy of its name extraction (its public `to_view` includes heap headers);
  `rfl::Rename` and `rfl::Flatten` fields are refused. Under libstdc++ the device build `#error`s when a heap or
  exception header came in before `node.hpp`.
- **One `#ifndef RP_DEVICE` around a device-capable node's desktop hooks**, by the user's decision (the design
  promised a node "written exactly as now"; `validate` returns `std::optional<std::string>` and `adjust_layout`
  takes a `graph::Layout`, desktop types the node's own source names). Three things enforce it: `Layout` is only
  forward-declared in device mode, `DeviceTypeCheck` `static_assert`s that no `validate` or `adjust_layout` is
  visible, and `_retroplug_device_check` compiles every target of a `device: true` manifest entry a second time in
  device mode (`retroplug-core-nodes-device`, `retroplug-dsp-nodes-device`: object libraries in the default build
  that nothing links), so an unguarded hook fails the desktop build and CI on every compiler. `rp::choice` replaces
  `rfl::Literal` (`AddKind`, `BiquadType`, `DitherAlgorithm`): one byte, `Literal`'s interface, and a desktop parser
  that delegates to the same `Literal`, so the JSON, the refusals and the schema are identical.
- **Enums on the device, with limits.** `enums.hpp` scans an enum's enumerators as enchantum does, in -256 to 256
  (clipped to the underlying type, 64-bit unsigned types included), and reads a name or a number in a string as
  reflect-cpp does. An enum needs a fixed underlying type and no `operator|`: an unscoped enum without one and a
  flag enum are refused at compile time, and `enumMatchesReflect` is `static_assert`ed for every enum a portable
  `Settings` holds and every `enum_param`, so an `rfl::config::enum_range<E>` override fails there too. That narrows
  `enum_param` on the desktop as well; a custom `rfl::Reflector<E>` is a rule (no node has one), not a refusal. Each
  portable enum costs about 55 to 95 ms of compile time per translation unit.
- **`RP_SETTINGS_MSGPACK` and our own reader.** `rp_settings.encoding` 1 is accepted on both sides: the desktop's
  `query_memory` and `create` read either encoding for a type whose `Settings` is portable, so the device's decode
  runs on the desktop; `rp.host.audio-out`, the emulators and script nodes read JSON alone. The reader is ours,
  about 530 lines with no allocation and no exception, because reflect-cpp's needs msgpack-c (not in the tree),
  unpacks into a malloc'd zone, reads `<istream>` and reports through `std::string`. It matches fields by name,
  keeps a missing field's default, steps over an unknown one and refuses in the JSON reader's words.
  `settingsToMsgpack(json)` in `node-sdk` writes each number as the JSON spells it, so a blob refuses what the JSON
  refuses (`{"u32": 2.0}`). A `resource_ref` travels in msgpack as its `"res:N"` string.
- **The device instance** counts its `Settings` in `query_memory` and constructs pins, settings and node in the
  host's memory, destroyed in reverse; a type with no resource field holds no pin storage. `node_type<T>()` is
  constexpr and `peer_as` compares with `&kVtable<T>`, so a package's `kTypes[]` is constant-initialised and a
  device object needs no `__cxa_guard_acquire`. The dither's `<random>` (heap-free, but it includes `<string>` and
  `<vector>`) is replaced by `rp::dsp::Mt19937`, the same engine, constexpr, checked against the standard's 10000th
  draw at compile time and against `std::mt19937` over 70 seeds; the kit goldens and parity do not move.
- **Both builds in one test binary.** Catch2 is one library built with exceptions, so the device-mode sources are an
  object library (`retroplug-device-mode-objects`, `RP_DEVICE`, `-fno-exceptions -fno-rtti`; MSVC keeps its `/EHsc`)
  reached only through the C ABI. In device mode the SDK lives in the inline namespace `rp::device`, declared before
  any header opens `rp::graph`, so a node's own `namespace rp::graph` lands in `rp::device::graph` and the two
  builds link with no ODR clash; the packages' `extern "C"` entries are renamed `<entry>_device_mode` by compile
  definitions. At every non-MSVC configure three `try_compile`s require `HeapType`, `PointerSettings` and
  `HeapHeader` to fail in device mode with their own reasons on the compiler's `error:` lines; `HeapHeader` runs
  only under libstdc++, whose include guards the heap-header `#error` reads.
- **The device build.** `rp-device-check` (`packages/graph-runtime/device-check/`) is a Pico SDK project of its own
  (`RP_DEVICE_BUILD`, `PICO_BOARD` pico2, `build-device/`), a DEVICE product of `device-check/packages.cmake`'s list
  (`core dsp`), which its case generator includes too. A DEVICE product takes the `device` entries alone (a listed
  package with none is refused) and fills its registration per call, so no guard. The cases come from
  `rp-device-check-cases`, a desktop DEVICE product, which reads each type's defaults through a new desktop SDK
  interface, `rp.sdk.settings-json/1` (the ABI had no way to get them and the descriptor schema has none), converts
  them with `settingsToMsgpack`, completes `rp.dsp.normalize` with `"buffer": "res:1"` and writes
  `DeviceCheckCases.hpp`, a build artifact never committed; the image refuses at compile time a header made for
  other entries. reflect-cpp's pin is read from rpcpp's `CPMAddPackage` line (0.26.2). No UF2 (the ELF is the
  artifact); Release by default, so the Executor's asserts compile out; a Debug image links too.
- **The runtime keeps stdio; only `pico_malloc` leaves.** `rp-device-runtime` and `rp-device-stdlib` are derived at
  configure from `pico_runtime` and `pico_stdlib` less `pico_malloc` (the configure stops if the name is gone).
  `pico_malloc` defines the wrapped malloc family, so it has to go; stdio over the UART and the SDK's printf link
  under the full trap, since the SDK wraps `printf` and `puts` to its own heap-free code. The image prints one
  result line on the UART.
- **The link trap: 78 symbols and a probe on every run.** One `--wrap` per symbol (a comma-joined list is split into
  input files): the malloc family with newlib's `_r`, aligned and `memalign` forms, `sbrk`, the `__cxa_guard_*`
  entries, and every `operator new` and `delete`, `__cxa_` throw entry and `std::__throw_*` the device multilib's
  `libstdc++.a` defines (read with `nm` at configure, which stops if an expected one is missing); `check-trap.cmake`
  fails the build if the image defines a `__wrap_` symbol. The package libraries compile with the SDK's
  code-generation flags, so the trap names what the image keeps. `rp-device-trap-probe` (`new int(1)`,
  `std::malloc(16)`) must fail to link naming both, on every run of `tools/build-device-check.sh` (`pnpm
  device:check`), which also prints the sizes by owner.
- **The sizes.** Release: 65,156 bytes of flash, of which `core`, `dsp` and the executor are 41,857 (26,469 at -Os),
  with no static RAM: their RAM is per instance, from `query_memory`. The check's static 400 KiB arena is the bss.
  CI's linux-x64 job installs `gcc-arm-none-eabi` with newlib and its libstdc++, clones the Pico SDK 2.1.1 with no
  submodules and runs `pnpm device:check`; the image is built, never run.
- **Left for E3, and owed.** `rp.core.delay`'s default needs 384 KB per instance (`maxSamples` 48,000, stereo;
  384,104 bytes on x86-64), about 74% of the RP2350's 520 KB, and sizes the arena: E3 needs a device default or a
  planner cap, and measures the device's stack in `query_memory` and `create` (the `Settings` plus up to three
  264-byte error buffers) and the instance sizes on ARM, which no run measured. Windows has not built E1: the
  `-device` object libraries are in the default build under `/W4 /WX`, so a `build.bat` and `test.bat` run is owed
  (the risks: the inline-namespace reopen, consteval in default member initialisers, `enums.hpp`'s variable
  templates, `__FUNCSIG__` in a consteval function, the copied `/bigobj`, `std::strcpy` in `DeviceCheck.cpp`). macOS
  (AppleClang with libc++) and CI's arm64 Linux job have not built E1 either: their first run is CI's, on the user's
  push. On macOS the default build compiles the `-device` object libraries and the device-mode objects with Apple
  clang and libc++, and the configure runs the refused-source checks; under libc++ two of the three are meant to run
  (`HeapHeader` reads libstdc++'s include guards and is skipped), but no libc++ build has run them, and the
  prototype's clang was Linux clang 18 with libstdc++. Task 6 states all three runs as owed, not runnable here.
- **Smaller departures, and a residual.** The committed design had a manifest name its TypeScript entry; no E1
  package has a TypeScript part, so no manifest has a `ts` key, and the corrected section 4 says one will name the
  entry when a package has one (nothing reads it yet). The committed design started on-device runs at E2 or E3; as
  built, E3 is the first phase to run the check's successor on the rig, with plans the box loads (E2 runs the
  firmware with the pack's Edio, but no graph node). The trap probe's check in `tools/build-device-check.sh` and the
  lines quoted from it assume GNU ld's C-locale quoting (`undefined reference to `...'`); a binutils locale that
  prints curly quotes would make the script fail although the trap held. This devcontainer (`LANG=C.UTF-8`), the
  `env -i` run and CI are all C or C.UTF-8, so nothing here is affected; running the probe under `LC_ALL=C` is left
  for spec/07 or E2.
- **What no log backs.** No stage ran the Reaper suite or `pnpm validate`; Task 6 asks for both. The red runs (Task
  2's compile and mock failures, Task 3's configure stop) are predicted from the code at each step. Of the
  mutations, the product check's error, the refused source's flip and the decode's scaling were the stages' own
  runs; the registration order, the unguarded hook and the device-check list are this plan's, their failures
  predicted from the tests and the wording the stages logged for the same checks. Each step says which. No parity
  baseline, kit golden, render baseline, descriptor golden, `buildGraph` golden or skip baseline is re-recorded by
  any task: the one new golden (Task 1) is quoted whole, and the one generated file committed (`pnpm-lock.yaml`) is
  proved by its blob.


**Ground rules for whoever executes this:**
- This plan lives at `docs/modular-graph-phase-e1-plan.md` and is committed before Task 1 (a docs commit of its own):
  the header's links are relative to that location; every command and path in the steps is relative to the worktree
  root.
- Commit after each task (the commands are given; Task 6 commits nothing unless a step fails and is fixed, in a commit
  of its own). No `Co-Authored-By` or "generated with" trailer. Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context lines of older code, and in Task 5 one removed line of
  `AGENTS.md`): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the session
  itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with uncommitted
  work in the tree. Never touch a submodule. `deps/sameboy` shows as modified after any configure (the two tracked
  patches): expected, leave it. `build-device/` (Task 4 on) is a scratch build dir the device check owns and
  `.gitignore` lists; removing it is fine.
- The derived files are never committed: the embedded bundle C arrays (`build/native/*bundle_data.c`), the products'
  generated registration sources (`build/packages/products/`), the device-mode test's `device-entries.cpp` and the
  device check's `DeviceCheckCases.hpp` (`build/device-check/`, or the device build dir's `cases/`).
- **Parity holds the engine's output to the bit, and no task re-records anything.** Never set `RP_PARITY_UPDATE`,
  `RP_KIT_GOLDEN_UPDATE`, `RP_RENDER_GOLDEN_UPDATE` or `RP_GOLDEN_UPDATE`, and never pass `--update-skip-baseline`: a
  golden that moves means the step is wrong. **The one new golden is Task 1's `product-registrations.json`**, quoted
  whole below: it is the oracle, recorded from the code before the move. **The one generated file a task commits is
  Task 1's `pnpm-lock.yaml`**, written by `pnpm install --offline`; its step proves the blob.
- **A move is a `git mv`, never a copy and a delete**, so history follows the file: the step prints the commands, and
  where the commit also changes a moved file its diff follows the move, on the new path. Until Task 1's CMake step
  lands, the tree between the moves and that step does not configure; the steps say when to build.
- Every whole-file block below (a `Create`) is followed by the `sed -n` range that extracts it from this plan and by the
  blob hash the file must have: write or extract the file, then `git hash-object <path>` must print the hash the step
  quotes, which is the prototype commit's blob. A retyped slip fails there rather than in a test that never reads the
  line. One file is executable (`tools/build-device-check.sh`): its step sets the bit.
- Every `check:docs` count below is 79: the reports record 78, and this plan is the 79th tracked Markdown file
  `check-doc-links` walks.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the same command line; without it their
  cases skip. A shell `export` does not survive between commands. `pnpm test:native` takes ONE filter per run (an
  exact slug like `render-baseline`, or a dash-form prefix; `risa-render` runs its three files). The CI views pass
  `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa`, because without them the runners
  fall back to `../resources`, which exists beside this worktree and holds some fixtures.
- The Catch2 binaries, `retroplug-render-host-test` and `rp-device-check-cases` are `EXCLUDE_FROM_ALL`: `cmake --build
  build` does not relink them, so a library edit needs the target built by name before a run. A new or removed source
  needs `cmake -S . -B build` first. The `-device` object libraries (Task 3 on) are in the default build.
- The sanitizer trees are their own: `build-tsan/` and `build-asan/`. Configure one only if its `CMakeCache.txt` is
  missing (`cmake -S . -B build-tsan -DRETROPLUG_SANITIZE=thread`, or `build-asan` with `=address`); a build there
  re-runs CMake itself when a listed file changed, and finds the moved files. Build the binary in that tree before
  running it there; a stale prototype binary proves nothing.
- Case counts are exact. A `test cases: N | P passed | S skipped` line, an `All tests passed (A assertions in N test
  cases)` line and a per-file count must match the quoted N/P/S (Catch2 pads the `test cases:` line's columns with
  spaces, which the quotes below collapse to one). Assertion totals are exact for
  `retroplug-graph-test`, `retroplug-audio-test`, `retroplug-device-mode-test`, the launchpad, n8 and serial binaries
  and the small six. **A run that includes `[engine-jobs]` or `[capability]`, and so any whole engine-graph run,
  reports an assertion total that moves from run to run** (the engine-graph binary with the fixtures gave 1712306,
  1712421, 1712608 and 1713095 across the four code stages, none of which changed a case it runs): for those the case
  count must match and the total is the prototype's, quoted for scale.
- The prototype branch `proto/e1-portable` is this plan's provenance, and no step reads it: every byte check is a blob
  hash quoted in the step.
- The device steps (Task 4 on) need `arm-none-eabi-g++` (13.2.1 here) and a Pico SDK: `tools/build-device-check.sh`
  uses `PICO_SDK_PATH` when it is set (this devcontainer sets `/opt/pico-sdk`, an SDK with no submodules, which is all
  the check needs) and `/workspaces/pico-sdk` when it is not; a `PICO_SDK_PATH` that is set and is not an SDK stops
  the run. The CI dry runs clone the SDK's 2.1.1 tag from GitHub.
- No step touches the NES, the N8 Pro or any serial port: nothing is flashed in E1.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run one Reaper invocation
  at a time (`pnpm reaper:all`'s own fan-out is fine), with nothing else using Reaper or jackd meanwhile, and before
  each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper; pgrep -a jackd`).
- Track progress outside the committed plan: never tick its checkboxes. Ticking one modifies
  `docs/modular-graph-phase-e1-plan.md`, and every task's expected `git status` list would then gain a line it does
  not show.
- The Windows build has not seen this tree (this plan's prototype is local and Linux-only), so an MSVC-only warning
  is a real finding to report, not a stale quote; no step below builds on Windows, and Task 6 says what the owed
  Windows run is. macOS (AppleClang, libc++) and the arm64 Linux CI job have not built it either; their first run is
  CI's, on the user's push.
- **When a step's result differs from its Expected, stop and report.** A different exit status, case count or quoted
  text is a finding, with two exceptions: an assertion total the rule above calls moving, and a predicted failure
  whose failing file, case or target is the one named but whose compiler wording differs. Never edit code, a test or
  a golden to make a step pass. In Tasks 1 to 5 each task's tree must equal its prototype commit, so a real defect
  found there is reported, not fixed; a fix belongs to a commit of its own after the plan.

**Commit this plan before Task 1.** Before the commit, check where the tree is: `git rev-parse --abbrev-ref HEAD` must
print `feature/modularize`, `git rev-parse --short=8 HEAD` must print `a27f5343`, and `git status --short` must show
only ` m deps/sameboy`. If any of the three differs, stop: every step below assumes the design commit on a clean tree.
(`git -C deps/sameboy status --short` shows the five files the two tracked SameBoy patches change: `Core/apu.c`,
`Core/apu.h`, `Core/camera.c`, `Core/random.c` and `Core/sgb.c`.) The run below copies the plan to its home in the
repository and commits it on its own, so every later task's `git status` and `git add` see a clean tree:

````bash
cp /tmp/e1plan/plan.md docs/modular-graph-phase-e1-plan.md
git add -- docs/modular-graph-phase-e1-plan.md
git commit -F - <<'EOF'
docs: the E1 implementation plan

The plan for Phase E1, a portable node core and the package boundary, generated from the prototype's five commits on
proto/e1-portable: the graph libraries and the core and DSP nodes as workspace packages with generated per-product
registration, fixed-capacity settings, the device build of node.hpp with msgpack settings, the RP2350 device check
with its link trap, and the docs. Every expected count is quoted from a stage report, a stage log or the design's
record, and the code was replayed commit by commit against the prototype's trees.
EOF
````

---

## File structure

| Path | Responsibility |
|---|---|
| `cmake/packages.cmake` | the manifests, products, generated registration and the product check (Task 1); the `<target>-device` checks (Task 3); DEVICE products and the device build mode (Task 4) |
| `packages/node-sdk/` | `rp_node.h`, `node.hpp` and the description library, `SpscRing` (Task 1); `resource_ref`'s id, `settings_types.hpp` (Task 2); the split headers, the msgpack decode and the converter (Task 3); `rp.sdk.settings-json/1` and the device build's header-only SDK (Task 4) |
| `packages/graph-runtime/native/` | the executor, planner, registry, `GraphHost`, `rp.host.audio-out` and `graph/Packages.hpp` (Task 1); the executor alone in a device build (Task 4) |
| `packages/graph-runtime/device-check/` | `rp-device-check`, its portable run, its case generator, the trap and its probe (Task 4) |
| `packages/core/`, `packages/dsp/` | the `rp.core` nodes; the kernels and the `rp.dsp` nodes (Task 1); their settings types (Task 2); their device-mode guards, `rp::choice`, `Mt19937` (Task 3); the device build's targets (Task 4) |
| `packages/native/package.json`, `src/host/graph/` | the five not-yet-carved entries (Task 1) |
| `CMakeLists.txt`, `packages/native/CMakeLists.txt`, `packages/native/cmake/` | the packages, the products and the check (Task 1); the device-mode test and the refused sources (Task 3); the case generator and the device-check cases (Task 4) |
| `packages/native/test/engine-graph/` | `[packages]` and its golden (Task 1) |
| `packages/native/test/graph/` | `[settings-types]` (Task 2), `[fields]`, `[enums]` and the `Mt19937` sweep (Task 3) |
| `packages/native/test/device-mode/` | `retroplug-device-mode-test` and the refused sources (Task 3), `[device-check]` (Task 4) |
| `packages/retroplug/testing/mockBackend.ts`, `test/meta.test.ts` | the malformed ref refused first (Task 2) |
| `tools/build-device-check.sh`, `package.json`, `.github/workflows/build.yml`, `.gitignore` | `pnpm device:check` and the CI step (Task 4); the eleventh `test:plugin` binary (Task 3) |
| `pnpm-lock.yaml` | the four new workspace members (Task 1) |
| `docs/`, `spec/`, `AGENTS.md` | the links to moved files (Task 1); the design's as-built record and the live docs (Task 5) |

---

### Task 1: the graph libraries and the core and DSP nodes become workspace packages

Design sections 4 and 9's first commit. The node SDK, the graph runtime, the `rp.core` nodes and the DSP kernels with
the `rp.dsp` nodes move into `packages/node-sdk`, `graph-runtime`, `core` and `dsp`, each a pnpm workspace member whose
`package.json` names its native target, its entry functions and which of them build for the device. Every file keeps
the path it is included by under its package's `native/src`, so no `#include` changes. `cmake/packages.cmake` reads
the manifests and generates each product's `rp_register_packages()`, which replaces the engine's hard-coded package
loop, and the configure refuses any binary that links the engine and is no product. A golden holds every product to
the 26 type ids the old loop registered. Nothing else changes: every suite runs at the baseline's counts plus the two
`[packages]` cases, and only the doc links to moved files are fixed (the prose about the packages is Task 5's).

The golden is written first and is the oracle: the stage recorded it at the design commit with a throwaway program
that linked the nine package functions of the old loop and printed each package's type ids, the same 26 for every
product, so it holds what the engine registered before anything moved and cannot agree with the new code by
construction. The moves come next as `git mv` commands, then the entries, the manifests and the CMake, and only the
CMake step makes the tree configure again.

**Files:**

- Create: `cmake/packages.cmake`, `packages/core/native/CMakeLists.txt`, `packages/core/package.json`, `packages/dsp/native/CMakeLists.txt`, `packages/dsp/package.json`, `packages/graph-runtime/native/CMakeLists.txt`, `packages/graph-runtime/native/src/graph/Packages.hpp`, `packages/graph-runtime/package.json`, `packages/native/test/engine-graph/Packages.test.cpp`, `packages/native/test/engine-graph/golden/product-registrations.json`, `packages/node-sdk/native/CMakeLists.txt`, `packages/node-sdk/package.json`
- Move (44 files, each by the `git mv` Step 2 prints; 8 of them changed too, `packages/core/native/src/graph/nodes/CoreNodes.cpp`, `packages/core/native/src/graph/nodes/CoreNodes.hpp`, `packages/dsp/native/src/graph/nodes/DspNodes.cpp`, `packages/dsp/native/src/graph/nodes/DspNodes.hpp`, `packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp`, `packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp`, `packages/graph-runtime/native/src/graph/HostNodes.cpp`, `packages/graph-runtime/native/src/graph/HostNodes.hpp`): from `packages/native/src/dsp/`, `packages/native/src/graph/`, `packages/native/src/graph/nodes/`, `packages/native/src/transport/`, `packages/native/src/util/`
- Modify: `CMakeLists.txt`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-b3a-design.md`, `docs/modular-graph-phase-b3b-design.md`, `docs/modular-graph-phase-c1-design.md`, `docs/modular-graph-phase-c3-design.md`, `packages/native/CMakeLists.txt`, `packages/native/cmake/node-addon.cmake`, `packages/native/cmake/plugin.cmake`, `packages/native/cmake/test-binary.cmake`, `packages/native/cmake/tests.cmake`, `packages/native/cmake/warnings.cmake`, `packages/native/package.json`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/graph/HostIoNodes.cpp`, `packages/native/src/host/graph/HostIoNodes.hpp`, `packages/native/src/host/graph/LaunchpadNodes.cpp`, `packages/native/src/host/graph/LaunchpadNodes.hpp`, `packages/native/src/host/graph/N8Nodes.cpp`, `packages/native/src/host/graph/N8Nodes.hpp`, `packages/native/src/host/graph/SystemNodes.cpp`, `packages/native/src/host/graph/SystemNodes.hpp`, `packages/retroplug/src/graph/canonicalJson.ts`, `packages/retroplug/src/graph/defineNode.ts`, `packages/retroplug/src/graph/descriptor.ts`, `packages/retroplug/src/graph/graphDocument.ts`, `packages/retroplug/src/graph/systemResources.ts`, `pnpm-lock.yaml`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`
- Delete: `packages/native/cmake/graph.cmake`

- [ ] **Step 1: The registration golden, recorded before the move**

Nine rows, one per product this task declares (the Node addon's among them, though a default configure does not build
it), each the 26 node type ids `Engine.cpp`'s loop registered at the design commit, in that order: `rp.core`'s ten,
`rp.dsp`'s five, `rp.dsp.resample`, `rp.host.audio-out`, then the types still in `packages/native` (`rp.sameboy.gb`,
`rp.mesen`'s three, `rp.host.io`'s two, `rp.n8.nes` and the Launchpad's two). It is a new file, not a re-recording,
and no step below writes it again.

Create `packages/native/test/engine-graph/golden/product-registrations.json`:

````json
{
  "retroplug": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-audio-test": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-cli": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-engine-graph-test": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-host": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-node": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-render-host-test": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-sdl": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ],
  "retroplug-ui-test": [
    "rp.core.add",
    "rp.core.merge",
    "rp.core.join",
    "rp.core.split",
    "rp.core.downmix",
    "rp.core.relabel",
    "rp.core.delay",
    "rp.core.constant",
    "rp.core.buffer-player",
    "rp.core.buffer-recorder",
    "rp.dsp.gain",
    "rp.dsp.biquad",
    "rp.dsp.dither",
    "rp.dsp.normalize",
    "rp.dsp.dpcm",
    "rp.dsp.resample",
    "rp.host.audio-out",
    "rp.sameboy.gb",
    "rp.mesen.nes",
    "rp.mesen.gba",
    "rp.mesen.sms",
    "rp.host.midi-in",
    "rp.host.midi-out",
    "rp.n8.nes",
    "rp.launchpad.surface-in",
    "rp.launchpad.surface-out"
  ]
}
````

Run: `mkdir -p packages/native/test/engine-graph/golden; sed -n '370,623p' docs/modular-graph-phase-e1-plan.md > packages/native/test/engine-graph/golden/product-registrations.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/engine-graph/golden/product-registrations.json`
Expected: `9ae829f654c203df76f54a4425f47448363c4557`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Run: `python3 -c "import json; g = json.load(open('packages/native/test/engine-graph/golden/product-registrations.json')); print(len(g), sorted({len(v) for v in g.values()}), len({tuple(v) for v in g.values()}))"`
Expected: `9 [26] 1`: nine products, 26 ids each, one list for all of them.

- [ ] **Step 2: Move the graph libraries and the kernels with `git mv`**

44 files move: `packages/native/src/graph/` (the SDK half to `node-sdk`, the runtime half to `graph-runtime`),
`src/graph/nodes/` (to `core` and `dsp`), `src/dsp/` and `src/util/Hash.hpp` (to `dsp`) and
`src/transport/SpscRing.hpp` (to `node-sdk`). Each keeps its path below its package's `native/src`. The directories
come first, as `git mv` needs its destination's. Nothing configures until Step 5 points the CMake at the new places,
so build nothing in between.

The moves, as one script (each `git mv` stages its move):

````bash
mkdir -p packages/core/native/src/graph/nodes packages/dsp/native/src/dsp packages/dsp/native/src/graph/nodes \
  packages/dsp/native/src/util packages/graph-runtime/native/src/graph packages/node-sdk/native/src/graph \
  packages/node-sdk/native/src/transport
git mv packages/native/src/graph/nodes/CoreNodes.cpp packages/core/native/src/graph/nodes/CoreNodes.cpp
git mv packages/native/src/graph/nodes/CoreNodes.hpp packages/core/native/src/graph/nodes/CoreNodes.hpp
git mv packages/native/src/dsp/Biquad.cpp packages/dsp/native/src/dsp/Biquad.cpp
git mv packages/native/src/dsp/Biquad.hpp packages/dsp/native/src/dsp/Biquad.hpp
git mv packages/native/src/dsp/Dither.cpp packages/dsp/native/src/dsp/Dither.cpp
git mv packages/native/src/dsp/Dither.hpp packages/dsp/native/src/dsp/Dither.hpp
git mv packages/native/src/dsp/Dpcm.cpp packages/dsp/native/src/dsp/Dpcm.cpp
git mv packages/native/src/dsp/Dpcm.hpp packages/dsp/native/src/dsp/Dpcm.hpp
git mv packages/native/src/dsp/Gain.cpp packages/dsp/native/src/dsp/Gain.cpp
git mv packages/native/src/dsp/Gain.hpp packages/dsp/native/src/dsp/Gain.hpp
git mv packages/native/src/dsp/R8bAllocator.hpp packages/dsp/native/src/dsp/R8bAllocator.hpp
git mv packages/native/src/dsp/R8bBase.cpp packages/dsp/native/src/dsp/R8bBase.cpp
git mv packages/native/src/dsp/Resample.cpp packages/dsp/native/src/dsp/Resample.cpp
git mv packages/native/src/dsp/Resample.hpp packages/dsp/native/src/dsp/Resample.hpp
git mv packages/native/src/graph/nodes/DspNodes.cpp packages/dsp/native/src/graph/nodes/DspNodes.cpp
git mv packages/native/src/graph/nodes/DspNodes.hpp packages/dsp/native/src/graph/nodes/DspNodes.hpp
git mv packages/native/src/graph/nodes/DspResampleNodes.cpp packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
git mv packages/native/src/graph/nodes/DspResampleNodes.hpp packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp
git mv packages/native/src/util/Hash.hpp packages/dsp/native/src/util/Hash.hpp
git mv packages/native/src/graph/Executor.cpp packages/graph-runtime/native/src/graph/Executor.cpp
git mv packages/native/src/graph/Executor.hpp packages/graph-runtime/native/src/graph/Executor.hpp
git mv packages/native/src/graph/GraphHost.cpp packages/graph-runtime/native/src/graph/GraphHost.cpp
git mv packages/native/src/graph/GraphHost.hpp packages/graph-runtime/native/src/graph/GraphHost.hpp
git mv packages/native/src/graph/HostNodes.cpp packages/graph-runtime/native/src/graph/HostNodes.cpp
git mv packages/native/src/graph/HostNodes.hpp packages/graph-runtime/native/src/graph/HostNodes.hpp
git mv packages/native/src/graph/NodeRegistry.cpp packages/graph-runtime/native/src/graph/NodeRegistry.cpp
git mv packages/native/src/graph/NodeRegistry.hpp packages/graph-runtime/native/src/graph/NodeRegistry.hpp
git mv packages/native/src/graph/Plan.hpp packages/graph-runtime/native/src/graph/Plan.hpp
git mv packages/native/src/graph/PlanStorage.cpp packages/graph-runtime/native/src/graph/PlanStorage.cpp
git mv packages/native/src/graph/PlanStorage.hpp packages/graph-runtime/native/src/graph/PlanStorage.hpp
git mv packages/native/src/graph/Planner.cpp packages/graph-runtime/native/src/graph/Planner.cpp
git mv packages/native/src/graph/Planner.hpp packages/graph-runtime/native/src/graph/Planner.hpp
git mv packages/native/src/graph/ResourceTable.cpp packages/graph-runtime/native/src/graph/ResourceTable.cpp
git mv packages/native/src/graph/ResourceTable.hpp packages/graph-runtime/native/src/graph/ResourceTable.hpp
git mv packages/native/src/graph/CanonicalJson.cpp packages/node-sdk/native/src/graph/CanonicalJson.cpp
git mv packages/native/src/graph/CanonicalJson.hpp packages/node-sdk/native/src/graph/CanonicalJson.hpp
git mv packages/native/src/graph/Descriptor.cpp packages/node-sdk/native/src/graph/Descriptor.cpp
git mv packages/native/src/graph/Descriptor.hpp packages/node-sdk/native/src/graph/Descriptor.hpp
git mv packages/native/src/graph/GraphDocument.cpp packages/node-sdk/native/src/graph/GraphDocument.cpp
git mv packages/native/src/graph/GraphDocument.hpp packages/node-sdk/native/src/graph/GraphDocument.hpp
git mv packages/native/src/graph/Queue.hpp packages/node-sdk/native/src/graph/Queue.hpp
git mv packages/native/src/graph/node.hpp packages/node-sdk/native/src/graph/node.hpp
git mv packages/native/src/graph/rp_node.h packages/node-sdk/native/src/graph/rp_node.h
git mv packages/native/src/transport/SpscRing.hpp packages/node-sdk/native/src/transport/SpscRing.hpp
````

Run: `git status --short --untracked-files=all`
Expected: the moves staged, each under its new path, with Step 1's golden untracked; these lines, in this order, and nothing else:

````
 m deps/sameboy
R  packages/native/src/graph/nodes/CoreNodes.cpp -> packages/core/native/src/graph/nodes/CoreNodes.cpp
R  packages/native/src/graph/nodes/CoreNodes.hpp -> packages/core/native/src/graph/nodes/CoreNodes.hpp
R  packages/native/src/dsp/Biquad.cpp -> packages/dsp/native/src/dsp/Biquad.cpp
R  packages/native/src/dsp/Biquad.hpp -> packages/dsp/native/src/dsp/Biquad.hpp
R  packages/native/src/dsp/Dither.cpp -> packages/dsp/native/src/dsp/Dither.cpp
R  packages/native/src/dsp/Dither.hpp -> packages/dsp/native/src/dsp/Dither.hpp
R  packages/native/src/dsp/Dpcm.cpp -> packages/dsp/native/src/dsp/Dpcm.cpp
R  packages/native/src/dsp/Dpcm.hpp -> packages/dsp/native/src/dsp/Dpcm.hpp
R  packages/native/src/dsp/Gain.cpp -> packages/dsp/native/src/dsp/Gain.cpp
R  packages/native/src/dsp/Gain.hpp -> packages/dsp/native/src/dsp/Gain.hpp
R  packages/native/src/dsp/R8bAllocator.hpp -> packages/dsp/native/src/dsp/R8bAllocator.hpp
R  packages/native/src/dsp/R8bBase.cpp -> packages/dsp/native/src/dsp/R8bBase.cpp
R  packages/native/src/dsp/Resample.cpp -> packages/dsp/native/src/dsp/Resample.cpp
R  packages/native/src/dsp/Resample.hpp -> packages/dsp/native/src/dsp/Resample.hpp
R  packages/native/src/graph/nodes/DspNodes.cpp -> packages/dsp/native/src/graph/nodes/DspNodes.cpp
R  packages/native/src/graph/nodes/DspNodes.hpp -> packages/dsp/native/src/graph/nodes/DspNodes.hpp
R  packages/native/src/graph/nodes/DspResampleNodes.cpp -> packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
R  packages/native/src/graph/nodes/DspResampleNodes.hpp -> packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp
R  packages/native/src/util/Hash.hpp -> packages/dsp/native/src/util/Hash.hpp
R  packages/native/src/graph/Executor.cpp -> packages/graph-runtime/native/src/graph/Executor.cpp
R  packages/native/src/graph/Executor.hpp -> packages/graph-runtime/native/src/graph/Executor.hpp
R  packages/native/src/graph/GraphHost.cpp -> packages/graph-runtime/native/src/graph/GraphHost.cpp
R  packages/native/src/graph/GraphHost.hpp -> packages/graph-runtime/native/src/graph/GraphHost.hpp
R  packages/native/src/graph/HostNodes.cpp -> packages/graph-runtime/native/src/graph/HostNodes.cpp
R  packages/native/src/graph/HostNodes.hpp -> packages/graph-runtime/native/src/graph/HostNodes.hpp
R  packages/native/src/graph/NodeRegistry.cpp -> packages/graph-runtime/native/src/graph/NodeRegistry.cpp
R  packages/native/src/graph/NodeRegistry.hpp -> packages/graph-runtime/native/src/graph/NodeRegistry.hpp
R  packages/native/src/graph/Plan.hpp -> packages/graph-runtime/native/src/graph/Plan.hpp
R  packages/native/src/graph/PlanStorage.cpp -> packages/graph-runtime/native/src/graph/PlanStorage.cpp
R  packages/native/src/graph/PlanStorage.hpp -> packages/graph-runtime/native/src/graph/PlanStorage.hpp
R  packages/native/src/graph/Planner.cpp -> packages/graph-runtime/native/src/graph/Planner.cpp
R  packages/native/src/graph/Planner.hpp -> packages/graph-runtime/native/src/graph/Planner.hpp
R  packages/native/src/graph/ResourceTable.cpp -> packages/graph-runtime/native/src/graph/ResourceTable.cpp
R  packages/native/src/graph/ResourceTable.hpp -> packages/graph-runtime/native/src/graph/ResourceTable.hpp
R  packages/native/src/graph/CanonicalJson.cpp -> packages/node-sdk/native/src/graph/CanonicalJson.cpp
R  packages/native/src/graph/CanonicalJson.hpp -> packages/node-sdk/native/src/graph/CanonicalJson.hpp
R  packages/native/src/graph/Descriptor.cpp -> packages/node-sdk/native/src/graph/Descriptor.cpp
R  packages/native/src/graph/Descriptor.hpp -> packages/node-sdk/native/src/graph/Descriptor.hpp
R  packages/native/src/graph/GraphDocument.cpp -> packages/node-sdk/native/src/graph/GraphDocument.cpp
R  packages/native/src/graph/GraphDocument.hpp -> packages/node-sdk/native/src/graph/GraphDocument.hpp
R  packages/native/src/graph/Queue.hpp -> packages/node-sdk/native/src/graph/Queue.hpp
R  packages/native/src/graph/node.hpp -> packages/node-sdk/native/src/graph/node.hpp
R  packages/native/src/graph/rp_node.h -> packages/node-sdk/native/src/graph/rp_node.h
R  packages/native/src/transport/SpscRing.hpp -> packages/node-sdk/native/src/transport/SpscRing.hpp
?? packages/native/test/engine-graph/golden/product-registrations.json
````

- [ ] **Step 3: Each package's entry function**

A product's generated registration calls each package's entry by name, so every `rp_package` gains a one-line `extern
"C"` wrapper beside the C++ function that builds it, declared in the package's header with a comment naming the
manifest that lists it: `rp_core_package`, `rp_dsp_package`, `rp_dsp_resample_package` and `rp_host_package` in the
moved files, and `rp_sameboy_package`, `rp_mesen_package`, `rp_host_io_package`, `rp_n8_package` and
`rp_launchpad_package` in `packages/native/src/host/graph/`. The C++ functions stay: OfflineJobs and the tests call
them. The first diff is the moved files' (apply it after Step 2's moves), the second the host nodes'.

````diff
diff --git a/packages/core/native/src/graph/nodes/CoreNodes.cpp b/packages/core/native/src/graph/nodes/CoreNodes.cpp
index 12159d052..b3898cb42 100644
--- a/packages/core/native/src/graph/nodes/CoreNodes.cpp
+++ b/packages/core/native/src/graph/nodes/CoreNodes.cpp
@@ -595,3 +595,5 @@ const rp_package* corePackage() {
 }
 
 }  // namespace rp::graph
+
+extern "C" const rp_package* rp_core_package(void) { return rp::graph::corePackage(); }
diff --git a/packages/core/native/src/graph/nodes/CoreNodes.hpp b/packages/core/native/src/graph/nodes/CoreNodes.hpp
index 83d2ddafa..a38f9bc70 100644
--- a/packages/core/native/src/graph/nodes/CoreNodes.hpp
+++ b/packages/core/native/src/graph/nodes/CoreNodes.hpp
@@ -28,3 +28,7 @@ namespace rp::graph {
 const rp_package* corePackage();
 
 }  // namespace rp::graph
+
+// The package's entry, named in packages/core/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_core_package(void);
diff --git a/packages/dsp/native/src/graph/nodes/DspNodes.cpp b/packages/dsp/native/src/graph/nodes/DspNodes.cpp
index 3f086fea4..3a2c244d0 100644
--- a/packages/dsp/native/src/graph/nodes/DspNodes.cpp
+++ b/packages/dsp/native/src/graph/nodes/DspNodes.cpp
@@ -655,3 +655,5 @@ const rp_package* dspPackage() {
 }
 
 }  // namespace rp::graph
+
+extern "C" const rp_package* rp_dsp_package(void) { return rp::graph::dspPackage(); }
diff --git a/packages/dsp/native/src/graph/nodes/DspNodes.hpp b/packages/dsp/native/src/graph/nodes/DspNodes.hpp
index 64c92b4cc..5a6e32dc3 100644
--- a/packages/dsp/native/src/graph/nodes/DspNodes.hpp
+++ b/packages/dsp/native/src/graph/nodes/DspNodes.hpp
@@ -38,3 +38,7 @@ namespace rp::graph {
 const rp_package* dspPackage();
 
 }  // namespace rp::graph
+
+// The package's entry, named in packages/dsp/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_dsp_package(void);
diff --git a/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp b/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
index 35ec04fec..bfba27b93 100644
--- a/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
+++ b/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
@@ -169,3 +169,5 @@ const rp_package* dspResamplePackage() {
 }
 
 }  // namespace rp::graph
+
+extern "C" const rp_package* rp_dsp_resample_package(void) { return rp::graph::dspResamplePackage(); }
diff --git a/packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp b/packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp
index 891974db6..a74469ab6 100644
--- a/packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp
+++ b/packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp
@@ -32,3 +32,7 @@ namespace rp::graph {
 const rp_package* dspResamplePackage();
 
 }  // namespace rp::graph
+
+// The package's entry, named in packages/dsp/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_dsp_resample_package(void);
diff --git a/packages/graph-runtime/native/src/graph/HostNodes.cpp b/packages/graph-runtime/native/src/graph/HostNodes.cpp
index 18fe5f748..c0e9885d1 100644
--- a/packages/graph-runtime/native/src/graph/HostNodes.cpp
+++ b/packages/graph-runtime/native/src/graph/HostNodes.cpp
@@ -97,3 +97,5 @@ const rp_package* hostPackage() {
 }
 
 }  // namespace rp::graph
+
+extern "C" const rp_package* rp_host_package(void) { return rp::graph::hostPackage(); }
diff --git a/packages/graph-runtime/native/src/graph/HostNodes.hpp b/packages/graph-runtime/native/src/graph/HostNodes.hpp
index 3ce8bf6d4..98910970a 100644
--- a/packages/graph-runtime/native/src/graph/HostNodes.hpp
+++ b/packages/graph-runtime/native/src/graph/HostNodes.hpp
@@ -11,3 +11,7 @@ namespace rp::graph {
 const rp_package* hostPackage();
 
 }  // namespace rp::graph
+
+// The package's entry, named in packages/graph-runtime/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_host_package(void);
````

Run: `sed -n '757,844p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

````diff
diff --git a/packages/native/src/host/graph/HostIoNodes.cpp b/packages/native/src/host/graph/HostIoNodes.cpp
index 7b14d301e..ee13d96e7 100644
--- a/packages/native/src/host/graph/HostIoNodes.cpp
+++ b/packages/native/src/host/graph/HostIoNodes.cpp
@@ -137,3 +137,5 @@ const rp_package* hostIoPackage() {
 }
 
 }  // namespace rp::host
+
+extern "C" const rp_package* rp_host_io_package(void) { return rp::host::hostIoPackage(); }
diff --git a/packages/native/src/host/graph/HostIoNodes.hpp b/packages/native/src/host/graph/HostIoNodes.hpp
index 8b1f9a4df..25bae8e14 100644
--- a/packages/native/src/host/graph/HostIoNodes.hpp
+++ b/packages/native/src/host/graph/HostIoNodes.hpp
@@ -43,3 +43,7 @@ inline constexpr std::uint32_t kHostMidiInMessages = 2048;
 inline constexpr std::uint32_t kHostMidiInBytes = 65536;
 
 }  // namespace rp::host
+
+// The package's entry, named in packages/native/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_host_io_package(void);
diff --git a/packages/native/src/host/graph/LaunchpadNodes.cpp b/packages/native/src/host/graph/LaunchpadNodes.cpp
index e8fb9dabc..07253825a 100644
--- a/packages/native/src/host/graph/LaunchpadNodes.cpp
+++ b/packages/native/src/host/graph/LaunchpadNodes.cpp
@@ -128,3 +128,5 @@ const rp_package* launchpadPackage() {
 }
 
 }  // namespace rp::host
+
+extern "C" const rp_package* rp_launchpad_package(void) { return rp::host::launchpadPackage(); }
diff --git a/packages/native/src/host/graph/LaunchpadNodes.hpp b/packages/native/src/host/graph/LaunchpadNodes.hpp
index 952ecb167..460320a12 100644
--- a/packages/native/src/host/graph/LaunchpadNodes.hpp
+++ b/packages/native/src/host/graph/LaunchpadNodes.hpp
@@ -33,3 +33,7 @@ inline constexpr std::uint32_t kSurfaceInMessages = 1024;
 inline constexpr std::uint32_t kSurfaceInBytes = 16384;
 
 }  // namespace rp::host
+
+// The package's entry, named in packages/native/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_launchpad_package(void);
diff --git a/packages/native/src/host/graph/N8Nodes.cpp b/packages/native/src/host/graph/N8Nodes.cpp
index e93ac609c..125f8c069 100644
--- a/packages/native/src/host/graph/N8Nodes.cpp
+++ b/packages/native/src/host/graph/N8Nodes.cpp
@@ -195,3 +195,5 @@ const rp_package* n8Package() {
 }
 
 }  // namespace rp::host
+
+extern "C" const rp_package* rp_n8_package(void) { return rp::host::n8Package(); }
diff --git a/packages/native/src/host/graph/N8Nodes.hpp b/packages/native/src/host/graph/N8Nodes.hpp
index a2d6ec122..02a12b327 100644
--- a/packages/native/src/host/graph/N8Nodes.hpp
+++ b/packages/native/src/host/graph/N8Nodes.hpp
@@ -23,3 +23,7 @@ const rp_package* n8Package();
 int expansionVolumeRegister(double percent);
 
 }  // namespace rp::host
+
+// The package's entry, named in packages/native/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_n8_package(void);
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index 24bebe314..a36f2e067 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -871,3 +871,6 @@ const rp_package* mesenPackage() {
 }
 
 }  // namespace rp::host
+
+extern "C" const rp_package* rp_sameboy_package(void) { return rp::host::sameboyPackage(); }
+extern "C" const rp_package* rp_mesen_package(void) { return rp::host::mesenPackage(); }
diff --git a/packages/native/src/host/graph/SystemNodes.hpp b/packages/native/src/host/graph/SystemNodes.hpp
index cf8a1d17f..106db5d80 100644
--- a/packages/native/src/host/graph/SystemNodes.hpp
+++ b/packages/native/src/host/graph/SystemNodes.hpp
@@ -18,3 +18,8 @@ inline constexpr int kExpansionVolumeMax = 200;
 inline constexpr int kExpansionVolumeDefault = 100;
 
 }  // namespace rp::host
+
+// The packages' entries, named in packages/native/package.json: what a product's generated registration calls
+// (rp_register_packages, graph/Packages.hpp).
+extern "C" const rp_package* rp_sameboy_package(void);
+extern "C" const rp_package* rp_mesen_package(void);
````

Run: `sed -n '851,940p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

- [ ] **Step 4: The manifests, and the lockfile's four new members**

Each new package's `package.json` is private, carries no `main` (none has a TypeScript part yet) and names its native
part in its `retroplug` field: `node-sdk` a target and no entry; `graph-runtime` `retroplug-graph` with
`rp_host_package`, `device: false`; `core` `retroplug-core-nodes` with `rp_core_package`, `device: true`; `dsp` a list
of two, `rp.dsp`'s (`device: true`) and `rp.dsp.resample`'s (`device: false`). `packages/native/package.json`, already
a workspace member, lists the five not-yet-carved entries on `retroplug-backend`. Then pnpm records the four new
members.

Create `packages/node-sdk/package.json`:

````json
{
  "name": "@retroplug/node-sdk",
  "version": "0.1.0",
  "private": true,
  "description": "The node SDK: the C ABI (rp_node.h), the C++ authoring layer (node.hpp) and the description library it compiles against. A pack depends on this package alone.",
  "retroplug": {
    "native": { "target": "retroplug-node-sdk" }
  }
}
````

Run: `mkdir -p packages/node-sdk; sed -n '958,966p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/package.json`
Expected: `ad1846c5e4836876f9408559a5b250a3a6f09fc0`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/package.json`:

````json
{
  "name": "@retroplug/graph-runtime",
  "version": "0.1.0",
  "private": true,
  "description": "The graph runtime: the executor, the planner, the registry, the resource table, GraphHost and the rp.host node types the runtime implements itself.",
  "retroplug": {
    "native": { "target": "retroplug-graph", "entry": "rp_host_package", "device": false }
  }
}
````

Run: `mkdir -p packages/graph-runtime; sed -n '978,986p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/package.json`
Expected: `f6d3cca48ffd0d2b5a3cafb49d2fbb5b3a100b52`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/core/package.json`:

````json
{
  "name": "@retroplug/core",
  "version": "0.1.0",
  "private": true,
  "description": "The rp.core node types: routing, delay, constants and the buffer player and recorder.",
  "retroplug": {
    "native": { "target": "retroplug-core-nodes", "entry": "rp_core_package", "device": true }
  }
}
````

Run: `mkdir -p packages/core; sed -n '998,1006p' docs/modular-graph-phase-e1-plan.md > packages/core/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/core/package.json`
Expected: `1d2c02511c68b940b040843a53afe2caf6fbbe69`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/dsp/package.json`:

````json
{
  "name": "@retroplug/dsp",
  "version": "0.1.0",
  "private": true,
  "description": "The DSP kernels and the rp.dsp node types around them, and rp.dsp.resample (r8brain) as a package of its own.",
  "retroplug": {
    "native": [
      { "target": "retroplug-dsp-nodes", "entry": "rp_dsp_package", "device": true },
      { "target": "retroplug-dsp-resample-nodes", "entry": "rp_dsp_resample_package", "device": false }
    ]
  }
}
````

Run: `mkdir -p packages/dsp; sed -n '1018,1029p' docs/modular-graph-phase-e1-plan.md > packages/dsp/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/dsp/package.json`
Expected: `9f55a64abb853a0ac633963074bb33934f462526`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/package.json b/packages/native/package.json
index 7bd83c396..2c42ed5c8 100644
--- a/packages/native/package.json
+++ b/packages/native/package.json
@@ -2,5 +2,14 @@
   "name": "@retroplug/native",
   "version": "0.1.0",
   "private": true,
-  "description": "RetroPlug native core (C++). Built via the top-level CMake; this manifest only marks it as a workspace member."
+  "description": "RetroPlug native core (C++): the engine, the systems, the hosts, and the node types not yet carved into packages of their own. Built via the top-level CMake.",
+  "retroplug": {
+    "native": [
+      { "target": "retroplug-backend", "entry": "rp_sameboy_package", "device": false },
+      { "target": "retroplug-backend", "entry": "rp_mesen_package", "device": false },
+      { "target": "retroplug-backend", "entry": "rp_host_io_package", "device": false },
+      { "target": "retroplug-backend", "entry": "rp_n8_package", "device": false },
+      { "target": "retroplug-backend", "entry": "rp_launchpad_package", "device": false }
+    ]
+  }
 }
````

Run: `sed -n '1039,1058p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm install --offline`
Expected: exit 0, no network needed (the report: `pnpm install --offline` was enough (lockfile: 4 empty importers)). `git diff pnpm-lock.yaml` shows exactly four added importers, `packages/core: {}`, `packages/dsp: {}`, `packages/graph-runtime: {}` and `packages/node-sdk: {}`, each followed by a blank line.

Run: `git hash-object pnpm-lock.yaml`
Expected: `bde33bddf349f06c6d27b7a3ceea6a882671266d`: the blob 364040f65 holds, so the generator wrote the prototype's bytes. Never edit the lockfile by hand.

- [ ] **Step 5: The packages' CMake, the products and the registration test**

`cmake/packages.cmake` (root `cmake/`, beside `sameboy.cmake`) holds the package and product functions and their
configure-time refusals. Each package's `native/CMakeLists.txt` defines its targets where the old
`packages/native/cmake/graph.cmake` and `packages/native/CMakeLists.txt` did, under the same names, plus two new ones:
`retroplug-node-sdk` (the description library, which `retroplug-graph` carried before) with its include-only
`retroplug-node-sdk-headers`, and `retroplug-core-nodes` (`CoreNodes.cpp` was a source of `retroplug-graph`).
`graph/Packages.hpp` declares `rp_register_packages()` and its span, which `Engine.cpp`'s loop now reads. The root
adds the packages before `packages/native` and names `RP_DESKTOP_PACKAGES`; every product lists it (the four test
binaries through `retroplug_add_test_binary`'s new `PACKAGES`, the plugin through the binaries DPF built). The
Launchpad and N8 tests take `retroplug-node-sdk-headers` for the ring, and graph-test adds the engine tree's include
root for the two headers it reads beside the packages. `retroplug_product_table` gives the engine-graph binary every
product's entries, and `Packages.test.cpp` (`[packages]`, two cases) holds them to the golden: each product's registry
registers exactly its row's ids, in order, every golden row but the opt-in Node addon's has its product, and this
binary's own registration is its row, each package of which its `Engine` already holds. `graph.cmake` goes.

Create `cmake/packages.cmake`:

````cmake
# Workspace packages and the products that compose them (docs/modular-graph-phase-e1-design.md section 4).
#
# A package is packages/<name>/, a pnpm workspace member whose package.json carries a `retroplug` field: its manifest.
# The manifest's `native` member names the CMake target the package builds and, for a package that holds node types,
# the entry function returning its rp_package and whether those types build for the device:
#
#   "retroplug": { "native": { "target": "retroplug-core-nodes", "entry": "rp_core_package", "device": true } }
#
# `native` may also be a list of those, for a package whose types register as more than one rp_package: dsp holds
# rp.dsp and rp.dsp.resample, which is an rp_package of its own so that a build can leave r8brain out (its target
# links r8brain, its entry is not `device`). An object with no `entry` is a library and nothing more (node-sdk). The
# manifest is the ONE source of a package's targets and entries: retroplug_package() reads them with string(JSON), and
# nothing else in the build spells an entry.
#
# A product is an executable or a module that constructs an Engine. retroplug_product() generates its registration
# source, which defines rp_register_packages() (graph/Packages.hpp) as the entries of the packages it lists, in the
# order it lists them, and links those packages' targets. The engine registers what that function returns, so which
# node types a product carries is its CMake's choice. A binary that pulls the engine in without declaring itself a
# product has no definition of rp_register_packages: an executable (and a module on macOS or Windows) fails to link
# naming it, but a Linux module links and fails only when it is loaded. So the configure checks it instead, for every
# binary: retroplug_check_products() refuses one that links the engine's libraries and is not a product.

set(RP_PACKAGES_DIR "${CMAKE_SOURCE_DIR}/packages")
set(RP_PRODUCTS_DIR "${CMAKE_BINARY_DIR}/packages/products")

# retroplug_package(<name>): reads packages/<name>/package.json's manifest and, when the package has a
# native/CMakeLists.txt, adds it (its binary dir build/packages/<name>). Call it once per package, before any product
# that lists the package. A package whose targets are defined elsewhere (packages/native, the engine's own tree, whose
# CMakeLists the root adds) has no native/CMakeLists.txt, and this only reads its manifest.
function(retroplug_package name)
    set(_dir "${RP_PACKAGES_DIR}/${name}")
    set(_manifest "${_dir}/package.json")
    if(NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "retroplug_package(${name}): ${_manifest} does not exist")
    endif()
    get_property(_packages GLOBAL PROPERTY RP_PACKAGES)
    if(name IN_LIST _packages)
        message(FATAL_ERROR "retroplug_package(${name}): the package is added twice")
    endif()
    # A manifest is configure input, as a CMakeLists.txt is: editing one re-runs the configure.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")
    file(READ "${_manifest}" _json)
    string(JSON _kind ERROR_VARIABLE _err TYPE "${_json}" retroplug native)
    if(_err)
        message(FATAL_ERROR "retroplug_package(${name}): ${_manifest} has no retroplug.native field (${_err})")
    endif()
    string(JSON _native GET "${_json}" retroplug native)
    if(_kind STREQUAL "OBJECT")
        set(_native "[${_native}]")
    elseif(NOT _kind STREQUAL "ARRAY")
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.native is a ${_kind}, not an object or a list")
    endif()
    string(JSON _count LENGTH "${_native}")
    if(_count EQUAL 0)
        message(FATAL_ERROR "retroplug_package(${name}): retroplug.native is an empty list")
    endif()

    get_property(_taken GLOBAL PROPERTY RP_PACKAGE_ENTRIES)
    set(_targets "")
    set(_entries "")
    set(_device "")
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        string(JSON _target ERROR_VARIABLE _err GET "${_native}" ${_i} target)
        if(_err)
            message(FATAL_ERROR "retroplug_package(${name}): retroplug.native[${_i}] names no target")
        endif()
        list(APPEND _targets ${_target})
        string(JSON _entry ERROR_VARIABLE _noEntry GET "${_native}" ${_i} entry)
        if(_noEntry)
            continue()
        endif()
        # A boolean reads back as ON or OFF. Required wherever there is an entry, so no package's device standing is
        # a default nobody chose.
        string(JSON _isDevice ERROR_VARIABLE _err GET "${_native}" ${_i} device)
        if(_err OR NOT (_isDevice STREQUAL "ON" OR _isDevice STREQUAL "OFF"))
            message(FATAL_ERROR "retroplug_package(${name}): entry ${_entry} needs \"device\": true or false")
        endif()
        if(NOT _entry MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
            message(FATAL_ERROR "retroplug_package(${name}): entry \"${_entry}\" is not a C identifier")
        endif()
        # Every statically linked entry is an extern "C" symbol of the program, so two packages may not share one;
        # rp_package_entry is a downloaded pack's, never a linked package's.
        if(_entry IN_LIST _taken OR _entry STREQUAL "rp_package_entry")
            message(FATAL_ERROR "retroplug_package(${name}): entry ${_entry} is taken")
        endif()
        list(APPEND _taken ${_entry})
        list(APPEND _entries ${_entry})
        if(_isDevice)
            list(APPEND _device ${_entry})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _targets)

    set_property(GLOBAL APPEND PROPERTY RP_PACKAGES ${name})
    set_property(GLOBAL APPEND PROPERTY RP_PACKAGE_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_TARGETS ${_targets})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_ENTRIES ${_entries})
    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_ENTRIES ${_device})

    if(EXISTS "${_dir}/native/CMakeLists.txt")
        add_subdirectory("${_dir}/native" "${CMAKE_BINARY_DIR}/packages/${name}")
    endif()
endfunction()

# retroplug_package_library(<target> <sources>...): a package's static library, with the settings every one of them
# shares. Position-independent, as each is linked into the plugin's shared module, and /bigobj on MSVC: reflect-cpp's
# templates instantiate inside any translation unit that includes graph/node.hpp, enough COMDAT sections to pass COFF's
# 2^16 limit (C1128), which packages/native sets directory-wide for its own and a package's directory does not inherit.
function(retroplug_package_library target)
    add_library(${target} STATIC ${ARGN})
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
    endif()
endfunction()

# retroplug_product(<product> PACKAGES <package>... [TARGETS <target>...]): declares a product and the packages it
# carries, in registration order. Generates build/packages/products/<product>.cpp, which defines the product's
# rp_register_packages(), and adds it to each of TARGETS (default: the target named <product>) with the packages'
# targets linked. TARGETS is for one product built as several binaries: the plugin's formats, each its own module.
function(retroplug_product product)
    cmake_parse_arguments(P "" "" "PACKAGES;TARGETS" ${ARGN})
    if(P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_product(${product}): unrecognised argument(s): ${P_UNPARSED_ARGUMENTS}")
    endif()
    # TARGETS given but empty (a list that resolved to nothing) is an error, not the default: for the plugin the default
    # would be DPF's static `retroplug` library, the one place the registration must not go.
    if("TARGETS" IN_LIST ARGN AND NOT P_TARGETS)
        message(FATAL_ERROR "retroplug_product(${product}): TARGETS names no target")
    endif()
    if(NOT P_PACKAGES)
        message(FATAL_ERROR "retroplug_product(${product}): PACKAGES is required")
    endif()
    if(NOT P_TARGETS)
        set(P_TARGETS ${product})
    endif()
    get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
    if(product IN_LIST _products)
        message(FATAL_ERROR "retroplug_product(${product}): the product is declared twice")
    endif()
    get_property(_known GLOBAL PROPERTY RP_PACKAGES)
    set(_entries "")
    set(_targets "")
    foreach(_p IN LISTS P_PACKAGES)
        if(NOT _p IN_LIST _known)
            message(FATAL_ERROR "retroplug_product(${product}): no package `${_p}` (retroplug_package() adds one)")
        endif()
        get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_ENTRIES)
        get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_TARGETS)
        # Every package's targets exist by the first product (the packages are added before packages/native, whose
        # retroplug-backend comes before its first product), so a name that is not one is a manifest typo, refused here
        # rather than left to the linker as a bare -l flag.
        foreach(_target IN LISTS _t)
            if(NOT TARGET ${_target})
                message(FATAL_ERROR "retroplug_product(${product}): ${RP_PACKAGES_DIR}/${_p}/package.json names "
                                    "`${_target}`, which is not a target")
            endif()
        endforeach()
        list(APPEND _entries ${_e})
        list(APPEND _targets ${_t})
    endforeach()
    list(REMOVE_DUPLICATES _targets)

    _retroplug_entry_declarations(_decls ${_entries})
    list(LENGTH _entries _n)
    if(_n EQUAL 0)
        set(_body "    *count = 0;\n    return nullptr;\n")
    else()
        set(_calls "")
        foreach(_e IN LISTS _entries)
            string(APPEND _calls "        ${_e}(),\n")
        endforeach()
        set(_body "    static const rp_package* const packages[] = {\n${_calls}    };\n")
        string(APPEND _body "    *count = ${_n};\n    return packages;\n")
    endif()
    list(JOIN P_PACKAGES " " _listed)
    set(_src "${RP_PRODUCTS_DIR}/${product}.cpp")
    file(GENERATE OUTPUT "${_src}" CONTENT
"// Generated by retroplug_product(${product}) in cmake/packages.cmake from the manifests of the packages it lists:
// ${_listed}. Do not edit; edit the product's PACKAGES or a manifest.

#include \"graph/Packages.hpp\"

${_decls}
const rp_package* const* rp_register_packages(size_t* count) {
${_body}}
")
    foreach(_t IN LISTS P_TARGETS)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR "retroplug_product(${product}): no target `${_t}`")
        endif()
        target_sources(${_t} PRIVATE "${_src}")
        target_link_libraries(${_t} PRIVATE ${_targets})
    endforeach()
    set_property(GLOBAL APPEND PROPERTY RP_PRODUCTS ${product})
    set_property(GLOBAL APPEND PROPERTY RP_PRODUCT_TARGETS ${P_TARGETS})
    set_property(GLOBAL PROPERTY RP_PRODUCT_${product}_ENTRIES ${_entries})
endfunction()

# retroplug_check_products(LIBRARIES <library>... [EXEMPT <target>...]): refuses, at configure time, every executable,
# module or shared library in the build that links one of LIBRARIES directly and is neither a product's target nor
# EXEMPT. LIBRARIES are the ones that carry the engine; EXEMPT is for a binary that links one of them and is known never
# to pull the engine's object in. Call it after the last retroplug_product(). It stands in for the link error a Linux
# module never gives (this file's header says why); -Wl,--no-undefined would not, as it breaks the sanitizer
# builds and guards only the modules that are already products.
function(retroplug_check_products)
    cmake_parse_arguments(C "" "" "LIBRARIES;EXEMPT" ${ARGN})
    get_property(_products GLOBAL PROPERTY RP_PRODUCT_TARGETS)
    _retroplug_binaries(_binaries "${CMAKE_SOURCE_DIR}")
    set(_missing "")
    foreach(_t IN LISTS _binaries)
        if(_t IN_LIST _products OR _t IN_LIST C_EXEMPT)
            continue()
        endif()
        get_target_property(_links ${_t} LINK_LIBRARIES)
        foreach(_l IN LISTS C_LIBRARIES)
            if(_l IN_LIST _links)
                list(APPEND _missing "${_t} (links ${_l})")
                break()
            endif()
        endforeach()
    endforeach()
    if(_missing)
        list(JOIN _missing ", " _missing)
        message(FATAL_ERROR "retroplug_check_products: these binaries link the engine and are not products, so none "
                            "defines rp_register_packages: ${_missing}. Declare each with retroplug_product() (or "
                            "PACKAGES in retroplug_add_test_binary), or exempt one that never pulls the engine in.")
    endif()
endfunction()

# The executables, modules and shared libraries of `dir` and every directory under it.
function(_retroplug_binaries out dir)
    set(_found "")
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
            list(APPEND _found ${_t})
        endif()
    endforeach()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _retroplug_binaries(_more "${_sub}")
        list(APPEND _found ${_more})
    endforeach()
    set(${out} ${_found} PARENT_SCOPE)
endfunction()

# retroplug_product_table(<target>): generates build/packages/products/product-table.cpp, every product this configure
# declared with the entries its rp_register_packages() returns, as rp_product_registrations(), and adds it to <target>,
# which must link every package those name. The registration golden reads it (test/engine-graph/Packages.test.cpp), so
# call this after the last retroplug_product().
function(retroplug_product_table target)
    get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
    set(_all "")
    set(_rows "")
    foreach(_p IN LISTS _products)
        get_property(_e GLOBAL PROPERTY RP_PRODUCT_${_p}_ENTRIES)
        list(APPEND _all ${_e})
        list(JOIN _e ", " _list)
        string(APPEND _rows "        {\"${_p}\", {${_list}}},\n")
    endforeach()
    list(REMOVE_DUPLICATES _all)
    _retroplug_entry_declarations(_decls ${_all})
    set(_src "${RP_PRODUCTS_DIR}/product-table.cpp")
    file(GENERATE OUTPUT "${_src}" CONTENT
"// Generated by retroplug_product_table() in cmake/packages.cmake: every product the configure declared, with the
// package entries its rp_register_packages() returns, in order. Do not edit.

#include <string>
#include <utility>
#include <vector>

#include \"graph/rp_node.h\"

${_decls}
std::vector<std::pair<std::string, std::vector<rp_package_entry_fn>>> rp_product_registrations() {
    return {
${_rows}    };
}
")
    target_sources(${target} PRIVATE "${_src}")
endfunction()

# The extern "C" declarations of a list of package entries, for a generated source.
function(_retroplug_entry_declarations out)
    set(_decls "")
    foreach(_e IN LISTS ARGN)
        string(APPEND _decls "extern \"C\" const rp_package* ${_e}(void);\n")
    endforeach()
    set(${out} "${_decls}" PARENT_SCOPE)
endfunction()
````

Run: `mkdir -p cmake; sed -n '1089,1384p' docs/modular-graph-phase-e1-plan.md > cmake/packages.cmake`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object cmake/packages.cmake`
Expected: `e71b05bbbaa7a895750d3147c1337013aceb6247`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/CMakeLists.txt`:

````cmake
# The node SDK (docs/modular-graph-phase-e1-design.md section 4): the C ABI (graph/rp_node.h), the C++ authoring layer
# (graph/node.hpp) and what that header compiles against: the descriptor writer, canonical JSON, the graph document
# types it names and the queue views (graph/Queue.hpp), with the single-producer ring the runtime and the device links
# share (transport/SpscRing.hpp). Added by retroplug_package(node-sdk) (cmake/packages.cmake).
#
# Its include root, src/, is where every one of these keeps the path it is included by ("graph/node.hpp"), so a node's
# source is the same whichever package it sits in. A pack depends on this package and nothing else.
#
# retroplug-node-sdk-headers: the include root alone, no library. What a target takes when it needs the ABI, the queue
# views or the ring and must not link reflect-cpp: the no-heap executor (graph-runtime's retroplug-graph-exec), and the
# device links' tests.
add_library(retroplug-node-sdk-headers INTERFACE)
target_include_directories(retroplug-node-sdk-headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# retroplug-node-sdk: the description library. Links reflect-cpp (settings parsing, JSON Schema).
retroplug_package_library(retroplug-node-sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CanonicalJson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphDocument.cpp)
target_link_libraries(retroplug-node-sdk PUBLIC retroplug-node-sdk-headers reflectcpp::reflectcpp)
````

Run: `mkdir -p packages/node-sdk/native; sed -n '1396,1415p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/CMakeLists.txt`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/CMakeLists.txt`
Expected: `2fb041df90fa628125bb8b91acfe30bc4478b699`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/native/CMakeLists.txt`:

````cmake
# The graph runtime (docs/modular-graph-design.md, docs/modular-graph-phase-e1-design.md section 4): what turns a graph
# document into a plan and runs it. Added by retroplug_package(graph-runtime) (cmake/packages.cmake); retroplug-backend
# links it, as the engine runs every block on a GraphHost.
#
# retroplug-graph-exec: the no-heap executor. No heap, no exceptions and no dependencies beyond libc's memcpy / memset
# (it is C++20 over the C ABI in graph/rp_node.h), which is what lets the same code run a plan on the Pico later. It
# takes node-sdk's headers for the ABI and the queue views, not node-sdk's library, which links reflect-cpp.
retroplug_package_library(retroplug-graph-exec
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Executor.cpp)
target_include_directories(retroplug-graph-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-graph-exec PUBLIC retroplug-node-sdk-headers)

# retroplug-graph: everything the control thread needs to turn a graph document into a plan - the registry, resources,
# the planner, GraphHost - plus the node types the runtime implements itself (rp.host.audio-out, the manifest's
# rp_host_package).
retroplug_package_library(retroplug-graph
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/NodeRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/ResourceTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/PlanStorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Planner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/HostNodes.cpp)
target_link_libraries(retroplug-graph PUBLIC retroplug-graph-exec retroplug-node-sdk)
````

Run: `mkdir -p packages/graph-runtime/native; sed -n '1427,1449p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/native/CMakeLists.txt`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/native/CMakeLists.txt`
Expected: `16309c4f091135a45fd55e4f02c1f40d231b7276`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/core/native/CMakeLists.txt`:

````cmake
# The rp.core node types (graph/nodes/CoreNodes.cpp): routing, delay, constants and the buffer player and recorder.
# Added by retroplug_package(core) (cmake/packages.cmake). Like any pack, it depends on node-sdk alone.
retroplug_package_library(retroplug-core-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/CoreNodes.cpp)
target_include_directories(retroplug-core-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-core-nodes PUBLIC retroplug-node-sdk)
````

Run: `mkdir -p packages/core/native; sed -n '1461,1466p' docs/modular-graph-phase-e1-plan.md > packages/core/native/CMakeLists.txt`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/core/native/CMakeLists.txt`
Expected: `980f29eec7368bc71f2eb1eb98aaf8a276c13279`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/dsp/native/CMakeLists.txt`:

````cmake
# The DSP package (docs/modular-graph-phase-d1-design.md sections 4 and 5, docs/modular-graph-phase-e1-design.md
# section 4): the kernels (src/dsp/, with util/Hash.hpp, the FNV-1a a dither's seed is drawn from) and the rp.dsp and
# rp.dsp.resample node types around them. Added by retroplug_package(dsp) (cmake/packages.cmake).

# --- retroplug-dsp-kernels: the DSP the rp.dsp nodes run: gain and peak, the RBJ biquads, dither / quantise, and risa's
# DMC encoder (a whole-buffer kernel, Dpcm.hpp says why; the others stream a block at a time). A kit compile is a chain
# of those nodes, so it reaches this DSP through them.
# Core-free on purpose: it links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node
# package can use it with no core, txiki, backend or r8brain.
# (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.)
#
# The resampler is its own target on purpose, because its kernel is the one that is not freestanding: r8brain brings
# the heap, pthreads (its filter cache is a std::mutex) and RTTI (its resamplers are virtual). A build that has no use
# for it - the Pico's, which runs rp.dsp's gain / biquad / dither / normalize alone - omits the target and never sees
# any of that, and the rp.dsp package's resample node, which is the only node that needs it, is a target of its own for
# the same reason.
retroplug_package_library(retroplug-dsp-kernels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Gain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dpcm.cpp)
target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# r8brain's static members are vendored code, so they get a target of their own rather than riding in one of ours:
# /W4 /WX is scoped to RetroPlug's own targets (packages/native/cmake/warnings.cmake), which this one is deliberately
# not on, and GNU/Clang get the same `-w` a vendored dep gets (retroplug_silence_dep_warnings). Its one source includes
# r8brain's r8bbase.cpp through dsp/R8bAllocator.hpp, so every r8brain class in the program is built on the throwing
# allocator; one translation unit in one target means there is no second definition of those classes to keep, and the
# tree declares no target for the vendored library, so there is none to link.
retroplug_package_library(retroplug-r8brain-base ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/R8bBase.cpp)
target_include_directories(retroplug-r8brain-base PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/r8brain)
retroplug_silence_dep_warnings(retroplug-r8brain-base)

# Threads: r8brain's filter cache is a std::mutex. Found here as well as in packages/native, as an imported target is
# visible only in the directory that finds it and below.
find_package(Threads REQUIRED)
retroplug_package_library(retroplug-dsp-resample ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resample.cpp)
target_include_directories(retroplug-dsp-resample
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    # r8brain's headers, all this build takes from the vendored dep, stay private to the translation units that include
    # dsp/R8bAllocator.hpp first: that header is what puts r8brain's classes on the throwing allocator, and a TU that
    # read them without it would build them on r8brain's defaults.
    PRIVATE ${CMAKE_SOURCE_DIR}/deps/r8brain)
# The tree declares no target for the vendored `r8brain` library. Its r8bbase.cpp would build r8brain's static members
# on r8brain's own malloc hooks, where src/dsp/R8bBase.cpp (in retroplug-r8brain-base) builds them on the throwing
# allocator the rest of this tree uses, and the linker would keep one copy of every r8brain class with no diagnostic
# saying which.
target_link_libraries(retroplug-dsp-resample PRIVATE Threads::Threads retroplug-r8brain-base)

# --- retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad, dither, normalize and dpcm,
# each a node around the kernels above (docs/modular-graph-phase-d1-design.md section 5). A target of its own rather
# than a source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the CLI, the
# test hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying.
# It links retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or
# RTTI, and it stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and
# runs these nodes with only the graph library and the kernels.
retroplug_package_library(retroplug-dsp-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
target_include_directories(retroplug-dsp-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-node-sdk PRIVATE retroplug-dsp-kernels)

# --- retroplug-dsp-resample-nodes: the rp.dsp.resample package (src/graph/nodes/DspResampleNodes.cpp), the resampler
# as a node, apart from retroplug-dsp-nodes because its kernel is retroplug-dsp-resample: r8brain, and with it the
# heap, pthreads and RTTI. A build for a target that cannot take those (the Pico's) links retroplug-dsp-nodes and
# retroplug-dsp-kernels alone and leaves this package's second entry, rp_dsp_resample_package, out of its registration
# list (the manifest marks it not `device`), so neither this node nor r8brain reaches its binary; the planner's refusal
# of a `heap` node there is the second line, not the only one.
retroplug_package_library(retroplug-dsp-resample-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspResampleNodes.cpp)
target_include_directories(retroplug-dsp-resample-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-dsp-resample-nodes PUBLIC retroplug-node-sdk PRIVATE retroplug-dsp-resample)
````

Run: `mkdir -p packages/dsp/native; sed -n '1478,1550p' docs/modular-graph-phase-e1-plan.md > packages/dsp/native/CMakeLists.txt`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/dsp/native/CMakeLists.txt`
Expected: `c7fd23aa6ea2b8a00011acd7870b91616b688999`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/native/src/graph/Packages.hpp`:

````cpp
#pragma once

#include <cstddef>
#include <span>

#include "graph/rp_node.h"

// The node packages a product carries (docs/modular-graph-phase-e1-design.md section 4). A product's CMake lists the
// packages it is built with (retroplug_product, cmake/packages.cmake), and the configure generates that product's
// definition of this function from their manifests: their entries' rp_packages, in the order the product lists them,
// count written to *count, the array valid for the life of the program. The engine registers what it returns, so a
// product's node types are its CMake's choice. Defined once per product, never by a library. A binary that links the
// engine without declaring itself a product has no definition: an executable fails to link naming this function, but
// a Linux module links and fails only when loaded, so the configure refuses any such binary first
// (retroplug_check_products).
extern "C" const rp_package* const* rp_register_packages(size_t* count);

namespace rp::graph {

// rp_register_packages(), as a span.
inline std::span<const rp_package* const> productPackages() {
    std::size_t n = 0;
    const rp_package* const* packages = rp_register_packages(&n);
    return {packages, n};
}

}  // namespace rp::graph
````

Run: `mkdir -p packages/graph-runtime/native/src/graph; sed -n '1562,1588p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/native/src/graph/Packages.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/native/src/graph/Packages.hpp`
Expected: `52a87f8b5b38fdf03d04c544b6ebf7b6ee61f6c0`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/engine-graph/Packages.test.cpp`:

````cpp
// The products' registrations (docs/modular-graph-phase-e1-design.md sections 4 and 8). Every product the configure
// declares (retroplug_product, cmake/packages.cmake) registers, through its generated rp_register_packages(), exactly
// the node type ids golden/product-registrations.json holds for it, in registration order. The golden was recorded from
// the list Engine.cpp registered before products declared their packages, which every product carried whole, so a row
// changes only when a product gains or drops a package on purpose. The script node types are not in it: the built-in
// script pack is loaded at run time, not linked, and keeps its own path.
//
// The products are read from the table the configure generates for this binary (rp_product_registrations), which names
// each product's entries; this binary links every package, so it can call all of them. Every golden row must have its
// product in the table, but for kOptIn's: products a configure builds only when asked to (a format is never one, as
// the plugin's formats are targets of the one `retroplug` product), so a renamed or dropped product cannot leave its
// row unchecked.
//
// Run via `pnpm test:plugin engine-graph`.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <rfl/json.hpp>

#include "graph/NodeRegistry.hpp"
#include "graph/Packages.hpp"
#include "graph/rp_node.h"
#include "host/engine/Engine.hpp"

// Generated by retroplug_product_table() (build/packages/products/product-table.cpp).
std::vector<std::pair<std::string, std::vector<rp_package_entry_fn>>> rp_product_registrations();

namespace {

using Golden = std::map<std::string, std::vector<std::string>>;

// The products whose binaries are opt-in: retroplug-node, built with -DRETROPLUG_NODE_ADDON=ON.
const std::vector<std::string> kOptIn = {"retroplug-node"};

Golden readGolden() {
    std::ifstream in(RP_PRODUCT_REGISTRATIONS_PATH, std::ios::binary);
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = rfl::json::read<Golden>(text);
    REQUIRE(parsed);
    return *parsed;
}

const std::vector<rp_package_entry_fn>& rowOf(
    const std::vector<std::pair<std::string, std::vector<rp_package_entry_fn>>>& rows, const std::string& product) {
    for (const auto& [name, entries] : rows)
        if (name == product) return entries;
    FAIL("the configure declares no product " << product);
    return rows.front().second;  // not reached: FAIL throws
}

}  // namespace

TEST_CASE("each product registers exactly the node types its golden row holds", "[packages]") {
    const auto rows = rp_product_registrations();
    const Golden golden = readGolden();
    REQUIRE_FALSE(rows.empty());
    for (const auto& [product, entries] : rows) {
        INFO("product " << product);
        const auto it = golden.find(product);
        REQUIRE(it != golden.end());
        // A fresh registry of the product's packages, as its engine's is before the script pack joins it.
        rp::graph::NodeRegistry  registry;
        std::vector<std::string> ids;
        for (const rp_package_entry_fn entry : entries) {
            const rp_package* package = entry();
            REQUIRE(package != nullptr);
            std::string why;
            const bool  added = registry.addPackage(package, &why);
            INFO(why);
            CHECK(added);
            for (std::uint32_t k = 0; k < package->node_type_count; ++k) {
                ids.emplace_back(package->node_types[k]->id);
                CHECK(registry.find(ids.back()) != nullptr);
            }
        }
        CHECK(ids == it->second);
        CHECK(registry.size() == it->second.size());
    }
    // And no golden row is left without its product, but for an opt-in one this configure did not build.
    for (const auto& [product, ids] : golden) {
        INFO("golden row " << product);
        bool declared = false;
        for (const auto& row : rows) declared = declared || row.first == product;
        CHECK((declared || std::find(kOptIn.begin(), kOptIn.end(), product) != kOptIn.end()));
    }
}

TEST_CASE("this binary's registration is its product's row, and its engine holds every package in it", "[packages]") {
    const auto  rows = rp_product_registrations();
    const auto& row = rowOf(rows, "retroplug-engine-graph-test");
    const auto  packages = rp::graph::productPackages();
    REQUIRE(packages.size() == row.size());
    for (std::size_t i = 0; i < row.size(); ++i) CHECK(packages[i] == row[i]());

    // The engine registered each of them: a package id registers once, so offering one again is refused.
    Engine e(48000.0);
    std::size_t types = 0;
    for (const rp_package* package : packages) {
        types += package->node_type_count;
        std::string why;
        CHECK_FALSE(e.addNodePackage(package, &why));
        CHECK_FALSE(why.empty());
    }
    CHECK(e.nodeTypeCount() >= types);
}
````

Run: `mkdir -p packages/native/test/engine-graph; sed -n '1600,1715p' docs/modular-graph-phase-e1-plan.md > packages/native/test/engine-graph/Packages.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/engine-graph/Packages.test.cpp`
Expected: `e7013ac1e20cd2d96f77186d2b8984c7065ef454`: the blob 364040f65 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Delete `packages/native/cmake/graph.cmake` with `git rm`, which stages the deletion:

````bash
git rm -q -- packages/native/cmake/graph.cmake
````

Then the CMake that adds the packages and declares the products, the test binaries' `PACKAGES`, and the engine's loop:

````diff
diff --git a/CMakeLists.txt b/CMakeLists.txt
index fc173b572..b4c918eb7 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -277,8 +277,8 @@ add_subdirectory(deps/mesen)
 
 # --- r8brain-free: high-quality SRC for LSDJ kit compilation.
 # Also used for non-44.1 kHz host-rate sync accuracy.
-# Headers only: packages/native/src/dsp/R8bBase.cpp compiles its one source (r8bbase.cpp) on the throwing allocator, so
-# the tree declares no target for the vendored library and there is none to link.
+# Headers only: packages/dsp/native/src/dsp/R8bBase.cpp compiles its one source (r8bbase.cpp) on the throwing
+# allocator, so the tree declares no target for the vendored library and there is none to link.
 
 # --- enkiTS: a task scheduler, vendored under deps/enkiTS but not built. No RetroPlug code uses it, so the tree adds
 # no target for it and links none; the directory stays for a later cleanup to judge.
@@ -354,6 +354,24 @@ set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
 add_subdirectory(deps/catch2 "${CMAKE_BINARY_DIR}/deps/catch2" EXCLUDE_FROM_ALL)
 retroplug_silence_dep_warnings(Catch2 Catch2WithMain)
 
+# --- the workspace packages (cmake/packages.cmake, docs/modular-graph-phase-e1-design.md section 4): each reads its
+# manifest from its package.json and adds its native/ tree. The node SDK first, as every other package links it.
+include(cmake/packages.cmake)
+retroplug_package(node-sdk)        # rp_node.h, node.hpp and the description library: what a pack depends on
+retroplug_package(graph-runtime)   # the executor, the planner, the registry, GraphHost, rp.host.audio-out
+retroplug_package(core)            # the rp.core nodes
+retroplug_package(dsp)             # the kernels, the rp.dsp nodes and rp.dsp.resample
+# The node types not yet carved into packages of their own (sameboy, mesen, host.io, n8, launchpad) register from
+# packages/native, the engine's tree, whose manifest names their entries; its targets are defined below.
+retroplug_package(native)
+
+# The packages a desktop product carries (retroplug_product), every one today; a product that differs lists its own.
+# The ORDER is load-bearing: it is the order the products register in, which the registration golden holds
+# (packages/native/test/engine-graph/golden/product-registrations.json), and it reproduces the list the engine
+# registered before products declared theirs: rp.core, rp.dsp, rp.dsp.resample, then rp.host (graph-runtime, after
+# dsp for that reason alone), then the types still in packages/native.
+set(RP_DESKTOP_PACKAGES core dsp graph-runtime native)
+
 # --- the native package: retroplug-core (shared emulator/Project lib) + the Backend / txiki host
 # + the DPF plugin + the test host / cli / ui-test.
 add_subdirectory(packages/native native)
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index 77364b1c1..4437b1aa9 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -9,7 +9,6 @@
 #   cmake/bundles.cmake       the embedded control-plane, React-UI and script-node bytecode
 #   cmake/deps.cmake          rtmidi / serial / portaudio, and the retroplug-cli N8 facets
 #   cmake/plugin.cmake        dpf_add_plugin + per-platform packaging
-#   cmake/graph.cmake         the node-graph libraries the engine runs on
 #   cmake/tests.cmake         the twelve native test binaries
 #   cmake/warnings.cmake      /W4 /WX on our own targets (MSVC)
 #
@@ -36,64 +35,10 @@ endif()
 # The shape shared by every native test binary (see the file for why).
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/test-binary.cmake)
 
-# --- retroplug-dsp-kernels: the DSP the rp.dsp nodes run (src/dsp/, docs/modular-graph-phase-d1-design.md section 4):
-# gain and peak, the RBJ biquads, dither / quantise, and risa's DMC encoder (a whole-buffer kernel, Dpcm.hpp says why;
-# the others stream a block at a time). A kit compile is a chain of those nodes, so it reaches this DSP through them.
-# Core-free on purpose: it links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node
-# package can use it with no core, txiki, backend or r8brain.
-# (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.)
-#
-# The resampler is its own target on purpose, because its kernel is the one that is not freestanding: r8brain brings
-# the heap, pthreads (its filter cache is a std::mutex) and RTTI (its resamplers are virtual). A build that has no use
-# for it - the Pico's, which runs rp.dsp's gain / biquad / dither / normalize alone - omits the target and never sees
-# any of that, and the rp.dsp package's resample node, which is the only node that needs it, is a target of its own for
-# the same reason.
-add_library(retroplug-dsp-kernels STATIC
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Gain.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dpcm.cpp)
-target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
-set_target_properties(retroplug-dsp-kernels PROPERTIES
-    CXX_STANDARD 20
-    CXX_STANDARD_REQUIRED ON
-    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through the rp.dsp node package
-
-# r8brain's static members are vendored code, so they get a target of their own rather than riding in one of ours:
-# /W4 /WX is scoped to RetroPlug's own targets (cmake/warnings.cmake), which this one is deliberately not on, and
-# GNU/Clang get the same `-w` a vendored dep gets (retroplug_silence_dep_warnings). Its one source includes r8brain's
-# r8bbase.cpp through dsp/R8bAllocator.hpp, so every r8brain class in the program is built on the throwing allocator;
-# one translation unit in one target means there is no second definition of those classes to keep, and the tree
-# declares no target for the vendored library, so there is none to link.
-add_library(retroplug-r8brain-base STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/R8bBase.cpp)
-target_include_directories(retroplug-r8brain-base PRIVATE
-    ${CMAKE_CURRENT_SOURCE_DIR}/src
-    ${CMAKE_SOURCE_DIR}/deps/r8brain)
-set_target_properties(retroplug-r8brain-base PROPERTIES
-    CXX_STANDARD 20
-    CXX_STANDARD_REQUIRED ON
-    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through the rp.dsp.resample node package
-retroplug_silence_dep_warnings(retroplug-r8brain-base)
-
-# Threads: r8brain's filter cache is a std::mutex, efsw-static needs pthreads on Linux (its CMake links it to the
-# shared lib only), and the Launchpad scan thread and N8Link's serial thread do too. Found once, for this directory.
+# Threads: efsw-static needs pthreads on Linux (its CMake links it to the shared lib only), and the Launchpad scan
+# thread and N8Link's serial thread do too. Found once, for this directory. (The DSP package finds its own, for
+# r8brain.)
 find_package(Threads REQUIRED)
-add_library(retroplug-dsp-resample STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resample.cpp)
-target_include_directories(retroplug-dsp-resample
-    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
-    # r8brain's headers, all this build takes from the vendored dep, stay private to the translation units that include
-    # dsp/R8bAllocator.hpp first: that header is what puts r8brain's classes on the throwing allocator, and a TU that
-    # read them without it would build them on r8brain's defaults.
-    PRIVATE ${CMAKE_SOURCE_DIR}/deps/r8brain)
-# The tree declares no target for the vendored `r8brain` library. Its r8bbase.cpp would build r8brain's static members
-# on r8brain's own malloc hooks, where src/dsp/R8bBase.cpp (in retroplug-r8brain-base) builds them on the throwing
-# allocator the rest of this tree uses, and the linker would keep one copy of every r8brain class with no diagnostic
-# saying which.
-target_link_libraries(retroplug-dsp-resample PRIVATE Threads::Threads retroplug-r8brain-base)
-set_target_properties(retroplug-dsp-resample PROPERTIES
-    CXX_STANDARD 20
-    CXX_STANDARD_REQUIRED ON
-    POSITION_INDEPENDENT_CODE ON)   # linked into the plugin .so through the rp.dsp.resample node package
 
 # --- retroplug-core: the shared emulator core, as a neutrally-named static lib. Extracted out of
 # packages/native/cli (which also compiled the CLI-only HarnessRpcService) so the host links ONLY the
@@ -151,6 +96,11 @@ target_link_libraries(retroplug-core PUBLIC
     # Windows: SameBoySystem.cpp lives in this clang-cl-compiled lib (see root CMakeLists).
     $<$<BOOL:${WIN32}>:retroplug-samebsys>
 )
+# SampleCache's content hash is util/Hash.hpp's FNV-1a, which lives in the DSP package beside the dither kernel that
+# draws its seeds with it. The core takes that header alone from there, its include root and not the kernels'
+# library, which nothing in the core calls.
+target_include_directories(retroplug-core PRIVATE
+    $<TARGET_PROPERTY:retroplug-dsp-kernels,INTERFACE_INCLUDE_DIRECTORIES>)
 
 set_target_properties(retroplug-core PROPERTIES
     CXX_STANDARD 20
@@ -224,9 +174,12 @@ target_include_directories(retroplug-backend PUBLIC
 
 target_link_libraries(retroplug-backend PUBLIC
     retroplug-core      # SystemBase + SameBoy/Mesen; PUBLIC-brings rpcpp + miniz
-    retroplug-graph     # the graph executor, planner and GraphHost the engine runs on
-    retroplug-dsp-nodes # the rp.dsp package (gain, biquad, dither), which the engine registers
-    retroplug-dsp-resample-nodes # and the rp.dsp.resample package beside it
+    retroplug-graph     # the graph executor, planner and GraphHost the engine runs on (packages/graph-runtime)
+    # The core and DSP packages: the offline job pool registers its fixed subset of them itself (OfflineJobs.cpp),
+    # where the engine registers the product's list (rp_register_packages, graph/Packages.hpp).
+    retroplug-core-nodes
+    retroplug-dsp-nodes
+    retroplug-dsp-resample-nodes
     tjs                 # txiki.js/QuickJS runtime (transitively brings qjs + libuv)
     efsw-static         # NativeFileWatcher's filesystem watcher (PUBLIC-brings efsw/efsw.hpp include dir)
     Threads::Threads
@@ -243,6 +196,7 @@ set_target_properties(retroplug-backend PROPERTIES
 add_executable(retroplug-host src/main.cpp)
 
 target_link_libraries(retroplug-host PRIVATE retroplug-backend)
+retroplug_product(retroplug-host PACKAGES ${RP_DESKTOP_PACKAGES})
 
 set_target_properties(retroplug-host PROPERTIES
     CXX_STANDARD 20
@@ -255,6 +209,7 @@ set_target_properties(retroplug-host PROPERTIES
 # build-tsan/ and build-asan/. EXCLUDE_FROM_ALL: built by name, not part of a normal build.
 add_executable(retroplug-render-host-test EXCLUDE_FROM_ALL test/render-host-main.cpp)
 target_link_libraries(retroplug-render-host-test PRIVATE retroplug-backend)
+retroplug_product(retroplug-render-host-test PACKAGES ${RP_DESKTOP_PACKAGES})
 set_target_properties(retroplug-render-host-test PROPERTIES
     CXX_STANDARD 20
     CXX_STANDARD_REQUIRED ON
@@ -343,6 +298,7 @@ endif()
 add_executable(retroplug-cli cli/main.cpp ${CLI_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C} ${SDK_ASSETS_C} ${SDK_MODULE_C})
 
 target_link_libraries(retroplug-cli PRIVATE retroplug-backend)
+retroplug_product(retroplug-cli PACKAGES ${RP_DESKTOP_PACKAGES})
 add_dependencies(retroplug-cli retroplug-cli-bundle)             # the embedded dispatcher bytecode must exist first
 add_dependencies(retroplug-cli retroplug-ts-stripper-bundle)     # ...and the stripper bytecode
 add_dependencies(retroplug-cli retroplug-cli-sdk-assets)         # ...and the embedded SDK
@@ -430,6 +386,7 @@ target_link_libraries(retroplug-sdl PRIVATE
     portaudio                     # audio backend (PipeWire/ALSA on Linux); replaces SDL audio
     serial                        # wjwwood/serial -> Everdrive N8 Pro (cmake/serial.cmake)
     PkgConfig::SDL2)              # window + present + gamepad (audio now via PortAudio; found at root scope)
+retroplug_product(retroplug-sdl PACKAGES ${RP_DESKTOP_PACKAGES})
 
 target_include_directories(retroplug-sdl PRIVATE
     # quickjs.h for the JS trampolines (lvgl-js-native PUBLIC-propagates the lv_binding_js render include
@@ -480,6 +437,7 @@ add_executable(retroplug-ui-test
 target_link_libraries(retroplug-ui-test PRIVATE
     lvgl-js-native                # LvglJsEngine + lvgl + lv_binding_js native + lodepng (+ tjs)
     retroplug-backend) # BackendFacade + rpcpp + the cores + the txiki host
+retroplug_product(retroplug-ui-test PACKAGES ${RP_DESKTOP_PACKAGES})
 
 target_include_directories(retroplug-ui-test PRIVATE
     # quickjs.h for the JS trampolines in RenderCore/UiRunner (lvgl-js-native
@@ -495,11 +453,19 @@ set_target_properties(retroplug-ui-test PROPERTIES
 # The embedded bundle must exist before the test binary compiles it.
 add_dependencies(retroplug-ui-test retroplug-ui-bundle)
 
-# The node-graph libraries (docs/modular-graph-design.md); retroplug-backend links them, as the engine runs on them.
-include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/graph.cmake)
-
 # The twelve native test binaries (EXCLUDE_FROM_ALL; built by name).
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests.cmake)
 
+# Every product declared above, with its packages' entries, for the registration golden (test/engine-graph/
+# Packages.test.cpp). After the last retroplug_product(), which tests.cmake and plugin.cmake hold.
+retroplug_product_table(retroplug-engine-graph-test)
+
+# Every binary that links the engine's libraries is a product, refused here if it is not (cmake/packages.cmake). The two
+# exempt link the backend for its file watcher and its txiki host, and never pull the engine's object in.
+retroplug_check_products(
+    LIBRARIES retroplug-backend retroplug retroplug-dsp retroplug-ui
+    EXEMPT    retroplug-classid-test      # TjsHostRuntime + ClassIdSpace.hpp
+              retroplug-watcher-test)     # NativeFileWatcher + efsw-static
+
 # /W4 /WX on our own targets (MSVC), with per-number suppressions for what we do not own.
 include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/warnings.cmake)
diff --git a/packages/native/cmake/node-addon.cmake b/packages/native/cmake/node-addon.cmake
index d4aaacbf4..e2099b629 100644
--- a/packages/native/cmake/node-addon.cmake
+++ b/packages/native/cmake/node-addon.cmake
@@ -41,6 +41,7 @@ if(RETROPLUG_NODE_ADDON)
         ${CMAKE_CURRENT_SOURCE_DIR}/node
     )
     target_link_libraries(retroplug-node PRIVATE retroplug-backend)
+    retroplug_product(retroplug-node PACKAGES ${RP_DESKTOP_PACKAGES})
     set_target_properties(retroplug-node PROPERTIES
         CXX_STANDARD 20
         CXX_STANDARD_REQUIRED ON
diff --git a/packages/native/cmake/plugin.cmake b/packages/native/cmake/plugin.cmake
index 55bf36277..3c495b584 100644
--- a/packages/native/cmake/plugin.cmake
+++ b/packages/native/cmake/plugin.cmake
@@ -45,6 +45,29 @@ target_link_libraries(retroplug PUBLIC
 
 target_compile_definitions(retroplug PUBLIC DPFJS_ENV_PREFIX="RETROPLUG_")
 
+# The plugin is one product (cmake/packages.cmake): every binary DPF builds from the plugin's DSP or UI library composes
+# the engine, so each gets the product's generated rp_register_packages(). They are read from what dpf_add_plugin made
+# rather than listed by format, since a format can make more than its own module (on macOS `au` also builds
+# retroplug-export, the executable its POST_BUILD runs to write the component's Info.plist) and a format added later
+# must not be missed. (Not on DPF's static `retroplug` library: an archive's object would sit before the backend that
+# calls it on a single-pass linker's command line.) A static format would have to make its consumer the product, so it
+# is refused here until one needs it.
+set(_rp_plugin_binaries "")
+get_property(_rp_dir_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
+foreach(_t IN LISTS _rp_dir_targets)
+    get_target_property(_links ${_t} LINK_LIBRARIES)
+    if(NOT ("retroplug-dsp" IN_LIST _links OR "retroplug-ui" IN_LIST _links))
+        continue()
+    endif()
+    get_target_property(_type ${_t} TYPE)
+    if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
+        list(APPEND _rp_plugin_binaries ${_t})
+    else()
+        message(FATAL_ERROR "plugin.cmake: DPF's ${_t} is a ${_type} built from the plugin; it cannot be a product")
+    endif()
+endforeach()
+retroplug_product(retroplug PACKAGES ${RP_DESKTOP_PACKAGES} TARGETS ${_rp_plugin_binaries})
+
 # The embedded bundles must exist before the plugin compiles them.
 add_dependencies(retroplug retroplug-cp-bundle retroplug-ui-bundle)
 
diff --git a/packages/native/cmake/test-binary.cmake b/packages/native/cmake/test-binary.cmake
index 22f12e66c..3895fa32b 100644
--- a/packages/native/cmake/test-binary.cmake
+++ b/packages/native/cmake/test-binary.cmake
@@ -15,13 +15,16 @@
 #                             INCLUDES ${RP_QUICKJS_INCLUDE}
 #                             DEFS     RP_MGB_ROM_PATH="...")
 #
+# PACKAGES makes the binary a product (retroplug_product, cmake/packages.cmake): one that constructs an Engine lists
+# the node packages that engine registers, and gets its generated rp_register_packages().
+#
 # NO_CATCH2 opts out of the Catch2 link for a binary that brings its own main() —
 # retroplug-n8-hwtest is the one, a manual harness driven against real hardware.
 #
 # Sources are interpreted relative to packages/native/ (CMAKE_CURRENT_SOURCE_DIR at the point
 # this is included), matching how they were written before.
 function(retroplug_add_test_binary)
-    cmake_parse_arguments(T "NO_CATCH2" "NAME" "SOURCES;LIBS;INCLUDES;DEFS" ${ARGN})
+    cmake_parse_arguments(T "NO_CATCH2" "NAME" "SOURCES;LIBS;INCLUDES;DEFS;PACKAGES" ${ARGN})
 
     if(NOT T_NAME)
         message(FATAL_ERROR "retroplug_add_test_binary: NAME is required")
@@ -48,6 +51,10 @@ function(retroplug_add_test_binary)
         target_compile_definitions(${T_NAME} PRIVATE ${T_DEFS})
     endif()
 
+    if(T_PACKAGES)
+        retroplug_product(${T_NAME} PACKAGES ${T_PACKAGES})
+    endif()
+
     set_target_properties(${T_NAME} PROPERTIES
         CXX_STANDARD 20
         CXX_STANDARD_REQUIRED ON
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 4ac907812..f522cc7c0 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -76,6 +76,7 @@ retroplug_add_test_binary(
              ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
     LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
              retroplug-dsp-kernels                     # Effects.cpp's wrappers call these
+    PACKAGES ${RP_DESKTOP_PACKAGES}
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
              RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
@@ -117,6 +118,7 @@ retroplug_add_test_binary(
              test/engine-graph/ProtocolNodes.test.cpp
              test/engine-graph/ScriptBench.test.cpp    # hidden: run with "[bench]"
              test/engine-graph/BuildGraphGoldens.test.cpp
+             test/engine-graph/Packages.test.cpp       # each product registers the node types its golden row holds
              test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
              src/host/n8/Edio.cpp                      # the cart's adapters N8Adapters.test.cpp composes: serial-free,
              src/host/n8/N8Link.cpp                    # as retroplug-n8-test compiles them (the backend holds none of
@@ -126,6 +128,7 @@ retroplug_add_test_binary(
     LIBS     retroplug-backend                         # Engine + retroplug-core (SystemBase + the cores)
              retroplug-dsp-kernels                     # OfflineJobs.test.cpp holds a job's buffers to the kernels
              retroplug-dsp-resample                    # and to resampledLength, which the resample's node wraps
+    PACKAGES ${RP_DESKTOP_PACKAGES}
     INCLUDES ${RP_QUICKJS_INCLUDE}
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
              RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
@@ -139,6 +142,7 @@ retroplug_add_test_binary(
              RP_CORE_ROLES_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/coreRoles.ts"  # their ranges
              RP_SYSTEM_SETTINGS_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/systemSettings.ts"  # gain's range
              RP_PORTS_TS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/script/nodes/ports.ts"  # the queue capacities
+             RP_PRODUCT_REGISTRATIONS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/engine-graph/golden/product-registrations.json"
              RP_BUILD_TYPE="$<CONFIG>")                 # the bench prints the configuration it was built in
 add_dependencies(retroplug-engine-graph-test retroplug-script-test-bundle)  # the embedded bytecode first
 
@@ -175,8 +179,9 @@ retroplug_add_test_binary(
              src/host/launchpad/LaunchpadScanner.cpp
              test/graph/CountingAllocator.cpp          # the block thread's drain allocates and frees nothing
     LIBS     Threads::Threads                          # LaunchpadScanner's scan thread
+             retroplug-node-sdk-headers                # transport/SpscRing.hpp, the rings' type
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
-             ${CMAKE_CURRENT_SOURCE_DIR}/src)          # host/launchpad/*.hpp + transport/SpscRing.hpp
+             ${CMAKE_CURRENT_SOURCE_DIR}/src)          # host/launchpad/*.hpp
 
 # Guards the Everdrive N8 Pro protocol framing (Edio): that fifoWR emits the exact krikzz CMD_MEM_WR
 # byte stream to ADDR_FIFO and the connect handshake accepts / rejects the 0xA5 status word; plus the
@@ -197,6 +202,7 @@ retroplug_add_test_binary(
              src/host/n8/N8Menu.cpp
     LIBS     reflectcpp::reflectcpp                    # rfl::json - parse the shared Edio golden (twins edio.test.ts)
              Threads::Threads                          # N8Link's serial thread
+             retroplug-node-sdk-headers                # transport/SpscRing.hpp, N8Link's rings
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src           # host/n8/Edio.hpp
     # The shared Edio framing golden both this and edio.test.ts assert against (single source, no drift).
     DEFS     EDIO_GOLDEN_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/n8/edio-golden.json")
@@ -226,6 +232,7 @@ retroplug_add_test_binary(
              src/host/n8/WjwwoodSerialPort.cpp
     LIBS     serial
              Threads::Threads
+             retroplug-node-sdk-headers                # transport/SpscRing.hpp, N8Link's rings
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src)
 # `timing` times against an ALSA capture of the console's audio, so it exists only where ALSA does. Linking
 # ALSA::ALSA unconditionally failed the GENERATE step on macOS/Windows - EXCLUDE_FROM_ALL does not spare a target
@@ -280,6 +287,7 @@ retroplug_add_test_binary(
              # named in LIBS below, and nothing of the core.
              ${CMAKE_CURRENT_SOURCE_DIR}/src/lsdj/Effects.cpp
     LIBS     retroplug-graph
+             retroplug-core-nodes                     # the rp.core package under test
              retroplug-dsp-nodes                      # the rp.dsp package under test
              retroplug-dsp-resample-nodes             # and the rp.dsp.resample package
              retroplug-dsp-kernels                    # core-free: the standard library
@@ -288,4 +296,6 @@ retroplug_add_test_binary(
     DEFS     RP_GRAPH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/graph/golden"
     # Dsp.test.cpp includes dsp/R8bAllocator.hpp first, and that header includes r8brain's CDSPResampler.h. The dep's
     # include directory is private to the resampler target, so the one test that reads it asks for it here.
-    INCLUDES ${CMAKE_SOURCE_DIR}/deps/r8brain)
+    INCLUDES ${CMAKE_SOURCE_DIR}/deps/r8brain
+             # The engine tree's headers it reads beside the packages: lsdj/Effects.hpp, system/InputCapacity.hpp.
+             ${CMAKE_CURRENT_SOURCE_DIR}/src)
diff --git a/packages/native/cmake/warnings.cmake b/packages/native/cmake/warnings.cmake
index a390c0155..db6d69681 100644
--- a/packages/native/cmake/warnings.cmake
+++ b/packages/native/cmake/warnings.cmake
@@ -38,8 +38,10 @@ if(MSVC)
     set(_rp_werror_targets
         retroplug-core retroplug-backend retroplug-host retroplug-cli retroplug-sdl
         retroplug retroplug-dsp retroplug-ui
-        retroplug-graph-exec retroplug-graph retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes
-        retroplug-dsp-resample-nodes)
+        # The workspace packages' targets (cmake/packages.cmake), defined in their own directories: a target's options
+        # can be set from any directory, so the list stays here, where every one of our targets is named.
+        retroplug-node-sdk retroplug-graph-exec retroplug-graph retroplug-core-nodes
+        retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes retroplug-dsp-resample-nodes)
     foreach(_t IN LISTS _rp_werror_targets)
         if(TARGET ${_t})
             target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 2db4e8c0f..1454d3a96 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -25,11 +25,8 @@
 
 #include "graph/Descriptor.hpp"
 #include "graph/GraphHost.hpp"
-#include "graph/HostNodes.hpp"
+#include "graph/Packages.hpp"  // rp_register_packages: the product's node packages
 #include "graph/Queue.hpp"
-#include "graph/nodes/CoreNodes.hpp"
-#include "graph/nodes/DspNodes.hpp"
-#include "graph/nodes/DspResampleNodes.hpp"
 #include "host/dsp/ScriptCompiler.hpp"
 #include "host/devices/DeviceRegistry.hpp"
 #include "host/engine/DspCommand.hpp"
@@ -194,10 +191,10 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
         graph_.get());
     if (!graph_->script.load(scriptBundle, &why))
         std::fprintf(stderr, "[engine] the script-node bundle did not load (%s); no script node type is registered\n", why.c_str());
-    // The routing, the controller and the protocol nodes are the script package's (src/script/nodes/).
-    for (const rp_package* p : {rp::graph::corePackage(), rp::graph::dspPackage(), rp::graph::dspResamplePackage(),
-                                rp::graph::hostPackage(), rp::host::sameboyPackage(), rp::host::mesenPackage(),
-                                rp::host::hostIoPackage(), rp::host::n8Package(), rp::host::launchpadPackage()})
+    // The native node packages are the product's: the list its CMake declares (retroplug_product,
+    // cmake/packages.cmake), generated into its rp_register_packages(). The routing, the controller and the protocol
+    // nodes are the script package's (src/script/nodes/).
+    for (const rp_package* p : rp::graph::productPackages())
         if (!graph_->registry.addPackage(p, &why))
             std::fprintf(stderr, "[engine] a node package was refused (%s); its types are not registered\n", why.c_str());
     if (!graph_->registry.addPackage(graph_->script.package(), &why))
````

Run: `sed -n '1733,2150p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0. It reads the five manifests; a FATAL from `retroplug_package`, `retroplug_product` or `retroplug_check_products` here means a step above was placed wrong.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0: every product's generated registration (`build/packages/products/<product>.cpp`) compiles into it, and every moved file is found at its new place. Most of the tree recompiles, since the include roots moved.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-engine-graph-test "[packages]"`
Expected: `All tests passed (464 assertions in 2 test cases)` (the report: `[packages]`: 464 assertions in 2 cases; this worktree's build has the Node addon on, so nine products are declared).

- [ ] **Step 6: Show that the golden bites**

The order of `RP_DESKTOP_PACKAGES` is the registration order, and the golden holds it. Apply this temporary mutation,
which registers `rp.host` before the DSP packages, and the first `[packages]` case must fail; then reverse it. The
edit re-runs the configure when the binary builds.

````patch
diff --git a/CMakeLists.txt b/CMakeLists.txt
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -370,7 +370,7 @@
 # (packages/native/test/engine-graph/golden/product-registrations.json), and it reproduces the list the engine
 # registered before products declared theirs: rp.core, rp.dsp, rp.dsp.resample, then rp.host (graph-runtime, after
 # dsp for that reason alone), then the types still in packages/native.
-set(RP_DESKTOP_PACKAGES core dsp graph-runtime native)
+set(RP_DESKTOP_PACKAGES core graph-runtime dsp native)
 
 # --- the native package: retroplug-core (shared emulator/Project lib) + the Backend / txiki host
 # + the DPF plugin + the test host / cli / ui-test.
````

Run: `sed -n '2175,2186p' docs/modular-graph-phase-e1-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: exit 0 (the configure re-runs first).

Run: `build/bin/retroplug-engine-graph-test "[packages]"`
Expected: fails in `each product registers exactly the node types its golden row holds`, its `CHECK(ids == it->second)` once for each product the configure declares (nine here; `rp.host.audio-out` now comes after `rp.core`'s ten ids, where the golden has `rp.dsp.gain`); the second case passes, since this binary's registration and its row moved together. (Predicted from the test; the stage proved the golden bites another way: `a mutated golden id was confirmed to fail`.)

Run: `sed -n '2175,2186p' docs/modular-graph-phase-e1-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `CMakeLists.txt` is the task's file again (`git diff` of it shows only the task's own change, if the task changes it at all). Then `cmake --build build --target retroplug-engine-graph-test -j$(nproc)` and `build/bin/retroplug-engine-graph-test "[packages]"`: `All tests passed (464 assertions in 2 test cases)` again.

- [ ] **Step 7: Show that the product check bites**

A binary that links the engine without being a product would have no `rp_register_packages()`; a Linux module would
load and fail only then, so the configure refuses it. Apply this temporary mutation, an executable that links
`retroplug-backend` and is not a product, and configure a scratch tree (so `build/` is not left half-configured): the
configure must stop. Then reverse it and remove the scratch tree.

````patch
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -197,6 +197,8 @@
 
 target_link_libraries(retroplug-host PRIVATE retroplug-backend)
 retroplug_product(retroplug-host PACKAGES ${RP_DESKTOP_PACKAGES})
+add_executable(rp-scratch-not-a-product EXCLUDE_FROM_ALL src/main.cpp)
+target_link_libraries(rp-scratch-not-a-product PRIVATE retroplug-backend)
 
 set_target_properties(retroplug-host PROPERTIES
     CXX_STANDARD 20
````

Run: `sed -n '2209,2220p' docs/modular-graph-phase-e1-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake -S . -B /tmp/e1-scratch`
Expected: fails, after about 20 s of configuring, with this error, word-wrapped by CMake as the stage's own run of this check printed it (grep for `rp-scratch-not-a-product`, which fits on one line):

````text
CMake Error at cmake/packages.cmake:229 (message):
  retroplug_check_products: these binaries link the engine and are not
  products, so none defines rp_register_packages: rp-scratch-not-a-product
  (links retroplug-backend).  Declare each with retroplug_product() (or
  PACKAGES in retroplug_add_test_binary), or exempt one that never pulls the
  engine in.
Call Stack (most recent call first):
  packages/native/CMakeLists.txt:467 (retroplug_check_products)
````

then `-- Configuring incomplete, errors occurred!`.

Run: `sed -n '2209,2220p' docs/modular-graph-phase-e1-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `packages/native/CMakeLists.txt` is the task's file again (`git diff` of it shows only the task's own change, if the task changes it at all). Then `rm -rf /tmp/e1-scratch`.

- [ ] **Step 8: The doc links and the comments that name a moved path**

`pnpm check:docs` must pass at every task, so the links to moved files are fixed here, the link targets only: the
three that named the deleted `cmake/graph.cmake` now point at `packages/dsp/native/CMakeLists.txt` and
`packages/graph-runtime/native/CMakeLists.txt`, their link text unchanged until Task 5. The TypeScript twins' comments
that name a moved C++ file get its new path. `docs/*-plan.md` are records and are never edited.

````diff
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 550f23785..55bb003e1 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -343,7 +343,7 @@ as the same C structures. The shape follows CLAP: a small core table, plus optio
 by versioned string id. Statically linked packages are registered through the same entry point as
 downloaded ones, so there is one loading path.
 
-A sketch, abridged from [rp_node.h](../packages/native/src/graph/rp_node.h), which is the reference:
+A sketch, abridged from [rp_node.h](../packages/node-sdk/native/src/graph/rp_node.h), which is the reference:
 
 ```c
 #define RP_ABI_VERSION 1
diff --git a/docs/modular-graph-phase-b3a-design.md b/docs/modular-graph-phase-b3a-design.md
index 2a23e3463..7d2ee7778 100644
--- a/docs/modular-graph-phase-b3a-design.md
+++ b/docs/modular-graph-phase-b3a-design.md
@@ -104,7 +104,7 @@ cannot be read refuses the whole package, and a second `load` is refused, leavin
 Every type is served by the same functions, which the ABI could not tell apart: its type-level calls had no context
 (§10). So `descriptor`, `layout_for`, `query_memory` and `create` now take the `const rp_node_type*`
 they are called through, and `rp_node_type` gains a trailing `ctx`, null for a C++ node and never read by the host,
-which for a script type points at its definition's record ([rp_node.h](../packages/native/src/graph/rp_node.h)).
+which for a script type points at its definition's record ([rp_node.h](../packages/node-sdk/native/src/graph/rp_node.h)).
 
 ### 2.3 Marshalling
 
@@ -184,14 +184,14 @@ instance, with the layout `layout_for` gives for the new settings.
   now refused (no in-tree node did so, and ABI v1 has not shipped).
 - A reshape keeps the instance (its node id and type are unchanged) and binds the new plan's ports to it **by
   stable port id**. What is bound to a layout lives in a per-layout `PortState`, which each plan holds
-  ([PlanStorage.hpp](../packages/native/src/graph/PlanStorage.hpp)): the layout, the param, control-output and
+  ([PlanStorage.hpp](../packages/graph-runtime/native/src/graph/PlanStorage.hpp)): the layout, the param, control-output and
   resource-output stores, and the control side's records (the document values sent, the wired params, the last
   mask). A reshape builds a new one on the control thread while the running plan still uses the old, sharing into it,
   never copying, each store whose port or param persists: a param whose id, type, range and enum labels match, a
   control or resource output whose id, kind and resource type match. The rest start afresh: a param at its default,
   its document value (if the document gives one) sent again, a control output at 0, a resource slot empty.
 - Feedback wires keep their last block, as they already do across a plan swap when a wire's key (its two
-  endpoints) and shape are unchanged ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`). A queue
+  endpoints) and shape are unchanged ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). A queue
   front whose source's capacity changed carries its block too while no audio thread runs, counting what the new
   capacity cannot hold as the source's drops, and starts empty while one does (the control thread may not read a
   front the audio thread is filling); a front whose channel count changed starts silent.
@@ -211,7 +211,7 @@ config, the MIDI routing, the controller) carries one to the kernel. So the wait
 commit whose node reads its current settings to prepare. A type promises the opposite with
 `RP_TYPE_STATELESS_PREPARE` on `rp_node_type.flags`, a plan records whether any commit it carries is to a type
 without it (`PlanStorage::awaitsAdoption`), and `GraphHost::waitForCommits` waits only for such plans
-([GraphHost.cpp](../packages/native/src/graph/GraphHost.cpp)). Every script type is stateless (the control-side
+([GraphHost.cpp](../packages/graph-runtime/native/src/graph/GraphHost.cpp)). Every script type is stateless (the control-side
 runtime checks the new settings alone, and the planner already knows whether the layout moved); a C++ node says so
 with `static constexpr bool stateless_prepare`, which a `static_assert` refuses wherever its prepare would call
 `needs_rebuild` (that reads the current settings). Consecutive kernel commits then queue on the ring and apply in
diff --git a/docs/modular-graph-phase-b3b-design.md b/docs/modular-graph-phase-b3b-design.md
index fba655ad9..6254e9f81 100644
--- a/docs/modular-graph-phase-b3b-design.md
+++ b/docs/modular-graph-phase-b3b-design.md
@@ -113,7 +113,7 @@ protocol node", one table ([graph/protocolNodes.ts](../packages/retroplug/src/gr
 
 A profile build's `apu-render` span begins where the last script node's `process` ended, which is right only while
 every script node runs before the systems. The planner keeps document order among nodes ready at once
-([Planner.cpp](../packages/native/src/graph/Planner.cpp) `order()`), so the span needed no change.
+([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `order()`), so the span needed no change.
 
 ## 5. Host I/O, capacities, and what must match
 
diff --git a/docs/modular-graph-phase-c1-design.md b/docs/modular-graph-phase-c1-design.md
index ca0216069..e5c85c5a8 100644
--- a/docs/modular-graph-phase-c1-design.md
+++ b/docs/modular-graph-phase-c1-design.md
@@ -206,7 +206,7 @@ ring (release on push, acquire on adopt) carries the whole built pack with it.
      holds already, or a type id another package holds.
   3. The Engine replans its current document: the last request `setGraph` published, with its tap carried as it is
      (the request re-arms no tap, so its swap carries none: section 11's second question). The planner already
-     rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/native/src/graph/Planner.cpp)
+     rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp)
      `reconcile`), so every node of the pack gets a fresh instance of the new code under the same node id, its JS state
      not carried over, while every other node keeps its instance. A document that places none of the old
      generation's types would plan as it runs, so it is not replanned. The replan leaves a core parked since that
diff --git a/docs/modular-graph-phase-c3-design.md b/docs/modular-graph-phase-c3-design.md
index c1bcd08a2..d72997ec4 100644
--- a/docs/modular-graph-phase-c3-design.md
+++ b/docs/modular-graph-phase-c3-design.md
@@ -44,7 +44,7 @@ construct-time fields ... stay in the build spec until C3 builds cores in their
   and `adopt` (a project load: one build per system, then one publish). System ids are never reused, so every rebuild
   is a new id and a new node id `sys-<id>`. The load-time roles' `onConstruct` hooks fold asset overrides into
   `romBytes` (`lsdj-assets`, `risa-assets`, `bliptoaster-assets`) or seed an empty LSDj battery (`lsdj-sync`).
-- **Resources** exist in the graph layer ([ResourceTable.hpp](../packages/native/src/graph/ResourceTable.hpp)):
+- **Resources** exist in the graph layer ([ResourceTable.hpp](../packages/graph-runtime/native/src/graph/ResourceTable.hpp)):
   immutable byte blocks with one owner and a pin per plan binding, freed once the owner has dropped them and no pin is
   left, ids never reused. Nothing creates one over RPC yet, and no node reads one from its settings: main design
   section 2.5 planned that host service "with the first system node".
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index e3149820e..d6f948f0e 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -96,7 +96,7 @@ goes through the command ring; all audio→control observation goes through the
 
 The `Engine` itself is **single-threaded and thread-unaware**: it holds no locks and knows
 nothing about who is calling it. Thread-safety is entirely a property of the two rings (both
-inside the Engine's [`GraphHost`](../packages/native/src/graph/GraphHost.hpp)) and the registry
+inside the Engine's [`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp)) and the registry
 that surround it, plus the script host's own pair, which carries a destroyed script node's release
 to the block thread and its record back. One control-thread-written bit,
 [`QueuedInvoker::audioThreadOwns_`](../packages/native/src/host/engine/EngineInvoker.hpp),
@@ -131,7 +131,7 @@ Three primitives, three directions:
 - **release ring**: audio → control, a replaced plan handed back so the audio thread never frees (§5).
 
 All three are built on the shared-core transport primitives:
-[`SpscRing`](../packages/native/src/transport/SpscRing.hpp),
+[`SpscRing`](../packages/node-sdk/native/src/transport/SpscRing.hpp),
 [`FrameBufferTriple`](../packages/native/src/transport/FrameBufferTriple.hpp), and
 [`MemorySnapshotTriple`](../packages/native/src/transport/MemorySnapshotTriple.hpp).
 
@@ -140,7 +140,7 @@ All three are built on the shared-core transport primitives:
 ## 3. The command ring — the ONE mutation path
 
 Every control-plane edit lands on ONE ring: the `SpscRing<Command, 256>` inside the Engine's
-[`GraphHost`](../packages/native/src/graph/GraphHost.hpp)
+[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp)
 ([modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md) section 5). An edit
 arrives one of three ways:
 
@@ -562,15 +562,15 @@ lost mutation.
 | [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) | mounts the capability facets onto an RPC server over the shared service instances |
 | [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) | `QueuedInvoker`: every edit but structure onto the ring, `audioThreadOwns_` |
 | [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp) | the POD command the ring carries as an opaque host command |
-| [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp) | the one command ring, the release ring of plans, `setGraph` / `drain` / `run` / `reclaim` |
+| [`GraphHost.hpp`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/graph-runtime/native/src/graph/GraphHost.cpp) | the one command ring, the release ring of plans, `setGraph` / `drain` / `run` / `reclaim` |
 | [`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts) / [`engineGraph.ts`](../packages/retroplug/src/engineGraph.ts) | the engine's graph as TS builds it from the stores, and its publisher (the `ProjectStore`'s, or a `DirectGraph`'s or bare `SystemsStore`'s own) |
 | [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) / [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) / [`N8Nodes.cpp`](../packages/native/src/host/graph/N8Nodes.cpp) / [`LaunchpadNodes.cpp`](../packages/native/src/host/graph/LaunchpadNodes.cpp) / [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp) | the cores as nodes (each owning its core), the host MIDI I/O nodes, the cart's node and the control surface's two, and the host services they ask for |
 | [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) | the id-keyed read door; claim/publishSystem/read/release |
 | [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), its one `SystemFactory`, the staged host MIDI, the host outputs and the registry; `buildSystem` / `destroySystem` / `setGraph` / `loadScriptPack`, `processBlock`, the command handler, `SyncTap` |
 | [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in packs, each two bare QuickJS runtimes of its own (the built-in pack first, more loaded and reloaded at runtime), and the audio runtimes' profiling |
 | [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
-| [`src/dsp/`](../packages/native/src/dsp) | the `rp.dsp` nodes' kernels, core-free: gain and peak, the normalising gain, the RBJ biquads, dither and quantise, the DMC encode, and the r8brain resampler |
-| [`DspNodes.cpp`](../packages/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither, normalize, dpcm) and the `rp.dsp.resample` package beside it |
+| [`src/dsp/`](../packages/dsp/native/src/dsp) | the `rp.dsp` nodes' kernels, core-free: gain and peak, the normalising gain, the RBJ biquads, dither and quantise, the DMC encode, and the r8brain resampler |
+| [`DspNodes.cpp`](../packages/dsp/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither, normalize, dpcm) and the `rp.dsp.resample` package beside it |
 | [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp) | the offline job pool: a document run to completion on a host of its own, or a render (an engine job: a `RenderHost` and `Engine` of its own), its inputs, outputs and completion (the request and completion types are in [`OfflineTypes.hpp`](../packages/native/src/host/engine/OfflineTypes.hpp), which `Engine.hpp` includes in place of the pool) |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
 | [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index b8c4a8ca7..3b00ff024 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -251,7 +251,7 @@ nodes (the MIDI routing, the controller session and each role's protocol node) r
 holding the node packages (`rp.core`,
 `rp.host`, `rp.sameboy`, `rp.mesen`, `rp.host.io`, `rp.n8`, `rp.script`, and each script node pack
 `loadScriptPack` loaded), a `ResourceTable`, the one `SystemFactory` every core is built through, the
-[`GraphHost`](../packages/native/src/graph/GraphHost.hpp) running the plan, and every live core by id. Every op
+[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) running the plan, and every live core by id. Every op
 runs "now, on the calling thread"; it holds **no locks**: transport and the ppq clock are plain members mutated
 only by whichever thread owns it (the pull path or the audio loop, never concurrently). The Engine's own
 atomics are block-thread tallies the control thread reads (blocks too long to run and the last one's
@@ -1109,7 +1109,7 @@ its **runtime** classes:
 | `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |
 
 The graph the engine runs on is not in `retroplug-core`: the executor, planner and
-[`GraphHost`](../packages/native/src/graph/GraphHost.hpp), with the `rp.core` mixing nodes and the
+[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp), with the `rp.core` mixing nodes and the
 `rp.host` package's `rp.host.audio-out`, are the `retroplug-graph` / `retroplug-graph-exec` libs
 (`src/graph/`), and the engine's own nodes (`src/host/graph/`: the system nodes, the host MIDI I/O, the cart's node
 and the control surface's two) and the script host (`src/host/script/`) compile into
@@ -1126,7 +1126,7 @@ library alone), `retroplug-dsp-resample` (the r8brain resampler, which brings th
 `retroplug-r8brain-base` (r8brain's static members, one vendored translation unit compiled on an allocator that throws,
 with no target declared for the vendored library; kept off RetroPlug's own warning level), and the two node packages
 `retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and `retroplug-dsp-resample-nodes`
-(`rp.dsp.resample`), in [cmake/graph.cmake](../packages/native/cmake/graph.cmake). `retroplug-core` links neither
+(`rp.dsp.resample`), in [cmake/graph.cmake](../packages/dsp/native/CMakeLists.txt). `retroplug-core` links neither
 (nothing of the core calls a kernel), and `retroplug-graph` links neither; `retroplug-backend` links both packages
 and `Engine.cpp` registers them beside the other static packages, the offline job pool registering them in its own
 registry too. The split is load-bearing: `rp.dsp` never
@@ -1275,7 +1275,7 @@ is one automatable parameter applied post-render across all 8 channels
 - [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp), [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs, the registry and the offline job pool; `processBlock`, `setGraph`, `setParam`, `loadScriptPack`, `submitOfflineJob`, `drainOfflineJobs`, `cancelOfflineJob`, the command handler, `SyncTap`.
 - [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
 - [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes, with their settings and params), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host MIDI I/O nodes), [`N8Nodes.cpp`](../packages/native/src/host/graph/N8Nodes.cpp) (the cart's node), [`LaunchpadNodes.cpp`](../packages/native/src/host/graph/LaunchpadNodes.cpp) (the control surface's two), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
-- [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
+- [`GraphHost.hpp`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/graph-runtime/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/node-sdk/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
 - [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
 - [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
 - [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime and its packs, + the compiler a pack's source and the tests' toys go through; [`src/script/`](../packages/retroplug/src/script) (`bundle.ts`, `register.ts`, `nodes/`): the bundle every Engine embeds, how it registers its nodes, and the script nodes themselves.
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 689ed19c9..dbd4f9b1e 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -37,8 +37,8 @@ All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.tx
 | `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the `rp.dsp` nodes run (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode); the C++ standard library alone. | yes (via the node packages; the two DSP test binaries link it directly) |
 | `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via the resample node package) |
 | `retroplug-r8brain-base` | static lib | r8brain's static members, one vendored translation unit (`R8bBase.cpp` includes `r8bbase.cpp`) compiled on a throwing allocator and linked into `retroplug-dsp-resample`. Kept off RetroPlug's `/W4 /WX` (vendored code keeps the compiler's default level) and given the deps' `-w` on GNU/Clang. | yes (via `retroplug-dsp-resample`) |
-| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in [cmake/graph.cmake](../packages/native/cmake/graph.cmake), so `retroplug-graph` carries neither. | yes (via `retroplug-backend`) |
-| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/native/cmake/graph.cmake)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
+| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in [cmake/graph.cmake](../packages/dsp/native/CMakeLists.txt), so `retroplug-graph` carries neither. | yes (via `retroplug-backend`) |
+| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/graph-runtime/native/CMakeLists.txt)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
 | `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
 | `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
 | `retroplug-sdl` | executable | `build/bin/retroplug-sdl` — the SDL2 standalone ([sdl-standalone.md](../docs/sdl-standalone.md)). Shipped in the CI artifact, deliberately **not** in releases. | yes |
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 86308e267..cc976f9a3 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -587,7 +587,7 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     the input node carries, or a merge of k runs, would bound it.
   - A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front:
     the block in flight is lost, and `droppedMessages` never counts it, since the carry and its count run
-    only while quiescent ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`). No port changes
+    only while quiescent ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). No port changes
     its capacity live today, and no document `buildGraph` builds holds a feedback wire: the Game Boy's `serialOut`,
     fixed, was the one source until LSDj's decoder moved after it.
   - `retroplug-engine-graph-test` replaces the global `operator new` with a counting allocator
````

Run: `sed -n '2253,2461p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

````diff
diff --git a/packages/retroplug/src/graph/canonicalJson.ts b/packages/retroplug/src/graph/canonicalJson.ts
index f89775323..1f2f9c8b2 100644
--- a/packages/retroplug/src/graph/canonicalJson.ts
+++ b/packages/retroplug/src/graph/canonicalJson.ts
@@ -1,5 +1,5 @@
 // The one JSON spelling descriptors, layouts and settings comparisons use, so a TS node and a C++ node of
-// the same shape produce the same bytes. Its C++ twin is packages/native/src/graph/CanonicalJson.cpp:
+// the same shape produce the same bytes. Its C++ twin is packages/node-sdk/native/src/graph/CanonicalJson.cpp:
 // object keys sorted (JavaScript's default order, by UTF-16 code unit, which the C++ side reproduces), no
 // whitespace, strings and numbers exactly as JSON.stringify spells them, undefined object members dropped
 // and undefined array elements (holes included) written as null. Both are held to the same golden tables
diff --git a/packages/retroplug/src/graph/defineNode.ts b/packages/retroplug/src/graph/defineNode.ts
index d28736215..d4b68e604 100644
--- a/packages/retroplug/src/graph/defineNode.ts
+++ b/packages/retroplug/src/graph/defineNode.ts
@@ -272,7 +272,7 @@ const MAX_QUEUE_BYTES = 16777216;
 const q = (s: string): string => JSON.stringify(s);
 
 /** How many channels a layout name means (mono 1, stereo 2, 5.1 6, 7.1 8, discrete(n) n); null for "any"
- *  or an unknown name. The twin of layoutChannels in packages/native/src/graph/Descriptor.cpp. */
+ *  or an unknown name. The twin of layoutChannels in packages/node-sdk/native/src/graph/Descriptor.cpp. */
 export function layoutChannels(layout: string): number | null {
   switch (layout) {
     case "mono": return 1;
diff --git a/packages/retroplug/src/graph/descriptor.ts b/packages/retroplug/src/graph/descriptor.ts
index 8fd3f3878..b29534788 100644
--- a/packages/retroplug/src/graph/descriptor.ts
+++ b/packages/retroplug/src/graph/descriptor.ts
@@ -1,4 +1,4 @@
-// The descriptor model shared with C++ (packages/native/src/graph/Descriptor.hpp): what a node type
+// The descriptor model shared with C++ (packages/node-sdk/native/src/graph/Descriptor.hpp): what a node type
 // declares (TypeDescriptor) and what one instance resolves to for its settings (Layout). These are the
 // JSON shapes, field for field; canonicalJson spells them identically on both sides. The id rules both
 // sides share are here too.
diff --git a/packages/retroplug/src/graph/graphDocument.ts b/packages/retroplug/src/graph/graphDocument.ts
index aa56a534a..aa1e82b52 100644
--- a/packages/retroplug/src/graph/graphDocument.ts
+++ b/packages/retroplug/src/graph/graphDocument.ts
@@ -1,5 +1,5 @@
 // The graph document TS owns and sends to the C++ planner with setGraph - the JSON shape of
-// packages/native/src/graph/GraphDocument.hpp. Node ids may not contain ".", so an endpoint always splits
+// packages/node-sdk/native/src/graph/GraphDocument.hpp. Node ids may not contain ".", so an endpoint always splits
 // at its first dot: "gb1.pins.pulse" is node "gb1", port "pins.pulse".
 import { canonicalJson } from "./canonicalJson";
 import { plainId, wellFormed } from "./descriptor";
diff --git a/packages/retroplug/src/graph/systemResources.ts b/packages/retroplug/src/graph/systemResources.ts
index 60fb4d709..88eb4ccdd 100644
--- a/packages/retroplug/src/graph/systemResources.ts
+++ b/packages/retroplug/src/graph/systemResources.ts
@@ -17,8 +17,8 @@ import { stem } from "../pathUtil";
 export type ResourceBackend = Pick<EmulatorBackend, "putResource" | "releaseResource"> &
   Pick<HostBackend, "fileExists">;
 
-/** A node's settings name a resource as `"res:<id>"` (packages/native/src/graph/node.hpp): the resource's id in the
- *  engine's table, never a path. */
+/** A node's settings name a resource as `"res:<id>"` (packages/node-sdk/native/src/graph/node.hpp): the resource's
+ *  id in the engine's table, never a path. */
 export const resourceRef = (id: number): string => `res:${id}`;
 
 /** A system's resources, as the store counts and DirectGraph places them: the ROM its core boots from, and the seeds
````

Run: `sed -n '2468,2527p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

- [ ] **Step 9: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 and no warning in the prototype's log (the one known warning, `-Wformat-truncation` in `plugin/PluginDSP.cpp`, appears only when that file recompiles).

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 174 test file(s) passed (jobs=16)`: the descriptor conformance and `buildGraph`'s goldens inside it pass, unchanged.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 313 | 302 passed | 11 skipped`; graph-test `All tests passed (356156 assertions in 273 test cases)`; audio-test `All tests passed (1234835 assertions in 108 test cases)`; launchpad `All tests passed (144 assertions in 29 test cases)`; the small six at their own counts (midi 35 / 19, dynparams 24 / 7, plugin 21 / 6, lottie 7 / 1, classid 10 / 3, watcher 11 / 3).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1712306 assertions in 313 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: the same 167 files, with `# 144 case(s) SKIPPED in 65 file(s)` and the skip check passing: CI's view. Nothing in E1 adds a skip, so the skip baseline does not move.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed (jobs=16)`.

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` (fakes only, no hardware).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, then `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa renders match the golden's hashes`, then 5 passed, 0 skipped, 0 failed.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed.

Run: `bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'`
Expected: exit 0: `render-menu` and `render-badge` 1 passed each, then `# 7 test file(s) passed (jobs=16)`.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-bliptoaster && mkdir -p /tmp/e1-bliptoaster/build /tmp/e1-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/e1-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/e1-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/e1-bliptoaster/retroplug-cli/ && cd /tmp/e1-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/e1-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed. The CLI is a product now; a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-nesvj && mkdir -p /tmp/e1-nesvj/build /tmp/e1-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/e1-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/e1-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/e1-nesvj/retroplug-cli/ && cd /tmp/e1-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/e1-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed. The CLI is a product now; a consumer's suite does not change.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

Run: `cmake -S . -B /tmp/e1-fresh`
Expected: exit 0: a fresh tree, so a stale cache in `build/` cannot hide a path that moved.

Run: `cmake --build /tmp/e1-fresh --target retroplug-graph-test retroplug-engine-graph-test retroplug-launchpad-test retroplug-n8-test -j$(nproc)`
Expected: exit 0.

Run: `bash -c '/tmp/e1-fresh/bin/retroplug-graph-test | tail -2; /tmp/e1-fresh/bin/retroplug-engine-graph-test "[packages]" | tail -2; /tmp/e1-fresh/bin/retroplug-launchpad-test | tail -2'`
Expected: the three pass: graph-test 356156 / 273, `[packages]` 417 / 2, launchpad 144 / 29 (eight products there: a default configure leaves the Node addon off). Then `rm -rf /tmp/e1-fresh`.

- [ ] **Step 10: The sanitizers**

Both sanitizer trees re-run their configure on the build and find the moved files (the report: "build-tsan /
build-asan re-configured themselves on the build and found the moved files.").

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc)`
Expected: exit 0: the ThreadSanitizer tree's two binaries, built from this task's tree (a stale binary proves nothing).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag; the list is the verification script's), then `All tests passed (1698267 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), and no ThreadSanitizer report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263160 assertions in 79 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test -j$(nproc)`
Expected: exit 0: the AddressSanitizer tree's three binaries.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (1680089 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356156 assertions in 273 test cases)`, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report.

- [ ] **Step 11: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M CMakeLists.txt
 m deps/sameboy
 M docs/modular-graph-design.md
 M docs/modular-graph-phase-b3a-design.md
 M docs/modular-graph-phase-b3b-design.md
 M docs/modular-graph-phase-c1-design.md
 M docs/modular-graph-phase-c3-design.md
RM packages/native/src/graph/nodes/CoreNodes.cpp -> packages/core/native/src/graph/nodes/CoreNodes.cpp
RM packages/native/src/graph/nodes/CoreNodes.hpp -> packages/core/native/src/graph/nodes/CoreNodes.hpp
R  packages/native/src/dsp/Biquad.cpp -> packages/dsp/native/src/dsp/Biquad.cpp
R  packages/native/src/dsp/Biquad.hpp -> packages/dsp/native/src/dsp/Biquad.hpp
R  packages/native/src/dsp/Dither.cpp -> packages/dsp/native/src/dsp/Dither.cpp
R  packages/native/src/dsp/Dither.hpp -> packages/dsp/native/src/dsp/Dither.hpp
R  packages/native/src/dsp/Dpcm.cpp -> packages/dsp/native/src/dsp/Dpcm.cpp
R  packages/native/src/dsp/Dpcm.hpp -> packages/dsp/native/src/dsp/Dpcm.hpp
R  packages/native/src/dsp/Gain.cpp -> packages/dsp/native/src/dsp/Gain.cpp
R  packages/native/src/dsp/Gain.hpp -> packages/dsp/native/src/dsp/Gain.hpp
R  packages/native/src/dsp/R8bAllocator.hpp -> packages/dsp/native/src/dsp/R8bAllocator.hpp
R  packages/native/src/dsp/R8bBase.cpp -> packages/dsp/native/src/dsp/R8bBase.cpp
R  packages/native/src/dsp/Resample.cpp -> packages/dsp/native/src/dsp/Resample.cpp
R  packages/native/src/dsp/Resample.hpp -> packages/dsp/native/src/dsp/Resample.hpp
RM packages/native/src/graph/nodes/DspNodes.cpp -> packages/dsp/native/src/graph/nodes/DspNodes.cpp
RM packages/native/src/graph/nodes/DspNodes.hpp -> packages/dsp/native/src/graph/nodes/DspNodes.hpp
RM packages/native/src/graph/nodes/DspResampleNodes.cpp -> packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
RM packages/native/src/graph/nodes/DspResampleNodes.hpp -> packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp
R  packages/native/src/util/Hash.hpp -> packages/dsp/native/src/util/Hash.hpp
R  packages/native/src/graph/Executor.cpp -> packages/graph-runtime/native/src/graph/Executor.cpp
R  packages/native/src/graph/Executor.hpp -> packages/graph-runtime/native/src/graph/Executor.hpp
R  packages/native/src/graph/GraphHost.cpp -> packages/graph-runtime/native/src/graph/GraphHost.cpp
R  packages/native/src/graph/GraphHost.hpp -> packages/graph-runtime/native/src/graph/GraphHost.hpp
RM packages/native/src/graph/HostNodes.cpp -> packages/graph-runtime/native/src/graph/HostNodes.cpp
RM packages/native/src/graph/HostNodes.hpp -> packages/graph-runtime/native/src/graph/HostNodes.hpp
R  packages/native/src/graph/NodeRegistry.cpp -> packages/graph-runtime/native/src/graph/NodeRegistry.cpp
R  packages/native/src/graph/NodeRegistry.hpp -> packages/graph-runtime/native/src/graph/NodeRegistry.hpp
R  packages/native/src/graph/Plan.hpp -> packages/graph-runtime/native/src/graph/Plan.hpp
R  packages/native/src/graph/PlanStorage.cpp -> packages/graph-runtime/native/src/graph/PlanStorage.cpp
R  packages/native/src/graph/PlanStorage.hpp -> packages/graph-runtime/native/src/graph/PlanStorage.hpp
R  packages/native/src/graph/Planner.cpp -> packages/graph-runtime/native/src/graph/Planner.cpp
R  packages/native/src/graph/Planner.hpp -> packages/graph-runtime/native/src/graph/Planner.hpp
R  packages/native/src/graph/ResourceTable.cpp -> packages/graph-runtime/native/src/graph/ResourceTable.cpp
R  packages/native/src/graph/ResourceTable.hpp -> packages/graph-runtime/native/src/graph/ResourceTable.hpp
 M packages/native/CMakeLists.txt
D  packages/native/cmake/graph.cmake
 M packages/native/cmake/node-addon.cmake
 M packages/native/cmake/plugin.cmake
 M packages/native/cmake/test-binary.cmake
 M packages/native/cmake/tests.cmake
 M packages/native/cmake/warnings.cmake
 M packages/native/package.json
 M packages/native/src/host/engine/Engine.cpp
 M packages/native/src/host/graph/HostIoNodes.cpp
 M packages/native/src/host/graph/HostIoNodes.hpp
 M packages/native/src/host/graph/LaunchpadNodes.cpp
 M packages/native/src/host/graph/LaunchpadNodes.hpp
 M packages/native/src/host/graph/N8Nodes.cpp
 M packages/native/src/host/graph/N8Nodes.hpp
 M packages/native/src/host/graph/SystemNodes.cpp
 M packages/native/src/host/graph/SystemNodes.hpp
R  packages/native/src/graph/CanonicalJson.cpp -> packages/node-sdk/native/src/graph/CanonicalJson.cpp
R  packages/native/src/graph/CanonicalJson.hpp -> packages/node-sdk/native/src/graph/CanonicalJson.hpp
R  packages/native/src/graph/Descriptor.cpp -> packages/node-sdk/native/src/graph/Descriptor.cpp
R  packages/native/src/graph/Descriptor.hpp -> packages/node-sdk/native/src/graph/Descriptor.hpp
R  packages/native/src/graph/GraphDocument.cpp -> packages/node-sdk/native/src/graph/GraphDocument.cpp
R  packages/native/src/graph/GraphDocument.hpp -> packages/node-sdk/native/src/graph/GraphDocument.hpp
R  packages/native/src/graph/Queue.hpp -> packages/node-sdk/native/src/graph/Queue.hpp
R  packages/native/src/graph/node.hpp -> packages/node-sdk/native/src/graph/node.hpp
R  packages/native/src/graph/rp_node.h -> packages/node-sdk/native/src/graph/rp_node.h
R  packages/native/src/transport/SpscRing.hpp -> packages/node-sdk/native/src/transport/SpscRing.hpp
 M packages/retroplug/src/graph/canonicalJson.ts
 M packages/retroplug/src/graph/defineNode.ts
 M packages/retroplug/src/graph/descriptor.ts
 M packages/retroplug/src/graph/graphDocument.ts
 M packages/retroplug/src/graph/systemResources.ts
 M pnpm-lock.yaml
 M spec/01-architecture.md
 M spec/02-native-host.md
 M spec/06-build-test.md
 M spec/07-remaining-work.md
?? cmake/packages.cmake
?? packages/core/native/CMakeLists.txt
?? packages/core/package.json
?? packages/dsp/native/CMakeLists.txt
?? packages/dsp/package.json
?? packages/graph-runtime/native/CMakeLists.txt
?? packages/graph-runtime/native/src/graph/Packages.hpp
?? packages/graph-runtime/package.json
?? packages/native/test/engine-graph/Packages.test.cpp
?? packages/native/test/engine-graph/golden/product-registrations.json
?? packages/node-sdk/native/CMakeLists.txt
?? packages/node-sdk/package.json
````

````bash
git add -- CMakeLists.txt cmake/packages.cmake docs/modular-graph-design.md docs/modular-graph-phase-b3a-design.md \
  docs/modular-graph-phase-b3b-design.md docs/modular-graph-phase-c1-design.md docs/modular-graph-phase-c3-design.md \
  packages/core/native/CMakeLists.txt packages/core/native/src/graph/nodes/CoreNodes.cpp \
  packages/core/native/src/graph/nodes/CoreNodes.hpp packages/core/package.json packages/dsp/native/CMakeLists.txt \
  packages/dsp/native/src/graph/nodes/DspNodes.cpp packages/dsp/native/src/graph/nodes/DspNodes.hpp \
  packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp packages/dsp/native/src/graph/nodes/DspResampleNodes.hpp \
  packages/dsp/package.json packages/graph-runtime/native/CMakeLists.txt \
  packages/graph-runtime/native/src/graph/HostNodes.cpp packages/graph-runtime/native/src/graph/HostNodes.hpp \
  packages/graph-runtime/native/src/graph/Packages.hpp packages/graph-runtime/package.json \
  packages/native/CMakeLists.txt packages/native/cmake/node-addon.cmake packages/native/cmake/plugin.cmake \
  packages/native/cmake/test-binary.cmake packages/native/cmake/tests.cmake packages/native/cmake/warnings.cmake \
  packages/native/package.json packages/native/src/host/engine/Engine.cpp \
  packages/native/src/host/graph/HostIoNodes.cpp packages/native/src/host/graph/HostIoNodes.hpp \
  packages/native/src/host/graph/LaunchpadNodes.cpp packages/native/src/host/graph/LaunchpadNodes.hpp \
  packages/native/src/host/graph/N8Nodes.cpp packages/native/src/host/graph/N8Nodes.hpp \
  packages/native/src/host/graph/SystemNodes.cpp packages/native/src/host/graph/SystemNodes.hpp \
  packages/native/test/engine-graph/Packages.test.cpp \
  packages/native/test/engine-graph/golden/product-registrations.json packages/node-sdk/native/CMakeLists.txt \
  packages/node-sdk/package.json packages/retroplug/src/graph/canonicalJson.ts \
  packages/retroplug/src/graph/defineNode.ts packages/retroplug/src/graph/descriptor.ts \
  packages/retroplug/src/graph/graphDocument.ts packages/retroplug/src/graph/systemResources.ts pnpm-lock.yaml \
  spec/01-architecture.md spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md
git commit -F - <<'EOF'
refactor(graph): the graph libraries and the core and DSP nodes become workspace packages

The node SDK, the graph runtime, the rp.core nodes and the DSP kernels with the rp.dsp nodes move into
packages/node-sdk, graph-runtime, core and dsp (docs/modular-graph-phase-e1-design.md section 4), each a pnpm
workspace member whose package.json `retroplug` field names its native target, its entry and whether its node types
build for the device. Every file keeps the path it is included by under its package's native/src, so no #include
changes. SpscRing goes to node-sdk (the host links that a pack will carry use it too), with an INTERFACE target for
node-sdk's headers that the no-heap executor links; Hash.hpp goes to dsp beside dither.

cmake/packages.cmake reads the manifests (retroplug_package) and generates each product's rp_register_packages()
(retroplug_product), which replaces the engine's hard-coded package loop. The node types not yet carved register
from packages/native's own manifest. The products are every binary DPF builds from the plugin, retroplug-host, -cli,
-sdl, -node, -ui-test, -render-host-test, -audio-test and -engine-graph-test, all listing RP_DESKTOP_PACKAGES, whose
order is the old loop's; the configure refuses any other binary that links the engine's libraries, since a Linux
module without a registration would only fail when loaded. A registration golden holds each product to the 26 type
ids the loop registered. The offline pool keeps its fixed core and DSP subset, and the script pack its own path.
EOF
````

---

### Task 2: fixed-capacity settings types, and the core and DSP nodes use them

Design section 5 and section 9's second commit. A portable node's settings may not hold a `std::string` or a
`std::vector`, so `node-sdk` gains `rp::text<N>` and `rp::list<T, N>`, which read and write the JSON and the JSON Schema
of the types they replace and refuse an over-long value naming the field, and `rp::resource_ref` stores its resource id
as a number while the document keeps its `"res:N"`. The core nodes move to them (`join` and `split`'s counts, every
`layout`, `from` and `to`), and so does `rp.dsp.resample`'s layout; the system nodes and the DSP nodes only follow the
new `resource_ref` spelling (`.empty()`). No descriptor golden moves and no document changes. One behaviour moves: a
malformed `"res:007"` is refused when the settings are read (`settings-invalid`) rather than when the node is created
(`create-failed`), in the same words, and the TypeScript mock backend refuses it first the same way.

**Files:**

- Create: `packages/native/test/graph/SettingsTypes.test.cpp`, `packages/node-sdk/native/src/graph/settings_types.hpp`
- Modify: `packages/core/native/src/graph/nodes/CoreNodes.cpp`, `packages/dsp/native/src/graph/nodes/DspNodes.cpp`, `packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp`, `packages/native/cmake/tests.cmake`, `packages/native/src/host/graph/SystemNodes.cpp`, `packages/native/test/graph/Resources.test.cpp`, `packages/node-sdk/native/src/graph/Descriptor.hpp`, `packages/node-sdk/native/src/graph/node.hpp`, `packages/retroplug/test/meta.test.ts`, `packages/retroplug/testing/mockBackend.ts`

- [ ] **Step 1: Write the failing settings-type cases**

`SettingsTypes.test.cpp` (`[settings-types]`, six cases) holds `rp::text` and `rp::list` to the `std::string` and
`std::vector` they replace: the same JSON written and read (defaults, empty values, a full list, an escaped string),
the same JSON Schema, every bound refused naming its field, the value operations, a `resource_ref` that round-trips
`"res:N"` as a number and empty as none, and the core nodes refusing an over-long list or layout naming the field.
`Resources.test.cpp`'s malformed-ref case now expects the refusal at the settings read, and its node reads the ref
through `ref()`. The CMake line that adds the new source lands here, before the code it tests.

Create `packages/native/test/graph/SettingsTypes.test.cpp`:

````cpp
// The fixed-capacity settings types (docs/modular-graph-phase-e1-design.md section 5): rp::text and rp::list read and
// write the JSON the std::string and std::vector they replace did, an over-long value is refused naming its field, and
// an rp::resource_ref keeps the document's "res:N" while storing the number.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <rfl/DefaultIfMissing.hpp>
#include <rfl/json.hpp>

#include "GraphRig.hpp"
#include "graph/node.hpp"

using rp::graph::test::GraphRig;

namespace settings_types_test {

// The same fields twice: as std::string and std::vector hold them, and as rp::text and rp::list do. Named namespace, as
// every Settings struct is.
struct Before {
    std::vector<std::uint32_t> counts = {1, 1};
    std::string                layout = "stereo";
    std::string                empty;
};
struct After {
    rp::list<std::uint32_t, 16> counts = {1, 1};
    rp::text<16>                layout = "stereo";
    rp::text<8>                 empty;
};

struct Refs {
    rp::resource_ref<"rom">  rom;
    rp::resource_ref<"sram"> sram;
};

template <class S>
rfl::Result<S> readJson(const std::string& json) {
    return rfl::json::read<S, rfl::DefaultIfMissing>(json);
}

}  // namespace settings_types_test

using namespace settings_types_test;

TEST_CASE("text and list write the JSON a string and a vector did, defaults included", "[settings-types]") {
    CHECK(rfl::json::write(After{}) == rfl::json::write(Before{}));

    for (const std::string json : {R"({})", R"({"counts":[],"layout":""})", R"({"counts":[7],"layout":"5.1"})",
                                   R"x({"counts":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16],"layout":"discrete(64)"})x",
                                   R"({"layout":"héllo \"q\" \\ \/","empty":"abcdefgh"})"}) {
        const auto before = readJson<Before>(json);
        const auto after = readJson<After>(json);
        REQUIRE(before.has_value());
        REQUIRE(after.has_value());
        CHECK(after->layout.view() == before->layout);
        CHECK(std::vector<std::uint32_t>(after->counts.begin(), after->counts.end()) == before->counts);
        CHECK(rfl::json::write(*after) == rfl::json::write(*before));
    }
}

TEST_CASE("text and list have the JSON Schema of the types they replace", "[settings-types]") {
    CHECK(rfl::json::to_schema<rp::text<16>>() == rfl::json::to_schema<std::string>());
    CHECK(rfl::json::to_schema<rp::list<std::uint32_t, 16>>() == rfl::json::to_schema<std::vector<std::uint32_t>>());
    CHECK(rfl::json::to_schema<rp::list<double, 4>>() == rfl::json::to_schema<std::vector<double>>());
}

TEST_CASE("a text and a list hold their bound and refuse one past it, naming the field", "[settings-types]") {
    const auto atText = readJson<After>(R"({"layout":"0123456789abcdef"})");  // 16 bytes
    REQUIRE(atText.has_value());
    CHECK(atText->layout.size() == 16);
    const auto overText = readJson<After>(R"({"layout":"0123456789abcdefg"})");  // 17
    REQUIRE_FALSE(overText.has_value());
    CHECK(overText.error().what().find("'layout'") != std::string::npos);

    // The bound is bytes: five two-byte code points fit eight, a fifth three-byte one does not.
    CHECK(readJson<After>(R"({"empty":"éééé"})").has_value());
    CHECK_FALSE(readJson<After>(R"({"empty":"ééééé"})").has_value());

    const auto atList = readJson<After>(R"({"counts":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]})");  // 16
    REQUIRE(atList.has_value());
    CHECK(atList->counts.size() == 16);
    const auto overList = readJson<After>(R"({"counts":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]})");  // 17
    REQUIRE_FALSE(overList.has_value());
    CHECK(overList.error().what().find("'counts'") != std::string::npos);

    // An element that is not an element still reports as it did.
    CHECK_FALSE(readJson<After>(R"({"counts":[1,-1]})").has_value());
    CHECK_FALSE(readJson<After>(R"({"layout":3})").has_value());
}

TEST_CASE("text and list as values: compare, view, assign and fill", "[settings-types]") {
    rp::text<4> t = "ab";
    CHECK(t == "ab");
    CHECK(t == std::string_view("ab"));
    CHECK(t.view() == "ab");
    CHECK(t.str() == "ab");
    CHECK_FALSE(t.empty());
    CHECK(t.assign("abcd"));
    CHECK_FALSE(t.assign("abcde"));
    CHECK(t == "abcd");  // a refused assign changes nothing
    CHECK(rp::text<4>().empty());
    CHECK(rp::text<4>::capacity() == 4);

    rp::list<int, 3> l = {1, 2};
    CHECK(l.size() == 2);
    CHECK(l[1] == 2);
    CHECK(l.push_back(3));
    CHECK_FALSE(l.push_back(4));
    CHECK(l.size() == 3);
    CHECK(l == rp::list<int, 3>{1, 2, 3});
    CHECK_FALSE(l == rp::list<int, 3>{1, 2});
    int sum = 0;
    for (int v : l) sum += v;
    CHECK(sum == 6);
    CHECK(l.view().size() == 3);
}

TEST_CASE("a resource_ref round-trips res:N as a number, and empty as none", "[settings-types]") {
    const auto r = readJson<Refs>(R"({"rom":"res:7"})");
    REQUIRE(r.has_value());
    CHECK(r->rom.id == 7);
    CHECK_FALSE(r->rom.empty());
    CHECK(r->rom.ref() == "res:7");
    CHECK(r->sram.empty());
    CHECK(rfl::json::write(*r) == R"({"rom":"res:7","sram":""})");

    const auto none = readJson<Refs>(R"({"rom":"","sram":"res:4294967295"})");
    REQUIRE(none.has_value());
    CHECK(none->rom.empty());
    CHECK(none->sram.id == 4294967295u);
    CHECK(rfl::json::write(*none) == R"({"rom":"","sram":"res:4294967295"})");
    CHECK(rfl::json::write(Refs{}) == R"({"rom":"","sram":""})");

    for (const char* bad : {"res:0", "res:007", "res:", "res:1x", "7", "rom"}) {
        const auto refused = readJson<Refs>(std::string(R"({"rom":")") + bad + R"("})");
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().what().find("'rom'") != std::string::npos);
        CHECK(refused.error().what().find("is not a resource reference") != std::string::npos);
    }
    CHECK(rfl::json::to_schema<rp::resource_ref<"rom">>() == rfl::json::to_schema<std::string>());
}

// The converted nodes plan and run as they did through the cases that already place them: "split, join and downmix move
// channels explicitly, in order", "5.1 downmixes to stereo ..." and "join fixes a mono-into-stereo mismatch; relabel
// asserts a layout" ([core], CoreNodes.test.cpp), the buffer-player cases there, the [dsp-resample] cases (a resample's
// layout), and the engine-graph case that plans and runs every buildGraph golden document (buildGraph emits the joins and splits),
// whose audio the parity groups hold to the bit.
TEST_CASE("the core nodes refuse an over-long list or layout naming the field", "[settings-types][core]") {
    GraphRig rig;
    const auto refusedWith = [&](const std::string& settings, const std::string& type, const std::string& field) {
        const auto r = rig.set(R"({"nodes":[{"id":"n","type":")" + type + R"(","settings":)" + settings + R"(}],"wires":[]})");
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("'" + field + "'") != std::string::npos);
    };
    // 16 ports plans, 17 does not: the bound the validator holds is also the storage's.
    CHECK(rig.set(R"x({"nodes":[{"id":"j","type":"rp.core.join","settings":{"inputs":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"layout":"discrete(16)"}}],"wires":[]})x").ok);
    refusedWith(R"x({"inputs":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"layout":"discrete(17)"})x", "rp.core.join", "inputs");
    refusedWith(R"x({"outputs":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"layout":"discrete(17)"})x", "rp.core.split", "outputs");
    // A layout name is 16 bytes at most; the longest any table accepts, "discrete(64)", is 12.
    CHECK(rig.set(R"x({"nodes":[{"id":"p","type":"rp.core.relabel","settings":{"channels":64,"layout":"discrete(64)"}}],"wires":[]})x").ok);
    refusedWith(R"x({"channels":1,"layout":"discrete(0000000001)"})x", "rp.core.relabel", "layout");
    refusedWith(R"({"from":"stereo","to":"0123456789abcdefg"})", "rp.core.downmix", "to");
    refusedWith(R"({"from":"0123456789abcdefg","to":"mono"})", "rp.core.downmix", "from");
}
````

Run: `mkdir -p packages/native/test/graph; sed -n '2819,2986p' docs/modular-graph-phase-e1-plan.md > packages/native/test/graph/SettingsTypes.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/SettingsTypes.test.cpp`
Expected: `6584b44ef5bdeba410b92d1c4283556e788edd31`: the blob 63c96599a holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index f522cc7c0..fd2ac26a8 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -276,6 +276,7 @@ retroplug_add_test_binary(
              test/graph/HostCommands.test.cpp
              test/graph/Reshape.test.cpp
              test/graph/Resources.test.cpp            # a node that reads a resource its settings name
+             test/graph/SettingsTypes.test.cpp         # rp::text, rp::list and the numeric resource_ref
              test/graph/QueueCapacity.test.cpp
              test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
              test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's math
diff --git a/packages/native/test/graph/Resources.test.cpp b/packages/native/test/graph/Resources.test.cpp
index d5d63b4b8..60ef4a0dc 100644
--- a/packages/native/test/graph/Resources.test.cpp
+++ b/packages/native/test/graph/Resources.test.cpp
@@ -48,7 +48,7 @@ struct ResourceNode {
         const rp_bytes* b = s.rom.bytes();
         sawBytes = b != nullptr;
         bytes = b ? static_cast<std::uint32_t>(b->size) : 0;
-        ref = s.rom.ref;
+        ref = s.rom.ref();
         level = static_cast<float>(s.level);
     }
     void process(const rp::Block& b) noexcept {
@@ -209,8 +209,12 @@ TEST_CASE("a resource a node's settings name must be a live resource of the fiel
     CHECK(dropped.find("not a live resource") != std::string::npos);
     const std::string wrongType = refused(doc(resRef(sram)));
     CHECK(wrongType.find("sram resource, not a rom") != std::string::npos);
-    const std::string malformed = refused(doc("res:007"));
-    CHECK(malformed.find("is not a resource reference") != std::string::npos);
+    // A ref that is not "res:N" never becomes a number, so it is refused where the settings are read, not at create.
+    const SetGraphResult malformed = rig.set(doc("res:007"));
+    REQUIRE_FALSE(malformed.ok);
+    REQUIRE(malformed.errors.size() == 1);
+    CHECK(malformed.errors[0].code == "settings-invalid");
+    CHECK(malformed.errors[0].message.find("is not a resource reference") != std::string::npos);
 
     // Every one of them left the running plan as it was: the node still runs, its own pin untouched.
     CHECK(rig.resources.pinCount(rom) == 1);
````

Run: `sed -n '2996,3035p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: the new test source is listed.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc)`
Expected: the build fails compiling `SettingsTypes.test.cpp` (`rp::list` and `rp::text` are not declared) and `Resources.test.cpp` (a `resource_ref`'s `ref` is still a `std::string` member, not a function). Nothing else fails to build. (Predicted from the code at this step; the stage logged no red run.)

- [ ] **Step 2: Write the failing mock-backend case**

`meta.test.ts`'s resource case now expects a malformed `rom` or `sram` ref refused with code `settings-invalid` and
native's words, `Failed to parse field '<field>': "res:007" is not a resource reference (res:N)`, before the
missing-ROM check and any create.

````diff
diff --git a/packages/retroplug/test/meta.test.ts b/packages/retroplug/test/meta.test.ts
index 12a47b8ba..c377b5d57 100644
--- a/packages/retroplug/test/meta.test.ts
+++ b/packages/retroplug/test/meta.test.ts
@@ -150,7 +150,16 @@ test("mock backend: a document naming a resource the engine does not hold is ref
     code: "create-failed",
     message: `sram: "res:${rom.id}" is a rom resource, not a sram`,
   });
-  expect(be.setGraph(doc({ rom: "res:007" })).errors[0].message).toBe('rom: "res:007" is not a resource reference (res:N)');
+  // A malformed ref is refused where native reads the settings, before the missing-ROM check and any create.
+  expect(be.setGraph(doc({ rom: "res:007" })).errors[0]).toEqual({
+    nodeId: "sys-1",
+    port: "",
+    code: "settings-invalid",
+    message: `Failed to parse field 'rom': "res:007" is not a resource reference (res:N)`,
+  });
+  expect(be.setGraph(doc({ sram: "res:007" })).errors[0].message).toBe(
+    `Failed to parse field 'sram': "res:007" is not a resource reference (res:N)`,
+  );
   expect(be.setGraph(doc({ sram: `res:${sram.id}` })).errors[0]).toEqual({
     nodeId: "sys-1",
     port: "",
````

Run: `sed -n '3054,3075p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test meta`
Expected: fails: 9 of the file's 10 cases pass and `mock backend: a document naming a resource the engine does not hold is refused, as native refuses it` fails, since the mock still answers `create-failed`. (Predicted.)

- [ ] **Step 3: The types, and the nodes on them**

`settings_types.hpp` (included by `node.hpp`) holds `rp::text<N>` (inline bytes and a length, an implicit
`string_view`, `==`, `assign`, `str()`, and a consteval literal constructor, so a default that does not fit fails to
compile) and `rp::list<T, N>` (an inline array, a consteval initializer list, `push_back`, `begin`/`end`, a span view,
`==`; trivially copyable elements only, a length byte up to 255 and a `uint16` above), with reflect-cpp parsers that
delegate to `std::string` and `std::vector` for the read, the write and the schema. `node.hpp`'s `resource_ref` stores
`std::uint32_t id` (0 is none), with `empty()` and a desktop `ref()` that builds `"res:N"` for messages;
`ResourceHold` keeps the id (`named`), and its parser reads `"res:N"` through `parseResourceRef` and writes it back.
`Descriptor.hpp` gains `kMaxLayoutName = 16` beside `kMaxChannels`. The core nodes' settings become
`rp::list<std::uint32_t, kMaxPorts>` and `rp::text<kMaxLayoutName>` (call sites take `.str()` where a `std::string` is
wanted), `rp.dsp.resample`'s layout too, and the system and DSP nodes call `.empty()`.

Create `packages/node-sdk/native/src/graph/settings_types.hpp`:

````cpp
#pragma once

// Fixed-capacity settings types (docs/modular-graph-phase-e1-design.md section 5). A node meant for the device keeps
// every settings field inline: no allocation to read, copy or destroy one. node.hpp includes this header, so a node
// names them as rp::text and rp::list.
//
//   struct Settings {
//       rp::text<16>                      layout = "stereo";   // JSON: "stereo"
//       rp::list<std::uint32_t, 16>       counts = {1, 1};     // JSON: [1, 1]
//   };
//
// Both are read and written as the JSON the std::string and std::vector they stand in for were, and their JSON Schema
// is that type's (a string, an array of the element's schema), so a descriptor does not change when a field moves to
// them. A JSON value over the bound is a settings error; reflect-cpp names the field it was reading in the message.
// Reading builds a std::string or std::vector first, which is why the JSON side is a desktop one: the device reads
// the same fields from msgpack (design section 6), and the types themselves allocate nothing.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <rfl/Result.hpp>
#include <rfl/json.hpp>
#include <rfl/parsing/Parser.hpp>

namespace rp {

namespace detail {
// Called by the consteval constructors when a literal or a braced list is over its type's bound. It is not constexpr,
// so evaluating it is a compile error where the default is written; a run-time value goes through assign /
// push_back, which answer false instead.
[[noreturn]] inline void capacityExceeded() { std::abort(); }
}  // namespace detail

// Up to N bytes of UTF-8 stored inline. The bound is bytes, not code points, and nothing checks the encoding: a
// value is a byte string that happens to be what a JSON string carried.
template <std::size_t N>
class text {
public:
    static_assert(N > 0 && N < 65536, "a text holds 1 to 65535 bytes");
    using size_type = std::conditional_t<(N <= 255), std::uint8_t, std::uint16_t>;

    constexpr text() = default;

    // From a literal, which is checked against the bound where it is written.
    template <std::size_t M>
    consteval text(const char (&s)[M]) {  // NOLINT(google-explicit-constructor): a default reads `text<16> x = "a"`
        static_assert(M - 1 <= N, "the literal does not fit the text's capacity");
        for (std::size_t i = 0; i + 1 < M; ++i) data_[i] = s[i];
        size_ = static_cast<size_type>(M - 1);
    }

    static constexpr std::size_t capacity() { return N; }

    // Replace the contents. False, with the contents unchanged, when `s` is over the bound.
    constexpr bool assign(std::string_view s) {
        if (s.size() > N) return false;
        for (std::size_t i = 0; i < N; ++i) data_[i] = i < s.size() ? s[i] : '\0';  // the tail stays zero
        size_ = static_cast<size_type>(s.size());
        return true;
    }

    constexpr std::string_view view() const { return {data_, size_}; }
    constexpr operator std::string_view() const { return view(); }  // NOLINT(google-explicit-constructor)
    constexpr std::size_t size() const { return size_; }
    constexpr bool        empty() const { return size_ == 0; }
    constexpr const char* data() const { return data_; }
    std::string           str() const { return std::string(view()); }

    friend constexpr bool operator==(const text& a, std::string_view b) { return a.view() == b; }

private:
    char      data_[N] = {};
    size_type size_ = 0;
};

// Up to N elements of T stored inline. T is a plain value (a number, an enum, a small struct): it is default
// constructed N times, copied bytewise by assignment, and never destroyed one at a time.
template <class T, std::size_t N>
class list {
public:
    static_assert(N > 0 && N < 65536, "a list holds 1 to 65535 elements");
    static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                  "a list holds plain values");
    // A resource_ref is pinned only as a top-level settings field (node.hpp walks no further), so one in a list would
    // name a resource nothing holds.
    static_assert(!requires { T::resource_type(); }, "a resource_ref cannot be a list element");
    using value_type = T;
    using size_type = std::conditional_t<(N <= 255), std::uint8_t, std::uint16_t>;

    constexpr list() = default;
    // From a braced list, which is checked against the bound where it is written. A list is filled at run time with
    // push_back.
    consteval list(std::initializer_list<T> values) {  // NOLINT(google-explicit-constructor)
        if (values.size() > N) detail::capacityExceeded();
        for (const T& v : values) data_[size_++] = v;
    }

    static constexpr std::size_t capacity() { return N; }

    // Append. False, with the list unchanged, when it is full.
    constexpr bool push_back(const T& v) {
        if (size_ == N) return false;
        data_[size_++] = v;
        return true;
    }
    constexpr void clear() {
        for (std::size_t i = 0; i < size_; ++i) data_[i] = T{};  // the tail stays value-initialized
        size_ = 0;
    }

    constexpr std::size_t size() const { return size_; }
    constexpr bool        empty() const { return size_ == 0; }
    constexpr const T&    operator[](std::size_t i) const { return data_[i]; }
    constexpr T&          operator[](std::size_t i) { return data_[i]; }
    constexpr const T*    begin() const { return data_; }
    constexpr const T*    end() const { return data_ + size_; }
    constexpr T*          begin() { return data_; }
    constexpr T*          end() { return data_ + size_; }
    constexpr std::span<const T> view() const { return {data_, size_}; }

    friend constexpr bool operator==(const list& a, const list& b) {
        if (a.size_ != b.size_) return false;
        for (std::size_t i = 0; i < a.size_; ++i)
            if (!(a.data_[i] == b.data_[i])) return false;
        return true;
    }

private:
    T         data_[N] = {};
    size_type size_ = 0;
};

}  // namespace rp

// A text is the JSON string it holds, and its schema is the string's.
template <class R, class W, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::text<N>>
struct rfl::parsing::Parser<R, W, rp::text<N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using String = Parser<R, W, std::string, ProcessorsType>;

    static Result<rp::text<N>> read(const R& r, const InputVarType& var) noexcept {
        auto s = String::read(r, var);
        if (!s) return rfl::Unexpected(s.error());
        rp::text<N> out;
        if (!out.assign(*s))
            return rfl::Unexpected(rfl::Error("a string of " + std::to_string(s->size()) + " bytes is over the " +
                                              std::to_string(N) + " the field holds"));
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::text<N>& field, const P& parent) {
        String::write(w, field.str(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return String::to_schema(definitions);
    }
};

// A list is the JSON array its elements make, and its schema is the array's. The elements are read as a std::vector
// would be, so each one reports its own error exactly as it did when the field was one.
template <class R, class W, class T, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::list<T, N>>
struct rfl::parsing::Parser<R, W, rp::list<T, N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using Vector = Parser<R, W, std::vector<T>, ProcessorsType>;

    static Result<rp::list<T, N>> read(const R& r, const InputVarType& var) noexcept {
        auto v = Vector::read(r, var);
        if (!v) return rfl::Unexpected(v.error());
        if (v->size() > N)
            return rfl::Unexpected(rfl::Error("an array of " + std::to_string(v->size()) + " elements is over the " +
                                              std::to_string(N) + " the field holds"));
        rp::list<T, N> out;
        for (const T& e : *v) out.push_back(e);
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::list<T, N>& field, const P& parent) {
        Vector::write(w, std::vector<T>(field.begin(), field.end()), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return Vector::to_schema(definitions);
    }
};
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '3100,3292p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/settings_types.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/settings_types.hpp`
Expected: `18b3aa4c73f0c7bf0d47cf3deb02fa16e0187ff8`: the blob 63c96599a holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/core/native/src/graph/nodes/CoreNodes.cpp b/packages/core/native/src/graph/nodes/CoreNodes.cpp
index b3898cb42..41eec046f 100644
--- a/packages/core/native/src/graph/nodes/CoreNodes.cpp
+++ b/packages/core/native/src/graph/nodes/CoreNodes.cpp
@@ -18,6 +18,9 @@ namespace rp::graph {
 // they need the name; everything else below stays internal to this file.
 namespace core {
 
+// A join or split names at most kMaxPorts ports (the validators enforce it).
+constexpr std::uint32_t kMaxPorts = 16;
+
 using AddKind = rfl::Literal<"audio", "control">;
 
 struct AddSettings {
@@ -30,23 +33,23 @@ struct MergeSettings {
 };
 
 struct JoinSettings {
-    std::vector<std::uint32_t> inputs = {1, 1};
-    std::string                layout = "stereo";
+    rp::list<std::uint32_t, kMaxPorts> inputs = {1, 1};
+    rp::text<kMaxLayoutName>           layout = "stereo";
 };
 
 struct SplitSettings {
-    std::vector<std::uint32_t> outputs = {1, 1};
-    std::string                layout = "stereo";
+    rp::list<std::uint32_t, kMaxPorts> outputs = {1, 1};
+    rp::text<kMaxLayoutName>           layout = "stereo";
 };
 
 struct DownmixSettings {
-    std::string from = "stereo";
-    std::string to = "mono";
+    rp::text<kMaxLayoutName> from = "stereo";
+    rp::text<kMaxLayoutName> to = "mono";
 };
 
 struct RelabelSettings {
-    std::uint32_t channels = 6;
-    std::string   layout = "5.1";
+    std::uint32_t            channels = 6;
+    rp::text<kMaxLayoutName> layout = "5.1";
 };
 
 struct DelaySettings {
@@ -60,9 +63,9 @@ struct ConstantSettings {
 };
 
 struct PlayerSettings {
-    std::uint32_t channels = 2;
-    std::string   layout = "stereo";
-    bool          loop = true;
+    std::uint32_t            channels = 2;
+    rp::text<kMaxLayoutName> layout = "stereo";
+    bool                     loop = true;
 };
 
 struct RecorderSettings {
@@ -75,8 +78,6 @@ namespace {
 
 using namespace core;
 
-constexpr std::uint32_t kMaxPorts = 16;
-
 PortDesc audioPort(std::string id, PortDir dir, ChannelSpec spec) {
     PortDesc p;
     p.id = std::move(id);
@@ -101,9 +102,8 @@ std::optional<std::string> checkCount(std::uint32_t n, std::string_view what) {
 }
 
 // A list of per-port channel counts: 1..kMaxPorts ports of at least one channel, kMaxChannels in all.
-std::optional<std::string> checkChannelList(const std::vector<std::uint32_t>& counts, std::string_view what) {
-    const auto ports = static_cast<std::uint32_t>(std::min<std::size_t>(counts.size(), UINT32_MAX));
-    if (auto e = checkCount(ports, what)) return e;
+std::optional<std::string> checkChannelList(const rp::list<std::uint32_t, kMaxPorts>& counts, std::string_view what) {
+    if (auto e = checkCount(static_cast<std::uint32_t>(counts.size()), what)) return e;
     std::uint64_t total = 0;
     for (auto c : counts) {
         if (c < 1) return "every one of the " + std::string(what) + " needs at least one channel";
@@ -112,7 +112,7 @@ std::optional<std::string> checkChannelList(const std::vector<std::uint32_t>& co
     return checkChannels(total, std::string(what) + " together");
 }
 
-std::uint32_t sumOf(const std::vector<std::uint32_t>& v) {  // validated: at most kMaxChannels
+std::uint32_t sumOf(const rp::list<std::uint32_t, kMaxPorts>& v) {  // validated: at most kMaxChannels
     std::uint32_t n = 0;
     for (auto c : v) n += c;
     return n;
@@ -266,7 +266,7 @@ struct Join {
         for (std::size_t k = 0; k < s.inputs.size(); ++k)
             l.ports.push_back(audioPort("in" + std::to_string(k + 1), PortDir::In,
                                         ChannelSpec::fixed(s.inputs[k], layoutForCount(s.inputs[k]))));
-        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(sumOf(s.inputs), s.layout)));
+        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(sumOf(s.inputs), s.layout.str())));
     }
 
     void process(const rp::Block& b) noexcept {
@@ -292,7 +292,7 @@ struct Split {
     }
 
     static void adjust_layout(Layout& l, const Settings& s) {
-        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(sumOf(s.outputs), s.layout)));
+        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(sumOf(s.outputs), s.layout.str())));
         for (std::size_t k = 0; k < s.outputs.size(); ++k)
             l.ports.push_back(audioPort("out" + std::to_string(k + 1), PortDir::Out,
                                         ChannelSpec::fixed(s.outputs[k], layoutForCount(s.outputs[k]))));
@@ -323,14 +323,14 @@ struct Downmix {
         if (auto e = checkChannels(*from, "the source layout")) return e;
         if (s.to == "mono") return std::nullopt;
         if (s.to == "stereo" && s.from == "5.1") return std::nullopt;
-        return "downmix supports any layout to mono, and 5.1 to stereo; not " + s.from + " to " + s.to;
+        return "downmix supports any layout to mono, and 5.1 to stereo; not " + s.from.str() + " to " + s.to.str();
     }
 
     static void adjust_layout(Layout& l, const Settings& s) {
         const std::uint32_t from = layoutChannels(s.from).value_or(2);
         const std::uint32_t to = layoutChannels(s.to).value_or(1);
-        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(from, s.from)));
-        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(to, s.to)));
+        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(from, s.from.str())));
+        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(to, s.to.str())));
     }
 
     bool toStereo = false;  // 5.1 -> stereo; every other downmix is to mono
@@ -371,7 +371,7 @@ struct Relabel {
 
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(s.channels, "any")));
-        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(s.channels, s.layout)));
+        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(s.channels, s.layout.str())));
     }
 
     void process(const rp::Block& b) noexcept {
@@ -504,7 +504,7 @@ struct BufferPlayer {
         return checkLayout(s.layout, s.channels);
     }
     static void adjust_layout(Layout& l, const Settings& s) {
-        l.ports[1].channels = ChannelSpec::fixed(s.channels, s.layout);
+        l.ports[1].channels = ChannelSpec::fixed(s.channels, s.layout.str());
     }
 
     bool               loop = true;
diff --git a/packages/dsp/native/src/graph/nodes/DspNodes.cpp b/packages/dsp/native/src/graph/nodes/DspNodes.cpp
index 3a2c244d0..9b6e538ca 100644
--- a/packages/dsp/native/src/graph/nodes/DspNodes.cpp
+++ b/packages/dsp/native/src/graph/nodes/DspNodes.cpp
@@ -496,7 +496,7 @@ struct Normalize {
 
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannels(s.channels, "a normalise")) return e;
-        if (s.buffer.ref.empty()) return "a normalise names an audio buffer";
+        if (s.buffer.empty()) return "a normalise names an audio buffer";
         return std::nullopt;
     }
     static void adjust_layout(Layout& l, const Settings& s) {
@@ -589,7 +589,7 @@ struct Dpcm {
     struct { rp::audio_out<rp::mono> out; } outputs;
 
     static std::optional<std::string> validate(const Settings& s) {
-        if (s.buffer.ref.empty()) return "a DPCM node names an audio buffer";
+        if (s.buffer.empty()) return "a DPCM node names an audio buffer";
         return std::nullopt;
     }
 
diff --git a/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp b/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
index bfba27b93..7e16bc0ac 100644
--- a/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
+++ b/packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
@@ -29,7 +29,7 @@ struct ResampleNodeSettings {
     // diagnostic. D2's driver fills a buffer before the plan that plays it, never in the same plan as the resampler.
     rp::resource_ref<"audio-buffer"> buffer;
     std::uint32_t                    channels = 2;
-    std::string                      layout = "stereo";
+    rp::text<kMaxLayoutName>         layout = "stereo";
     // Off: the buffer, then silence, which is the kernel's whole-buffer stream (the kit path's resample, before its
     // cut) continued for as long as the plan runs. On: the buffer again from its first frame at its end, as the player
     // loops, one seamless stream through the resampler. Off by default, unlike the player, because the stream the
@@ -73,7 +73,7 @@ struct Resample {
         return checkLayout(s.layout, s.channels);
     }
     static void adjust_layout(Layout& l, const Settings& s) {
-        l.ports[0].channels = ChannelSpec::fixed(s.channels, s.layout);
+        l.ports[0].channels = ChannelSpec::fixed(s.channels, s.layout.str());
     }
 
     std::vector<rp::dsp::Resampler> resamplers;
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index a36f2e067..46b146fc2 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -431,7 +431,7 @@ struct SystemNode {
     static std::optional<std::string> validate(const Settings& s) {
         if (s.system == 0)
             return std::string("a system node names the system it builds ({\"system\": <id>, \"rom\": \"res:N\"})");
-        if (s.rom.ref.empty()) return std::string("a system node names its ROM as {\"rom\": \"res:N\"}");
+        if (s.rom.empty()) return std::string("a system node names its ROM as {\"rom\": \"res:N\"}");
         return std::nullopt;
     }
 
diff --git a/packages/node-sdk/native/src/graph/Descriptor.hpp b/packages/node-sdk/native/src/graph/Descriptor.hpp
index 69d35b783..b56229adf 100644
--- a/packages/node-sdk/native/src/graph/Descriptor.hpp
+++ b/packages/node-sdk/native/src/graph/Descriptor.hpp
@@ -105,6 +105,11 @@ std::string_view kindName(PortKind kind);
 // The most channels one audio port carries; the planner refuses a port that resolves to more.
 constexpr std::uint32_t kMaxChannels = 64;
 
+// The bytes a layout name is stored in by a node's settings: "mono", "stereo", "5.1", "7.1" and "discrete(N)" with N up
+// to kMaxChannels, whose longest spelling "discrete(64)" is twelve bytes; sixteen holds every name layoutChannels
+// accepts.
+constexpr std::size_t kMaxLayoutName = 16;
+
 // Why a channel count outside 1..kMaxChannels cannot be used, or nullopt. `what` names the thing in the message ("a
 // delay", "inputs together"), which is the part of a settings refusal a document's author reads.
 std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what);
diff --git a/packages/node-sdk/native/src/graph/node.hpp b/packages/node-sdk/native/src/graph/node.hpp
index a5ba24e8d..f0755e5c1 100644
--- a/packages/node-sdk/native/src/graph/node.hpp
+++ b/packages/node-sdk/native/src/graph/node.hpp
@@ -100,6 +100,7 @@
 #include "graph/GraphDocument.hpp"
 #include "graph/Queue.hpp"
 #include "graph/rp_node.h"
+#include "graph/settings_types.hpp"
 
 namespace rp {
 
@@ -254,7 +255,7 @@ struct link {
 // Create pins before any hook that sees the instance's settings runs, so `on_settings`, `activate`, `process`, `step` and
 // `finish` read a resolved handle (and a prepare that keeps the refs copies the instance's pins into the settings object
 // it installs). The hooks that run before create has pinned anything (`validate`, `adjust_layout`, `rebuild_key`,
-// `extra_memory`, `query_memory`) see only `ref`: a node must not read `bytes()` or `audio()` there. `needs_rebuild`
+// `extra_memory`, `query_memory`) see only `id` (and `empty()`): a node must not read `bytes()` or `audio()` there. `needs_rebuild`
 // sees handles in both objects, and is skipped for a ref that moved, which is a rebuild on its own.
 //
 //   struct Settings { rp::resource_ref<"rom"> rom; };   // JSON: {"rom": "res:7"}; absent: the node names none
@@ -264,12 +265,17 @@ struct link {
 template <fixed_string Type>
 struct resource_ref {
     // rfl reads and writes the ref as the plain string the document carries, never the handle it resolves to, and its
-    // schema is that string inline (the rfl::parsing::Parser specialization at the end of this file).
+    // schema is that string inline (the rfl::parsing::Parser specialization at the end of this file). The id is held
+    // as the number, so a settings object owns nothing on the heap for it.
     static constexpr std::string_view resource_type() { return Type.view(); }
 
-    std::string        ref;                 // "res:N" as the document names it (empty: none)
+    std::uint32_t      id = 0;              // N of the document's "res:N" (0: the document names none)
     const rp_resource* resource = nullptr;  // resolved by the host layer at create, and again at each prepare
 
+    bool empty() const { return id == 0; }
+    // The document's spelling of the id, for an error message: "res:N", or empty for none.
+    std::string ref() const { return id ? "res:" + std::to_string(id) : std::string(); }
+
     // The payload of a bytes resource (every type but "audio-buffer"), or null when the field names none.
     const rp_bytes* bytes() const {
         return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") != 0
@@ -669,8 +675,8 @@ constexpr bool checkHooks() {
 // that names no resource still holds its place, so the pins line up with the settings' resource fields in order.
 struct ResourceHold {
     const rp_host_resources* service = nullptr;
-    std::string              ref;  // the ref the settings named, so prepare can tell whether it moved
-    std::uint32_t            id = 0;
+    std::uint32_t            named = 0;   // the id the settings named (0: none), so prepare can tell whether it moved
+    std::uint32_t            id = 0;      // the id pinned: equal to `named` while a pin is held, 0 after drop
     const rp_resource*       resource = nullptr;
 
     ResourceHold() = default;
@@ -678,14 +684,14 @@ struct ResourceHold {
     ResourceHold& operator=(const ResourceHold&) = delete;
     ResourceHold(ResourceHold&& other) noexcept
         : service(std::exchange(other.service, nullptr)),
-          ref(std::move(other.ref)),
+          named(other.named),
           id(std::exchange(other.id, 0)),
           resource(std::exchange(other.resource, nullptr)) {}
     ResourceHold& operator=(ResourceHold&& other) noexcept {
         if (this != &other) {
             drop();
             service = std::exchange(other.service, nullptr);
-            ref = std::move(other.ref);
+            named = other.named;
             id = std::exchange(other.id, 0);
             resource = std::exchange(other.resource, nullptr);
         }
@@ -783,7 +789,7 @@ void forEachResourceField(S& s, F&& f) {
 }
 
 // Pin every resource `s` names into `inst`, resolving each field's handle, in field order. False, with the reason in
-// *why, for a ref that is not "res:N", a host that offers no table, an id that is unknown or dropped, or a resource
+// *why, for a host that offers no table, an id that is unknown or dropped, or a resource
 // of another type than the field declares (a "rom" where the document names an sram).
 template <class T>
 bool acquireResources(Instance<T>& inst, const rp_host* host, typename SettingsOf<T>::type& s, std::string* why) {
@@ -795,32 +801,27 @@ bool acquireResources(Instance<T>& inst, const rp_host* host, typename SettingsO
     bool ok = true;
     forEachResourceField(s, [&](std::string_view name, auto& field) {
         ResourceHold hold;
-        hold.ref = field.ref;
+        hold.named = field.id;
         inst.resources.push_back(std::move(hold));
-        if (!ok || field.ref.empty()) return;  // the document names none here
+        if (!ok || field.empty()) return;  // the document names none here
         ResourceHold& held = inst.resources.back();
         if (!service) {
             *why = std::string(name) + ": the host offers no resource table";
             ok = false;
             return;
         }
-        const auto id = graph::parseResourceRef(field.ref);
-        if (!id) {
-            *why = std::string(name) + ": " + graph::jsonString(field.ref) + " is not a resource reference (res:N)";
-            ok = false;
-            return;
-        }
         rp_error err{};
-        const rp_resource* r = service->acquire(service->ctx, *id, std::string(field.resource_type()).c_str(), &err);
+        const rp_resource* r =
+            service->acquire(service->ctx, field.id, std::string(field.resource_type()).c_str(), &err);
         if (!r) {
             // The service's reason continues the sentence: "rom: \"res:7\" is not a live resource".
-            *why = std::string(name) + ": " + graph::jsonString(field.ref) + " " +
+            *why = std::string(name) + ": " + graph::jsonString(field.ref()) + " " +
                    (err.message[0] ? std::string(err.message) : std::string("cannot be used"));
             ok = false;
             return;
         }
         held.service = service;
-        held.id = *id;
+        held.id = field.id;
         held.resource = r;
         field.resource = r;
     });
@@ -837,7 +838,7 @@ bool resolveResources(Instance<T>& inst, typename SettingsOf<T>::type& s) {
     std::size_t i = 0;
     forEachResourceField(s, [&](std::string_view, auto& field) {
         if (!same) return;
-        if (i >= inst.resources.size() || inst.resources[i].ref != field.ref) {
+        if (i >= inst.resources.size() || inst.resources[i].named != field.id) {
             same = false;
             return;
         }
@@ -1207,12 +1208,23 @@ struct rfl::parsing::Parser<R, W, rp::resource_ref<Type>, ProcessorsType> {
     using InputVarType = typename R::InputVarType;
     using String = Parser<R, W, std::string, ProcessorsType>;
 
+    // The empty string is "none" (id 0); anything else must be "res:N", the one spelling of an id parseResourceRef
+    // accepts. A ref that is not is refused here, with the string it carried, where the settings are read.
     static Result<rp::resource_ref<Type>> read(const R& r, const InputVarType& var) noexcept {
-        return String::read(r, var).transform([](std::string ref) { return rp::resource_ref<Type>{std::move(ref)}; });
+        auto text = String::read(r, var);
+        if (!text) return rfl::Unexpected(text.error());
+        rp::resource_ref<Type> out;
+        if (text->empty()) return out;
+        const auto id = rp::graph::parseResourceRef(*text);
+        if (!id)
+            return rfl::Unexpected(
+                rfl::Error(rp::graph::jsonString(*text) + " is not a resource reference (res:N)"));
+        out.id = *id;
+        return out;
     }
     template <class P>
     static void write(const W& w, const rp::resource_ref<Type>& field, const P& parent) {
-        String::write(w, field.ref, parent);
+        String::write(w, field.ref(), parent);
     }
     static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
         return String::to_schema(definitions);
````

Run: `sed -n '3302,3669p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-graph-test "[settings-types]"`
Expected: all 6 test cases pass (the report's `tag [settings-types] (6 cases, registered in cmake/tests.cmake)`; no log records this tag's assertion total alone).

Run: `build/bin/retroplug-graph-test`
Expected: `All tests passed (356273 assertions in 279 test cases)`: the baseline's 273 cases and the six new ones, the descriptor goldens per type among them, unchanged.

- [ ] **Step 4: The mock refuses a malformed ref first**

`mockBackend.ts`'s resource check reads every ref field (`rom`, `sram`, `state`) before it validates anything, as
native's settings read does, and refuses one that is not `"res:N"` with code `settings-invalid` and rfl's wording; the
later per-field check keeps `create-failed` for a ref that names no live resource.

````diff
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index a72b4ff39..91cbbae4b 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -500,6 +500,17 @@ export class MockBackend implements Backend {
   private resourceProblem(node: NodeDoc): { code: string; message: string } | null {
     if (!SYSTEM_NODE_TYPES.has(node.type)) return null;
     const settings = node.settings ?? {};
+    // Native reads every settings field before it validates or creates anything, so a ref that is not "res:N" is
+    // refused first, as the settings read refuses it (rfl names the field it was reading).
+    for (const field of ["rom", "sram", "state"] as const) {
+      const value = settings[field];
+      if (value === undefined || value === "") continue;
+      if (typeof value !== "string" || !RES_REF.test(value))
+        return {
+          code: "settings-invalid",
+          message: `Failed to parse field '${field}': ${JSON.stringify(value)} is not a resource reference (res:N)`,
+        };
+    }
     for (const [field, type] of [["rom", "rom"], ["sram", "sram"], ["state", "state"]] as const) {
       const value = settings[field];
       if (value === undefined) {
@@ -507,7 +518,7 @@ export class MockBackend implements Backend {
         continue;
       }
       const match = typeof value === "string" ? RES_REF.exec(value) : null;
-      if (!match) return { code: "create-failed", message: `${field}: ${JSON.stringify(value)} is not a resource reference (res:N)` };
+      if (!match) return { code: "create-failed", message: `${field}: ${JSON.stringify(value)} names no resource` };
       const held = this.resources.get(Number(match[1]));
       if (!held) return { code: "create-failed", message: `${field}: "${value}" is not a live resource` };
       if (held.type !== type) return { code: "create-failed", message: `${field}: "${value}" is a ${held.type} resource, not a ${type}` };
````

Run: `sed -n '3691,3721p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm test meta`
Expected: `ok 7 - mock backend: a document naming a resource the engine does not hold is refused, as native refuses it` among its lines, then `# 10 passed, 0 skipped, 0 failed`.

- [ ] **Step 5: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 and no warning in the prototype's log (the one known warning, `-Wformat-truncation` in `plugin/PluginDSP.cpp`, appears only when that file recompiles).

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 174 test file(s) passed (jobs=16)`: the descriptor conformance and `buildGraph`'s goldens inside it pass, unchanged.

Run: `pnpm test:plugin`
Expected: all 10 binaries exit 0, `# 10 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 313 | 302 passed | 11 skipped`; graph-test `All tests passed (356273 assertions in 279 test cases)`; audio-test `All tests passed (1234835 assertions in 108 test cases)`; launchpad `All tests passed (144 assertions in 29 test cases)`; the small six at their own counts (midi 35 / 19, dynparams 24 / 7, plugin 21 / 6, lottie 7 / 1, classid 10 / 3, watcher 11 / 3).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1712421 assertions in 313 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: the same 167 files, with `# 144 case(s) SKIPPED in 65 file(s)` and the skip check passing: CI's view. Nothing in E1 adds a skip, so the skip baseline does not move.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed (jobs=16)`.

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` (fakes only, no hardware).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, then `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa renders match the golden's hashes`, then 5 passed, 0 skipped, 0 failed.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed.

Run: `bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'`
Expected: exit 0: `render-menu` and `render-badge` 1 passed each, then `# 7 test file(s) passed (jobs=16)`.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-bliptoaster && mkdir -p /tmp/e1-bliptoaster/build /tmp/e1-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/e1-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/e1-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/e1-bliptoaster/retroplug-cli/ && cd /tmp/e1-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/e1-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed. Settings documents do not change, so neither does a consumer's suite.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-nesvj && mkdir -p /tmp/e1-nesvj/build /tmp/e1-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/e1-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/e1-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/e1-nesvj/retroplug-cli/ && cd /tmp/e1-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/e1-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed. Settings documents do not change, so neither does a consumer's suite.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 6: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc)`
Expected: exit 0: the ThreadSanitizer tree's two binaries, built from this task's tree (a stale binary proves nothing).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag; the list is the verification script's), then `All tests passed (1698039 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), and no ThreadSanitizer report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263160 assertions in 79 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test -j$(nproc)`
Expected: exit 0: the AddressSanitizer tree's three binaries.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (1680205 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356273 assertions in 279 test cases)`, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report.

- [ ] **Step 7: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/core/native/src/graph/nodes/CoreNodes.cpp
 M packages/dsp/native/src/graph/nodes/DspNodes.cpp
 M packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp
 M packages/native/cmake/tests.cmake
 M packages/native/src/host/graph/SystemNodes.cpp
 M packages/native/test/graph/Resources.test.cpp
 M packages/node-sdk/native/src/graph/Descriptor.hpp
 M packages/node-sdk/native/src/graph/node.hpp
 M packages/retroplug/test/meta.test.ts
 M packages/retroplug/testing/mockBackend.ts
?? packages/native/test/graph/SettingsTypes.test.cpp
?? packages/node-sdk/native/src/graph/settings_types.hpp
````

````bash
git add -- packages/core/native/src/graph/nodes/CoreNodes.cpp packages/dsp/native/src/graph/nodes/DspNodes.cpp \
  packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp packages/native/cmake/tests.cmake \
  packages/native/src/host/graph/SystemNodes.cpp packages/native/test/graph/Resources.test.cpp \
  packages/native/test/graph/SettingsTypes.test.cpp packages/node-sdk/native/src/graph/Descriptor.hpp \
  packages/node-sdk/native/src/graph/node.hpp packages/node-sdk/native/src/graph/settings_types.hpp \
  packages/retroplug/test/meta.test.ts packages/retroplug/testing/mockBackend.ts
git commit -F - <<'EOF'
feat(node-sdk): fixed-capacity settings types, and the core and DSP nodes use them

rp::text<N> and rp::list<T, N> (graph/settings_types.hpp, included by node.hpp) hold a string or an array inline,
read and written as the JSON string or array they replace, with the same JSON Schema, so no descriptor golden moves.
A value over its bound is a settings error naming the field. rp::resource_ref<Type> stores the resource id as a
number: the document's "res:N" is parsed into it and written back as the same string, and a ref that is not "res:N"
is now refused where the settings are read (settings-invalid) rather than at create.

rp.core join/split counts become rp::list<uint32_t, 16> and every layout/from/to an rp::text<16> (the longest layout
name, "discrete(64)", is twelve bytes); rp.dsp.resample's layout follows. The system, host and N8 nodes keep
std::string. rp.host.audio-out's buses vector stays: the runtime implements it.

Tests: [settings-types] in retroplug-graph-test.
EOF
````

---

### Task 3: a device build of node.hpp, and settings as msgpack

Design section 6 and section 9's third commit. `graph/node.hpp` is built two ways. With `RP_DEVICE` (and no exceptions
and no RTTI) the SDK is a portable core: the wrappers, `Block`, the compile-time reflection, the hook checks, the
resource pins, an instance that holds its settings in the host's memory, and a settings read from msgpack, with no
descriptor, no JSON and no allocation. The desktop keeps everything it had and reads msgpack too, for any type whose
settings are portable. The split is by file (`node_core.hpp`, `node_device.hpp`, `node_desc.hpp` and the shared
headers), the field walk is reflect-cpp's compile-time core with our own name extraction, enums are scanned by our own
`enums.hpp`, and the msgpack reader is our own, because reflect-cpp's needs msgpack-c and a heap. `rp::choice` replaces
`rfl::Literal` in the portable nodes. By the user's decision a device-capable node keeps its desktop hooks (`validate`,
`adjust_layout` and the helpers only they use) inside one `#ifndef RP_DEVICE`, and the desktop build compiles every
`device: true` entry's target in device mode too (`<target>-device`), so a hook left outside fails every desktop build.

`retroplug-device-mode-test`, the eleventh `test:plugin` binary, holds the device build to the desktop's through the C
ABI: every portable `rp.core` and `rp.dsp` type renders bit for bit what the desktop renders, allocating nothing, and
the decode reads and refuses what JSON reads and refuses. graph-test gains `[fields]` and `[enums]` (our walk and scan
against reflect-cpp's) and the dither's `Mt19937` against `std::mt19937`, and every configure requires three sources to
fail in device mode with their own reasons. The stage also cross-compiled each device-mode source with
`arm-none-eabi-g++` (`-mcpu=cortex-m33 -mthumb -fno-exceptions -fno-rtti -Os`): every file built with no warning and no
object had a malloc, `operator new` or `delete`, `__cxa_*`, `std::__throw_*` or guard symbol (`s3g-arm.log`: `CoreNodes exit=0 text=13836` among its lines); Task 4's image is where that becomes a link
that CI runs.

**Files:**

- Create: `packages/dsp/native/src/dsp/Mt19937.hpp`, `packages/native/test/device-mode/DeviceMode.test.cpp`, `packages/native/test/device-mode/DeviceModeEntries.hpp`, `packages/native/test/device-mode/DeviceModeNodes.cpp`, `packages/native/test/device-mode/EchoNode.hpp`, `packages/native/test/device-mode/refused/HeapHeader.cpp`, `packages/native/test/device-mode/refused/HeapType.cpp`, `packages/native/test/device-mode/refused/PointerSettings.cpp`, `packages/native/test/graph/Fields.test.cpp`, `packages/node-sdk/native/src/graph/JsonToMsgpack.cpp`, `packages/node-sdk/native/src/graph/JsonToMsgpack.hpp`, `packages/node-sdk/native/src/graph/PortTypes.hpp`, `packages/node-sdk/native/src/graph/ResourceId.hpp`, `packages/node-sdk/native/src/graph/enums.hpp`, `packages/node-sdk/native/src/graph/fields.hpp`, `packages/node-sdk/native/src/graph/msgpack_reader.hpp`, `packages/node-sdk/native/src/graph/node_core.hpp`, `packages/node-sdk/native/src/graph/node_desc.hpp`, `packages/node-sdk/native/src/graph/node_device.hpp`, `packages/node-sdk/native/src/graph/sdk_namespace.hpp`, `packages/node-sdk/native/src/graph/settings_json.hpp`, `packages/node-sdk/native/src/graph/settings_msgpack.hpp`
- Modify: `cmake/packages.cmake`, `package.json`, `packages/core/native/src/graph/nodes/CoreNodes.cpp`, `packages/dsp/native/src/dsp/Dither.cpp`, `packages/dsp/native/src/dsp/Dither.hpp`, `packages/dsp/native/src/graph/nodes/DspNodes.cpp`, `packages/native/cmake/test-binary.cmake`, `packages/native/cmake/tests.cmake`, `packages/native/cmake/warnings.cmake`, `packages/native/test/graph/Dsp.test.cpp`, `packages/node-sdk/native/CMakeLists.txt`, `packages/node-sdk/native/src/graph/Descriptor.hpp`, `packages/node-sdk/native/src/graph/GraphDocument.cpp`, `packages/node-sdk/native/src/graph/node.hpp`, `packages/node-sdk/native/src/graph/rp_node.h`, `packages/node-sdk/native/src/graph/settings_types.hpp`, `packages/retroplug/scripts/run-plugin-tests.mjs`

- [ ] **Step 1: Write the failing device-mode cases and the refused sources**

`test/device-mode/` holds the new binary: `DeviceMode.test.cpp` (six cases: a device-mode type carries no description
and never re-plans; the twin table renders bit for bit with nothing allocated; the desktop reads msgpack and renders
the table as from JSON; a missing field keeps its default and an unknown one is stepped over; the decode refuses what
JSON refuses in the same words; the converter writes each number as the JSON spells it), `DeviceModeNodes.cpp` (the
device-mode half: the `rp.devtest` package), `EchoNode.hpp` (`rp.devtest.echo`, a settings field of every portable
kind, `pair` with two resources and `shaper` with an enum field and an `enum_param`, built in both modes) and
`DeviceModeEntries.hpp` (the C entries and the plain view both halves share). `refused/` holds three sources that must
not compile in device mode: a `heap` type, settings holding a pointer, and `<string>` included before `node.hpp`.
`Fields.test.cpp` holds the field walk and the enum scan to reflect-cpp's, and `Dsp.test.cpp` gains the `Mt19937`
sweep. `tests.cmake` adds `Fields.test.cpp`, the object library of device-mode sources (with the C entries renamed
`<entry>_device_mode`), the refused-source `try_compile`s and the binary; the root `package.json`'s `test:plugin` and
`run-plugin-tests.mjs` gain it as the eleventh, and `test-binary.cmake`'s count follows.

Create `packages/native/test/device-mode/DeviceMode.test.cpp`:

````cpp
// The device build of the node SDK (docs/modular-graph-phase-e1-design.md sections 6 and 8), tested on the desktop.
//
// rp.core and rp.dsp are built twice into this binary: by their own targets, as every desktop product has them, and in
// device mode (RP_DEVICE, no exceptions, no RTTI) by retroplug-device-mode-objects, beside the echo node of
// EchoNode.hpp (cmake/tests.cmake says how the two builds share one binary). A device-mode type has no descriptor and
// no layout, since a device never plans, so the planner reaches one here through a twin: a type that answers
// descriptor and layout_for from the desktop's build of the same node and creates its instances with the device's,
// handing the device's create the msgpack the desktop's settings converter (graph/JsonToMsgpack.hpp) writes from the
// document's JSON, as E3's plan export will. The twin's instances run through a real GraphHost.
//
// - Every portable rp.core and rp.dsp type (every one but rp.dsp.resample and rp.dsp.dpcm, which declare heap) over a
//   table of settings, its defaults and each field moved, resources named where the type takes one: a plan whose node
//   is the device's build renders, over ragged blocks and with its params moving, exactly what the same plan renders
//   with the desktop's build, at every probe the plan feeds and in every buffer it records.
// - The device-mode instance's query_memory, create, activate, process, ports_changed, get_interface, deactivate and
//   destroy allocate nothing (the counting allocator, around each call the twin forwards).
// - The decode: a missing field keeps its default, an unknown one is stepped over, every kind of field reads what the
//   desktop's JSON reader reads, and an over-long text or list, a value of the wrong type and a malformed blob are
//   refused, a field's refusal naming it. The desktop's build reads msgpack too, through the same decode: a node made
//   from msgpack is the one made from JSON, and the whole table renders the same with the desktop's types reading
//   msgpack.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/node.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"

// The rig's own nodes' settings, in a named namespace as a desktop Settings struct's schema needs.
namespace rp::graph::devtest_rig {

struct NoiseSettings {
    std::uint32_t channels = 2;
    std::string   layout;  // empty: the count's own name (mono, stereo, discrete(n))
    std::uint32_t seed = 1;
};

}  // namespace rp::graph::devtest_rig

namespace {

using namespace rp::graph;

// ---- the rig's nodes: a deterministic source of every port kind, and a probe that digests what reaches it -------

struct Noise {
    static constexpr const char* id = "rp.devtest.noise";
    using Settings = devtest_rig::NoiseSettings;
    struct {
        rp::audio_out<rp::stereo> out;
        rp::events_out            events;
        rp::control_out           control;
    } outputs;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "noise"); }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels =
            ChannelSpec::fixed(s.channels, s.layout.empty() ? layoutForCount(s.channels) : s.layout);
    }

    std::uint32_t state = 1;
    void on_settings(const Settings& s) noexcept { state = s.seed * 2654435761u + 1u; }

    std::uint32_t next() noexcept {  // xorshift32
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float sample() noexcept { return (static_cast<float>(next() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 1.2f; }

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = sample();
        // Up to three messages and two control changes a block, each at a frame of its own, in order.
        std::uint32_t frame = 0;
        for (std::uint32_t k = next() % 4; k > 0 && frame < b.frames(); --k, frame += 1 + next() % 8) {
            const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(next() & 0x7f), 100};
            outputs.events.push(frame, note);
        }
        frame = 0;
        for (std::uint32_t k = next() % 3; k > 0 && frame < b.frames(); --k, frame += 1 + next() % 16)
            outputs.control.push(frame, static_cast<double>(sample()) * 3.0);
    }
};

// FNV-1a 64 over everything a block brings each input: the frame count, each audio channel's samples bit for bit,
// each message's frame, flags and bytes, the control stream's start and changes. Nothing is allocated on the block.
struct Probe {
    static constexpr const char* id = "rp.devtest.probe";
    struct {
        rp::audio_in<rp::any> in;
        rp::events_in         events;
        rp::control_in        control;
    } inputs;

    std::uint64_t hash = 1469598103934665603ull;
    std::uint32_t blocks = 0;
    bool          heard = false;  // anything but silence and empty queues reached it

    void mix(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    }
    template <class V>
    void mix(const V& v) noexcept { mix(&v, sizeof v); }

    void process(const rp::Block& b) noexcept {
        mix(b.frames());
        mix(inputs.in.channel_count);
        for (std::uint32_t c = 0; c < inputs.in.channel_count; ++c) {
            mix(inputs.in[c], b.frames() * sizeof(float));
            for (std::uint32_t i = 0; i < b.frames(); ++i) heard = heard || inputs.in[c][i] != 0.0f;
        }
        for (const auto& m : inputs.events.messages()) {
            mix(m.frame);
            mix(m.flags);
            const auto data = inputs.events.data(m);
            mix(data.data(), data.size());
            heard = true;
        }
        mix(inputs.control.start());
        for (const auto& c : inputs.control.changes()) {
            mix(c.frame);
            mix(c.value);
            heard = true;
        }
        heard = heard || inputs.control.start() != 0.0;
        ++blocks;
    }
};

const rp_package* rigPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Noise>(), rp::node_type<Probe>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.rig","version":"1.0.0"})", 2, kTypes};
    return &kPackage;
}

const rp_package* echoPackageDesktop() {
    static const rp_node_type* const kTypes[] = {rp::node_type<devtest::Echo>(), rp::node_type<devtest::Pair>(),
                                                 rp::node_type<devtest::Shaper>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest","version":"1.0.0"})", 3, kTypes};
    return &kPackage;
}

// ---- twins: the desktop's description, another build's instances -----------------------------------------------

struct Tally {
    int allocations = 0;
    int frees = 0;
    int calls = 0;
};

struct Twin {
    rp_node_type        type{};
    const rp_node_type* desc = nullptr;  // descriptor and layout_for: the desktop's build
    const rp_node_type* inst = nullptr;  // query_memory, create, destroy: the build under test
    bool                counted = false;  // count the instance type's calls (a device-mode type's)
    Tally               tally;
};

struct LiveNode {
    const rp_node_vtable* vt;  // the instance's own vtable, which the counting one forwards to
    Twin*                 twin;
};
std::map<const rp_node*, LiveNode>& liveNodes() {
    static std::map<const rp_node*, LiveNode> nodes;
    return nodes;
}

// f() with this thread's allocations counted into the twin's tally (the C library's too, where the binary sees them).
template <class F>
auto counted(Twin& twin, F&& f) {
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    struct End {
        Twin& twin;
        ~End() {
            const auto c = rp::test::endCountingAllocations();
            twin.tally.allocations += c.allocations;
            twin.tally.frees += c.frees;
            ++twin.tally.calls;
        }
    } end{twin};
    return f();
}

Twin* twinOf(const rp_node_type* t) { return static_cast<Twin*>(t->ctx); }

// The settings the planner hands a type, as JSON, converted to the msgpack a device reads. A conversion that fails
// is reported through the type's error, so the plan refuses rather than a callback throwing into the host.
bool toMsgpack(const rp_settings* s, std::vector<std::uint8_t>* out, rp_error* err) {
    const std::string_view json = s && s->data && s->size ? std::string_view(s->data, s->size) : std::string_view("{}");
    auto converted = settingsToMsgpack(json);
    if (!converted) {
        rp::detail::copyError(err, converted.error().what());
        return false;
    }
    *out = std::move(*converted);
    return true;
}

const char* twinDescriptor(const rp_node_type* t) { return twinOf(t)->desc->descriptor(twinOf(t)->desc); }

std::size_t twinLayoutFor(const rp_node_type* t, const rp_settings* s, char* out, std::size_t capacity, rp_error* err) {
    return twinOf(t)->desc->layout_for(twinOf(t)->desc, s, out, capacity, err);
}

bool twinQueryMemory(const rp_node_type* t, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    Twin& twin = *twinOf(t);
    std::vector<std::uint8_t> bytes;
    if (!toMsgpack(s, &bytes, err)) return false;
    const rp_settings m{reinterpret_cast<const char*>(bytes.data()), bytes.size(), RP_SETTINGS_MSGPACK};
    if (!twin.counted) return twin.inst->query_memory(twin.inst, &m, out, err);
    return counted(twin, [&] { return twin.inst->query_memory(twin.inst, &m, out, err); });
}

bool countedActivate(rp_node* n, double rate, std::uint32_t frames, rp_error* err) {
    const LiveNode& l = liveNodes().at(n);
    return counted(*l.twin, [&] { return l.vt->activate(n, rate, frames, err); });
}
void countedDeactivate(rp_node* n) {
    const LiveNode& l = liveNodes().at(n);
    counted(*l.twin, [&] { l.vt->deactivate(n); });
}
void countedPortsChanged(rp_node* n, const std::uint8_t* connected, std::uint32_t count) {
    const LiveNode& l = liveNodes().at(n);
    counted(*l.twin, [&] { l.vt->ports_changed(n, connected, count); });
}
rp_status countedProcess(rp_node* n, const rp_process* p) {
    const LiveNode& l = liveNodes().at(n);
    return counted(*l.twin, [&] { return l.vt->process(n, p); });
}
const void* countedInterface(rp_node* n, const char* id) {
    const LiveNode& l = liveNodes().at(n);
    return counted(*l.twin, [&] { return l.vt->get_interface(n, id); });
}
// A device-mode vtable's settings entries are null, and so are these: a twin is planned once and never re-planned.
const rp_node_vtable kCountingVtable{&countedActivate, &countedDeactivate, &countedPortsChanged, &countedProcess,
                                     nullptr,          nullptr,            nullptr,              &countedInterface};

rp_node* twinCreate(const rp_node_type* t, const rp_host* host, void* memory, const rp_settings* s, rp_error* err) {
    Twin& twin = *twinOf(t);
    std::vector<std::uint8_t> bytes;
    if (!toMsgpack(s, &bytes, err)) return nullptr;
    const rp_settings m{reinterpret_cast<const char*>(bytes.data()), bytes.size(), RP_SETTINGS_MSGPACK};
    rp_node* n = twin.counted ? counted(twin, [&] { return twin.inst->create(twin.inst, host, memory, &m, err); })
                              : twin.inst->create(twin.inst, host, memory, &m, err);
    if (!n) return nullptr;
    liveNodes().emplace(n, LiveNode{n->vt, &twin});
    if (twin.counted) n->vt = &kCountingVtable;
    return n;
}

void twinDestroy(rp_node* n) {
    const auto it = liveNodes().find(n);
    REQUIRE(it != liveNodes().end());
    const LiveNode l = it->second;
    liveNodes().erase(it);
    n->vt = l.vt;
    if (l.twin->counted) counted(*l.twin, [&] { l.twin->inst->destroy(n); });
    else l.twin->inst->destroy(n);
}

// A package of twins: every type `instances` has, described by the type of the same id in `desktop`.
struct TwinPackage {
    std::vector<std::unique_ptr<Twin>> twins;
    std::vector<const rp_node_type*>   types;
    rp_package                         package{};

    TwinPackage(const rp_package* desktop, const rp_package* instances, bool count) {
        for (std::uint32_t i = 0; i < instances->node_type_count; ++i) {
            const rp_node_type* inst = instances->node_types[i];
            const rp_node_type* desc = nullptr;
            for (std::uint32_t k = 0; k < desktop->node_type_count; ++k)
                if (std::string_view(desktop->node_types[k]->id) == inst->id) desc = desktop->node_types[k];
            INFO(inst->id);
            REQUIRE(desc);
            REQUIRE(desc->flags == inst->flags);
            auto twin = std::make_unique<Twin>();
            twin->desc = desc;
            twin->inst = inst;
            twin->counted = count;
            twin->type = rp_node_type{inst->id,     &twinDescriptor, &twinLayoutFor, &twinQueryMemory,
                                      &twinCreate,  &twinDestroy,    inst->flags,    twin.get()};
            types.push_back(&twin->type);
            twins.push_back(std::move(twin));
        }
        package = rp_package{RP_ABI_VERSION, instances->manifest_json, static_cast<std::uint32_t>(types.size()),
                             types.data()};
    }
};

// ---- the rig and the table -------------------------------------------------------------------------------------

constexpr std::uint32_t kMaxBlock = 1024;
constexpr std::size_t   kFrames = 4096;
// Ragged blocks, as the DSP nodes' tests split a stream: one frame, a prime, a large one, and so on, cycling.
const std::vector<std::uint32_t> kSplit = {1, 31, 512, 3, 1024, 77};

struct Rig {
    NodeRegistry               registry;
    ResourceTable              resources;
    std::unique_ptr<GraphHost> host;
    std::vector<std::vector<float>> outs = std::vector<std::vector<float>>(2, std::vector<float>(kMaxBlock));
    std::vector<float*>        outPointers{outs[0].data(), outs[1].data()};
    rp_transport               transport{48000.0, 120.0, 0.0, 0};
    ResourceTable::Id          a = 0, b = 0, c = 0;  // a stereo source, a mono source, a stereo record target

    explicit Rig(const std::vector<const rp_package*>& packages) {
        std::string why;
        for (const rp_package* p : packages) {
            const bool added = registry.addPackage(p, &why);
            INFO(why);
            REQUIRE(added);
        }
        host = std::make_unique<GraphHost>(registry, resources, HostConfig{48000.0, kMaxBlock, Target::Desktop});
        a = buffer(2, 3000, 11);
        b = buffer(1, 2000, 23);
        c = resources.createAudioBuffer(2, 4096, 48000.0);
    }

    ResourceTable::Id buffer(std::uint32_t channels, std::uint32_t frames, std::uint32_t seed) {
        const auto id = resources.createAudioBuffer(channels, frames, 48000.0);
        auto* buf = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        std::uint32_t x = seed;
        for (std::uint32_t ch = 0; ch < channels; ++ch)
            for (std::uint32_t i = 0; i < frames; ++i) {
                x = x * 1664525u + 1013904223u;
                buf->data[ch][i] = (static_cast<float>(x >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.4f;
            }
        return id;
    }

    // `document` with $A, $B and $C replaced by this rig's resource ids, as "res:N".
    std::string resolve(std::string document) const {
        for (const auto& [mark, id] : {std::pair{std::string("$A"), a}, {std::string("$B"), b}, {std::string("$C"), c}})
            for (std::size_t at; (at = document.find(mark)) != std::string::npos;)
                document.replace(at, mark.size(), "res:" + std::to_string(id));
        return document;
    }
};

// One param change, at the start of the block with this index.
struct Move {
    std::size_t block;
    std::string node;
    std::string param;
    double      value;
};

struct Case {
    std::string              name;
    std::string              document;
    std::vector<std::string> probes;
    std::vector<Move>        moves;
    bool                     records = false;  // the plan records into $C

    Case(std::string n, std::string d, std::vector<std::string> p, std::vector<Move> m = {}, bool r = false)
        : name(std::move(n)), document(std::move(d)), probes(std::move(p)), moves(std::move(m)), records(r) {}
};

std::string noise(const std::string& id, std::uint32_t channels, std::uint32_t seed, const std::string& layout = "") {
    return R"({"id":")" + id + R"(","type":"rp.devtest.noise","settings":{"channels":)" + std::to_string(channels) +
           R"(,"seed":)" + std::to_string(seed) + R"(,"layout":")" + layout + R"("}})";
}
std::string probe(const std::string& id) { return R"({"id":")" + id + R"(","type":"rp.devtest.probe"})"; }
std::string node(const std::string& type, const std::string& settings, const std::string& params = "{}",
                 const std::string& resources = "{}") {
    return R"({"id":"x","type":")" + type + R"(","settings":)" + settings + R"(,"params":)" + params +
           R"(,"resources":)" + resources + "}";
}
std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + R"("})";
}
std::string doc(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string out = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) out += (i ? "," : "") + nodes[i];
    out += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) out += (i ? "," : "") + wires[i];
    return out + "]}";
}

// A node with one audio input and output, fed by noise and probed.
Case through(const std::string& name, const std::string& type, const std::string& settings, std::uint32_t channels,
             const std::string& layout = "", const std::string& params = "{}", std::vector<Move> moves = {}) {
    return {name,
            doc({noise("n", channels, 7, layout), node(type, settings, params), probe("p")},
                {wire("n.out", "x.in"), wire("x.out", "p.in")}),
            {"p"},
            std::move(moves)};
}

std::vector<Case> cases() {
    std::vector<Case> t;
    // rp.core.add, audio and control
    t.push_back({"add, defaults",
                 doc({noise("n1", 2, 1), noise("n2", 2, 2), node("rp.core.add", "{}"), probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"add, three inputs",
                 doc({noise("n1", 1, 1), noise("n2", 1, 2), noise("n3", 1, 3), node("rp.core.add", R"({"inputs":3})"),
                      probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("n3.out", "x.in3"),
                      wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"add, control",
                 doc({noise("n1", 1, 4), noise("n2", 1, 5), node("rp.core.add", R"({"kind":"control"})"), probe("p")},
                     {wire("n1.control", "x.in1"), wire("n2.control", "x.in2"), wire("x.out", "p.control")}),
                 {"p"}});
    // rp.core.merge
    t.push_back({"merge, defaults",
                 doc({noise("n1", 1, 6), noise("n2", 1, 7), node("rp.core.merge", "{}"), probe("p")},
                     {wire("n1.events", "x.in1"), wire("n2.events", "x.in2"), wire("x.out", "p.events")}),
                 {"p"}});
    t.push_back({"merge, three inputs",
                 doc({noise("n1", 1, 6), noise("n2", 1, 7), noise("n3", 1, 8), node("rp.core.merge", R"({"inputs":3})"),
                      probe("p")},
                     {wire("n1.events", "x.in1"), wire("n2.events", "x.in2"), wire("n3.events", "x.in3"),
                      wire("x.out", "p.events")}),
                 {"p"}});
    // rp.core.join and rp.core.split
    t.push_back({"join, defaults",
                 doc({noise("n1", 1, 9), noise("n2", 1, 10), node("rp.core.join", "{}"), probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"join, inputs and layout moved",
                 doc({noise("n1", 2, 9), noise("n2", 1, 10),
                      node("rp.core.join", R"j({"inputs":[2,1],"layout":"discrete(3)"})j"), probe("p")},
                     {wire("n1.out", "x.in1"), wire("n2.out", "x.in2"), wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"split, defaults",
                 doc({noise("n", 2, 11), node("rp.core.split", "{}"), probe("p1"), probe("p2")},
                     {wire("n.out", "x.in"), wire("x.out1", "p1.in"), wire("x.out2", "p2.in")}),
                 {"p1", "p2"}});
    t.push_back({"split, outputs and layout moved",
                 doc({noise("n", 3, 11), node("rp.core.split", R"j({"outputs":[1,2],"layout":"discrete(3)"})j"),
                      probe("p1"), probe("p2")},
                     {wire("n.out", "x.in"), wire("x.out1", "p1.in"), wire("x.out2", "p2.in")}),
                 {"p1", "p2"}});
    // rp.core.downmix and rp.core.relabel
    t.push_back(through("downmix, defaults", "rp.core.downmix", "{}", 2));
    t.push_back(through("downmix, 5.1 to stereo", "rp.core.downmix", R"({"from":"5.1","to":"stereo"})", 6, "5.1"));
    t.push_back(through("downmix, 5.1 to mono", "rp.core.downmix", R"({"from":"5.1"})", 6, "5.1"));
    t.push_back(through("relabel, defaults", "rp.core.relabel", "{}", 6));
    t.push_back(through("relabel, channels and layout moved", "rp.core.relabel",
                        R"({"channels":2,"layout":"stereo"})", 2));
    // rp.core.delay
    t.push_back(through("delay, defaults", "rp.core.delay", "{}", 2, "", R"({"time":100})",
                        {{3, "x", "time", 2000}, {7, "x", "time", 1}}));
    t.push_back(through("delay, maxSamples and channels moved", "rp.core.delay",
                        R"({"maxSamples":100,"channels":1})", 1, "", R"({"time":37})",
                        {{2, "x", "time", 250}, {5, "x", "time", 12}}));
    // rp.core.constant
    t.push_back({"constant, defaults",
                 doc({node("rp.core.constant", "{}", R"({"value":0.3})"), probe("p")}, {wire("x.out", "p.control")}),
                 {"p"},
                 {{2, "x", "value", 0.9}, {4, "x", "value", 0.1}}});
    t.push_back({"constant, min and max moved",
                 doc({node("rp.core.constant", R"({"min":-2,"max":5})", R"({"value":4})"), probe("p")},
                     {wire("x.out", "p.control")}),
                 {"p"},
                 {{3, "x", "value", -1.5}}});
    // rp.core.buffer-player and rp.core.buffer-recorder
    t.push_back({"buffer-player, defaults",
                 doc({node("rp.core.buffer-player", "{}", "{}", R"({"sample":"$A"})"), probe("p")},
                     {wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"buffer-player, channels, layout and loop moved",
                 doc({node("rp.core.buffer-player", R"({"channels":1,"layout":"mono","loop":false})", "{}",
                           R"({"sample":"$B"})"),
                      probe("p")},
                     {wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"buffer-recorder, defaults",
                 doc({noise("n", 2, 12), node("rp.core.buffer-recorder", "{}", "{}", R"({"target":"$C"})")},
                     {wire("n.out", "x.in")}),
                 {},
                 {{3, "x", "record", 0}, {6, "x", "record", 1}},
                 true});
    t.push_back({"buffer-recorder, channels moved",
                 doc({noise("n", 1, 13), node("rp.core.buffer-recorder", R"({"channels":1})", "{}",
                                               R"({"target":"$C"})")},
                     {wire("n.out", "x.in")}),
                 {},
                 {{2, "x", "record", 0}, {4, "x", "record", 1}},
                 true});
    // rp.dsp.gain, rp.dsp.biquad, rp.dsp.dither
    t.push_back(through("gain, defaults", "rp.dsp.gain", "{}", 2, "", R"({"gain":0.37})",
                        {{2, "x", "gain", -1.5}, {5, "x", "gain", 0.37}}));
    t.push_back(through("gain, channels moved", "rp.dsp.gain", R"({"channels":1})", 1, "", R"({"gain":2.5})"));
    t.push_back(through("biquad, defaults", "rp.dsp.biquad", "{}", 2, "", R"({"frequency":2000,"q":0.7})",
                        {{3, "x", "frequency", 300}, {6, "x", "q", 4}}));
    t.push_back(through("biquad, type and channels moved", "rp.dsp.biquad", R"({"type":"peak","channels":1})", 1, "",
                        R"({"frequency":900,"q":2,"gainDb":6})", {{4, "x", "gainDb", -9}}));
    t.push_back(through("biquad, high-shelf", "rp.dsp.biquad", R"({"type":"high-shelf"})", 2, "",
                        R"({"frequency":5000,"gainDb":-4})"));
    t.push_back(through("dither, defaults", "rp.dsp.dither", "{}", 2));
    t.push_back(through("dither, algorithm, bits and seed moved", "rp.dsp.dither",
                        R"({"algorithm":"high-pass-tpdf","bits":6,"seed":99})", 2));
    t.push_back(through("dither, jjn and channels moved", "rp.dsp.dither", R"({"algorithm":"jjn","channels":1})", 1));
    t.push_back(
        through("dither, seedFrom named", "rp.dsp.dither", R"({"algorithm":"shaped-tpdf","seedFrom":"$B"})", 2));
    // rp.devtest.shaper: an enum settings field and an enum_param, which a device reads with enums.hpp
    t.push_back(through("shaper, defaults", "rp.devtest.shaper", "{}", 2, "", "{}",
                        {{2, "x", "shape", 0}, {4, "x", "shape", 2}, {6, "x", "shape", 1.4}, {8, "x", "shape", 9}}));
    t.push_back(through("shaper, curve and channels moved", "rp.devtest.shaper", R"({"curve":"Fold","channels":1})",
                        1, "", R"({"shape":"invert"})", {{3, "x", "shape", -1}}));
    t.push_back(through("shaper, curve as a number", "rp.devtest.shaper", R"({"curve":"1"})", 2));
    // rp.dsp.normalize, which names its buffer in its settings
    t.push_back({"normalize, buffer named",
                 doc({node("rp.dsp.normalize", R"({"buffer":"$A"})"), probe("p")}, {wire("x.out", "p.in")}),
                 {"p"}});
    t.push_back({"normalize, gain and channels moved",
                 doc({node("rp.dsp.normalize", R"({"buffer":"$B","gain":0.5,"channels":1})"), probe("p")},
                     {wire("x.out", "p.in")}),
                 {"p"}});
    return t;
}

struct Run {
    std::vector<std::uint64_t> hashes;
    std::vector<bool>          heard;
    std::vector<float>         recorded;
};

Run render(Rig& rig, const Case& c) {
    const auto result = rig.host->setGraph(rig.resolve(c.document));
    std::string errors;
    for (const auto& e : result.errors) errors += e.code + ": " + e.message + "\n";
    INFO(c.name << "\n" << errors);
    REQUIRE(result.ok);
    std::size_t at = 0, block = 0;
    while (at < kFrames) {
        for (const auto& m : c.moves)
            if (m.block == block) REQUIRE(rig.host->setParam(m.node, m.param, m.value));
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(kSplit[block % kSplit.size()], kFrames - at));
        rig.host->processBlock(n, rig.transport, rig.outPointers.data(), 2);
        at += n;
        ++block;
    }
    Run run;
    for (const auto& id : c.probes) {
        const Probe* p = rp::peer_as<Probe>(rig.host->node(id));
        REQUIRE(p);
        REQUIRE(p->blocks == block);
        run.hashes.push_back(p->hash);
        run.heard.push_back(p->heard);
    }
    if (c.records) {
        const auto* buf = static_cast<const rp_audio_buffer*>(rig.resources.get(rig.c)->payload);
        for (std::uint32_t ch = 0; ch < buf->channels; ++ch)
            run.recorded.insert(run.recorded.end(), buf->data[ch], buf->data[ch] + buf->frames);
    }
    return run;
}

bool anyNonZero(const std::vector<float>& v) {
    for (const float x : v)
        if (x != 0.0f) return true;
    return false;
}

// Every case rendered by `twins` and by the desktop's own types, compared bit for bit.
void compareTable(const TwinPackage& core, const TwinPackage& dsp, const TwinPackage& devtest) {
    for (const Case& c : cases()) {
        INFO(c.name);
        Rig desktop({rp_core_package(), rp_dsp_package(), echoPackageDesktop(), hostPackage(), rigPackage()});
        Rig twin({&core.package, &dsp.package, &devtest.package, hostPackage(), rigPackage()});
        const Run want = render(desktop, c);
        const Run got = render(twin, c);
        for (std::size_t i = 0; i < want.hashes.size(); ++i) {
            INFO("probe " << c.probes[i]);
            CHECK(want.heard[i]);  // the comparison is of a stream, not of two silences
            CHECK(got.hashes[i] == want.hashes[i]);
        }
        if (c.records) {
            CHECK(anyNonZero(want.recorded));
            CHECK(got.recorded == want.recorded);
        }
    }
}

// ---- direct calls: one instance of a type, from settings in either encoding -------------------------------------

std::vector<std::uint8_t> msgpackOf(const std::string& json) {
    auto r = settingsToMsgpack(json);
    INFO(json);
    REQUIRE(r);
    return *r;
}

rp_settings asSettings(const std::vector<std::uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size(), RP_SETTINGS_MSGPACK};
}
rp_settings asSettings(const std::string& json) { return {json.data(), json.size(), RP_SETTINGS_JSON}; }

// One instance in memory of its own, destroyed with this; `refusal` is what query_memory or create refused it with.
struct Made {
    const rp_node_type*            type;
    std::vector<std::max_align_t>  memory;
    rp_node*                       node = nullptr;
    std::string                    refusal;

    Made(const rp_node_type* t, const rp_settings& s, const rp_host* host = nullptr) : type(t) {
        rp_memory_req req{};
        rp_error      err{};
        if (!t->query_memory(t, &s, &req, &err)) {
            refusal = err.message;
            return;
        }
        REQUIRE(req.align <= alignof(std::max_align_t));
        memory.resize(req.size / sizeof(std::max_align_t) + 1);
        node = t->create(t, host, memory.data(), &s, &err);
        if (!node) refusal = err.message;
    }
    ~Made() {
        if (node) type->destroy(node);
    }
    Made(const Made&) = delete;
    Made& operator=(const Made&) = delete;

    const EchoView& echo() const {
        REQUIRE(node);
        const auto* view = static_cast<const EchoView*>(node->vt->get_interface(node, kEchoInterface));
        REQUIRE(view);
        return *view;
    }
};

const rp_node_type* deviceEcho() { return rp_devtest_package_device_mode()->node_types[0]; }
const rp_node_type* desktopEcho() { return echoPackageDesktop()->node_types[0]; }

void checkSame(const EchoView& a, const EchoView& b) {
    CHECK(a.flag == b.flag);
    CHECK(a.u8 == b.u8);
    CHECK(a.u32 == b.u32);
    CHECK(a.i32 == b.i32);
    CHECK(a.i64 == b.i64);
    CHECK(a.f == b.f);
    CHECK(a.d == b.d);
    CHECK(a.pick == b.pick);
    CHECK(std::string(a.name, a.nameSize) == std::string(b.name, b.nameSize));
    CHECK(std::vector<std::uint16_t>(a.counts, a.counts + a.countsSize) ==
          std::vector<std::uint16_t>(b.counts, b.counts + b.countsSize));
    CHECK(a.rom == b.rom);
    CHECK(a.romResolved == b.romResolved);
}

// The device's echo made from `json` as msgpack is the desktop's made from `json` as JSON, field for field.
void checkEcho(const std::string& json) {
    INFO(json);
    const auto bytes = msgpackOf(json);
    Made device(deviceEcho(), asSettings(bytes));
    Made desktop(desktopEcho(), asSettings(json));
    INFO(device.refusal << " / " << desktop.refusal);
    REQUIRE(device.node);
    REQUIRE(desktop.node);
    checkSame(device.echo(), desktop.echo());
}

// What the device's echo refuses `bytes` with; the desktop's build reads msgpack through the same decode, so it must
// refuse them in the same words.
std::string refusal(const std::vector<std::uint8_t>& bytes) {
    Made device(deviceEcho(), asSettings(bytes));
    Made desktop(desktopEcho(), asSettings(bytes));
    CHECK_FALSE(device.node);
    CHECK_FALSE(desktop.node);
    CHECK(device.refusal == desktop.refusal);
    return device.refusal;
}
std::string refusal(const std::string& json) { return refusal(msgpackOf(json)); }

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// A host whose resource table holds one resource, a "rom" at id 1, and counts the pins it hands out and takes back.
struct OneRomHost {
    rp_bytes          bytes{nullptr, 0};
    rp_resource       rom{"rom", &bytes};
    int               acquired = 0;
    int               released = 0;
    rp_host_resources service{this, &acquire, &release};
    rp_host           host{RP_ABI_VERSION, this, nullptr, nullptr, nullptr, &interface};

    static const rp_resource* acquire(void* ctx, std::uint32_t id, const char* type, rp_error* err) {
        auto* self = static_cast<OneRomHost*>(ctx);
        if (id != 1 || std::string_view(type) != "rom") {
            rp::detail::copyError(err, "is not a live resource");
            return nullptr;
        }
        ++self->acquired;
        return &self->rom;
    }
    static bool release(void* ctx, std::uint32_t) {
        ++static_cast<OneRomHost*>(ctx)->released;
        return true;
    }
    static const void* interface(const rp_host* h, const char* id) {
        return std::string_view(id) == RP_HOST_RESOURCES_INTERFACE ? &static_cast<OneRomHost*>(h->ctx)->service
                                                                    : nullptr;
    }
};

}  // namespace

TEST_CASE("a device-mode type carries no description and never re-plans", "[device-mode]") {
    for (const rp_package* p : {rp_core_package_device_mode(), rp_dsp_package_device_mode()}) {
        for (std::uint32_t i = 0; i < p->node_type_count; ++i) {
            const rp_node_type* t = p->node_types[i];
            INFO(t->id);
            CHECK(t->descriptor == nullptr);
            CHECK(t->layout_for == nullptr);
            CHECK(t->query_memory != nullptr);
            CHECK(t->create != nullptr);
            CHECK(t->destroy != nullptr);
        }
    }
    // The device's rp.dsp has no rp.dsp.dpcm, which declares heap; every other type is there.
    CHECK(rp_core_package_device_mode()->node_type_count == rp_core_package()->node_type_count);
    CHECK(rp_dsp_package_device_mode()->node_type_count == rp_dsp_package()->node_type_count - 1);
    for (std::uint32_t i = 0; i < rp_dsp_package_device_mode()->node_type_count; ++i)
        CHECK(std::string_view(rp_dsp_package_device_mode()->node_types[i]->id) != "rp.dsp.dpcm");

    // An instance's vtable: the block path and the lifecycle, and no settings swap.
    Made echo(deviceEcho(), asSettings(std::vector<std::uint8_t>{}));
    REQUIRE(echo.node);
    CHECK(echo.node->vt->prepare_settings == nullptr);
    CHECK(echo.node->vt->commit_settings == nullptr);
    CHECK(echo.node->vt->release_settings == nullptr);
    CHECK(echo.node->vt->process != nullptr);

    // And a planning host refuses such a type rather than calling a null descriptor.
    NodeRegistry registry;
    std::string  why;
    CHECK_FALSE(registry.addPackage(rp_devtest_package_device_mode(), &why));
    CHECK(contains(why, "descriptor"));
}

TEST_CASE("every portable rp.core and rp.dsp type renders, built for the device, what the desktop's build renders",
          "[device-mode]") {
    TwinPackage core(rp_core_package(), rp_core_package_device_mode(), true);
    TwinPackage dsp(rp_dsp_package(), rp_dsp_package_device_mode(), true);
    TwinPackage devtest(echoPackageDesktop(), rp_devtest_package_device_mode(), true);
    compareTable(core, dsp, devtest);

    // Every device-mode type the table places ran (the echo and pair nodes are the direct cases' below), and not one
    // of its calls allocated or freed.
    for (const auto* package : {&core, &dsp, &devtest}) {
        for (const auto& twin : package->twins) {
            INFO(twin->type.id);
            const std::string_view id = twin->type.id;
            if (id != "rp.devtest.echo" && id != "rp.devtest.pair") CHECK(twin->tally.calls > 0);
            CHECK(twin->tally.allocations == 0);
            CHECK(twin->tally.frees == 0);
        }
    }
}

TEST_CASE("the desktop's build reads msgpack settings, and renders the table as it does from JSON", "[device-mode]") {
    TwinPackage core(rp_core_package(), rp_core_package(), false);
    TwinPackage dsp(rp_dsp_package(), rp_dsp_package(), false);
    TwinPackage devtest(echoPackageDesktop(), echoPackageDesktop(), false);
    compareTable(core, dsp, devtest);

    // A node made from msgpack is the one made from JSON, field for field.
    const std::string json =
        R"({"flag":true,"u8":200,"u32":4000000000,"i32":-77,"i64":-5000000000,"f":1.5,"d":-0.125,"pick":"three",)"
        R"("name":"abcdefgh","counts":[9,8,7,6]})";
    const auto bytes = msgpackOf(json);
    Made fromMsgpack(desktopEcho(), asSettings(bytes));
    Made fromJson(desktopEcho(), asSettings(json));
    REQUIRE(fromMsgpack.node);
    REQUIRE(fromJson.node);
    checkSame(fromMsgpack.echo(), fromJson.echo());
    CHECK(fromMsgpack.echo().pick == 2);
    CHECK(fromMsgpack.echo().countsSize == 4);

    // A desktop type whose settings hold desktop types reads JSON alone, and says so.
    const rp_node_type* audioOut = hostPackage()->node_types[0];
    REQUIRE(std::string_view(audioOut->id) == "rp.host.audio-out");
    const auto buses = msgpackOf(R"({"buses":[2]})");
    const rp_settings s = asSettings(buses);
    rp_memory_req req{};
    rp_error      err{};
    CHECK_FALSE(audioOut->query_memory(audioOut, &s, &req, &err));
    CHECK(contains(err.message, "reads JSON alone"));
}

TEST_CASE("the msgpack decode keeps a missing field's default and steps over an unknown one", "[device-mode]") {
    checkEcho("{}");
    checkEcho(R"({"u32":9})");
    checkEcho(R"({"name":"","counts":[]})");
    checkEcho(R"({"flag":true,"u8":255,"u32":4294967295,"i32":-2147483648,"i64":-9223372036854775807,"f":-3,"d":1e300,)"
              R"("pick":"two","name":"12345678","counts":[65535,0,1,2]})");
    // Unknown keys of every msgpack kind, nested ones among them, cost nothing but the step over them.
    checkEcho(R"({"later":{"nested":[1,{"deep":[null,true,"x",2.5]}]},"u8":7,"another":null,"x":[[[]]],"f":2})");
    // An enum by its enumerator's name, or by a number in the string as std::stoi reads one, as reflect-cpp reads it.
    checkEcho(R"({"mode":"Fast"})");
    checkEcho(R"({"mode":"Off"})");
    checkEcho(R"({"mode":"7"})");
    checkEcho(R"({"mode":" 2 and more"})");
    checkEcho(R"({"mode":"+1"})");

    // Empty settings are the empty map; a blob with fields in any order is read by name.
    Made empty(deviceEcho(), rp_settings{nullptr, 0, RP_SETTINGS_MSGPACK});
    Made defaults(desktopEcho(), asSettings(std::string("{}")));
    REQUIRE(empty.node);
    checkSame(empty.echo(), defaults.echo());
    checkEcho(R"({"counts":[3],"name":"zz","u8":3})");

    // A field the map names twice keeps the last, as JSON.parse does.
    Made twice(deviceEcho(), asSettings(msgpackOf(R"({"u8":1,"u8":2})")));
    REQUIRE(twice.node);
    CHECK(twice.echo().u8 == 2);

    // Kinds the converter never writes still read: an int stored in a signed form, a float32, and a bin to step over.
    const std::vector<std::uint8_t> handWritten = {0x83,                                   // a map of three
                                                   0xa3, 'u', '3', '2', 0xd0, 0x05,        // "u32": int8 5
                                                   0xa1, 'f', 0xca, 0x3f, 0xc0, 0x00, 0x00,  // "f": float32 1.5
                                                   0xa1, 'z', 0xc4, 0x02, 0xaa, 0xbb};     // "z": bin of 2
    Made hand(deviceEcho(), asSettings(handWritten));
    REQUIRE(hand.node);
    CHECK(hand.echo().u32 == 5);
    CHECK(hand.echo().f == 1.5f);

    // Unknown keys whose values are a bin 16, an ext 8 and a fixext 4 are stepped over too.
    const std::vector<std::uint8_t> exotic = {0x84,                                          // a map of four
                                              0xa1, 'a', 0xc5, 0x00, 0x02, 0x01, 0x02,       // bin 16 of 2
                                              0xa1, 'b', 0xc7, 0x03, 0x05, 0x01, 0x02, 0x03,  // ext 8: 3 bytes, type 5
                                              0xa1, 'c', 0xd6, 0x07, 0x01, 0x02, 0x03, 0x04,  // fixext 4, type 7
                                              0xa2, 'u', '8', 0x09};                         // "u8": 9
    Made skipped(deviceEcho(), asSettings(exotic));
    REQUIRE(skipped.node);
    CHECK(skipped.echo().u8 == 9);
}

TEST_CASE("the msgpack decode refuses what JSON refuses, naming the field", "[device-mode]") {
    // Over a bound: the text's bytes and the list's elements, as the JSON reader words them.
    CHECK(refusal(R"({"name":"123456789"})") ==
          "Failed to parse field 'name': a string of 9 bytes is over the 8 the field holds");
    CHECK(refusal(R"({"counts":[1,2,3,4,5]})") ==
          "Failed to parse field 'counts': an array of 5 elements is over the 4 the field holds");
    // The JSON reader refuses the same settings in the same words; a list element's reason is the one place the
    // decode words its own ("element 1: ...", where reflect-cpp's vector reader has its own phrasing).
    for (const char* json : {R"({"name":"123456789"})", R"({"counts":[1,2,3,4,5]})", R"({"u32":-1})",
                             R"({"u32":2.0})", R"({"u8":256})", R"({"flag":1})", R"({"flag":null})",
                             R"({"pick":"four"})", R"({"rom":"res:007"})", R"({"mode":"Medium"})", R"({"mode":3})",
                             R"({"i32":"1"})", R"({"f":"1"})", R"({"name":5})"}) {
        INFO(json);
        Made desktop(desktopEcho(), asSettings(std::string(json)));
        CHECK_FALSE(desktop.node);
        CHECK(refusal(json) == desktop.refusal);
    }
    {
        Made desktop(desktopEcho(), asSettings(std::string(R"({"counts":[1,"x"]})")));
        CHECK_FALSE(desktop.node);
        CHECK(contains(desktop.refusal, "'counts'"));
        CHECK(refusal(R"({"counts":[1,"x"]})") == "Failed to parse field 'counts': element 1: Could not cast to uint.");
    }

    // A value of the wrong kind or out of its field's range.
    CHECK(refusal(R"({"u32":-1})") == "Failed to parse field 'u32': Could not cast to uint.");
    CHECK(refusal(R"({"u32":2.0})") == "Failed to parse field 'u32': Could not cast to uint.");
    CHECK(refusal(R"({"u8":256})") == "Failed to parse field 'u8': Integer is out of range for the target type.");
    CHECK(refusal(R"({"i32":2147483648})") ==
          "Failed to parse field 'i32': Integer is out of range for the target type.");
    CHECK(refusal(R"({"i32":"1"})") == "Failed to parse field 'i32': Could not cast to int.");
    CHECK(refusal(R"({"f":"1"})") == "Failed to parse field 'f': Could not cast to double.");
    CHECK(refusal(R"({"flag":1})") == "Failed to parse field 'flag': Could not cast to boolean.");
    CHECK(refusal(R"({"flag":null})") == "Failed to parse field 'flag': Could not cast to boolean.");
    CHECK(refusal(R"({"name":5})") == "Failed to parse field 'name': Could not cast to string.");
    CHECK(refusal(R"({"pick":"four"})") == "Failed to parse field 'pick': Literal does not support string 'four'. The "
                                             "following strings are supported: 'one', 'two', 'three'.");
    CHECK(refusal(R"({"mode":"Medium"})") ==
          "Failed to parse field 'mode': Invalid enum value: 'Medium'. Must be one of [Off, Slow, Fast].");
    CHECK(refusal(R"({"mode":"99999999999"})") ==
          "Failed to parse field 'mode': Invalid enum value: '99999999999'. Must be one of [Off, Slow, Fast].");
    CHECK(refusal(R"({"counts":[1,-2]})") == "Failed to parse field 'counts': element 1: Could not cast to uint.");
    CHECK(refusal(R"({"counts":{"a":1}})") == "Failed to parse field 'counts': Could not cast to array.");
    CHECK(refusal(R"({"rom":"res:007"})") ==
          "Failed to parse field 'rom': \"res:007\" is not a resource reference (res:N)");
    CHECK(refusal(R"({"rom":7})") == "Failed to parse field 'rom': Could not cast to string.");

    // A blob that is not one well-formed map is refused whole.
    CHECK(refusal(msgpackOf("[1,2]")) == "settings must be a msgpack map");
    CHECK(refusal(msgpackOf("7")) == "settings must be a msgpack map");
    auto truncated = msgpackOf(R"({"name":"abc"})");
    truncated.pop_back();
    CHECK(refusal(truncated) == "settings are not valid msgpack");
    auto trailing = msgpackOf(R"({"u8":1})");
    trailing.push_back(0xc0);
    CHECK(refusal(trailing) == "settings hold bytes after their map");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0x01, 0x02}) == "a settings key must be a string");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'z', 0xc1}) == "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0xdf, 0xff, 0xff, 0xff, 0xff}) == "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xdb, 0xff, 0xff, 0xff, 0xff}) == "settings are not valid msgpack");
    // A value cut short in each wide form: an int16, a float64, a str8.
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa3, 'u', '3', '2', 0xcd, 0x01}) ==
          "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'd', 0xcb, 0x3f, 0xf0, 0x00}) ==
          "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa4, 'n', 'a', 'm', 'e', 0xd9, 0x05, 'a', 'b'}) ==
          "settings are not valid msgpack");
    // And an unknown key's value cut short: a bin 16 and a fixext 8.
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'z', 0xc5, 0x00, 0x04, 0x01}) ==
          "settings are not valid msgpack");
    CHECK(refusal(std::vector<std::uint8_t>{0x81, 0xa1, 'z', 0xd7, 0x01, 0x02}) == "settings are not valid msgpack");

    // A device reads msgpack alone.
    Made json(deviceEcho(), asSettings(std::string("{}")));
    CHECK_FALSE(json.node);
    CHECK(json.refusal == "settings encoding 0 is not supported; a device node reads msgpack");

    // A create whose second resource will not pin releases the first, which it had pinned, and names the field that
    // failed, on both builds.
    const rp_node_type* const devicePair = rp_devtest_package_device_mode()->node_types[1];
    const rp_node_type* const desktopPair = echoPackageDesktop()->node_types[1];
    for (const rp_node_type* pair : {devicePair, desktopPair}) {
        INFO((pair == desktopPair ? "desktop" : "device"));
        REQUIRE(std::string_view(pair->id) == "rp.devtest.pair");
        OneRomHost        host;
        const std::string json = R"({"rom":"res:1","sram":"res:2"})";
        const auto        bytes = msgpackOf(json);
        Made              made(pair, pair == desktopPair ? asSettings(json) : asSettings(bytes), &host.host);
        CHECK_FALSE(made.node);
        CHECK(made.refusal == "sram: \"res:2\" is not a live resource");
        CHECK(host.acquired == 1);
        CHECK(host.released == 1);
    }
    {
        // And one that pins both holds them until it is destroyed.
        OneRomHost host;
        {
            Made made(devicePair, asSettings(msgpackOf(R"({"rom":"res:1"})")), &host.host);
            REQUIRE(made.node);
            CHECK(host.acquired == 1);
            CHECK(host.released == 0);
        }
        CHECK(host.released == 1);
    }

    // A resource the settings name is pinned at create, so a host with no resource table refuses it there, naming the
    // field and the ref, in the desktop's words.
    Made device(deviceEcho(), asSettings(msgpackOf(R"({"rom":"res:5"})")));
    Made desktop(desktopEcho(), asSettings(std::string(R"({"rom":"res:5"})")));
    CHECK_FALSE(device.node);
    CHECK(device.refusal == "rom: the host offers no resource table");
    CHECK(device.refusal == desktop.refusal);
}

TEST_CASE("the JSON to msgpack converter writes each number as the kind the JSON spells", "[device-mode]") {
    // An integer literal is an int, in its smallest form; anything with a fraction or an exponent is a float64.
    CHECK(msgpackOf("0") == std::vector<std::uint8_t>{0x00});
    CHECK(msgpackOf("127") == std::vector<std::uint8_t>{0x7f});
    CHECK(msgpackOf("128") == std::vector<std::uint8_t>{0xcc, 0x80});
    CHECK(msgpackOf("-32") == std::vector<std::uint8_t>{0xe0});
    CHECK(msgpackOf("-33") == std::vector<std::uint8_t>{0xd0, 0xdf});
    CHECK(msgpackOf("65536") == std::vector<std::uint8_t>{0xce, 0x00, 0x01, 0x00, 0x00});
    CHECK(msgpackOf("18446744073709551615") ==
          std::vector<std::uint8_t>{0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    CHECK(msgpackOf("2.0") == std::vector<std::uint8_t>{0xcb, 0x40, 0x00, 0, 0, 0, 0, 0, 0});
    CHECK(msgpackOf("1e2").front() == 0xcb);
    CHECK(msgpackOf("null") == std::vector<std::uint8_t>{0xc0});
    CHECK(msgpackOf("[true,false]") == std::vector<std::uint8_t>{0x92, 0xc3, 0xc2});
    CHECK(msgpackOf(R"({"a":"bc"})") == std::vector<std::uint8_t>{0x81, 0xa1, 'a', 0xa2, 'b', 'c'});
    // A long string and a long array take the wider headers.
    const auto longString = msgpackOf("\"" + std::string(40, 'x') + "\"");
    CHECK(longString[0] == 0xd9);
    CHECK(longString[1] == 40);
    std::string array = "[";
    for (int i = 0; i < 20; ++i) array += (i ? ",1" : "1");
    CHECK(msgpackOf(array + "]")[0] == 0xdc);
    // Not JSON, refused.
    CHECK_FALSE(settingsToMsgpack("{"));
}
````

Run: `mkdir -p packages/native/test/device-mode; sed -n '3919,4907p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/DeviceMode.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/DeviceMode.test.cpp`
Expected: `6dd9d990f95446932d7a6984e831dac04f9b91cd`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/device-mode/DeviceModeEntries.hpp`:

````cpp
#pragma once

// What retroplug-device-mode-test's two halves share: the device-mode objects (built with RP_DEVICE and no exceptions
// or RTTI, cmake/tests.cmake) export their packages through C entries renamed for this binary, and the echo node
// reports the settings it decoded through a plain struct. Nothing here names an SDK type, so both builds read it alike.

#include <cstdint>

#include "graph/rp_node.h"

extern "C" {
// rp.core and rp.dsp as a device builds them: CoreNodes.cpp and DspNodes.cpp compiled in device mode, their entries
// (rp_core_package, rp_dsp_package) renamed by the objects' compile definitions so they sit beside the desktop's.
const rp_package* rp_core_package_device_mode(void);
const rp_package* rp_dsp_package_device_mode(void);
// rp.devtest: the echo, pair and shaper nodes (EchoNode.hpp) in device mode.
const rp_package* rp_devtest_package_device_mode(void);
}

// The settings an echo node decoded, as its on_settings copied them: get_interface(kEchoInterface) answers one.
inline constexpr const char* kEchoInterface = "rp.devtest.echo/1";

struct EchoView {
    bool          flag = false;
    std::uint8_t  u8 = 0;
    std::uint32_t u32 = 0;
    std::int32_t  i32 = 0;
    std::int64_t  i64 = 0;
    float         f = 0.0f;
    double        d = 0.0;
    std::uint32_t pick = 0;
    char          name[9] = {};
    std::uint32_t nameSize = 0;
    std::uint16_t counts[4] = {};
    std::uint32_t countsSize = 0;
    std::uint32_t rom = 0;
    bool          romResolved = false;
    int           mode = 0;  // the enum field, as its value
};
````

Run: `mkdir -p packages/native/test/device-mode; sed -n '4919,4957p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/DeviceModeEntries.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/DeviceModeEntries.hpp`
Expected: `d48651af6d755fb7e7a1344e9bcaacb6dd42d1a5`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/device-mode/DeviceModeNodes.cpp`:

````cpp
// The device-mode half of retroplug-device-mode-test: built with RP_DEVICE and with neither exceptions nor RTTI
// (cmake/tests.cmake), beside the device-mode builds of CoreNodes.cpp and DspNodes.cpp. It holds the echo node's
// package (echo, pair, shaper); the test reaches it, and the two node packages, only through their C entries.

#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"

#ifndef RP_DEVICE
#error "DeviceModeNodes.cpp is the device-mode half: it is built with RP_DEVICE"
#endif

namespace rp::graph::devtest {
namespace {

const rp_node_type* const kTypes[] = {rp::node_type<Echo>(), rp::node_type<Pair>(), rp::node_type<Shaper>()};
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest","version":"1.0.0"})", 3, kTypes};

}  // namespace
}  // namespace rp::graph::devtest

extern "C" const rp_package* rp_devtest_package_device_mode(void) { return &rp::graph::devtest::kPackage; }
````

Run: `mkdir -p packages/native/test/device-mode; sed -n '4969,4989p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/DeviceModeNodes.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/DeviceModeNodes.cpp`
Expected: `4151f524e3e27d46c51cf4f57cbaf439066097ba`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/device-mode/EchoNode.hpp`:

````cpp
#pragma once

// The test's own node types, one source built twice into retroplug-device-mode-test: in device mode by
// DeviceModeNodes.cpp, and on the desktop by DeviceMode.test.cpp, so the device's build can be held to the desktop's.
//
// - echo: a settings struct with a field of every kind a device-mode Settings may hold, copied into an EchoView
//   (DeviceModeEntries.hpp) for the test to read, so a decode is held to the desktop's JSON reading field for field.
// - pair: two resource fields, so a create whose second pin fails can be seen releasing the first.
// - shaper: an enum settings field and an enum_param, both shaping its audio, so the device's enum reading and its
//   enum_param render what the desktop's do.

#include <cstdint>
#include <cstring>
#include <string_view>

#include "DeviceModeEntries.hpp"
#include "graph/node.hpp"

// rp::graph::devtest, so the device-mode build of it is rp::device::graph::devtest (graph/sdk_namespace.hpp) and the
// two never share a name; a named namespace, as a desktop Settings struct's schema needs.
namespace rp::graph::devtest {

enum class Mode : std::uint8_t { Off, Slow, Fast = 7 };

struct EchoSettings {
    bool                         flag = false;
    std::uint8_t                 u8 = 1;
    std::uint32_t                u32 = 2;
    std::int32_t                 i32 = -3;
    std::int64_t                 i64 = 4;
    float                        f = 0.5f;
    double                       d = 0.25;
    rp::choice<"one", "two", "three"> pick;
    rp::text<8>                  name = "echo";
    rp::list<std::uint16_t, 4>   counts = {1, 2};
    rp::resource_ref<"rom">      rom;
    Mode                         mode = Mode::Slow;
};

struct Echo {
    static constexpr const char* id = "rp.devtest.echo";
    using Settings = EchoSettings;

    struct { rp::audio_out<rp::mono> out; } outputs;

    EchoView view{};

    void on_settings(const Settings& s) noexcept {
        view.flag = s.flag;
        view.u8 = s.u8;
        view.u32 = s.u32;
        view.i32 = s.i32;
        view.i64 = s.i64;
        view.f = s.f;
        view.d = s.d;
        view.pick = s.pick.value();
        view.nameSize = static_cast<std::uint32_t>(s.name.size());
        std::memcpy(view.name, s.name.data(), s.name.size());
        view.countsSize = static_cast<std::uint32_t>(s.counts.size());
        for (std::size_t i = 0; i < s.counts.size(); ++i) view.counts[i] = s.counts[i];
        view.rom = s.rom.id;
        view.romResolved = s.rom.resource != nullptr;
        view.mode = static_cast<int>(s.mode);
    }

    const void* get_interface(std::string_view want) noexcept { return want == kEchoInterface ? &view : nullptr; }

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};

struct PairSettings {
    rp::resource_ref<"rom">  rom;
    rp::resource_ref<"sram"> sram;
};

struct Pair {
    static constexpr const char* id = "rp.devtest.pair";
    using Settings = PairSettings;
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};

// The curve is a settings enum (read by its enumerator's name), the shape an enum_param (driven by index).
enum class Curve : std::int8_t { Soft = -1, Hard = 1, Fold = 2 };
enum class Shape { Clip, Square, Invert };

struct ShaperSettings {
    Curve         curve = Curve::Soft;
    std::uint32_t channels = 2;
};

struct Shaper {
    static constexpr const char* id = "rp.devtest.shaper";
    using Settings = ShaperSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct { rp::enum_param<Shape, "Shape", Shape::Square> shape; } params;

#ifndef RP_DEVICE
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }
#endif

    Curve curve = Curve::Soft;
    void on_settings(const Settings& s) noexcept { curve = s.curve; }

    float bend(float x) const noexcept {
        switch (curve) {
            case Curve::Soft: return x / (1.0f + (x < 0 ? -x : x));
            case Curve::Hard: return x > 0.5f ? 0.5f : x < -0.5f ? -0.5f : x;
            case Curve::Fold: return x > 0.5f ? 1.0f - x : x < -0.5f ? -1.0f - x : x;
        }
        return x;
    }

    void process(const rp::Block& b) noexcept {
        const Shape shape = params.shape.value();
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) {
                const float y = bend(inputs.in[c][i]);
                outputs.out[c][i] = shape == Shape::Clip ? y : shape == Shape::Square ? y * y : -y;
            }
    }
};

}  // namespace rp::graph::devtest
````

Run: `mkdir -p packages/native/test/device-mode; sed -n '5001,5132p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/EchoNode.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/EchoNode.hpp`
Expected: `6db3d3b26872391f278d02fd6357d460a0812e01`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/device-mode/refused/HeapType.cpp`:

````cpp
// Must not compile in device mode (cmake/tests.cmake checks it at configure): a type that declares heap.
#include "graph/node.hpp"

namespace {
struct Heavy {
    static constexpr const char* id = "x.heavy";
    static constexpr bool        heap = true;
    void process(const rp::Block&) noexcept {}
};
}  // namespace

const rp_node_type* heavy() { return rp::node_type<Heavy>(); }
int main() { return heavy() != nullptr ? 0 : 1; }
````

Run: `mkdir -p packages/native/test/device-mode/refused; sed -n '5144,5156p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/refused/HeapType.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/refused/HeapType.cpp`
Expected: `ffce3ef44cff56ccc48dd05b66ed03d4f00451c2`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/device-mode/refused/PointerSettings.cpp`:

````cpp
// Must not compile in device mode (cmake/tests.cmake checks it at configure): settings a device cannot hold.
#include "graph/node.hpp"

namespace rp::graph::refused {
struct Settings {
    float  gain = 1.0f;
    float* table = nullptr;
};
}  // namespace rp::graph::refused

namespace {
struct Pointing {
    static constexpr const char* id = "x.pointing";
    using Settings = rp::graph::refused::Settings;
    void process(const rp::Block&) noexcept {}
};
}  // namespace

const rp_node_type* pointing() { return rp::node_type<Pointing>(); }
int main() { return pointing() != nullptr ? 0 : 1; }
````

Run: `mkdir -p packages/native/test/device-mode/refused; sed -n '5168,5187p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/refused/PointerSettings.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/refused/PointerSettings.cpp`
Expected: `fb0b1bf8e40a021d7e0ae303c7595825225e2ac8`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/device-mode/refused/HeapHeader.cpp`:

````cpp
// Must not compile in device mode (cmake/tests.cmake checks it at configure): a heap header before the SDK.
#include <string>

#include "graph/node.hpp"

int main() { return 0; }
````

Run: `mkdir -p packages/native/test/device-mode/refused; sed -n '5199,5204p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/refused/HeapHeader.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/refused/HeapHeader.cpp`
Expected: `735dd875e443a81d2959ac2e2321962b224b9ea6`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/native/test/graph/Fields.test.cpp`:

````cpp
// The node SDK's field walk (graph/fields.hpp) held to reflect-cpp's own. The walk reads a field's name the way
// reflect-cpp does, over reflect-cpp's compile-time internals, and every JSON key a node's settings are read by is
// reflect-cpp's, so the two must name every field alike: a port or param id, a settings key a msgpack blob is matched
// by, a resource field a refusal names. Held here over each shape of struct the authoring layer walks (a named
// namespace's, an anonymous namespace's, a class template's, the unnamed member struct a node's `inputs` is) and over a
// field of every kind a settings struct or a port struct holds, so a reflect-cpp upgrade that spells a name otherwise
// fails here by name rather than as a moved descriptor golden. The enum scan a device reads an enum's names with
// (graph/enums.hpp) is held to reflect-cpp's enumerator list the same way, over every underlying type it takes.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rfl/enums.hpp>
#include <rfl/fields.hpp>

#include "graph/node.hpp"

namespace fields_test {

enum class Mode : std::uint8_t { Off, On };

// One enum per underlying type the scan takes, with enumerators at the ends of its range where the type reaches them.
enum class U8 : std::uint8_t { Zero, Gap = 7, Top = 255 };
enum class S8 : std::int8_t { Bottom = -128, Minus = -1, Top = 127 };
enum class S16 : std::int16_t { Least = -256, Most = 256, Past = 300 };  // Past is outside the scanned range
enum class U16 : std::uint16_t { A = 1, B = 256 };
enum class S32 { Neg = -5, Zero, Big = 200 };
enum class S64 : std::int64_t { A = -3, B = 3 };
enum class U64 : std::uint64_t { First, Second = 2, Last = 256 };
enum class ULong : unsigned long { One = 1, Two };  // 64 bits on LP64 Linux and macOS, 32 on Windows
enum Plain : int { PlainA = 4, PlainB = 9 };       // unscoped, with a fixed type
enum class Bool : bool { No, Yes };
enum class Char : char { A = 'a', Z = 'z' };

struct Kinds {
    bool                              flag = false;
    std::uint8_t                      small = 0;
    std::int64_t                      wide = 0;
    float                             level = 0.0f;
    double                            precise = 0.0;
    Mode                              mode = Mode::Off;
    rp::choice<"one", "two">          pick;
    rp::text<8>                       name;
    rp::list<std::uint32_t, 4>        counts;
    rp::resource_ref<"rom">           rom;
    int                               x_1 = 0;  // an underscore and a digit
    int                               lastFieldWithALongerName = 0;
};

template <class T>
struct Boxed {
    T             value{};
    std::uint32_t count = 0;
};

struct Node {
    struct {
        rp::audio_in<rp::any> in;
        rp::events_in         events;
        rp::control_in        control;
    } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct {
        rp::float_param<"Gain"> gain;
        rp::enum_param<Mode, "Mode", Mode::On> mode;
    } params;
};

}  // namespace fields_test

namespace {

struct Local {
    int   a = 0;
    float b = 0.0f;
};

template <class S>
std::vector<std::string> ours() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return std::vector<std::string>{std::string(rp::detail::fieldName<S, I>())...};
    }(std::make_index_sequence<rp::detail::kFieldCount<S>>{});
}

template <class S>
std::vector<std::string> reflectCpps() {
    std::vector<std::string> names;
    for (const auto& f : rfl::fields<S>()) names.push_back(f.name());
    return names;
}

template <class S>
void same(const char* what) {
    INFO(what);
    CHECK(ours<S>() == reflectCpps<S>());
    CHECK(rp::detail::kFieldCount<S> == rfl::fields<S>().size());
}

}  // namespace

template <class E>
void sameEnum(const char* what) {
    INFO(what);
    constexpr auto kReflected = rfl::get_enumerator_array<E>();
    using Ours = rp::detail::Enumerators<E>;
    REQUIRE(Ours::count == kReflected.size());
    for (std::size_t i = 0; i < kReflected.size(); ++i) {
        INFO(i);
        CHECK(Ours::name(i) == kReflected[i].first);
        CHECK(Ours::value(i) == kReflected[i].second);
    }
    CHECK(Ours::count > 0);
    static_assert(rp::detail::enumMatchesReflect<E>());
}

TEST_CASE("the enum scan lists every enum's enumerators as reflect-cpp does", "[fields][enums]") {
    using namespace fields_test;
    sameEnum<U8>("uint8_t, with a gap and the type's top");
    sameEnum<S8>("int8_t, both of the type's ends");
    sameEnum<S16>("int16_t, the scanned range's ends and one past them");
    sameEnum<U16>("uint16_t");
    sameEnum<S32>("int, negative and past the int8 range");
    sameEnum<S64>("int64_t");
    sameEnum<U64>("uint64_t, whose greatest value is no long long");
    sameEnum<ULong>("unsigned long");
    sameEnum<Plain>("an unscoped enum with a fixed type");
    sameEnum<Bool>("bool");
    sameEnum<Char>("char");
    CHECK(rp::detail::Enumerators<U64>::count == 3);
    CHECK(rp::detail::Enumerators<ULong>::count == 2);
}

TEST_CASE("the field walk names every field as reflect-cpp does", "[fields]") {
    same<fields_test::Kinds>("every settings kind, in a named namespace");
    same<Local>("an anonymous namespace's struct");
    same<fields_test::Boxed<rp::text<4>>>("a class template");
    same<fields_test::Boxed<fields_test::Mode>>("a class template over an enum");
    same<decltype(fields_test::Node::inputs)>("a node's unnamed inputs struct");
    same<decltype(fields_test::Node::outputs)>("a node's unnamed outputs struct");
    same<decltype(fields_test::Node::params)>("a node's unnamed params struct");

    CHECK(ours<fields_test::Kinds>().front() == "flag");
    CHECK(ours<decltype(fields_test::Node::params)>() == std::vector<std::string>{"gain", "mode"});
}
````

Run: `mkdir -p packages/native/test/graph; sed -n '5216,5362p' docs/modular-graph-phase-e1-plan.md > packages/native/test/graph/Fields.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/graph/Fields.test.cpp`
Expected: `3c8aab8155b49ec70706717c132b77ed293b2c29`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/package.json b/package.json
index 5242e75a3..c49fa1673 100644
--- a/package.json
+++ b/package.json
@@ -19,7 +19,7 @@
     "lsdj:gen-offsets": "node scripts/cmake-build.js retroplug-host && node packages/retroplug/scripts/gen-lsdj-offsets.mjs",
     "smsggdj:gen-symbols": "node packages/retroplug/scripts/gen-smsggdj-symbols.mjs",
     "test:ui": "node scripts/cmake-build.js retroplug-ui-test && node packages/retroplug/scripts/run-ui-tests.mjs",
-    "test:plugin": "node scripts/cmake-build.js retroplug-plugin-test retroplug-dynparams-test retroplug-classid-test retroplug-audio-test retroplug-watcher-test retroplug-lottie-test retroplug-midi-test retroplug-launchpad-test retroplug-graph-test retroplug-engine-graph-test && node packages/retroplug/scripts/run-plugin-tests.mjs",
+    "test:plugin": "node scripts/cmake-build.js retroplug-plugin-test retroplug-dynparams-test retroplug-classid-test retroplug-audio-test retroplug-watcher-test retroplug-lottie-test retroplug-midi-test retroplug-launchpad-test retroplug-graph-test retroplug-engine-graph-test retroplug-device-mode-test && node packages/retroplug/scripts/run-plugin-tests.mjs",
     "test:n8": "node scripts/cmake-build.js retroplug-n8-test retroplug-serial-test && build/bin/retroplug-n8-test && build/bin/retroplug-serial-test",
     "test:node": "node packages/native/node/test/run.mjs",
     "test:cli-ts": "node scripts/cmake-build.js retroplug-cli && tools/run-cli-ts-tests.sh",
diff --git a/packages/native/cmake/test-binary.cmake b/packages/native/cmake/test-binary.cmake
index 3895fa32b..449c7782f 100644
--- a/packages/native/cmake/test-binary.cmake
+++ b/packages/native/cmake/test-binary.cmake
@@ -1,6 +1,6 @@
 # retroplug_add_test_binary() — the shape every native test binary shares.
 #
-# There are twelve of them (ten run by `pnpm test:plugin`, plus `retroplug-n8-test` and the
+# There are thirteen of them (eleven run by `pnpm test:plugin`, plus `retroplug-n8-test` and the
 # hardware-only `retroplug-n8-hwtest`), and each was spelled out in full: the same
 # EXCLUDE_FROM_ALL, the same C++20 pair, the same output directory, the same Catch2 link.
 # Nine tenths of each block was boilerplate, which buried the one or two lines that were
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index fd2ac26a8..ab8ca7dfe 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -277,6 +277,7 @@ retroplug_add_test_binary(
              test/graph/Reshape.test.cpp
              test/graph/Resources.test.cpp            # a node that reads a resource its settings name
              test/graph/SettingsTypes.test.cpp         # rp::text, rp::list and the numeric resource_ref
+             test/graph/Fields.test.cpp                # the field walk's names held to reflect-cpp's own
              test/graph/QueueCapacity.test.cpp
              test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
              test/graph/Dsp.test.cpp                  # the rp::dsp kernels: streaming, no allocation, the kit's math
@@ -300,3 +301,99 @@ retroplug_add_test_binary(
     INCLUDES ${CMAKE_SOURCE_DIR}/deps/r8brain
              # The engine tree's headers it reads beside the packages: lsdj/Effects.hpp, system/InputCapacity.hpp.
              ${CMAKE_CURRENT_SOURCE_DIR}/src)
+
+# Guards the device build of the node SDK (docs/modular-graph-phase-e1-design.md sections 6 and 8): rp.core and rp.dsp
+# compiled as a device compiles them (RP_DEVICE, no exceptions, no RTTI) render, through real plans and over ragged
+# blocks, what the desktop's builds of the same sources render, bit for bit, created from msgpack where the desktop's
+# are created from JSON; their create, activate, process and destroy allocate nothing; and the msgpack settings decode
+# keeps JSON's leniency and refusals. The desktop's nodes read msgpack too.
+#
+# Both builds of one source sit in this one binary. The device-mode objects are an object library of their own, with
+# the device flags, and their C++ names are in rp::device (graph/sdk_namespace.hpp), so no name has two definitions as
+# long as a source keeps its own names under rp::graph, rp::detail or an anonymous namespace (that header's
+# convention); their packages' C entries, which no namespace reaches, are renamed by a compile definition. Catch2 is the shared
+# library every test binary links, built with exceptions, so the test's own translation unit keeps them and reaches the
+# device-mode code only through the C ABI, as a device's host would. MSVC keeps its /EHsc there: a second /EH option on
+# one command line is the D9025 warning AGENTS.md describes, and the GCC and Clang builds are where -fno-exceptions is
+# proved.
+add_library(retroplug-device-mode-objects OBJECT EXCLUDE_FROM_ALL
+    ${CMAKE_SOURCE_DIR}/packages/core/native/src/graph/nodes/CoreNodes.cpp
+    ${CMAKE_SOURCE_DIR}/packages/dsp/native/src/graph/nodes/DspNodes.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/DeviceModeNodes.cpp)
+set_target_properties(retroplug-device-mode-objects PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
+target_compile_definitions(retroplug-device-mode-objects PRIVATE
+    RP_DEVICE
+    rp_core_package=rp_core_package_device_mode
+    rp_dsp_package=rp_dsp_package_device_mode)
+if(NOT MSVC)
+    target_compile_options(retroplug-device-mode-objects PRIVATE -fno-exceptions -fno-rtti)
+endif()
+# The SDK's headers and reflect-cpp's (the field reflection, header-only and SYSTEM: nothing of reflect-cpp is linked),
+# and the packages' own source roots for their headers and the kernels'.
+target_link_libraries(retroplug-device-mode-objects PRIVATE retroplug-node-sdk-headers retroplug-reflect-headers)
+target_include_directories(retroplug-device-mode-objects PRIVATE
+    ${CMAKE_SOURCE_DIR}/packages/core/native/src
+    ${CMAKE_SOURCE_DIR}/packages/dsp/native/src
+    ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode)
+
+# What a device-mode build refuses, held at every configure (GCC and Clang): each source under
+# test/device-mode/refused must fail to compile as a device compiles it, and fail with its own reason on one of the
+# compiler's error lines, so a check that stopped firing (a static_assert gone, the heap-header #error gone) fails the
+# configure rather than going unnoticed. HeapHeader is checked only where the standard library is libstdc++: the
+# heap-header #error in graph/node.hpp reads libstdc++'s include guards (the device toolchain's library), so under
+# libc++ (macOS's AppleClang, a Linux Clang with -stdlib=libc++) that source compiles, the other two are still held,
+# and the device check there is the link trap's and graph/node.hpp's static_asserts.
+if(NOT MSVC)
+    include(CheckCXXSourceCompiles)
+    check_cxx_source_compiles("#include <cstddef>
+#ifndef __GLIBCXX__
+#error not libstdc++
+#endif
+int main() { return 0; }" RP_STDLIB_IS_LIBSTDCXX)
+    set(_rp_refused_sources
+        "HeapType|is never built for the device"
+        "PointerSettings|a device node's Settings holds only")
+    if(RP_STDLIB_IS_LIBSTDCXX)
+        list(APPEND _rp_refused_sources "HeapHeader|a heap or exception header")
+    endif()
+    get_target_property(_rp_reflect_dir reflectcpp SOURCE_DIR)
+    foreach(_rp_refused IN LISTS _rp_refused_sources)
+        string(REPLACE "|" ";" _rp_refused "${_rp_refused}")
+        list(GET _rp_refused 0 _rp_source)
+        list(GET _rp_refused 1 _rp_reason)
+        unset(RP_DEVICE_REFUSES_${_rp_source} CACHE)
+        try_compile(RP_DEVICE_REFUSES_${_rp_source}
+            "${CMAKE_BINARY_DIR}/device-mode-refused/${_rp_source}"
+            "${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/refused/${_rp_source}.cpp"
+            CMAKE_FLAGS
+                "-DINCLUDE_DIRECTORIES=${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src;${_rp_reflect_dir}/include"
+            COMPILE_DEFINITIONS -DRP_DEVICE -fno-exceptions -fno-rtti
+            CXX_STANDARD 20
+            CXX_STANDARD_REQUIRED ON
+            OUTPUT_VARIABLE _rp_output)
+        if(RP_DEVICE_REFUSES_${_rp_source})
+            message(FATAL_ERROR "test/device-mode/refused/${_rp_source}.cpp compiled in device mode; it must not")
+        endif()
+        # The reason on a diagnostic line ("error: static assertion failed: ...", "error: #error ..."), not anywhere in
+        # the output, which also echoes the source lines a diagnostic quotes.
+        string(REGEX MATCHALL "[^\n]*error:[^\n]*" _rp_errors "${_rp_output}")
+        string(FIND "${_rp_errors}" "${_rp_reason}" _rp_at)
+        if(_rp_at EQUAL -1)
+            message(FATAL_ERROR "test/device-mode/refused/${_rp_source}.cpp failed in device mode, but not with "
+                                "\"${_rp_reason}\":\n${_rp_output}")
+        endif()
+        unset(RP_DEVICE_REFUSES_${_rp_source} CACHE)
+    endforeach()
+endif()
+
+retroplug_add_test_binary(
+    NAME     retroplug-device-mode-test
+    SOURCES  test/device-mode/DeviceMode.test.cpp
+             test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
+             $<TARGET_OBJECTS:retroplug-device-mode-objects>
+    LIBS     retroplug-graph
+             retroplug-core-nodes                      # the desktop's rp.core, the device's twin
+             retroplug-dsp-nodes                       # and rp.dsp
+             retroplug-dsp-kernels                     # the kernels, which both builds of rp.dsp call
+    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
+             ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode)
diff --git a/packages/native/test/graph/Dsp.test.cpp b/packages/native/test/graph/Dsp.test.cpp
index aa4f78289..4d88a4a80 100644
--- a/packages/native/test/graph/Dsp.test.cpp
+++ b/packages/native/test/graph/Dsp.test.cpp
@@ -41,6 +41,7 @@
 #include "CountingAllocator.hpp"
 #include "dsp/Biquad.hpp"
 #include "dsp/Dither.hpp"
+#include "dsp/Mt19937.hpp"
 #include "dsp/Dpcm.hpp"
 #include "dsp/Gain.hpp"
 #include "dsp/R8bAllocator.hpp"  // r8brain, for the reference resample below
@@ -880,6 +881,28 @@ TEST_CASE("the dithers without a padded reference give the old whole-buffer loop
     }
 }
 
+TEST_CASE("the dither's Mt19937 is std::mt19937, seed for seed", "[dsp][dither]") {
+    // The kernel seeds through seed() alone (Dither::init); the constructor's seeding is the same recurrence. A spread
+    // of seeds, the edges among them, each held to the standard engine for a few twists' worth of draws.
+    std::vector<std::uint32_t> seeds = {0u, 1u, 5489u, 0x9e3779b9u, 0x80000000u, 0xffffffffu};
+    std::uint32_t x = 0x12345678u;
+    for (int i = 0; i < 64; ++i) seeds.push_back(x = x * 1664525u + 1013904223u);
+    for (const std::uint32_t seed : seeds) {
+        INFO("seed " << seed);
+        std::mt19937     reference(seed);
+        rp::dsp::Mt19937 constructed(seed);
+        rp::dsp::Mt19937 reseeded;
+        reseeded();  // a draw first, so seed() is seen replacing a state that has moved
+        reseeded.seed(seed);
+        bool same = true;
+        for (int k = 0; k < 2000 && same; ++k) {
+            const std::uint32_t want = reference();
+            same = constructed() == want && reseeded() == want;
+        }
+        CHECK(same);
+    }
+}
+
 TEST_CASE("the DPCM kernel's bits, packed, are the pre-kernel encoder's bytes", "[dsp][dpcm]") {
     // A short buffer; one longer than any kit slot resamples to; an overdriven source, where the DC blocker's clamp and
     // the delta counter's ends both do work; and silence, which pins the peak floor of 1 and the sign of every step.
diff --git a/packages/retroplug/scripts/run-plugin-tests.mjs b/packages/retroplug/scripts/run-plugin-tests.mjs
index 4e4f67ef5..b4696a756 100644
--- a/packages/retroplug/scripts/run-plugin-tests.mjs
+++ b/packages/retroplug/scripts/run-plugin-tests.mjs
@@ -8,9 +8,10 @@
 // Lottie rasterization behind the <Lottie> component (retroplug-lottie-test),
 // the MIDI device-selection policy (retroplug-midi-test), the control-surface
 // device link (retroplug-launchpad-test), the node-graph engine's foundations
-// (retroplug-graph-test), and the engine on the graph: its system nodes, the
+// (retroplug-graph-test), the engine on the graph: its system nodes, the
 // documents TS sends it, and buildGraph's goldens planned on it
-// (retroplug-engine-graph-test).
+// (retroplug-engine-graph-test), and the node SDK's device build, run against
+// the desktop's (retroplug-device-mode-test).
 //
 // A tiny runner (rather than chaining `build/bin/foo && …` in package.json) so
 // the suite is cross-platform: it appends `.exe` on Windows and spawns each
@@ -28,7 +29,7 @@ const REPO = resolve(PKG, "../..");
 const BIN_DIR = process.env.RETROPLUG_BIN_DIR || join(REPO, "build", "bin");
 const EXE = process.platform === "win32" ? ".exe" : "";
 
-const BINARIES = ["retroplug-plugin-test", "retroplug-dynparams-test", "retroplug-classid-test", "retroplug-audio-test", "retroplug-watcher-test", "retroplug-lottie-test", "retroplug-midi-test", "retroplug-launchpad-test", "retroplug-graph-test", "retroplug-engine-graph-test"];
+const BINARIES = ["retroplug-plugin-test", "retroplug-dynparams-test", "retroplug-classid-test", "retroplug-audio-test", "retroplug-watcher-test", "retroplug-lottie-test", "retroplug-midi-test", "retroplug-launchpad-test", "retroplug-graph-test", "retroplug-engine-graph-test", "retroplug-device-mode-test"];
 
 const jobs = resolveJobs();
 const filter = stripRunnerFlags()[0];
````

Run: `sed -n '5372,5575p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: the configure stops at the first refused source: `CMake Error at packages/native/cmake/tests.cmake:382 (message):`, then the message, which CMake word-wraps: its first line is `test/device-mode/refused/HeapType.cpp failed in device mode, but not with` and the next `"is never built for the device":` (grep for `HeapType.cpp failed in device mode`), then the compiler's output. Nothing refuses a heap type in device mode yet, so `HeapType.cpp` fails for another reason (this tree's `node.hpp` wraps its control entries in `try`, which `-fno-exceptions` refuses). (The wording is the stage's own run of this check, with another reason; the failure here is predicted.) Build nothing until the next step's configure passes.

- [ ] **Step 2: The SDK's two builds**

`node.hpp` stays the one header an author includes and reads: the hook guide (with a paragraph on the two builds and
the guard), `node_type<T>()` and `peer_as<T>()`, and under `RP_DEVICE` with libstdc++ an `#error` when a heap or
exception header (`<string>`, `<vector>`, `<map>`, `<optional>`, `<stdexcept>`, `<exception>`, `<iostream>`,
`<functional>`, `<memory>`, `<random>`, `rfl/json` or yyjson) came in first. Beside it: `sdk_namespace.hpp` (the
inline `rp::device` namespace in device mode), `PortTypes.hpp` (the port and param enums, the queue bounds,
`kMaxChannels` and `kMaxLayoutName`, out of `Descriptor.hpp`), `ResourceId.hpp` (a constexpr `"res:N"` parser that
cannot throw, which `GraphDocument`'s `parseResourceRef` wraps), `fields.hpp` (the field walk), `enums.hpp` (the enum
scan), `settings_types.hpp` (now portable, with `fixed_string`, `rp::choice` and `resource_ref`), `msgpack_reader.hpp`
and `settings_msgpack.hpp` (the decode), `node_core.hpp`, `node_device.hpp` (`DeviceTypeCheck`, the in-place instance,
a forward-declared `Layout`), `node_desc.hpp` (the desktop half, `ParamDescOf`, the msgpack branch of `parseSettings`)
and `settings_json.hpp` (the reflect-cpp parsers). `rp_node.h` gains `RP_SETTINGS_MSGPACK = 1u`; `node-sdk` builds
`JsonToMsgpack.cpp` (`settingsToMsgpack`) and defines `retroplug-reflect-headers`, reflect-cpp's include root as
SYSTEM headers for the device-mode compiles.

Create `packages/node-sdk/native/src/graph/sdk_namespace.hpp`:

````cpp
#pragma once

// Where the node SDK's C++ names live in each build of it (docs/modular-graph-phase-e1-design.md section 6).
//
// A desktop build (no RP_DEVICE) keeps them where they always were: rp, rp::graph, rp::detail. A device-mode build
// (RP_DEVICE) puts them in the inline namespace rp::device, so a source still names rp::text and
// rp::graph::pushMessage, but a program holding both builds of one node source (retroplug-device-mode-test holds the
// desktop's and the device's rp.core and rp.dsp) has two sets of names rather than one name with two definitions:
// rp::text has no str() in the device build, the device's instance owns its settings in place, and the linker would
// otherwise keep whichever definition it saw first.
//
// rp::graph and rp::detail are declared inside rp::device here, before any header opens them, which is what carries a
// node source's own namespaces along with no change to them: `namespace rp::graph {` extends whichever rp::graph
// lookup finds first, and in a device-mode build that is rp::device::graph. So every C++ header of the SDK includes
// this one first, and so does rp_node.h (under __cplusplus), which every node source's own header includes before it
// opens rp::graph. The SDK's headers that put names straight into rp (the authoring layer, the settings types) open it
// with RP_NAMESPACE_BEGIN.
//
// The convention that follows, for a source built both ways: its names with linkage live under rp::graph or
// rp::detail (rp::graph::core, rp::graph::dsp, ...), or in an anonymous namespace, or are static. A name it puts
// anywhere else (straight into rp, or into rp::dsp or rp::lsdj) is the same name in both builds, and a program holding
// both would keep one definition of it, silently (code that includes no SDK header, the rp::dsp kernels, is the same
// in both builds, and such a program links one copy). A build of one mode alone, a device image, is unaffected.
// The C entry points a package exports have no namespace at all; retroplug-device-mode-test renames them for its
// device-mode objects (cmake/tests.cmake).

#ifdef RP_DEVICE
namespace rp { inline namespace device { namespace graph {} namespace detail {} } }
#define RP_NAMESPACE_BEGIN namespace rp { inline namespace device {
#define RP_NAMESPACE_END } }
#else
#define RP_NAMESPACE_BEGIN namespace rp {
#define RP_NAMESPACE_END }
#endif
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '5603,5636p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/sdk_namespace.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/sdk_namespace.hpp`
Expected: `a493dfcf5f74ab14cbe59149aefffaaddb255c0c`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/PortTypes.hpp`:

````cpp
#pragma once

// The port kinds and directions, the param types, and the bounds a port's queue and a settings field are sized by:
// what the authoring layer's port wrappers and a node's settings need on both builds of graph/node.hpp, apart from the
// descriptor model (Descriptor.hpp), which is the desktop's and includes this.

#include <cstddef>
#include <cstdint>

#include "graph/sdk_namespace.hpp"

namespace rp::graph {

enum class PortKind : std::uint8_t { Audio, Events, Bytes, Control, Link, Resource };
enum class PortDir : std::uint8_t { In, Out, Link };  // link ports are undirected

enum class ParamType : std::uint8_t { Float, Int, Enum, Bool };

// An events or bytes output's queue: the most messages, and the most payload bytes, one block carries. A push past
// either is dropped and counted (rp_queue::dropped). An output may declare either part or both, as
// "capacity": {"messages": n, "bytes": n}; a part it leaves undeclared takes the default. A wire carries its
// source's queue (an output queue, or a feedback wire's front, which is sized from the source too), so an input has
// no queue of its own to size, and one that declares a capacity is refused rather than silently ignored.
constexpr std::uint32_t kDefaultQueueMessages = 256;
constexpr std::uint32_t kDefaultQueueBytes = 4096;
// The most a port may declare, so a typo cannot allocate gigabytes.
constexpr std::uint32_t kMaxQueueMessages = 1u << 20;  // 1 Mi
constexpr std::uint32_t kMaxQueueBytes = 16u << 20;    // 16 MiB

// The most channels one audio port carries; the planner refuses a port that resolves to more.
constexpr std::uint32_t kMaxChannels = 64;

// The bytes a layout name is stored in by a node's settings: "mono", "stereo", "5.1", "7.1" and "discrete(N)" with N up
// to kMaxChannels, whose longest spelling "discrete(64)" is twelve bytes; sixteen holds every name layoutChannels
// accepts.
constexpr std::size_t kMaxLayoutName = 16;

}  // namespace rp::graph
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '5648,5685p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/PortTypes.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/PortTypes.hpp`
Expected: `664f7b0995eb8f225fb2a9a16b7703af769068bd`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/ResourceId.hpp`:

````cpp
#pragma once

// A document's spelling of a resource id, "res:N", read with no allocation: the desktop's JSON settings reader and its
// graph document parser (parseResourceRef, GraphDocument.hpp) and the device's msgpack settings decode all read it
// here, so the three accept exactly the same spellings.

#include <cstdint>
#include <string_view>

#include "graph/sdk_namespace.hpp"

namespace rp::graph {

// "res:12" -> 12, true. False for anything else, "res:0", a leading zero ("res:007"), a sign and an id past 32 bits
// among them: one spelling per id.
constexpr bool parseResourceId(std::string_view text, std::uint32_t& id) noexcept {
    constexpr std::string_view kPrefix = "res:";
    // No substr or compare(pos, ...): each has a throwing range check, which a device build must not reach.
    if (text.size() <= kPrefix.size() || std::string_view(text.data(), kPrefix.size()) != kPrefix) return false;
    text.remove_prefix(kPrefix.size());
    if (text[0] == '0') return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
        if (value > 0xffffffffu) return false;
    }
    id = static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace rp::graph
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '5697,5728p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/ResourceId.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/ResourceId.hpp`
Expected: `b0af1ee0554336b7d350ecc8d82ccf13233508fe`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/fields.hpp`:

````cpp
#pragma once

// The one place a struct's fields are walked (the main design's 4.7 fields_of seam): the authoring layer reflects a
// node's `inputs`, `outputs` and `params` structs and its Settings through these, on both builds of node.hpp.
//
// The walk is reflect-cpp's compile-time core and nothing above it: num_fields (aggregate initialisation counts the
// fields), bind_to_tuple (a structured binding gives a pointer to each) and the fake object a field's name is read
// from. Those headers are portable (they include <tuple>, <array> and <utility>, and allocate and throw nothing),
// where reflect-cpp's public face is not: rfl/to_view.hpp includes <iostream> and <stdexcept>, and
// rfl/named_tuple_t.hpp and the field names behind it (rfl::Literal) include <string> and <vector>. So the name is
// read here as reflect-cpp reads it, from the compiler's spelling of a function whose template argument points at the
// field, and a name is the one rfl::named_tuple_t gives the same field: the descriptors (port and param ids) do not
// move. graph-test's [fields] case holds fieldName to rfl::fields<S>()[I].name() over every shape of struct the layer
// walks, so a reflect-cpp upgrade that spells a name differently fails there by name.
//
// What reflect-cpp's own walk does and this one does not: rfl::Rename (a field read under another key) and
// rfl::Flatten (a struct's fields read as the parent's). A walked struct holding either is refused at compile time
// (IsRenameOrFlatten below; the desktop's description layer, which can see those types, marks them), since its keys
// here would not be the ones its JSON has.

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#if __has_include(<source_location>)
#include <source_location>
#endif

#include <rfl/internal/bind_to_tuple.hpp>
#include <rfl/internal/cpp20/get_ith_field_from_fake_object.hpp>
#include <rfl/internal/cpp20/num_fields.hpp>

#include "graph/sdk_namespace.hpp"

// Under C++26 reflection reflect-cpp's bind_to_tuple switches to its cpp26 walk while the count and the names here stay
// cpp20's: the two could disagree on a field's index.
#ifdef REFLECTCPP_USE_CPP26_REFLECTION
#error "fields.hpp reads reflect-cpp's cpp20 reflection core; REFLECTCPP_USE_CPP26_REFLECTION needs it reworked"
#endif

RP_NAMESPACE_BEGIN
namespace detail {

// Whether a field's type is an rfl::Rename or an rfl::Flatten. False here, where neither is visible (rfl/Rename.hpp
// includes <string>, so a device-mode build never has one); node_desc.hpp specialises it true for both.
template <class F>
struct IsRenameOrFlatten : std::false_type {};

template <class S> inline constexpr std::size_t kFieldCount = rfl::internal::cpp20::num_fields<S>;

// A pointer to each field of `s`, as a tuple (rfl::get<I> reads one).
template <class S>
auto fieldPointers(S& s) {
    return rfl::internal::bind_to_tuple(s);
}

// The type of field I.
template <class S, std::size_t I>
using FieldType = std::remove_cvref_t<std::remove_pointer_t<
    std::remove_cvref_t<decltype(rfl::get<static_cast<int>(I)>(fieldPointers(std::declval<S&>())))>>>;

// Clang wants the field pointer wrapped before it is a template argument, as reflect-cpp does it.
template <class T>
struct FieldPtr {
    T v;
};
template <class T>
FieldPtr(T) -> FieldPtr<T>;
template <class T>
constexpr auto wrapFieldPtr(const T& p) noexcept {
    return FieldPtr{p};
}

// The field Ptr points at, by name: the tail of this function's own name, which spells its template argument
// (reflect-cpp's get_field_name_str_view, compiler for compiler). Its return type is deduced on purpose: a named one
// (std::string_view) adds a "[with ... std::string_view = ...]" note to GCC's spelling, after the field's name.
template <class S, auto Ptr>
consteval auto fieldNameFromSignature() {
#if __cpp_lib_source_location >= 201907L
    const auto name = std::string_view{std::source_location::current().function_name()};
#elif defined(_MSC_VER)
    const auto name = std::string_view{__FUNCSIG__};
#else
    const auto name = std::string_view{__PRETTY_FUNCTION__};
#endif
#if defined(__clang__)
    const auto split = name.substr(0, name.size() - 2);
    return split.substr(split.find_last_of(":.") + 1);
#elif defined(__GNUC__)
    const auto split = name.substr(0, name.size() - 2);
    return split.substr(split.find_last_of(":") + 1);
#elif defined(_MSC_VER)
    const auto split = name.substr(0, name.size() - 7);
    return split.substr(split.rfind("->") + 2);
#else
    static_assert(sizeof(S) == 0, "field names are read for GCC, Clang and MSVC");
#endif
}

template <class S, std::size_t I>
consteval std::string_view fieldNameView() {
    using rfl::internal::cpp20::get_ith_field_from_fake_object;
#if defined(__clang__)
    return fieldNameFromSignature<S, wrapFieldPtr(get_ith_field_from_fake_object<S, static_cast<int>(I)>())>();
#else
    return fieldNameFromSignature<S, get_ith_field_from_fake_object<S, static_cast<int>(I)>()>();
#endif
}

// The name copied out of the function signature into storage of its own, so it is a constant any build can keep.
template <std::size_t N>
struct FieldNameText {
    char v[N + 1] = {};
};
template <class S, std::size_t I>
struct FieldName {
    static constexpr std::string_view kRaw = fieldNameView<S, I>();
    static constexpr FieldNameText<kRaw.size()> kText = [] {
        FieldNameText<kRaw.size()> t;
        for (std::size_t i = 0; i < kRaw.size(); ++i) t.v[i] = kRaw[i];
        return t;
    }();
};

// Field I's name, the key it has in a JSON or msgpack settings object.
template <class S, std::size_t I>
constexpr std::string_view fieldName() {
    return {FieldName<S, I>::kText.v, FieldName<S, I>::kRaw.size()};
}

// No field of S is an rfl::Rename or an rfl::Flatten (the header comment says why). Checked by both walks below.
template <class S>
consteval bool plainFields() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return (!IsRenameOrFlatten<FieldType<S, I>>::value && ...);
    }(std::make_index_sequence<kFieldCount<S>>{});
}

// f(field) for each field of `s`, in declaration order.
template <class S, class F>
void forEachField(S& s, F&& f) noexcept {
    if constexpr (kFieldCount<S> > 0) {
        static_assert(plainFields<S>(), "a node's ports, params and settings hold no rfl::Rename or rfl::Flatten");
        auto ptrs = fieldPointers(s);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (f(*rfl::get<static_cast<int>(I)>(ptrs)), ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}

// f(name, field) for each field of `s`, in declaration order. Not `noexcept`, unlike forEachField, whose bodies only
// bind ports and params: the authoring layer's own callers here (pinning and resolving resources, a refusal written
// into a fixed ErrorText) throw nothing, but a desktop caller's callback may, and what it throws has to reach the
// caller's catch rather than terminate (graph-test's "the named field walk lets what its callers build throw out of
// it" holds that).
template <class S, class F>
void forEachNamedField(S& s, F&& f) {
    if constexpr (kFieldCount<S> > 0) {
        static_assert(plainFields<S>(), "a node's ports, params and settings hold no rfl::Rename or rfl::Flatten");
        auto ptrs = fieldPointers(s);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (f(fieldName<S, I>(), *rfl::get<static_cast<int>(I)>(ptrs)), ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}

}  // namespace detail
RP_NAMESPACE_END
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '5740,5907p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/fields.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/fields.hpp`
Expected: `c7ae6441414f2bb8d3926b31c676fb095a49ef31`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/enums.hpp`:

````cpp
#pragma once

// A C++ enum's enumerators, names and values, at compile time and with no heap: what a device-mode build reads an enum
// settings field and an enum_param by (settings_msgpack.hpp, node_core.hpp). Both builds use it; the desktop's
// description layer holds it to reflect-cpp's own enum reflection (node_desc.hpp's enumMatchesReflect), so a name here
// is the one rfl::json reads and writes for the same enumerator.
//
// reflect-cpp's enum reflection is enchantum's, behind rfl/enums.hpp, which includes <sstream> and <string>; this is
// the same technique in a few lines: every value in the scanned range is a template argument of a function whose own
// name spells it, an enumerator by its identifier and any other value as a cast ("(Kind)5"), and the values whose
// spelling ends in an identifier are the enumerators. The range is enchantum's default: -256 to 256, clipped to the
// underlying type, and 0 up for an unsigned one. An enum whose underlying type is not fixed (an unscoped enum with no
// `: type`) cannot be scanned (a value outside its enumerators is not a constant there), and a flag enum (one with
// operator|, which reflect-cpp reads as names joined by '|') is not one this reads either: kScannableEnum says which
// are.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#include "graph/sdk_namespace.hpp"

RP_NAMESPACE_BEGIN
namespace detail {

template <class E>
concept FixedEnum = std::is_enum_v<E> && requires { E{0}; };

template <class E>
concept FlagEnum = std::is_enum_v<E> && requires(E e) {
    { e | e } -> std::same_as<E>;
};

// Whether E's enumerators can be read here: an enum with a fixed underlying type that is not a flag enum.
template <class E>
inline constexpr bool kScannableEnum = FixedEnum<E> && !FlagEnum<E>;

template <class E>
struct EnumRange {
    using U = std::underlying_type_t<E>;
    // Each bound compared where it cannot wrap: a signed type's least value fits a long long, and any type's greatest
    // fits an unsigned long long, where a 64-bit unsigned one would turn negative as a long long.
    static constexpr long long kLeast =
        std::is_signed_v<U> ? static_cast<long long>((std::numeric_limits<U>::min)()) : 0;
    static constexpr unsigned long long kMost = static_cast<unsigned long long>((std::numeric_limits<U>::max)());
    static constexpr long long lo = kLeast > -256 ? kLeast : -256;
    static constexpr long long hi = kMost < 256 ? static_cast<long long>(kMost) : 256;
    static constexpr std::size_t size = static_cast<std::size_t>(hi - lo + 1);
};

// V's spelling, the identifier this function's own name ends in, or empty when V is no enumerator. The return type is
// deduced, so GCC's spelling ends at V (a named one would add a "[with ...]" note after it), and the function is
// noexcept, which MSVC's spelling then ends with.
template <class E, E V>
consteval auto enumNameFromSignature() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    const std::string_view name{__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 2};  // up to the closing ']'
#elif defined(_MSC_VER)
    const std::string_view name{__FUNCSIG__, sizeof(__FUNCSIG__) - 17};  // up to ">(void) noexcept"
#else
    static_assert(sizeof(E) == 0, "enum names are read for GCC, Clang and MSVC");
#endif
    std::size_t start = name.size();
    while (start > 0) {
        const char c = name[start - 1];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!word) break;
        --start;
    }
    const std::string_view tail(name.data() + start, name.size() - start);
    const bool identifier = !tail.empty() && !(tail[0] >= '0' && tail[0] <= '9');
    return identifier ? tail : std::string_view();
}

// One value's name, copied out of the signature into storage of its own.
template <std::size_t N>
struct EnumNameText {
    char v[N + 1] = {};
};
template <class E, E V>
struct EnumName {
    static constexpr std::string_view kRaw = enumNameFromSignature<E, V>();
    static constexpr EnumNameText<kRaw.size()> kText = [] {
        EnumNameText<kRaw.size()> t;
        for (std::size_t i = 0; i < kRaw.size(); ++i) t.v[i] = kRaw[i];
        return t;
    }();
    static constexpr std::string_view view() { return {kText.v, kRaw.size()}; }
};

template <class E, std::size_t I>
constexpr E kScanned = static_cast<E>(EnumRange<E>::lo + static_cast<long long>(I));

template <class T, std::size_t N>
struct EnumArray {
    T v[N > 0 ? N : 1] = {};
};

// Which scanned positions are enumerators. A pack expanded into a list, not a fold: Clang caps a fold's nesting at 256,
// and the range is 513 values.
template <class E>
inline constexpr auto kEnumValid = []<std::size_t... I>(std::index_sequence<I...>) {
    return EnumArray<bool, EnumRange<E>::size>{{!EnumName<E, kScanned<E, I>>::kRaw.empty()...}};
}(std::make_index_sequence<EnumRange<E>::size>{});

// How many enumerators E has in the scanned range.
template <class E>
inline constexpr std::size_t kEnumCount = [] {
    std::size_t n = 0;
    for (std::size_t i = 0; i < EnumRange<E>::size; ++i) n += kEnumValid<E>.v[i] ? 1 : 0;
    return n;
}();

// The scanned positions that are enumerators, in order.
template <class E>
inline constexpr auto kEnumPositions = [] {
    EnumArray<std::size_t, kEnumCount<E>> p;
    std::size_t                           n = 0;
    for (std::size_t i = 0; i < EnumRange<E>::size; ++i)
        if (kEnumValid<E>.v[i]) p.v[n++] = i;
    return p;
}();

template <class E>
inline constexpr auto kEnumValues = []<std::size_t... K>(std::index_sequence<K...>) {
    return EnumArray<E, kEnumCount<E>>{{static_cast<E>(EnumRange<E>::lo +
                                                       static_cast<long long>(kEnumPositions<E>.v[K]))...}};
}(std::make_index_sequence<kEnumCount<E>>{});

template <class E>
inline constexpr auto kEnumNames = []<std::size_t... K>(std::index_sequence<K...>) {
    return EnumArray<std::string_view, kEnumCount<E>>{{EnumName<E, kEnumValues<E>.v[K]>::view()...}};
}(std::make_index_sequence<kEnumCount<E>>{});

// E's enumerators in ascending order of value, as reflect-cpp lists them: count, value(i) and name(i).
template <class E>
struct Enumerators {
    static_assert(kScannableEnum<E>, "an enum read at compile time has a fixed underlying type and no operator|");
    static constexpr std::size_t count = kEnumCount<E>;
    static constexpr E                value(std::size_t i) { return kEnumValues<E>.v[i]; }
    static constexpr std::string_view name(std::size_t i) { return kEnumNames<E>.v[i]; }

    // The enumerator called `name`, or false.
    static constexpr bool fromName(std::string_view want, E& out) {
        for (std::size_t i = 0; i < count; ++i)
            if (name(i) == want) {
                out = value(i);
                return true;
            }
        return false;
    }
};

}  // namespace detail
RP_NAMESPACE_END
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '5919,6077p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/enums.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/enums.hpp`
Expected: `9ed2bb097b4f6e16d7c48821c5edc47f70b67a1a`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/msgpack_reader.hpp`:

````cpp
#pragma once

// A msgpack reader that allocates nothing and throws nothing: a cursor over a caller's bytes, which a settings decode
// (settings_msgpack.hpp) walks once, on either build of node.hpp. It reads every msgpack format a settings blob can
// carry (nil, bools, every int width, float 32 and 64, str, bin, array, map) and skips ext. Each read answers false,
// leaving the cursor where it was, when the next value is not of the asked type or runs past the end; a value that
// runs past the end makes the blob malformed, which `truncated` reports.
//
// It is not reflect-cpp's msgpack reader (rfl/msgpack), which unpacks with msgpack-c into a msgpack_zone (a heap
// arena), reports through rfl::Error (a std::string) and includes <istream>; msgpack-c is not a dependency of this
// tree either.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "graph/sdk_namespace.hpp"

namespace rp::graph::msgpack {

enum class Type : std::uint8_t { Nil, Bool, Int, Float, Str, Bin, Array, Map, Ext, Invalid };

// An integer as msgpack carries it: non-negative ones as their unsigned value, negative ones as their signed value.
struct Int {
    bool          negative = false;
    std::uint64_t u = 0;  // when !negative
    std::int64_t  i = 0;  // when negative
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}

    bool        atEnd() const { return p_ == end_; }
    std::size_t remaining() const { return static_cast<std::size_t>(end_ - p_); }
    // Set when a read found a value whose length runs past the end of the blob.
    bool        truncated() const { return truncated_; }

    // The type of the next value, without reading it (Invalid at the end, and for 0xc1, which msgpack never uses).
    Type peek() const {
        if (p_ == end_) return Type::Invalid;
        const std::uint8_t b = *p_;
        if (b <= 0x7f || b >= 0xe0) return Type::Int;  // positive and negative fixint
        if (b <= 0x8f) return Type::Map;
        if (b <= 0x9f) return Type::Array;
        if (b <= 0xbf) return Type::Str;
        switch (b) {
            case 0xc0: return Type::Nil;
            case 0xc2:
            case 0xc3: return Type::Bool;
            case 0xc4: case 0xc5: case 0xc6: return Type::Bin;
            case 0xc7: case 0xc8: case 0xc9: return Type::Ext;
            case 0xca: case 0xcb: return Type::Float;
            case 0xcc: case 0xcd: case 0xce: case 0xcf:
            case 0xd0: case 0xd1: case 0xd2: case 0xd3: return Type::Int;
            case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: return Type::Ext;
            case 0xd9: case 0xda: case 0xdb: return Type::Str;
            case 0xdc: case 0xdd: return Type::Array;
            case 0xde: case 0xdf: return Type::Map;
            default: return Type::Invalid;
        }
    }

    bool readNil() {
        if (peek() != Type::Nil) return false;
        ++p_;
        return true;
    }

    bool readBool(bool& out) {
        if (peek() != Type::Bool) return false;
        out = *p_++ == 0xc3;
        return true;
    }

    bool readInt(Int& out) {
        if (peek() != Type::Int) return false;
        const std::uint8_t b = *p_;
        if (b <= 0x7f) {
            out = Int{false, b, 0};
            ++p_;
            return true;
        }
        if (b >= 0xe0) {
            out = Int{true, 0, static_cast<std::int8_t>(b)};
            ++p_;
            return true;
        }
        const std::size_t n = std::size_t{1} << ((b - 0xcc) & 3);  // 0xcc..0xcf, 0xd0..0xd3: 1, 2, 4, 8 bytes
        if (!has(1 + n)) return false;
        const std::uint64_t raw = be(p_ + 1, n);
        if (b <= 0xcf) {
            out = Int{false, raw, 0};
        } else {
            // Sign-extend the n-byte two's complement value.
            const std::int64_t v = n == 8 ? static_cast<std::int64_t>(raw)
                                          : static_cast<std::int64_t>(raw << (64 - 8 * n)) >> (64 - 8 * n);
            out = v < 0 ? Int{true, 0, v} : Int{false, static_cast<std::uint64_t>(v), 0};
        }
        p_ += 1 + n;
        return true;
    }

    bool readFloat(double& out) {
        if (peek() != Type::Float) return false;
        const bool wide = *p_ == 0xcb;
        if (!has(wide ? 9 : 5)) return false;
        if (wide) {
            const std::uint64_t bits = be(p_ + 1, 8);
            std::memcpy(&out, &bits, sizeof out);
            p_ += 9;
        } else {
            const auto bits = static_cast<std::uint32_t>(be(p_ + 1, 4));
            float f;
            std::memcpy(&f, &bits, sizeof f);
            out = f;
            p_ += 5;
        }
        return true;
    }

    // A str's bytes, a view into the blob.
    bool readStr(std::string_view& out) {
        if (peek() != Type::Str) return false;
        const std::uint8_t b = *p_;
        std::size_t head = 1, len = 0;
        if (b >= 0xa0 && b <= 0xbf) len = b & 0x1f;
        else {
            head = 1 + (b == 0xd9 ? 1 : b == 0xda ? 2 : 4);
            if (!has(head)) return false;
            len = static_cast<std::size_t>(be(p_ + 1, head - 1));
        }
        if (!has(head, len)) return false;
        out = std::string_view(reinterpret_cast<const char*>(p_ + head), len);
        p_ += head + len;
        return true;
    }

    bool readArray(std::uint32_t& count) { return readContainer(Type::Array, 0x90, 0xdc, count); }
    bool readMap(std::uint32_t& count) { return readContainer(Type::Map, 0x80, 0xde, count); }

    // Step over the next value, whatever it is and whatever it holds. No recursion: a container adds its elements to
    // the count still to step over, so a deeply nested value costs no stack.
    bool skip() {
        std::uint64_t pending = 1;
        while (pending > 0) {
            --pending;
            std::uint32_t n = 0;
            Int i;
            double d;
            bool v;
            std::string_view s;
            switch (peek()) {
                case Type::Nil: ++p_; break;
                case Type::Bool: if (!readBool(v)) return false; break;
                case Type::Int: if (!readInt(i)) return false; break;
                case Type::Float: if (!readFloat(d)) return false; break;
                case Type::Str: if (!readStr(s)) return false; break;
                case Type::Bin: if (!skipSized(0xc4)) return false; break;
                case Type::Ext: if (!skipExt()) return false; break;
                case Type::Array:
                    if (!readArray(n)) return false;
                    pending += n;
                    break;
                case Type::Map:
                    if (!readMap(n)) return false;
                    pending += std::uint64_t{n} * 2;
                    break;
                case Type::Invalid: return false;
            }
        }
        return true;
    }

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
    bool                truncated_ = false;

    bool has(std::size_t n) {
        if (remaining() >= n) return true;
        truncated_ = true;
        return false;
    }
    // `head` bytes and then `len` more, checked without adding the two (a 32-bit length near 4 GiB would wrap a
    // 32-bit size_t).
    bool has(std::size_t head, std::size_t len) {
        if (!has(head)) return false;
        if (remaining() - head >= len) return true;
        truncated_ = true;
        return false;
    }

    static std::uint64_t be(const std::uint8_t* at, std::size_t n) {
        std::uint64_t v = 0;
        for (std::size_t k = 0; k < n; ++k) v = (v << 8) | at[k];
        return v;
    }

    bool readContainer(Type type, std::uint8_t fixBase, std::uint8_t wide16, std::uint32_t& count) {
        if (peek() != type) return false;
        const std::uint8_t b = *p_;
        if (b >= fixBase && b <= fixBase + 0x0f) {
            count = b & 0x0f;
            ++p_;
            return true;
        }
        const std::size_t n = b == wide16 ? 2 : 4;
        if (!has(1 + n)) return false;
        count = static_cast<std::uint32_t>(be(p_ + 1, n));
        p_ += 1 + n;
        return true;
    }

    // bin 8 / 16 / 32: a length of 1, 2 or 4 bytes, then that many bytes.
    bool skipSized(std::uint8_t base) {
        const std::size_t n = std::size_t{1} << (*p_ - base);
        if (!has(1 + n)) return false;
        const auto len = static_cast<std::size_t>(be(p_ + 1, n));
        if (!has(1 + n, len)) return false;
        p_ += 1 + n + len;
        return true;
    }

    // fixext 1 / 2 / 4 / 8 / 16 (a type byte and 2^k data bytes), ext 8 / 16 / 32 (a length, a type byte, the data).
    bool skipExt() {
        const std::uint8_t b = *p_;
        if (b >= 0xd4) {
            const std::size_t len = std::size_t{1} << (b - 0xd4);
            if (!has(2 + len)) return false;
            p_ += 2 + len;
            return true;
        }
        const std::size_t n = std::size_t{1} << (b - 0xc7);
        if (!has(1 + n)) return false;
        const auto len = static_cast<std::size_t>(be(p_ + 1, n));
        if (!has(2 + n, len)) return false;
        p_ += 2 + n + len;
        return true;
    }
};

}  // namespace rp::graph::msgpack
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '6089,6332p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/msgpack_reader.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/msgpack_reader.hpp`
Expected: `21a7e3600b6ffdcf48a1cbe9cc824d191e50acf5`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/settings_msgpack.hpp`:

````cpp
#pragma once

// A node's Settings read from msgpack (RP_SETTINGS_MSGPACK, docs/modular-graph-phase-e1-design.md section 6): the
// decode a device runs at plan load, and the desktop's create and query_memory run too, so the device's decode is the
// one the desktop exercises. It allocates nothing and throws nothing, and is written over the compile-time field list
// (fields.hpp) rather than reflect-cpp's msgpack reader (msgpack_reader.hpp says why).
//
// It reads as rfl::json::read<Settings, rfl::DefaultIfMissing> reads JSON: the blob is a map; a field is matched by
// name; a field the map does not name keeps its default; a key that names no field is stepped over, so a blob a newer
// desktop wrote still loads the fields this build knows. A field's value is taken as the JSON reader takes it: an
// unsigned field reads a non-negative integer and a signed one any integer in its range, neither of them a float
// (2.0 is refused, as JSON's 2.0 is); a float field reads an integer or a float; a bool reads a bool; text, a choice,
// a C++ enum and a resource_ref read a str (an enum its enumerator's name, or a number in the string as std::stoi
// reads one; a resource_ref "res:N" or "", the string the canonical JSON carries); a list reads an array of its
// element. A text or list over its bound is refused naming the field, as is a value of the wrong type, and a blob that
// is not one well-formed map is refused whole. A refusal is worded as the JSON reader words the same one.
//
// What a Settings may hold to be read this way is kPortableSettings's list: numbers, bools, C++ enums with a fixed
// underlying type and no operator| (enums.hpp reads their names), rp::choice, rp::text, rp::list of those scalars,
// and rp::resource_ref, in a struct that is trivially copyable. An enum is written by its enumerator's name, as
// reflect-cpp writes it; rp::choice is the kind whose labels a document spells in kebab-case ("high-pass").

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#include "graph/ResourceId.hpp"
#include "graph/enums.hpp"
#include "graph/fields.hpp"
#include "graph/msgpack_reader.hpp"
#include "graph/rp_node.h"
#include "graph/settings_types.hpp"

RP_NAMESPACE_BEGIN
namespace detail {

namespace msgpack = graph::msgpack;

// A reason built in place, the size of rp_error's message, for the paths that must not allocate: the msgpack decode
// and a device-mode create. Appends past the end are cut off.
struct ErrorText {
    char        text[sizeof(rp_error::message)] = {};
    std::size_t size = 0;

    ErrorText& operator<<(std::string_view s) {
        for (const char c : s) {
            if (size + 1 >= sizeof text) break;
            text[size++] = c;
        }
        text[size] = '\0';
        return *this;
    }
    ErrorText& operator<<(std::uint64_t n) {
        char digits[20];
        std::size_t k = 0;
        do {
            digits[k++] = static_cast<char>('0' + n % 10);
            n /= 10;
        } while (n != 0);
        while (k > 0) *this << std::string_view(&digits[--k], 1);
        return *this;
    }
    std::string_view view() const { return {text, size}; }
};

// ---- what a portable Settings holds --------------------------------------------------------------------------------

template <class T> struct IsText : std::false_type {};
template <std::size_t N> struct IsText<text<N>> : std::true_type {};
template <class T> struct IsList : std::false_type {};
template <class T, std::size_t N> struct IsList<list<T, N>> : std::true_type {};
template <class T> struct IsChoice : std::false_type {};
template <fixed_string... L> struct IsChoice<choice<L...>> : std::true_type {};
template <class T> struct IsResourceRef : std::false_type {};
template <fixed_string T> struct IsResourceRef<resource_ref<T>> : std::true_type {};

// A number, a bool, a choice, or a C++ enum enums.hpp can read (a fixed underlying type, no operator|).
template <class F>
inline constexpr bool kPortableScalar = std::is_arithmetic_v<F> || IsChoice<F>::value || kScannableEnum<F>;

template <class F>
consteval bool portableField() {
    if constexpr (IsList<F>::value) return kPortableScalar<typename F::value_type>;
    else return kPortableScalar<F> || IsText<F>::value || IsResourceRef<F>::value;
}

template <class S>
consteval bool portableSettings() {
    if constexpr (!std::is_class_v<S> || !std::is_aggregate_v<S> || !std::is_trivially_copyable_v<S>) {
        return false;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (portableField<FieldType<S, I>>() && ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}

// Whether S is read from msgpack: the list in this file's header comment.
template <class S>
inline constexpr bool kPortableSettings = portableSettings<S>();

// ---- the decode ----------------------------------------------------------------------------------------------------

inline constexpr std::string_view kNotMsgpack = "settings are not valid msgpack";
inline constexpr std::string_view kOutOfRange = "Integer is out of range for the target type.";

// Write a refusal's reason, in parts, and answer false: `return fail(why, "a string of ", n, " bytes ...");`.
template <class... Parts>
bool fail(ErrorText& why, const Parts&... parts) {
    (why << ... << parts);
    return false;
}

// A decimal number at the start of `s`, as std::stoi reads one, which is how reflect-cpp's JSON reader takes an enum
// given as a number in a string ("7", " -1", "7 slow"): leading white space, a sign, at least one digit, and nothing
// after the digits read. False for no digits, or a value past int.
constexpr bool stoiLike(std::string_view s, int& out) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) ++i;
    const bool negative = i < s.size() && s[i] == '-';
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    long long value = 0;
    std::size_t digits = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++digits) {
        value = value * 10 + (s[i] - '0');
        if (value > 2147483648LL) return false;
    }
    if (digits == 0) return false;
    if (negative) value = -value;
    if (value < -2147483648LL || value > 2147483647LL) return false;
    out = static_cast<int>(value);
    return true;
}

// One value into a field of type F. False with the reason (the JSON reader's words, where it has some) in `why`.
template <class F>
bool readField(msgpack::Reader& r, F& out, ErrorText& why) {
    if constexpr (std::is_same_v<F, bool>) {
        if (!r.readBool(out)) return fail(why, "Could not cast to boolean.");
    } else if constexpr (std::is_integral_v<F> && std::is_unsigned_v<F>) {
        msgpack::Int v;
        if (!r.readInt(v) || v.negative) return fail(why, "Could not cast to uint.");
        if constexpr (sizeof(F) < sizeof(std::uint64_t))
            if (v.u > std::numeric_limits<F>::max()) return fail(why, kOutOfRange);
        out = static_cast<F>(v.u);
    } else if constexpr (std::is_integral_v<F>) {
        msgpack::Int v;
        if (!r.readInt(v)) return fail(why, "Could not cast to int.");
        constexpr auto kMin = static_cast<std::int64_t>(std::numeric_limits<F>::min());
        constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<F>::max());
        if (v.negative ? v.i < kMin : v.u > kMax) return fail(why, kOutOfRange);
        out = v.negative ? static_cast<F>(v.i) : static_cast<F>(v.u);
    } else if constexpr (std::is_floating_point_v<F>) {
        // As the JSON reader takes a number: an integer or a float, through double whatever the field's own width.
        msgpack::Int v;
        double       d = 0.0;
        if (r.readInt(v)) d = v.negative ? static_cast<double>(v.i) : static_cast<double>(v.u);
        else if (!r.readFloat(d)) return fail(why, "Could not cast to double.");
        out = static_cast<F>(d);
    } else if constexpr (std::is_enum_v<F>) {
        // The enumerator's name, or a number in the string, as reflect-cpp's string_to_enum reads one.
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        int number = 0;
        if (Enumerators<F>::fromName(s, out)) return true;
        if (stoiLike(s, number)) {
            out = static_cast<F>(number);
            return true;
        }
        why << "Invalid enum value: '" << s << "'. Must be one of [";
        for (std::size_t i = 0; i < Enumerators<F>::count; ++i) why << (i ? ", " : "") << Enumerators<F>::name(i);
        return fail(why, "].");
    } else if constexpr (IsChoice<F>::value) {
        // The words rfl::Literal refuses with, which the desktop's JSON reading of a choice is.
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        if (out.assign(s)) return true;
        why << "Literal does not support string '" << s << "'. The following strings are supported: ";
        for (std::size_t i = 0; i < F::size(); ++i) why << (i ? ", '" : "'") << F::label(i) << "'";
        return fail(why, ".");
    } else if constexpr (IsText<F>::value) {
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        if (!out.assign(s))
            return fail(why, "a string of ", std::uint64_t{s.size()}, " bytes is over the ",
                        std::uint64_t{F::capacity()}, " the field holds");
    } else if constexpr (IsList<F>::value) {
        std::uint32_t n = 0;
        if (!r.readArray(n)) return fail(why, "Could not cast to array.");
        if (n > F::capacity())
            return fail(why, "an array of ", std::uint64_t{n}, " elements is over the ", std::uint64_t{F::capacity()},
                        " the field holds");
        out.clear();
        for (std::uint32_t k = 0; k < n; ++k) {
            typename F::value_type element{};
            ErrorText              inner;
            if (!readField(r, element, inner)) return fail(why, "element ", std::uint64_t{k}, ": ", inner.view());
            out.push_back(element);
        }
    } else if constexpr (IsResourceRef<F>::value) {
        std::string_view s;
        if (!r.readStr(s)) return fail(why, "Could not cast to string.");
        out.id = 0;
        if (!s.empty() && !graph::parseResourceId(s, out.id))
            return fail(why, "\"", s, "\" is not a resource reference (res:N)");
    } else {
        static_assert(sizeof(F) == 0, "a field kPortableSettings refuses");
    }
    return true;
}

// One named field, its refusal worded as the JSON reader words one ("Failed to parse field 'layout': ...").
template <class F>
bool readNamedField(msgpack::Reader& r, std::string_view name, F& field, ErrorText& why) {
    // No value where one must be (the end, or a byte msgpack never uses), or a value that runs past the end, is a
    // malformed blob rather than a wrong value.
    if (r.peek() == msgpack::Type::Invalid) return fail(why, kNotMsgpack);
    ErrorText reason;
    if (readField(r, field, reason)) return true;
    if (r.truncated()) return fail(why, kNotMsgpack);
    return fail(why, "Failed to parse field '", name, "': ", reason.view());
}

// The field of S called `key`, read from the value the reader is at: true with *known set when S has one (*read says
// whether its value was taken), *known false when it has none.
template <class S>
bool readKnownField(msgpack::Reader& r, std::string_view key, S& out, ErrorText& why, bool* known) {
    *known = false;
    bool read = true;
    if constexpr (kFieldCount<S> > 0) {
        auto fields = fieldPointers(out);
        // Each field in turn, until one's name is the key: that one reads the value.
        auto tryField = [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
            if (key != fieldName<S, I>()) return false;
            *known = true;
            read = readNamedField(r, fieldName<S, I>(), *rfl::get<static_cast<int>(I)>(fields), why);
            return true;
        };
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (void)(tryField(std::integral_constant<std::size_t, I>{}) || ...);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
    return read;
}

// Read `size` bytes of msgpack into `out`, which holds the defaults beforehand: the fields the map names are replaced,
// the others keep them. False, with the reason in `why` (and `out` partly written), for a blob that is refused; an
// empty blob is the empty map.
template <class S>
bool decodeSettings(const void* data, std::size_t size, S& out, ErrorText& why) {
    static_assert(kPortableSettings<S>, "only a portable Settings is read from msgpack (settings_msgpack.hpp)");
    if (size == 0) return true;
    msgpack::Reader r(static_cast<const std::uint8_t*>(data), size);
    std::uint32_t   count = 0;
    if (!r.readMap(count)) return fail(why, r.truncated() ? kNotMsgpack : "settings must be a msgpack map");
    for (std::uint32_t k = 0; k < count; ++k) {
        std::string_view key;
        if (!r.readStr(key)) {
            const bool malformed = r.truncated() || r.peek() == msgpack::Type::Invalid;
            return fail(why, malformed ? kNotMsgpack : "a settings key must be a string");
        }
        bool known = false;
        if (!readKnownField(r, key, out, why, &known)) return false;
        if (!known && !r.skip()) return fail(why, kNotMsgpack);  // a key S has no field for: stepped over
    }
    if (!r.atEnd()) return fail(why, "settings hold bytes after their map");
    return true;
}

// A settings-less node's blob: any map, whose keys it ignores, as the JSON side takes any object.
inline bool decodeNoSettings(const void* data, std::size_t size, ErrorText& why) {
    if (size == 0) return true;
    msgpack::Reader r(static_cast<const std::uint8_t*>(data), size);
    if (r.peek() != msgpack::Type::Map) return fail(why, r.atEnd() ? kNotMsgpack : "settings must be a msgpack map");
    if (!r.skip()) return fail(why, kNotMsgpack);
    if (!r.atEnd()) return fail(why, "settings hold bytes after their map");
    return true;
}

}  // namespace detail
RP_NAMESPACE_END
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '6344,6627p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/settings_msgpack.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/settings_msgpack.hpp`
Expected: `1ce04aea8b2b5d7d1cb5d2d471a74a9d8b5afeca`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/settings_json.hpp`:

````cpp
#pragma once

// The desktop's JSON side of the settings types (settings_types.hpp): reflect-cpp parsers that read and write each one
// as the JSON the type it stands in for was, with that type's schema. Part of the desktop build of node.hpp only; a
// device-mode build reads settings from msgpack (settings_msgpack.hpp) and never sees reflect-cpp's parsing layer.

#ifdef RP_DEVICE
#error "settings_json.hpp is the desktop's: a device-mode build reads settings with settings_msgpack.hpp"
#endif

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <rfl/Literal.hpp>
#include <rfl/Result.hpp>
#include <rfl/json.hpp>
#include <rfl/parsing/Parser.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/ResourceId.hpp"
#include "graph/settings_types.hpp"

// A text is the JSON string it holds, and its schema is the string's.
template <class R, class W, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::text<N>>
struct rfl::parsing::Parser<R, W, rp::text<N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using String = Parser<R, W, std::string, ProcessorsType>;

    static Result<rp::text<N>> read(const R& r, const InputVarType& var) noexcept {
        auto s = String::read(r, var);
        if (!s) return rfl::Unexpected<rfl::Error>(s.error());
        rp::text<N> out;
        if (!out.assign(*s))
            return rfl::Unexpected<rfl::Error>(rfl::Error("a string of " + std::to_string(s->size()) +
                                                          " bytes is over the " + std::to_string(N) +
                                                          " the field holds"));
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::text<N>& field, const P& parent) {
        String::write(w, field.str(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return String::to_schema(definitions);
    }
};

// A list is the JSON array its elements make, and its schema is the array's. The elements are read as a std::vector
// would be, so each one reports its own error exactly as it did when the field was one.
template <class R, class W, class T, std::size_t N, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::list<T, N>>
struct rfl::parsing::Parser<R, W, rp::list<T, N>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using Vector = Parser<R, W, std::vector<T>, ProcessorsType>;

    static Result<rp::list<T, N>> read(const R& r, const InputVarType& var) noexcept {
        auto v = Vector::read(r, var);
        if (!v) return rfl::Unexpected<rfl::Error>(v.error());
        if (v->size() > N)
            return rfl::Unexpected<rfl::Error>(rfl::Error("an array of " + std::to_string(v->size()) +
                                                          " elements is over the " + std::to_string(N) +
                                                          " the field holds"));
        rp::list<T, N> out;
        for (const T& e : *v) out.push_back(e);
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::list<T, N>& field, const P& parent) {
        Vector::write(w, std::vector<T>(field.begin(), field.end()), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return Vector::to_schema(definitions);
    }
};

namespace rp::detail {
// The rfl::Literal a choice stands in for, label for label.
template <rp::fixed_string... Labels>
using LiteralOf = rfl::Literal<rfl::internal::StringLiteral<sizeof(Labels.v)>(Labels.v)...>;
}  // namespace rp::detail

// A choice is the rfl::Literal over the same labels: the same JSON string, the same refusal of a string that is none
// of them, and the same schema.
template <class R, class W, rp::fixed_string... Labels, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::choice<Labels...>>
struct rfl::parsing::Parser<R, W, rp::choice<Labels...>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using Lit = rp::detail::LiteralOf<Labels...>;
    using Literal = Parser<R, W, Lit, ProcessorsType>;

    static Result<rp::choice<Labels...>> read(const R& r, const InputVarType& var) noexcept {
        auto lit = Literal::read(r, var);
        if (!lit) return rfl::Unexpected<rfl::Error>(lit.error());
        rp::choice<Labels...> out;
        out.assign_index(lit->value());
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::choice<Labels...>& field, const P& parent) {
        Literal::write(w, Lit::from_value(static_cast<typename Lit::ValueType>(field.value())).value(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return Literal::to_schema(definitions);
    }
};

// A resource_ref is the plain string the document carries, read and written as one, and its schema is a string
// INLINE rather than a $defs entry. rfl names a $defs entry after the C++ type, and a class template over a
// fixed_string has no spelling compilers agree on (GCC writes the argument's type, Clang drops it), so the default
// would put a different key into the descriptor under each compiler; a Settings struct is a named type for the same
// reason (node.hpp's hook list).
template <class R, class W, rp::fixed_string Type, class ProcessorsType>
    requires rfl::parsing::AreReaderAndWriter<R, W, rp::resource_ref<Type>>
struct rfl::parsing::Parser<R, W, rp::resource_ref<Type>, ProcessorsType> {
    using InputVarType = typename R::InputVarType;
    using String = Parser<R, W, std::string, ProcessorsType>;

    // The empty string is "none" (id 0); anything else must be "res:N", the one spelling of an id parseResourceId
    // accepts. A ref that is not is refused here, with the string it carried, where the settings are read.
    static Result<rp::resource_ref<Type>> read(const R& r, const InputVarType& var) noexcept {
        auto text = String::read(r, var);
        if (!text) return rfl::Unexpected<rfl::Error>(text.error());
        rp::resource_ref<Type> out;
        if (text->empty()) return out;
        if (!rp::graph::parseResourceId(*text, out.id))
            return rfl::Unexpected<rfl::Error>(
                rfl::Error(rp::graph::jsonString(*text) + " is not a resource reference (res:N)"));
        return out;
    }
    template <class P>
    static void write(const W& w, const rp::resource_ref<Type>& field, const P& parent) {
        String::write(w, field.ref(), parent);
    }
    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
        return String::to_schema(definitions);
    }
};
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '6639,6778p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/settings_json.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/settings_json.hpp`
Expected: `17d95f0ebb556845c3c53bbf3f0871c8a655ea35`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/node_core.hpp`:

````cpp
#pragma once

// The portable core of the C++ authoring layer (docs/modular-graph-phase-e1-design.md section 6): what both builds of
// graph/node.hpp share. A node includes graph/node.hpp, never this; node.hpp's header comment is the authoring guide.
//
// Here: the port and param wrappers, rp::Block, the compile-time reflection of a node's `inputs`, `outputs` and
// `params` and the binding of a block's views to them, the checks of the optional hooks, the resources a node's
// settings name, the msgpack settings decode, and the instance's block-path entries (process, ports_changed,
// rp.step/1, rp.link/1, get_interface). Each build adds its own instance and the type-level entries around it:
// node_device.hpp (RP_DEVICE) an instance that owns its settings in place and reads them from msgpack, node_desc.hpp
// (the desktop) the description layer, JSON settings and the prepare / commit swap.
//
// Portable means a device can build it: no allocation, no exception, no RTTI, and none of the headers that carry them.
// So this file and the headers it includes stay off <string>, <vector>, <map>, <optional>, <stdexcept>, <exception>,
// <iostream>, reflect-cpp's JSON and yyjson; a device-mode build of node.hpp checks that at its end.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "graph/PortTypes.hpp"
#include "graph/enums.hpp"
#include "graph/Queue.hpp"
#include "graph/fields.hpp"
#include "graph/rp_node.h"
#include "graph/settings_msgpack.hpp"
#include "graph/settings_types.hpp"

RP_NAMESPACE_BEGIN

// ---- channel specs -----------------------------------------------------------------------------------

// What an audio port's channel count is: any count, the count of another port, or a fixed count with its layout. Tags
// only: the desktop's description layer turns each into its graph::ChannelSpec (node_desc.hpp).

struct any {};
template <fixed_string Port> struct match {};
template <std::uint32_t N, fixed_string LayoutName> struct channels {};
using mono   = channels<1, "mono">;
using stereo = channels<2, "stereo">;


// ---- ports ---------------------------------------------------------------------------------------------

template <class Spec>
struct audio_in {
    static constexpr auto rp_kind = graph::PortKind::Audio;
    static constexpr auto rp_dir  = graph::PortDir::In;
    using rp_channels = Spec;
    const float* const* channels = nullptr;
    std::uint32_t       channel_count = 0;
    const float* operator[](std::uint32_t c) const { return channels[c]; }
};

template <class Spec>
struct audio_out {
    static constexpr auto rp_kind = graph::PortKind::Audio;
    static constexpr auto rp_dir  = graph::PortDir::Out;
    using rp_channels = Spec;
    float* const* channels = nullptr;
    std::uint32_t channel_count = 0;
    float* operator[](std::uint32_t c) const { return channels[c]; }
};

// An events or bytes output's queue capacity (graph::QueueCapacity): the most messages, and payload bytes, one block
// carries. A part left 0 takes the default (graph::kDefaultQueueMessages / kDefaultQueueBytes). It rides on the
// port's type, rp::bytes_out::sized<rp::capacity{.bytes = 65536}>, or adjust_layout sets it from settings. A wire
// carries its source's queue, so only an output has one: an input has no `sized` (naming it is a compile error),
// and a capacity adjust_layout sets on one is refused with the node's settings.
struct capacity {
    std::uint32_t messages = 0;
    std::uint32_t bytes = 0;
};

template <graph::PortKind Kind>
struct queue_in {
    static constexpr auto rp_kind = Kind;
    static constexpr auto rp_dir  = graph::PortDir::In;
    const rp_queue* queue = nullptr;
    std::span<const rp_message> messages() const { return {queue->items, queue->count}; }
    std::span<const std::uint8_t> data(const rp_message& m) const { return graph::messageData(*queue, m); }
};

template <graph::PortKind Kind, capacity C = capacity{}>
struct queue_out {
    static_assert(C.messages <= graph::kMaxQueueMessages && C.bytes <= graph::kMaxQueueBytes,
                  "a queue capacity is at most graph::kMaxQueueMessages messages and graph::kMaxQueueBytes bytes");
    static constexpr auto     rp_kind = Kind;
    static constexpr auto     rp_dir  = graph::PortDir::Out;
    static constexpr capacity rp_capacity = C;
    template <capacity N> using sized = queue_out<Kind, N>;
    rp_queue* queue = nullptr;
    bool push(std::uint32_t frame, std::span<const std::uint8_t> data, std::uint32_t flags = 0) const {
        return graph::pushMessage(*queue, frame, data, flags);
    }
};

using events_in  = queue_in<graph::PortKind::Events>;
using events_out = queue_out<graph::PortKind::Events>;
using bytes_in   = queue_in<graph::PortKind::Bytes>;
using bytes_out  = queue_out<graph::PortKind::Bytes>;

struct control_in {
    static constexpr auto rp_kind = graph::PortKind::Control;
    static constexpr auto rp_dir  = graph::PortDir::In;
    const rp_control_queue* queue = nullptr;
    double start() const { return queue->start; }
    std::span<const rp_control_change> changes() const { return {queue->changes, queue->count}; }
};

struct control_out {
    static constexpr auto rp_kind = graph::PortKind::Control;
    static constexpr auto rp_dir  = graph::PortDir::Out;
    rp_control_queue* queue = nullptr;
    bool push(std::uint32_t frame, double value) const { return graph::pushControl(*queue, frame, value); }
};

template <fixed_string Type>
struct resource_in {
    static constexpr auto rp_kind = graph::PortKind::Resource;
    static constexpr auto rp_dir  = graph::PortDir::In;
    static constexpr std::string_view rp_resource_type() { return Type.view(); }
    rp_resource* const* slot = nullptr;
    const rp_resource* get() const { return slot ? *slot : nullptr; }
};

// A resource input the node WRITES (a record buffer). At most one write port may be bound to a given
// mutable resource, and readers must be wired from the writer's output so the planner orders them.
template <fixed_string Type>
struct resource_write {
    static constexpr auto rp_kind = graph::PortKind::Resource;
    static constexpr auto rp_dir  = graph::PortDir::In;
    static constexpr bool rp_write = true;
    static constexpr std::string_view rp_resource_type() { return Type.view(); }
    rp_resource* const* slot = nullptr;
    rp_resource* get() const { return slot ? *slot : nullptr; }
};

template <fixed_string Type>
struct resource_out {
    static constexpr auto rp_kind = graph::PortKind::Resource;
    static constexpr auto rp_dir  = graph::PortDir::Out;
    static constexpr std::string_view rp_resource_type() { return Type.view(); }
    rp_resource** slot = nullptr;
    void set(rp_resource* r) const { *slot = r; }
};

struct link {
    static constexpr auto rp_kind = graph::PortKind::Link;
    static constexpr auto rp_dir  = graph::PortDir::Link;
};

// ---- params ----------------------------------------------------------------------------------------------

struct range {
    double min = 0.0, max = 1.0, init = 0.0;
};

namespace detail {
struct param_base {
    const rp_control_queue* queue = nullptr;
    // The value at block start, and this block's frame-stamped changes (sample-accurate nodes walk them).
    double start() const { return queue->start; }
    std::span<const rp_control_change> changes() const { return {queue->changes, queue->count}; }
    double latest() const { return graph::controlEnd(*queue); }
};
}  // namespace detail

// A param's descriptor entry (its name, type, range and unit) is built from these template arguments by the desktop's
// description layer (node_desc.hpp's ParamDescOf); the wrappers themselves are what a block reads.
template <fixed_string Name, range R = range{}, fixed_string Unit = "">
struct float_param : detail::param_base {
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max, "a param range needs min <= init <= max");
};

template <fixed_string Name, range R = range{}, fixed_string Unit = "">
struct int_param : detail::param_base {
    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max && R.min >= -2147483648.0 &&
                      R.max <= 2147483647.0 && R.min == static_cast<double>(static_cast<int>(R.min)) &&
                      R.max == static_cast<double>(static_cast<int>(R.max)) &&
                      R.init == static_cast<double>(static_cast<int>(R.init)),
                  "an int param's range is whole numbers with min <= init <= max, within int");
    // The value at block start, rounded to the nearest int (half away from zero); NaN reads as the declared
    // default and anything beyond int saturates. It is NOT clamped to the declared range: a wire can carry
    // any double and adjust_layout may widen the range, so a node clamps to its own resolved range where
    // that matters (Delay does).
    int value() const {
        const double v = start();
        if (!(v == v)) return static_cast<int>(R.init);
        if (v <= -2147483648.0) return std::numeric_limits<int>::min();
        if (v >= 2147483647.0) return std::numeric_limits<int>::max();
        return static_cast<int>(v + (v < 0 ? -0.5 : 0.5));
    }
};

template <fixed_string Name, bool Default = false>
struct bool_param : detail::param_base {
    bool value() const { return start() >= 0.5; }  // NaN reads as false
};

namespace detail {
template <class E, E Value>
constexpr bool isEnumerator() {
    for (std::size_t i = 0; i < Enumerators<E>::count; ++i)
        if (Enumerators<E>::value(i) == Value) return true;
    return false;
}
}  // namespace detail

// An enum param: labels are the enumerator names in kebab-case (the desktop's descriptor, node_desc.hpp), the runtime
// value is the index, rounded and clamped so any control can drive it (NaN reads as the default). E is an enum
// enums.hpp can read: a fixed underlying type and no operator|.
template <class E, fixed_string Name, E Default>
struct enum_param : detail::param_base {
    using Values = detail::Enumerators<E>;
    static_assert(Values::count > 0, "an enum param needs at least one enumerator");
    static_assert(detail::isEnumerator<E, Default>(), "an enum param's default must be one of its enumerators");

    E value() const {
        const double v = start();
        if (!(v == v)) return Default;
        const std::size_t last = Values::count - 1;
        std::size_t       i = 0;
        if (v >= static_cast<double>(last)) i = last;
        else if (v > 0.0) i = static_cast<std::size_t>(v + 0.5);
        return Values::value(i);
    }
};

// ---- the block -------------------------------------------------------------------------------------------

struct Block {
    const rp_process& raw;
    std::uint32_t frames() const { return raw.frames; }
    const rp_transport& transport() const { return *raw.transport; }
    // Ports and params beyond the reflected ones (appended by adjust_layout) are reached by index here.
    const rp_port_view& input(std::uint32_t i) const { return raw.inputs[i]; }
    const rp_port_view& output(std::uint32_t i) const { return raw.outputs[i]; }
    const rp_control_queue& param(std::uint32_t i) const { return *raw.params[i]; }
};

// ---- reflection glue ---------------------------------------------------------------------------------------

namespace detail {

struct NoSettings {};
struct NoPorts {};

template <class T> struct SettingsOf { using type = NoSettings; };
template <class T> requires requires { typename T::Settings; } struct SettingsOf<T> { using type = typename T::Settings; };

template <class T> constexpr bool reshapesOf() {
    if constexpr (requires { T::reshapes; }) return T::reshapes; else return false;
}
template <class T> constexpr bool statelessPrepareOf() {
    if constexpr (requires { T::stateless_prepare; }) return T::stateless_prepare; else return false;
}

// What rebuild_key returns, or NoKey for a type without one. Detected by the call, as needs_rebuild is in prepareFn,
// so the key the instance keeps and the key prepare compares are found the same way.
struct NoKey {};
template <class T> struct RebuildKeyOf { using type = NoKey; };
template <class T>
    requires requires(const typename SettingsOf<T>::type& s) { T::rebuild_key(s); }
struct RebuildKeyOf<T> {
    using type = std::remove_cvref_t<decltype(T::rebuild_key(std::declval<const typename SettingsOf<T>::type&>()))>;
};
template <class T> inline constexpr bool kHasRebuildKey = !std::is_same_v<typename RebuildKeyOf<T>::type, NoKey>;

template <class T> auto& inputsOf(T& t) { if constexpr (requires { t.inputs; }) return t.inputs; else { static NoPorts none; return none; } }
template <class T> auto& outputsOf(T& t) { if constexpr (requires { t.outputs; }) return t.outputs; else { static NoPorts none; return none; } }
template <class T> auto& paramsOf(T& t) { if constexpr (requires { t.params; }) return t.params; else { static NoPorts none; return none; } }

template <class S>
void bindPorts([[maybe_unused]] S& ports, [[maybe_unused]] const rp_port_view* views) noexcept {
    if constexpr (!std::is_same_v<S, NoPorts>) {
        std::uint32_t i = 0;
        forEachField(ports, [&](auto& port) {
            using P = std::remove_cvref_t<decltype(port)>;
            const rp_port_view& v = views[i++];
            if constexpr (P::rp_kind == graph::PortKind::Audio) {
                port.channels = v.u.audio.channels;
                port.channel_count = v.u.audio.channel_count;
            } else if constexpr (P::rp_kind == graph::PortKind::Events || P::rp_kind == graph::PortKind::Bytes) {
                port.queue = v.u.queue;
            } else if constexpr (P::rp_kind == graph::PortKind::Control) {
                port.queue = v.u.control;
            } else if constexpr (P::rp_kind == graph::PortKind::Resource) {
                port.slot = v.u.resource;
            }
        });
    }
}

template <class S>
void bindParams([[maybe_unused]] S& params, [[maybe_unused]] rp_control_queue* const* queues) noexcept {
    if constexpr (!std::is_same_v<S, NoPorts>) {
        std::uint32_t i = 0;
        forEachField(params, [&](auto& param) { param.queue = queues[i++]; });
    }
}

// ---- compile-time checks of the optional hooks ---------------------------------------------------------------

// A hook is a function; a data member that happens to share its name is not one.
template <class P>
inline constexpr bool kIsFunction = std::is_member_function_pointer_v<P> || std::is_function_v<std::remove_pointer_t<P>>;

template <class T>
constexpr bool checkHooks() {
    using S = typename SettingsOf<T>::type;
    static_assert(requires(T& t, const Block& b) { { t.process(b) } noexcept; },
                  "a node needs `process(const rp::Block&) noexcept`");
    static_assert(requires(T& t, const Block& b) { { t.process(b) } -> std::same_as<void>; } ||
                      requires(T& t, const Block& b) { { t.process(b) } -> std::same_as<rp_status>; },
                  "process returns void or rp_status");
    // validate and adjust_layout name desktop types; the desktop's checkDescriptionHooks checks them (node_desc.hpp).
    // stateless_prepare against needs_rebuild is checked in prepareFn, where it is called.
    if constexpr (requires { requires kIsFunction<decltype(&T::needs_rebuild)>; })
        static_assert(requires(const S& s) { { T::needs_rebuild(s, s) } -> std::same_as<bool>; },
                      "needs_rebuild must be `static bool needs_rebuild(const Settings&, const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::rebuild_key)>; }) {
        static_assert(requires(const S& s) { T::rebuild_key(s); },
                      "rebuild_key must be `static Key rebuild_key(const Settings&)`");
        // Only a key the call really gives has a type to check: a signature near-miss falls back to NoKey here.
        if constexpr (requires(const S& s) { T::rebuild_key(s); }) {
            using K = typename RebuildKeyOf<T>::type;
            static_assert(!std::is_void_v<K> && std::copy_constructible<K> && std::equality_comparable<K>,
                          "rebuild_key's Key must be copyable and compare with ==");
        }
    }
    static_assert(!(kHasRebuildKey<T> && requires(const S& s) { T::needs_rebuild(s, s); }),
                  "a node has needs_rebuild or rebuild_key, not both: the key already decides what rebuilds");
    if constexpr (requires { requires kIsFunction<decltype(&T::extra_memory)>; })
        static_assert(requires(const S& s) { { T::extra_memory(s) } -> std::same_as<std::size_t>; },
                      "extra_memory must be `static std::size_t extra_memory(const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::attach)>; })
        static_assert(requires(T& t, std::span<std::byte> m) { { t.attach(m) } noexcept; },
                      "attach must be `void attach(std::span<std::byte>) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::on_settings)>; })
        static_assert(requires(T& t, const S& s) { { t.on_settings(s) } noexcept; },
                      "on_settings must be `void on_settings(const Settings&) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::bind_host)>; })
        static_assert(requires(T& t, const rp_host* h) { { t.bind_host(h) } noexcept; },
                      "bind_host must be `void bind_host(const rp_host*) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::activate)>; })
        static_assert(requires(T& t) { { t.activate(1.0, 1u) } -> std::same_as<bool>; } ||
                          requires(T& t) { { t.activate(1.0, 1u) } -> std::same_as<void>; },
                      "activate must be `bool activate(double, std::uint32_t)` or return void");
    if constexpr (requires { requires kIsFunction<decltype(&T::deactivate)>; })
        static_assert(requires(T& t) { t.deactivate(); }, "deactivate must be `void deactivate()`");
    if constexpr (requires { requires kIsFunction<decltype(&T::ports_changed)>; })
        static_assert(requires(T& t, std::span<const std::uint8_t> c) { { t.ports_changed(c) } noexcept; },
                      "ports_changed must be `void ports_changed(std::span<const std::uint8_t>) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::set_peers)>; })
        static_assert(requires(T& t, std::span<rp_node* const> p) { { t.set_peers(p) } noexcept; },
                      "set_peers must be `void set_peers(std::span<rp_node* const>) noexcept`");
    if constexpr (requires { requires kIsFunction<decltype(&T::get_interface)>; })
        static_assert(
            requires(T& t, std::string_view id) { { t.get_interface(id) } noexcept -> std::same_as<const void*>; },
            "get_interface must be `const void* get_interface(std::string_view) noexcept`");
    constexpr bool anyStep = requires { requires kIsFunction<decltype(&T::prepare)>; } ||
                             requires { requires kIsFunction<decltype(&T::step)>; } ||
                             requires { requires kIsFunction<decltype(&T::finish)>; };
    if constexpr (anyStep) {
        static_assert(requires(T& t, const Block& b) { { t.prepare(b) } noexcept; { t.finish(b) } noexcept; },
                      "rp.step/1 needs `prepare(const rp::Block&) noexcept` and `finish(const rp::Block&) noexcept`");
        static_assert(requires(T& t, std::uint32_t f) { { t.step(f) } noexcept -> std::same_as<bool>; } ||
                          requires(T& t, std::uint32_t f) { { t.step(f) } noexcept -> std::same_as<rp_step_result>; },
                      "rp.step/1 needs `step(std::uint32_t) noexcept` returning bool or rp_step_result");
    }
    return true;
}

// ---- the instance ----------------------------------------------------------------------------------------------

// One pin an instance holds on a resource its settings name, and how to drop it: the host's table, by id. A field
// that names no resource still holds its place, so the pins line up with the settings' resource fields in order.
struct ResourceHold {
    const rp_host_resources* service = nullptr;
    std::uint32_t            named = 0;   // the id the settings named (0: none), so prepare can tell whether it moved
    std::uint32_t            id = 0;      // the id pinned: equal to `named` while a pin is held, 0 after drop
    const rp_resource*       resource = nullptr;

    ResourceHold() = default;
    ResourceHold(const ResourceHold&) = delete;
    ResourceHold& operator=(const ResourceHold&) = delete;
    ResourceHold(ResourceHold&& other) noexcept
        : service(std::exchange(other.service, nullptr)),
          named(other.named),
          id(std::exchange(other.id, 0)),
          resource(std::exchange(other.resource, nullptr)) {}
    ResourceHold& operator=(ResourceHold&& other) noexcept {
        if (this != &other) {
            drop();
            service = std::exchange(other.service, nullptr);
            named = other.named;
            id = std::exchange(other.id, 0);
            resource = std::exchange(other.resource, nullptr);
        }
        return *this;
    }
    ~ResourceHold() { drop(); }

    void drop() noexcept {
        if (service && id) service->release(service->ctx, id);
        service = nullptr;
        id = 0;
        resource = nullptr;
    }
};

// One instance of T as each build lays it out (node_device.hpp, node_desc.hpp): an rp_node first, then the pins on the
// resources its settings name, declared before the node so they are released after it, which may read one of them in
// its destructor.
template <class T> struct Instance;

template <class T>
Instance<T>* self(rp_node* n) noexcept { return static_cast<Instance<T>*>(n); }

inline void copyError(rp_error* err, std::string_view what) {
    if (!err) return;
    const std::size_t n = what.size() < sizeof err->message - 1 ? what.size() : sizeof err->message - 1;
    std::memcpy(err->message, what.data(), n);
    err->message[n] = '\0';
}

template <class T>
std::size_t extraBytes([[maybe_unused]] const typename SettingsOf<T>::type& s) {
    if constexpr (requires { T::extra_memory(s); }) return T::extra_memory(s);
    else return 0;
}

constexpr std::size_t alignUp(std::size_t n, std::size_t a) { return (n + a - 1) / a * a; }

// ---- resources named by settings (docs/modular-graph-phase-c3-design.md section 4) ------------------------

template <class Field, class F>
void resourceField(std::string_view name, Field& field, F&& f) {
    using P = std::remove_cvref_t<Field>;
    if constexpr (IsResourceRef<P>::value) f(name, field);
}

// A settings struct's resource fields, in declaration order, each with its JSON name: the order create pins them in
// and prepare compares them by. Only settings fields are walked this way; a port or param of the same type is not.
template <class S, class F>
void forEachResourceField(S& s, F&& f) {
    forEachNamedField(s, [&](std::string_view name, auto& field) { resourceField(name, field, f); });
}

// How many resource fields S has: the pins an instance holds, one per field whether or not it names a resource.
template <class S>
consteval std::size_t resourceFieldCount() {
    if constexpr (std::is_same_v<S, NoSettings>) {
        return 0;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (std::size_t{IsResourceRef<FieldType<S, I>>::value} + ... + 0);
        }(std::make_index_sequence<kFieldCount<S>>{});
    }
}
template <class S> inline constexpr std::size_t kResourceFields = resourceFieldCount<S>();

// Pin every resource `s` names, one ResourceHold per resource field in field order (`pins` has kResourceFields of
// them), resolving each field's handle. False, with the reason in *why, for a host that offers no table, an id that
// is unknown or dropped, or a resource of another type than the field declares (a "rom" where the document names an
// sram). The reason is built in place, so a device-mode create allocates nothing here either.
template <class S>
bool acquireResources(std::span<ResourceHold> pins, const rp_host* host, S& s, ErrorText* why) {
    if (pins.empty()) return true;  // a node that names none never looks the service up
    const auto* service =
        host ? static_cast<const rp_host_resources*>(host->get_interface(host, RP_HOST_RESOURCES_INTERFACE)) : nullptr;
    bool        ok = true;
    std::size_t i = 0;
    forEachResourceField(s, [&](std::string_view name, auto& field) {
        ResourceHold& held = pins[i++];
        held.named = field.id;
        if (!ok || field.empty()) return;  // the document names none here
        if (!service) {
            *why << name << ": the host offers no resource table";
            ok = false;
            return;
        }
        // The field's type as a C string for the service, from its compile-time spelling.
        constexpr auto kType = std::remove_cvref_t<decltype(field)>::resource_type();
        char type[kType.size() + 1] = {};
        for (std::size_t k = 0; k < kType.size(); ++k) type[k] = kType[k];
        rp_error err{};
        const rp_resource* r = service->acquire(service->ctx, field.id, type, &err);
        if (!r) {
            // The service's reason continues the sentence: "rom: \"res:7\" is not a live resource".
            *why << name << ": \"res:" << std::uint64_t{field.id} << "\" "
                 << (err.message[0] ? std::string_view(err.message) : std::string_view("cannot be used"));
            ok = false;
            return;
        }
        held.service = service;
        held.id = field.id;
        held.resource = r;
        field.resource = r;
    });
    return ok;
}

// ---- the block path ---------------------------------------------------------------------------------------------

template <class T>
void portsChangedFn([[maybe_unused]] rp_node* n, [[maybe_unused]] const std::uint8_t* connected,
                    [[maybe_unused]] std::uint32_t count) noexcept {
    if constexpr (requires(T& t, std::span<const std::uint8_t> c) { t.ports_changed(c); })
        self<T>(n)->node.ports_changed(std::span<const std::uint8_t>(connected, count));
}

template <class T>
void bindAll(T& node, const rp_process* p) noexcept {
    bindPorts(inputsOf(node), p->inputs);
    bindPorts(outputsOf(node), p->outputs);
    bindParams(paramsOf(node), p->params);
}

template <class T>
rp_status processFn(rp_node* n, const rp_process* p) noexcept {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    const Block block{*p};
    if constexpr (std::is_same_v<decltype(node.process(block)), rp_status>) return node.process(block);
    else {
        node.process(block);
        return RP_OK;
    }
}

template <class T> void stepPrepare(rp_node* n, const rp_process* p) noexcept {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    node.prepare(Block{*p});
}
template <class T> rp_step_result stepStep(rp_node* n, std::uint32_t target) noexcept {
    T& node = self<T>(n)->node;
    if constexpr (std::is_same_v<decltype(node.step(target)), bool>) return node.step(target) ? RP_STEP_DONE : RP_STEP_BELOW;
    else return node.step(target);
}
template <class T> void stepFinish(rp_node* n, const rp_process* p) noexcept {
    T& node = self<T>(n)->node;
    bindAll(node, p);
    node.finish(Block{*p});
}
template <class T> void linkSetPeers(rp_node* n, rp_node* const* peers, std::uint32_t count) noexcept {
    self<T>(n)->node.set_peers(std::span<rp_node* const>(peers, count));
}

template <class T>
const void* interfaceFn([[maybe_unused]] rp_node* n, const char* id) noexcept {
    [[maybe_unused]] const std::string_view want(id);
    if constexpr (requires(T& t, const Block& b, std::uint32_t f) { t.prepare(b); t.step(f); t.finish(b); }) {
        static constexpr rp_step_interface kStep{&stepPrepare<T>, &stepStep<T>, &stepFinish<T>};
        if (want == RP_STEP_INTERFACE) return &kStep;
    }
    if constexpr (requires(T& t, std::span<rp_node* const> peers) { t.set_peers(peers); }) {
        static constexpr rp_link_interface kLink{&linkSetPeers<T>};
        if (want == RP_LINK_INTERFACE) return &kLink;
    }
    if constexpr (requires(T& t, std::string_view w) { t.get_interface(w); })
        return self<T>(n)->node.get_interface(want);
    else
        return nullptr;
}

template <class T> constexpr const char* nameOf() { if constexpr (requires { T::name; }) return T::name; else return T::id; }
template <class T> constexpr const char* categoryOf() { if constexpr (requires { T::category; }) return T::category; else return ""; }
template <class T> constexpr const char* versionOf() { if constexpr (requires { T::version; }) return T::version; else return "1.0.0"; }
template <class T> constexpr bool heapOf() { if constexpr (requires { T::heap; }) return T::heap; else return false; }
template <class T> constexpr bool threadsOf() { if constexpr (requires { T::threads; }) return T::threads; else return false; }
// At most one node of the type in a plan (the planner refuses a second): a type that binds a host-owned device sets it.
template <class T> constexpr bool uniqueOf() {
    if constexpr (requires { T::unique; }) return T::unique; else return false;
}
template <class T> constexpr std::uint32_t typeFlags() {
    std::uint32_t flags = 0;
    if constexpr (statelessPrepareOf<T>()) flags |= RP_TYPE_STATELESS_PREPARE;
    return flags;
}

}  // namespace detail

RP_NAMESPACE_END
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '6790,7382p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/node_core.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/node_core.hpp`
Expected: `abca80e13f138cd3309be6d17825c2b432321155`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/node_device.hpp`:

````cpp
#pragma once

// The device-mode instance (RP_DEVICE, docs/modular-graph-phase-e1-design.md section 6): what graph/node.hpp adds to
// its portable core (node_core.hpp) when it is built for a device. A node includes graph/node.hpp, never this.
//
// A device runs a plan the desktop made, planned and validated, so nothing here describes a type or checks one: the
// type's descriptor and layout_for are null, its vtable's prepare_settings, commit_settings and release_settings are
// null (a device's plan never changes a node's settings), and validate and adjust_layout are never called. What is
// left is the lifecycle: query_memory and create read the settings from msgpack, and the instance owns them in place,
// counted in the size query_memory asks for, beside a fixed array of pins with one per resource field. Nothing here
// allocates or throws, and no entry catches: a node built for the device throws nothing either.
//
// A type is built for the device only when it can run there: it declares neither `heap` nor `threads` (the planner
// refuses those on the Pico, and a device build never has them at all), and its Settings is portable
// (settings_msgpack.hpp's kPortableSettings). Either one failing is a compile error naming the type
// (DeviceTypeCheck<T> in the error's instantiation context).

#ifndef RP_DEVICE
#error "node_device.hpp is the device-mode instance: include graph/node.hpp, which picks the build"
#endif

#include "graph/node_core.hpp"

RP_NAMESPACE_BEGIN

namespace graph {
// Declared and never defined in a device-mode build: the description layer's Layout is the desktop's. A node's
// adjust_layout left outside `#ifndef RP_DEVICE` (node.hpp's header comment) fails on this incomplete type, with this
// note beside the error, rather than on a name that does not exist.
struct Layout;
}  // namespace graph

namespace detail {

template <class T>
struct DeviceTypeCheck {
    using S = typename SettingsOf<T>::type;
    static_assert(!heapOf<T>() && !threadsOf<T>(),
                  "a node type that declares `heap` or `threads` is never built for the device: leave it out of the "
                  "device build (#ifndef RP_DEVICE around the type and its package entry)");
    static_assert(std::is_same_v<S, NoSettings> || kPortableSettings<S>,
                  "a device node's Settings holds only numbers, bools, enums (a fixed underlying type, no operator|), "
                  "rp::choice, rp::text, rp::list of those and rp::resource_ref, in a trivially copyable struct");
    // A deliberate check of node.hpp's rule, which otherwise holds only where a hook's body names something a device
    // build lacks (the incomplete graph::Layout; on libstdc++ the heap-header #error): on another standard library a
    // validate whose body happens to compile would slip through without it.
    static_assert(!requires { &T::validate; } && !requires { &T::adjust_layout; },
                  "validate and adjust_layout are the desktop's: a node built for the device keeps them (and what only "
                  "they use) inside #ifndef RP_DEVICE");
    // The descriptor's strings are never read here (a device has none), but they are the type's all the same.
    static_assert(nameOf<T>() != nullptr && categoryOf<T>() != nullptr && versionOf<T>() != nullptr);
    static constexpr bool ok = true;
};

// The pins on the resources a type's settings name, one per resource field; nothing at all for a type that names none.
template <std::size_t N>
struct Pins {
    ResourceHold          hold[N];
    std::span<ResourceHold> span() { return hold; }
};
template <>
struct Pins<0> {
    std::span<ResourceHold> span() { return {}; }
};

// The settings live in the instance, after the pins and before the node, so they are destroyed after it: a node may
// read its settings until its own destructor has run, as on the desktop.
template <class T>
struct Instance : rp_node {
    using S = typename SettingsOf<T>::type;
    // Released after the node and its settings (the node may read one in its destructor). Takes no room when empty.
    [[no_unique_address]] Pins<kResourceFields<S>> resources;
    S                                              settings;
    T                                              node;
    std::span<std::byte>                           extra;
};

template <class T>
std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }

// The settings in `s`, read into `out` (which holds the defaults). False with the reason for any encoding but msgpack:
// a device has no JSON reader.
template <class T>
bool readSettings(const rp_settings* s, typename SettingsOf<T>::type& out, ErrorText& why) {
    using S = typename SettingsOf<T>::type;
    if (s && s->encoding != RP_SETTINGS_MSGPACK) {
        why << "settings encoding " << std::uint64_t{s->encoding} << " is not supported; a device node reads msgpack";
        return false;
    }
    const void*       data = s ? s->data : nullptr;
    const std::size_t size = s && s->data ? s->size : 0;
    if constexpr (std::is_same_v<S, NoSettings>) {
        (void)out;
        return decodeNoSettings(data, size, why);
    } else {
        return decodeSettings(data, size, out, why);
    }
}

template <class T>
bool activateFn([[maybe_unused]] rp_node* n, [[maybe_unused]] double sampleRate,
                [[maybe_unused]] std::uint32_t maxFrames, [[maybe_unused]] rp_error* err) {
    if constexpr (!requires(T& t) { t.activate(sampleRate, maxFrames); }) {
        return true;
    } else if constexpr (std::is_same_v<decltype(self<T>(n)->node.activate(sampleRate, maxFrames)), bool>) {
        if (self<T>(n)->node.activate(sampleRate, maxFrames)) return true;
        copyError(err, "activate failed");
        return false;
    } else {
        self<T>(n)->node.activate(sampleRate, maxFrames);
        return true;
    }
}

template <class T>
void deactivateFn([[maybe_unused]] rp_node* n) {
    if constexpr (requires(T& t) { t.deactivate(); }) self<T>(n)->node.deactivate();
}

template <class T>
inline constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
                                        &processFn<T>,  nullptr,          nullptr,
                                        nullptr,        &interfaceFn<T>};

template <class T>
bool queryMemoryFn(const rp_node_type*, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    typename SettingsOf<T>::type settings{};
    ErrorText why;
    if (!readSettings<T>(s, settings, why)) {
        copyError(err, why.view());
        return false;
    }
    const std::size_t extra = extraBytes<T>(settings);
    if (extra > std::numeric_limits<std::size_t>::max() - extraOffset<T>()) {
        copyError(err, "extra memory request is too large");
        return false;
    }
    out->size = extra ? extraOffset<T>() + extra : sizeof(Instance<T>);
    out->align = alignof(Instance<T>) > alignof(std::max_align_t) ? alignof(Instance<T>) : alignof(std::max_align_t);
    return true;
}

template <class T>
rp_node* createFn(const rp_node_type*, [[maybe_unused]] const rp_host* host, void* memory, const rp_settings* s,
                  rp_error* err) {
    using S = typename SettingsOf<T>::type;
    auto* inst = new (memory) Instance<T>{};
    inst->vt = &kVtable<T>;
    ErrorText why;
    // The settings are read where they live. The pins are taken before any hook sees the settings, so on_settings and
    // activate read the handles; bind_host comes first, as on the desktop.
    bool ok = readSettings<T>(s, inst->settings, why);
    if (ok) {
        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
        ok = acquireResources(inst->resources.span(), host, inst->settings, &why);
    }
    if (ok) {
        if (const std::size_t extra = extraBytes<T>(inst->settings)) {
            inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
            std::memset(inst->extra.data(), 0, extra);
            if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
        }
        if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(inst->settings);
        return inst;
    }
    // Settings that would not read, or a resource that would not pin: leave nothing behind.
    copyError(err, why.view());
    inst->~Instance<T>();
    return nullptr;
}

template <class T>
void destroyFn(rp_node* n) {
    self<T>(n)->~Instance<T>();
}

template <class T>
inline constexpr rp_node_type kNodeType{T::id,          nullptr,          nullptr,
                                        &queryMemoryFn<T>, &createFn<T>, &destroyFn<T>,
                                        typeFlags<T>(), nullptr};

}  // namespace detail
RP_NAMESPACE_END
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '7394,7576p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/node_device.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/node_device.hpp`
Expected: `49eb6408050c5798a34b0f4aafcb73373c27dc78`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/node_desc.hpp`:

````cpp
#pragma once

// The desktop's description layer (docs/modular-graph-phase-e1-design.md section 6): what graph/node.hpp adds to its
// portable core (node_core.hpp) when it is built for the desktop, which plans. A node includes graph/node.hpp, never
// this.
//
// Here: each type's descriptor (its ports, params and settings schema, reflected), layout_for (the layout one set of
// settings gives, with adjust_layout), validate, the settings read from JSON by reflect-cpp (or from msgpack, by the
// core's decode), and an instance whose settings live on the heap so that prepare_settings can build the next object
// and commit_settings swap a pointer. Every control-thread entry catches, so an exception never crosses the ABI.

#ifdef RP_DEVICE
#error "node_desc.hpp is the desktop's description layer: include graph/node.hpp, which picks the build"
#endif

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <rfl/DefaultIfMissing.hpp>
#include <rfl/Flatten.hpp>
#include <rfl/Rename.hpp>
#include <rfl/enums.hpp>
#include <rfl/json.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/Descriptor.hpp"
#include "graph/GraphDocument.hpp"
#include "graph/node_core.hpp"
#include "graph/settings_json.hpp"

namespace rp {

namespace detail {

// The two reflect-cpp field wrappers the field walk does not follow (fields.hpp), marked so the walk refuses them.
template <rfl::internal::StringLiteral Name, class T>
struct IsRenameOrFlatten<rfl::Rename<Name, T>> : std::true_type {};
template <class T>
struct IsRenameOrFlatten<rfl::Flatten<T>> : std::true_type {};
template <class S> struct ChannelSpecOf;
template <> struct ChannelSpecOf<any> {
    static graph::ChannelSpec get() { return graph::ChannelSpec::any(); }
};
template <fixed_string P> struct ChannelSpecOf<match<P>> {
    static graph::ChannelSpec get() { return graph::ChannelSpec::matching(std::string(P.view())); }
};
template <std::uint32_t N, fixed_string L> struct ChannelSpecOf<channels<N, L>> {
    static graph::ChannelSpec get() { return graph::ChannelSpec::fixed(N, std::string(L.view())); }
};

// A param's descriptor entry, from the wrapper's template arguments (enum_param's is below, with the kebab-case
// labels). A param type of a node's own supplies `static graph::ParamDesc rp_param(std::string id)` instead.
template <class P>
struct ParamDescOf {
    static graph::ParamDesc get(std::string id) { return P::rp_param(std::move(id)); }
};
template <fixed_string Name, range R, fixed_string Unit>
struct ParamDescOf<float_param<Name, R, Unit>> {
    static graph::ParamDesc get(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Float, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
};
template <fixed_string Name, range R, fixed_string Unit>
struct ParamDescOf<int_param<Name, R, Unit>> {
    static graph::ParamDesc get(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Int, R.min, R.max, R.init, {},
                std::string(Unit.view())};
    }
};
template <fixed_string Name, bool Default>
struct ParamDescOf<bool_param<Name, Default>> {
    static graph::ParamDesc get(std::string id) {
        return {std::move(id), std::string(Name.view()), graph::ParamType::Bool, 0.0, 1.0, Default ? 1.0 : 0.0, {}, {}};
    }
};

// Enumerator names become kebab-case labels - MidiSync -> "midi-sync", HighPassTPDF -> "high-pass-tpdf",
// N8Fifo -> "n8-fifo" - so C++ and TS enums share one label convention in saved state.
inline std::string kebabCase(std::string_view name) {
    std::string out;
    auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto lowerOrDigit = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && upper(c)) {
            const bool afterLower = lowerOrDigit(name[i - 1]);
            const bool acronymEnd = upper(name[i - 1]) && i + 1 < name.size() && lowerOrDigit(name[i + 1]) &&
                                    !(name[i + 1] >= '0' && name[i + 1] <= '9');
            if (afterLower || acronymEnd) out += '-';
        }
        out += upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

// Whether enums.hpp's enumerators of E are reflect-cpp's, name for name and value for value: an enum_param's labels and
// an enum settings field's JSON spellings are reflect-cpp's on the desktop and enums.hpp's on a device, so every
// desktop build that names one checks the two agree (enum_param's descriptor, a portable Settings' msgpack reading).
template <class E>
consteval bool enumMatchesReflect() {
    constexpr auto kReflected = rfl::get_enumerator_array<E>();
    if (kReflected.size() != Enumerators<E>::count) return false;
    for (std::size_t i = 0; i < kReflected.size(); ++i)
        if (kReflected[i].first != Enumerators<E>::name(i) || kReflected[i].second != Enumerators<E>::value(i))
            return false;
    return true;
}

// An enum param's descriptor entry: labels are the enumerator names in kebab-case, its default the default's index.
template <class E, fixed_string Name, E Default>
struct ParamDescOf<enum_param<E, Name, Default>> {
    static_assert(enumMatchesReflect<E>(), "enums.hpp reads this enum's enumerators differently from reflect-cpp");
    static graph::ParamDesc get(std::string id) {
        using En = Enumerators<E>;
        graph::ParamDesc d{std::move(id), std::string(Name.view()), graph::ParamType::Enum, 0.0, 0.0, 0.0, {}, {}};
        for (std::size_t i = 0; i < En::count; ++i) {
            if (En::value(i) == Default) d.def = static_cast<double>(i);
            d.values.push_back(kebabCase(En::name(i)));
        }
        d.max = static_cast<double>(En::count - 1);
        return d;
    }
};

// Every enum a portable Settings holds (a field, or a list's element) checked as enum_param's labels are.
template <class F>
consteval bool fieldEnumMatchesReflect() {
    if constexpr (IsList<F>::value) return fieldEnumMatchesReflect<typename F::value_type>();
    else if constexpr (std::is_enum_v<F>) return enumMatchesReflect<F>();
    else return true;
}
template <class S>
consteval bool settingsEnumsMatchReflect() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return (fieldEnumMatchesReflect<FieldType<S, I>>() && ...);
    }(std::make_index_sequence<kFieldCount<S>>{});
}

// ---- the description: layouts and descriptors ------------------------------------------------------------------

enum class Side { Inputs, Outputs };

template <Side W, class S, std::size_t I>
graph::PortDesc portDesc() {
    using P = FieldType<S, I>;
    if constexpr (W == Side::Inputs) static_assert(P::rp_dir != graph::PortDir::Out, "a port in `inputs` must be an input or a link");
    else static_assert(P::rp_dir == graph::PortDir::Out, "a port in `outputs` must be an output");
    graph::PortDesc d;
    d.id = std::string(fieldName<S, I>());
    d.kind = P::rp_kind;
    d.dir = P::rp_dir;
    if constexpr (requires { typename P::rp_channels; }) d.channels = ChannelSpecOf<typename P::rp_channels>::get();
    if constexpr (requires { P::rp_resource_type(); }) d.resourceType = std::string(P::rp_resource_type());
    if constexpr (requires { P::rp_write; }) d.write = P::rp_write;
    if constexpr (requires { P::rp_capacity; }) {
        if constexpr (P::rp_capacity.messages != 0) d.capacity.messages = P::rp_capacity.messages;
        if constexpr (P::rp_capacity.bytes != 0) d.capacity.bytes = P::rp_capacity.bytes;
    }
    return d;
}

template <class S, std::size_t I>
graph::ParamDesc paramDesc() {
    return ParamDescOf<FieldType<S, I>>::get(std::string(fieldName<S, I>()));
}

template <Side W, class S, std::size_t... I>
void appendPorts(std::vector<graph::PortDesc>& out, std::index_sequence<I...>) {
    (out.push_back(portDesc<W, S, I>()), ...);
}

template <class S, std::size_t... I>
void appendParams(std::vector<graph::ParamDesc>& out, std::index_sequence<I...>) {
    (out.push_back(paramDesc<S, I>()), ...);
}

template <Side W, class S>
void appendPortsOf(std::vector<graph::PortDesc>& out) {
    if constexpr (!std::is_same_v<S, NoPorts>) appendPorts<W, S>(out, std::make_index_sequence<kFieldCount<S>>{});
}

template <class S>
void appendParamsOf(std::vector<graph::ParamDesc>& out) {
    if constexpr (!std::is_same_v<S, NoPorts>) appendParams<S>(out, std::make_index_sequence<kFieldCount<S>>{});
}

template <class T>
graph::Layout reflectedLayout() {
    using In  = std::remove_cvref_t<decltype(inputsOf(std::declval<T&>()))>;
    using Out = std::remove_cvref_t<decltype(outputsOf(std::declval<T&>()))>;
    using Par = std::remove_cvref_t<decltype(paramsOf(std::declval<T&>()))>;
    graph::Layout l;
    appendPortsOf<Side::Inputs, In>(l.ports);
    appendPortsOf<Side::Outputs, Out>(l.ports);
    appendParamsOf<Par>(l.params);
    return l;
}

// Reflected ports and params bind by position, so adjust_layout may patch their specs and append, but each
// reflected one must keep its place, id, kind and direction. Throws (which every caller reports as invalid
// settings) when it did not.
inline void checkReflectedPrefix(const graph::Layout& reflected, const graph::Layout& adjusted) {
    auto side = [](const graph::Layout& l, bool outputs) {
        std::vector<const graph::PortDesc*> v;
        for (const auto& p : l.ports)
            if ((p.dir == graph::PortDir::Out) == outputs) v.push_back(&p);
        return v;
    };
    for (const bool outputs : {false, true}) {
        const auto want = side(reflected, outputs);
        const auto got = side(adjusted, outputs);
        for (std::size_t i = 0; i < want.size(); ++i)
            if (i >= got.size() || got[i]->id != want[i]->id || got[i]->kind != want[i]->kind || got[i]->dir != want[i]->dir)
                throw std::logic_error("adjust_layout moved, removed or re-kinded reflected port \"" + want[i]->id + "\"");
    }
    // An enum's labels too: enum_param::value() maps the index back through its compile-time list.
    for (std::size_t i = 0; i < reflected.params.size(); ++i)
        if (i >= adjusted.params.size() || adjusted.params[i].id != reflected.params[i].id ||
            adjusted.params[i].type != reflected.params[i].type ||
            (reflected.params[i].type == graph::ParamType::Enum && adjusted.params[i].values != reflected.params[i].values))
            throw std::logic_error("adjust_layout moved, removed, re-typed or relabelled reflected param \"" + reflected.params[i].id + "\"");
}

// Inputs first, then outputs, each in declaration order - adjust_layout appends after the reflected ones.
template <class T>
graph::Layout layoutFor([[maybe_unused]] const typename SettingsOf<T>::type& settings) {
    const graph::Layout reflected = reflectedLayout<T>();
    graph::Layout l = reflected;
    if constexpr (requires(graph::Layout& lr) { T::adjust_layout(lr, settings); }) T::adjust_layout(l, settings);
    std::stable_partition(l.ports.begin(), l.ports.end(),
                          [](const graph::PortDesc& p) { return p.dir != graph::PortDir::Out; });
    checkReflectedPrefix(reflected, l);
    // A capacity adjust_layout set from settings is refused here, so it is the settings that are invalid.
    for (const auto& p : l.ports)
        if (auto why = graph::checkCapacity(p)) throw std::invalid_argument(*why);
    return l;
}

// The hooks that name desktop types, checked where the desktop builds its table (checkHooks has the rest).
template <class T>
constexpr bool checkDescriptionHooks() {
    using S = typename SettingsOf<T>::type;
    if constexpr (requires { requires kIsFunction<decltype(&T::validate)>; })
        static_assert(requires(const S& s) { { T::validate(s) } -> std::same_as<std::optional<std::string>>; },
                      "validate must be `static std::optional<std::string> validate(const Settings&)`");
    if constexpr (requires { requires kIsFunction<decltype(&T::adjust_layout)>; })
        static_assert(requires(graph::Layout& l, const S& s) { T::adjust_layout(l, s); },
                      "adjust_layout must be `static void adjust_layout(rp::graph::Layout&, const Settings&)`");
    return true;
}

// ---- the instance ----------------------------------------------------------------------------------------------

// The desktop's instance keeps its settings on the heap, so a commit swaps one pointer on the audio thread and the old
// object is freed back on the control thread (release_settings).
template <class T>
struct Instance : rp_node {
    // The resources this instance's settings name, pinned at create (ResourceHold). Declared first so they are
    // released after the node, which may read one of them in its destructor.
    std::vector<ResourceHold>     resources;
    T                             node;
    typename SettingsOf<T>::type* settings = nullptr;
    std::string                   layoutJson;  // as created, which prepare compares against; empty if it reshapes
    std::span<std::byte>          extra;
    // The rebuild_key of the settings it was created with, which prepare compares against (empty without the
    // hook). create is its only writer: a commit cannot change it, since prepare answers rebuild for a different
    // key.
    std::optional<typename RebuildKeyOf<T>::type> key;
};

inline std::string_view settingsText(const rp_settings* s) {
    if (!s || !s->data || s->size == 0) return "{}";
    return {s->data, s->size};
}

inline rfl::Unexpected<rfl::Error> settingsError(std::string what) {
    return rfl::Unexpected<rfl::Error>(rfl::Error(std::move(what)));
}

// A settings object in either encoding: JSON through reflect-cpp, msgpack through the decode a device runs
// (settings_msgpack.hpp), which reads only a portable Settings; a type whose settings hold desktop types reads JSON
// alone. Validated the same way whichever it came as.
template <class T>
rfl::Result<typename SettingsOf<T>::type> parseSettings(const rp_settings* s) {
    using S = typename SettingsOf<T>::type;
    if (s && s->encoding == RP_SETTINGS_MSGPACK) {
        const void*       data = s->data;
        const std::size_t size = s->data ? s->size : 0;
        ErrorText         why;
        if constexpr (std::is_same_v<S, NoSettings>) {
            if (!decodeNoSettings(data, size, why)) return settingsError(std::string(why.view()));
            return S{};
        } else if constexpr (kPortableSettings<S>) {
            static_assert(settingsEnumsMatchReflect<S>(),
                          "enums.hpp reads an enum in these settings differently from reflect-cpp's JSON");
            S out{};
            if (!decodeSettings(data, size, out, why)) return settingsError(std::string(why.view()));
            if constexpr (requires(const S& st) { T::validate(st); }) {
                if (auto reason = T::validate(out)) return settingsError(*reason);
            }
            return out;
        } else {
            return settingsError("this node's settings hold types a device cannot read, so it reads JSON alone");
        }
    }
    if (s && s->encoding != RP_SETTINGS_JSON)
        return settingsError("settings encoding " + std::to_string(s->encoding) +
                             " is not supported; this node reads JSON or msgpack");
    if constexpr (std::is_same_v<S, NoSettings>) {
        auto g = graph::parseJson(settingsText(s));
        if (!g || !g->to_object()) return settingsError("settings must be a JSON object");
        return S{};
    } else {
        auto parsed = rfl::json::read<S, rfl::DefaultIfMissing>(settingsText(s));
        if constexpr (requires(const S& st) { T::validate(st); }) {
            if (parsed) {
                if (auto why = T::validate(*parsed)) return settingsError(*why);
            }
        }
        return parsed;
    }
}

template <class T>
std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }

// Put the instance's held handles into the resource fields of `s`, which prepare parsed from a new document, and
// report whether they are the same resources: a ref that moved is a rebuild, since the fresh instance pins what it
// names and this one keeps reading what it holds until its plan is reclaimed. Reads only what create fixed (the
// pins), never the committed settings, so a type whose prepare is stateless may still call it.
template <class T>
bool resolveResources(Instance<T>& inst, typename SettingsOf<T>::type& s) {
    bool same = true;
    std::size_t i = 0;
    forEachResourceField(s, [&](std::string_view, auto& field) {
        if (!same) return;
        if (i >= inst.resources.size() || inst.resources[i].named != field.id) {
            same = false;
            return;
        }
        field.resource = inst.resources[i].resource;
        ++i;
    });
    return same && i == inst.resources.size();
}

template <class T>
std::size_t layoutForFn(const rp_node_type*, const rp_settings* s, char* out, std::size_t outSize, rp_error* err) {
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return 0;
        }
        const std::string json = graph::layoutJson(layoutFor<T>(*parsed));
        if (out && json.size() < outSize) {
            std::memcpy(out, json.data(), json.size());
            out[json.size()] = '\0';
        }
        return json.size();
    } catch (const std::exception& e) {
        copyError(err, e.what());
    } catch (...) {
        copyError(err, "settings threw");
    }
    return 0;
}

template <class T>
bool activateFn([[maybe_unused]] rp_node* n, [[maybe_unused]] double sampleRate,
                [[maybe_unused]] std::uint32_t maxFrames, [[maybe_unused]] rp_error* err) {
    if constexpr (!requires(T& t) { t.activate(sampleRate, maxFrames); }) {
        return true;
    } else {
        try {
            if constexpr (std::is_same_v<decltype(self<T>(n)->node.activate(sampleRate, maxFrames)), bool>) {
                if (!self<T>(n)->node.activate(sampleRate, maxFrames)) {
                    copyError(err, "activate failed");
                    return false;
                }
            } else {
                self<T>(n)->node.activate(sampleRate, maxFrames);
            }
            return true;
        } catch (const std::exception& e) {
            copyError(err, e.what());
            return false;
        } catch (...) {
            copyError(err, "activate threw");
            return false;
        }
    }
}

template <class T>
void deactivateFn([[maybe_unused]] rp_node* n) {
    if constexpr (requires(T& t) { t.deactivate(); }) {
        try {
            self<T>(n)->node.deactivate();
        } catch (...) {
            // Nothing to report through: deactivate has no error channel, and it must not unwind the host.
        }
    }
}

// Compares against the instance's extra memory, its rebuild_key and, unless it reshapes, its layout, none of which
// changes after create; a node that reshapes never compares layouts, since the planner takes a reshape that keeps
// the layout as a swap. Only needs_rebuild reads the instance's current settings, which is safe because a host never
// prepares while a commit for the same instance may be in flight: GraphHost waits for a published commit to be
// adopted before it plans, but only for commits from types without RP_TYPE_STATELESS_PREPARE (awaitsAdoption). A C++
// node without stateless_prepare is such a type; a stateless_prepare type (every script node) is not waited for,
// which is why that flag rules needs_rebuild out. The rule is asserted on the call itself, so no form of
// needs_rebuild this would call (a template, an overload set, a callable static member) slips past it. A
// rebuild_key decides the same thing from the key create kept, so a type that rebuilds on a key may prepare
// statelessly.
template <class T>
rp_prepare_result prepareFn(rp_node* n, const rp_settings* s, void** prepared, rp_error* err) {
    using S = typename SettingsOf<T>::type;
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return RP_PREPARE_INVALID;
        }
        auto* inst = self<T>(n);
        bool rebuild = extraBytes<T>(*parsed) != inst->extra.size();
        if constexpr (!reshapesOf<T>())
            rebuild = rebuild || graph::layoutJson(layoutFor<T>(*parsed)) != inst->layoutJson;
        if constexpr (kHasRebuildKey<T>) {
            // create engages it for a keyed type, so this only ever fires if the invariant broke.
            if (!inst->key) throw std::logic_error("a keyed instance was created without its key");
            rebuild = rebuild || !(T::rebuild_key(*parsed) == *inst->key);
        }
        // A resource field that moved is a rebuild whatever the type's key says, and the one that stays puts this
        // instance's pins into the prepared settings (nothing is pinned or dropped here: only create and destroy do
        // that, both on the control thread). Resolved before needs_rebuild, and that hook is skipped on a moved ref, so
        // it never sees a settings object whose handle is null: a node that reads a resource there would otherwise
        // rebuild on every prepare.
        const bool resourcesMoved = !resolveResources(*inst, *parsed);
        if constexpr (requires(const S& a) { T::needs_rebuild(a, a); }) {
            static_assert(!statelessPrepareOf<T>(),
                          "a stateless_prepare node cannot have needs_rebuild, which reads its current settings");
            if (!resourcesMoved) rebuild = rebuild || T::needs_rebuild(*inst->settings, *parsed);
        }
        rebuild = rebuild || resourcesMoved;
        if (rebuild) return RP_PREPARE_REBUILD;
        *prepared = new S(std::move(*parsed));
        if constexpr (reshapesOf<T>()) return RP_PREPARE_RESHAPE;
        else return RP_PREPARE_SWAP;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return RP_PREPARE_INVALID;
    } catch (...) {
        copyError(err, "settings threw");
        return RP_PREPARE_INVALID;
    }
}

template <class T>
void commitFn(rp_node* n, void* prepared, void** old) noexcept {
    using S = typename SettingsOf<T>::type;
    auto* inst = self<T>(n);
    *old = inst->settings;
    inst->settings = static_cast<S*>(prepared);
    if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
}

template <class T>
void releaseFn(rp_node*, void* old) noexcept {
    delete static_cast<typename SettingsOf<T>::type*>(old);
}

template <class T>
inline constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
                                        &processFn<T>,  &prepareFn<T>,    &commitFn<T>,
                                        &releaseFn<T>,  &interfaceFn<T>};

// An empty descriptor when building it throws (adjust_layout on default settings, say), which the registry
// refuses with a reason rather than letting the exception reach it.
template <class T>
const char* descriptorFn(const rp_node_type*) {
    static const std::string kJson = []() -> std::string {
        try {
            using S = typename SettingsOf<T>::type;
            graph::TypeDescriptor d;
            d.id = T::id;
            d.version = versionOf<T>();
            d.name = nameOf<T>();
            d.category = categoryOf<T>();
            d.heap = heapOf<T>();
            d.threads = threadsOf<T>();
            d.unique = uniqueOf<T>();
            d.layout = layoutFor<T>(S{});
            if constexpr (!std::is_same_v<S, NoSettings>) d.settingsSchema = rfl::json::to_schema<S, rfl::DefaultIfMissing>();
            return graph::descriptorJson(d);
        } catch (...) {
            return std::string();
        }
    }();
    return kJson.c_str();
}

template <class T>
bool queryMemoryFn(const rp_node_type*, const rp_settings* s, rp_memory_req* out, rp_error* err) {
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return false;
        }
        const std::size_t extra = extraBytes<T>(*parsed);
        if (extra > std::numeric_limits<std::size_t>::max() - extraOffset<T>()) {
            copyError(err, "extra memory request is too large");
            return false;
        }
        out->size = extra ? extraOffset<T>() + extra : sizeof(Instance<T>);
        out->align = alignof(Instance<T>) > alignof(std::max_align_t) ? alignof(Instance<T>) : alignof(std::max_align_t);
        return true;
    } catch (const std::exception& e) {
        copyError(err, e.what());
        return false;
    } catch (...) {
        copyError(err, "settings threw");
        return false;
    }
}

template <class T>
rp_node* createFn(const rp_node_type*, [[maybe_unused]] const rp_host* host, void* memory, const rp_settings* s,
                  rp_error* err) {
    using S = typename SettingsOf<T>::type;
    Instance<T>* inst = nullptr;
    try {
        auto parsed = parseSettings<T>(s);
        if (!parsed) {
            copyError(err, parsed.error().what());
            return nullptr;
        }
        // Everything that can throw on the node's behalf (adjust_layout, rebuild_key) runs before the instance exists.
        // Only a node that does not reshape keeps its layout, for prepare to compare against.
        std::string layout;
        if constexpr (reshapesOf<T>()) (void)layoutFor<T>(*parsed);
        else layout = graph::layoutJson(layoutFor<T>(*parsed));
        std::optional<typename RebuildKeyOf<T>::type> key;
        if constexpr (kHasRebuildKey<T>) key.emplace(T::rebuild_key(*parsed));
        const std::size_t extra = extraBytes<T>(*parsed);
        inst = new (memory) Instance<T>{};
        inst->vt = &kVtable<T>;
        inst->layoutJson = std::move(layout);
        inst->key = std::move(key);
        inst->settings = new S(std::move(*parsed));
        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
        // The resources the settings name are pinned before any hook sees the settings, so on_settings and activate
        // read the handles; a resource that cannot be pinned fails the create, and with it the document.
        inst->resources.resize(kResourceFields<S>);
        ErrorText refused;
        if (!acquireResources(std::span<ResourceHold>(inst->resources), host, *inst->settings, &refused)) {
            copyError(err, refused.view());
        } else {
            if (extra) {
                inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
                std::memset(inst->extra.data(), 0, extra);
                if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
            }
            if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
            return inst;
        }
    } catch (const std::exception& e) {
        copyError(err, e.what());
    } catch (...) {
        copyError(err, "create threw");
    }
    // An allocation failure, or a resource that would not pin, after the instance exists: leave nothing behind.
    if (inst) {
        delete inst->settings;
        inst->~Instance<T>();
    }
    return nullptr;
}

// The node goes first: it may hold a pointer to its settings until its own destructor has run.
template <class T>
void destroyFn(rp_node* n) {
    auto* inst = self<T>(n);
    auto* settings = inst->settings;
    inst->~Instance<T>();
    delete settings;
}

template <class T>
inline constexpr rp_node_type kNodeType{T::id,           &descriptorFn<T>, &layoutForFn<T>,
                                        &queryMemoryFn<T>, &createFn<T>,   &destroyFn<T>,
                                        typeFlags<T>(),  nullptr};

}  // namespace detail

}  // namespace rp
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '7588,8190p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/node_desc.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/node_desc.hpp`
Expected: `ccc1a5aded49ea29514c79940dd1c30195a0a9c4`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/JsonToMsgpack.hpp`:

````cpp
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <rfl/Result.hpp>

namespace rp::graph {

// A node's settings, from the JSON a document carries to the msgpack a device reads (RP_SETTINGS_MSGPACK,
// docs/modular-graph-phase-e1-design.md section 6): the desktop half of the encoding, which E3's plan export writes
// its blobs with. Any JSON value converts, an object becoming a map with its keys in the order the JSON has them (the
// decode matches by name, so the order is irrelevant) and a repeated key written twice (the decode keeps the last, as
// JSON.parse does).
//
// A number stays the kind of number the JSON text spells: an integer literal is a msgpack int in its smallest form, and
// anything with a fraction or an exponent is a float64, 2.0 included. That is what keeps the two encodings refusing
// the same settings: the JSON reader takes only an integer literal for an integer field (2.0 is refused there), and the
// msgpack decode takes only an int for one. An integer past 64 bits is the double yyjson reads it as. Strings are str,
// never bin; null is nil. Refused: text that is not JSON, and nesting deeper than 512 levels.
rfl::Result<std::vector<std::uint8_t>> settingsToMsgpack(std::string_view json);

}  // namespace rp::graph
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '8202,8225p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/JsonToMsgpack.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/JsonToMsgpack.hpp`
Expected: `f65c2aea48510c2243a21b1fc20ce21ebdd628f2`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/node-sdk/native/src/graph/JsonToMsgpack.cpp`:

````cpp
#include "graph/JsonToMsgpack.hpp"

#include <cstring>
#include <string>

#include <yyjson.h>

namespace rp::graph {

namespace {

constexpr int kMaxDepth = 512;  // as CanonicalJson's parseJson: deeper is refused rather than recursed into

struct Writer {
    std::vector<std::uint8_t>& out;

    void byte(std::uint8_t b) { out.push_back(b); }
    void be(std::uint64_t v, int bytes) {
        for (int k = bytes - 1; k >= 0; --k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    }

    void uint(std::uint64_t v) {
        if (v <= 0x7f) byte(static_cast<std::uint8_t>(v));
        else if (v <= 0xff) byte(0xcc), be(v, 1);
        else if (v <= 0xffff) byte(0xcd), be(v, 2);
        else if (v <= 0xffffffffu) byte(0xce), be(v, 4);
        else byte(0xcf), be(v, 8);
    }
    void sint(std::int64_t v) {
        if (v >= 0) return uint(static_cast<std::uint64_t>(v));
        const auto bits = static_cast<std::uint64_t>(v);
        if (v >= -32) byte(static_cast<std::uint8_t>(bits));
        else if (v >= -128) byte(0xd0), be(bits, 1);
        else if (v >= -32768) byte(0xd1), be(bits, 2);
        else if (v >= -2147483648LL) byte(0xd2), be(bits, 4);
        else byte(0xd3), be(bits, 8);
    }
    void float64(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        byte(0xcb);
        be(bits, 8);
    }
    void str(const char* s, std::size_t n) {
        if (n <= 31) byte(static_cast<std::uint8_t>(0xa0 | n));
        else if (n <= 0xff) byte(0xd9), be(n, 1);
        else if (n <= 0xffff) byte(0xda), be(n, 2);
        else byte(0xdb), be(n, 4);
        out.insert(out.end(), s, s + n);
    }
    void header(std::size_t n, std::uint8_t fix, std::uint8_t wide16) {
        if (n <= 15) byte(static_cast<std::uint8_t>(fix | n));
        else if (n <= 0xffff) byte(wide16), be(n, 2);
        else byte(static_cast<std::uint8_t>(wide16 + 1)), be(n, 4);
    }

    // False only when the value nests deeper than kMaxDepth.
    bool value(yyjson_val* v, int depth) {
        if (depth > kMaxDepth) return false;
        switch (yyjson_get_type(v)) {
            case YYJSON_TYPE_BOOL: byte(yyjson_get_bool(v) ? 0xc3 : 0xc2); return true;
            case YYJSON_TYPE_NUM:
                if (yyjson_is_uint(v)) uint(yyjson_get_uint(v));
                else if (yyjson_is_sint(v)) sint(yyjson_get_sint(v));
                else float64(yyjson_get_real(v));
                return true;
            case YYJSON_TYPE_STR: str(yyjson_get_str(v), yyjson_get_len(v)); return true;
            case YYJSON_TYPE_ARR: {
                header(yyjson_arr_size(v), 0x90, 0xdc);
                yyjson_val*     item;
                yyjson_arr_iter it = yyjson_arr_iter_with(v);
                while ((item = yyjson_arr_iter_next(&it)))
                    if (!value(item, depth + 1)) return false;
                return true;
            }
            case YYJSON_TYPE_OBJ: {
                header(yyjson_obj_size(v), 0x80, 0xde);
                yyjson_val*     key;
                yyjson_obj_iter it = yyjson_obj_iter_with(v);
                while ((key = yyjson_obj_iter_next(&it))) {
                    str(yyjson_get_str(key), yyjson_get_len(key));
                    if (!value(yyjson_obj_iter_get_val(key), depth + 1)) return false;
                }
                return true;
            }
            case YYJSON_TYPE_NULL:
            default: byte(0xc0); return true;
        }
    }
};

}  // namespace

rfl::Result<std::vector<std::uint8_t>> settingsToMsgpack(std::string_view json) {
    yyjson_read_err err{};
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &err);
    if (!doc) return rfl::Unexpected<rfl::Error>(rfl::Error(std::string("not JSON: ") + err.msg));
    std::vector<std::uint8_t> out;
    out.reserve(json.size());
    const bool ok = Writer{out}.value(yyjson_doc_get_root(doc), 0);
    yyjson_doc_free(doc);
    if (!ok) {
        return rfl::Unexpected<rfl::Error>(
            rfl::Error("JSON nests deeper than " + std::to_string(kMaxDepth) + " levels"));
    }
    return out;
}

}  // namespace rp::graph
````

Run: `mkdir -p packages/node-sdk/native/src/graph; sed -n '8237,8345p' docs/modular-graph-phase-e1-plan.md > packages/node-sdk/native/src/graph/JsonToMsgpack.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/node-sdk/native/src/graph/JsonToMsgpack.cpp`
Expected: `e92b014656214671a83085d327a50c56a5fdadc3`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/node-sdk/native/CMakeLists.txt b/packages/node-sdk/native/CMakeLists.txt
index 2fb041df9..3f26f6855 100644
--- a/packages/node-sdk/native/CMakeLists.txt
+++ b/packages/node-sdk/native/CMakeLists.txt
@@ -6,15 +6,31 @@
 # Its include root, src/, is where every one of these keeps the path it is included by ("graph/node.hpp"), so a node's
 # source is the same whichever package it sits in. A pack depends on this package and nothing else.
 #
+# graph/node.hpp is built two ways (docs/modular-graph-phase-e1-design.md section 6): the desktop's, which links the
+# library below, and a device-mode build (RP_DEVICE, no exceptions, no RTTI), which is headers alone: the portable
+# core (graph/node_core.hpp, node_device.hpp and the msgpack settings decode) and reflect-cpp's compile-time field
+# reflection, which is header-only and allocates nothing. retroplug-device-mode-test builds it on the desktop.
+#
 # retroplug-node-sdk-headers: the include root alone, no library. What a target takes when it needs the ABI, the queue
 # views or the ring and must not link reflect-cpp: the no-heap executor (graph-runtime's retroplug-graph-exec), and the
 # device links' tests.
 add_library(retroplug-node-sdk-headers INTERFACE)
 target_include_directories(retroplug-node-sdk-headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
 
-# retroplug-node-sdk: the description library. Links reflect-cpp (settings parsing, JSON Schema).
+# retroplug-reflect-headers: reflect-cpp's include root alone, as SYSTEM headers, and no library: what a device-mode
+# build of the SDK takes for the compile-time field reflection (graph/fields.hpp), which is header-only and allocates
+# nothing. SYSTEM for the reason packages/native/cmake/warnings.cmake marks reflect-cpp so: its templates instantiate
+# in our translation units, and our warning level is not its. The device-mode test's objects and every package's
+# device check (cmake/packages.cmake) take it, and the device build will.
+add_library(retroplug-reflect-headers INTERFACE)
+target_include_directories(retroplug-reflect-headers SYSTEM INTERFACE
+    $<TARGET_PROPERTY:reflectcpp,INTERFACE_INCLUDE_DIRECTORIES>)
+
+# retroplug-node-sdk: the description library. Links reflect-cpp (settings parsing, JSON Schema), and holds the JSON to
+# msgpack settings converter (graph/JsonToMsgpack.hpp), the desktop half of RP_SETTINGS_MSGPACK.
 retroplug_package_library(retroplug-node-sdk
     ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/CanonicalJson.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/Descriptor.cpp
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphDocument.cpp)
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/GraphDocument.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/JsonToMsgpack.cpp)
 target_link_libraries(retroplug-node-sdk PUBLIC retroplug-node-sdk-headers reflectcpp::reflectcpp)
diff --git a/packages/node-sdk/native/src/graph/Descriptor.hpp b/packages/node-sdk/native/src/graph/Descriptor.hpp
index b56229adf..a3579b6e3 100644
--- a/packages/node-sdk/native/src/graph/Descriptor.hpp
+++ b/packages/node-sdk/native/src/graph/Descriptor.hpp
@@ -8,14 +8,15 @@
 
 #include <rfl/Result.hpp>
 
+#include "graph/PortTypes.hpp"
+
 namespace rp::graph {
 
 // The descriptor model: what a node type declares (TypeDescriptor) and what one instance resolves to
 // for its settings (Layout). Both travel across the C ABI as canonical JSON (CanonicalJson.hpp), and
-// this file is the one place that spells and parses that JSON on the C++ side.
-
-enum class PortKind : std::uint8_t { Audio, Events, Bytes, Control, Link, Resource };
-enum class PortDir : std::uint8_t { In, Out, Link };  // link ports are undirected
+// this file is the one place that spells and parses that JSON on the C++ side. The port kinds, the param types and
+// the bounds a port is sized by (the queue's default and most, kMaxChannels) are PortTypes.hpp's, which a device-mode
+// node reads with no descriptor model at all.
 
 struct ChannelSpec {
     enum class Mode : std::uint8_t { Fixed, Any, Match };
@@ -29,17 +30,7 @@ struct ChannelSpec {
     static ChannelSpec matching(std::string port) { return {Mode::Match, 0, {}, std::move(port)}; }
 };
 
-// An events or bytes output's queue: the most messages, and the most payload bytes, one block carries. A push past
-// either is dropped and counted (rp_queue::dropped). An output may declare either part or both, as
-// "capacity": {"messages": n, "bytes": n}; a part it leaves undeclared takes the default. A wire carries its
-// source's queue (an output queue, or a feedback wire's front, which is sized from the source too), so an input has
-// no queue of its own to size, and one that declares a capacity is refused rather than silently ignored.
-constexpr std::uint32_t kDefaultQueueMessages = 256;
-constexpr std::uint32_t kDefaultQueueBytes = 4096;
-// The most a port may declare, so a typo cannot allocate gigabytes.
-constexpr std::uint32_t kMaxQueueMessages = 1u << 20;  // 1 Mi
-constexpr std::uint32_t kMaxQueueBytes = 16u << 20;    // 16 MiB
-
+// An output's declared queue capacity (PortTypes.hpp says what one bounds).
 struct QueueCapacity {
     std::optional<std::uint32_t> messages;  // as declared: unset is the default, and is written as nothing
     std::optional<std::uint32_t> bytes;
@@ -57,8 +48,6 @@ struct PortDesc {
     QueueCapacity capacity;      // events / bytes outputs only
 };
 
-enum class ParamType : std::uint8_t { Float, Int, Enum, Bool };
-
 struct ParamDesc {
     std::string              id;
     std::string              name;
@@ -102,14 +91,6 @@ const ParamDesc* findParam(const Layout& layout, std::string_view id);
 
 std::string_view kindName(PortKind kind);
 
-// The most channels one audio port carries; the planner refuses a port that resolves to more.
-constexpr std::uint32_t kMaxChannels = 64;
-
-// The bytes a layout name is stored in by a node's settings: "mono", "stereo", "5.1", "7.1" and "discrete(N)" with N up
-// to kMaxChannels, whose longest spelling "discrete(64)" is twelve bytes; sixteen holds every name layoutChannels
-// accepts.
-constexpr std::size_t kMaxLayoutName = 16;
-
 // Why a channel count outside 1..kMaxChannels cannot be used, or nullopt. `what` names the thing in the message ("a
 // delay", "inputs together"), which is the part of a settings refusal a document's author reads.
 std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what);
diff --git a/packages/node-sdk/native/src/graph/GraphDocument.cpp b/packages/node-sdk/native/src/graph/GraphDocument.cpp
index fb6004682..5051ee13f 100644
--- a/packages/node-sdk/native/src/graph/GraphDocument.cpp
+++ b/packages/node-sdk/native/src/graph/GraphDocument.cpp
@@ -1,11 +1,10 @@
 #include "graph/GraphDocument.hpp"
 
-#include <charconv>
-
 #include <rfl/NoExtraFields.hpp>
 #include <rfl/from_generic.hpp>
 
 #include "graph/CanonicalJson.hpp"
+#include "graph/ResourceId.hpp"
 
 namespace rp::graph {
 
@@ -27,13 +26,8 @@ std::optional<Endpoint> parseEndpoint(std::string_view text) {
 }
 
 std::optional<std::uint32_t> parseResourceRef(std::string_view text) {
-    constexpr std::string_view kPrefix = "res:";
-    if (text.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
-    const auto digits = text.substr(kPrefix.size());
     std::uint32_t id = 0;
-    if (digits.empty() || digits[0] == '0') return std::nullopt;  // one spelling per id: no "res:007", no "res:0"
-    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
-    if (ec != std::errc() || ptr != digits.data() + digits.size()) return std::nullopt;
+    if (!parseResourceId(text, id)) return std::nullopt;
     return id;
 }
 
diff --git a/packages/node-sdk/native/src/graph/node.hpp b/packages/node-sdk/native/src/graph/node.hpp
index f0755e5c1..386784ade 100644
--- a/packages/node-sdk/native/src/graph/node.hpp
+++ b/packages/node-sdk/native/src/graph/node.hpp
@@ -25,6 +25,8 @@
 //                                                         JSON Schema $defs key comes from the type name). A
 //                                                         field of type rp::resource_ref<Type> names a resource
 //                                                         ("res:N"), which create pins and destroy releases
+//                                                         (settings_types.hpp has the fixed-capacity types a
+//                                                         device-built node's settings use)
 //   static std::optional<std::string> validate(const Settings&);      reject settings with a reason
 //   static void adjust_layout(rp::graph::Layout&, const Settings&);   patch reflected specs (a queue's capacity
 //                                                         among them), append ports or params; never remove or
@@ -69,1164 +71,64 @@
 //
 // process(const rp::Block&) is required, noexcept, and returns void or rp_status. Exceptions never cross
 // the ABI: every control-thread entry catches, and every audio-thread entry is noexcept.
-
-#include <algorithm>
-#include <concepts>
-#include <cstddef>
-#include <cstdint>
-#include <cstring>
-#include <exception>
-#include <limits>
-#include <map>
-#include <new>
-#include <optional>
-#include <span>
-#include <stdexcept>
-#include <string>
-#include <string_view>
-#include <type_traits>
-#include <utility>
-#include <vector>
-
-#include <rfl/DefaultIfMissing.hpp>
-#include <rfl/enums.hpp>
-#include <rfl/json.hpp>
-#include <rfl/named_tuple_t.hpp>
-#include <rfl/num_fields.hpp>
-#include <rfl/to_view.hpp>
-
-#include "graph/CanonicalJson.hpp"
-#include "graph/Descriptor.hpp"
-#include "graph/GraphDocument.hpp"
-#include "graph/Queue.hpp"
-#include "graph/rp_node.h"
-#include "graph/settings_types.hpp"
-
-namespace rp {
-
-template <std::size_t N>
-struct fixed_string {
-    char v[N]{};
-    constexpr fixed_string(const char (&s)[N]) {
-        for (std::size_t i = 0; i < N; ++i) v[i] = s[i];
-    }
-    constexpr std::string_view view() const { return {v, N - 1}; }
-};
-
-// ---- channel specs -----------------------------------------------------------------------------------
-
-struct any {};
-template <fixed_string Port> struct match {};
-template <std::uint32_t N, fixed_string LayoutName> struct channels {};
-using mono   = channels<1, "mono">;
-using stereo = channels<2, "stereo">;
-
-namespace detail {
-template <class S> struct ChannelSpecOf;
-template <> struct ChannelSpecOf<any> {
-    static graph::ChannelSpec get() { return graph::ChannelSpec::any(); }
-};
-template <fixed_string P> struct ChannelSpecOf<match<P>> {
-    static graph::ChannelSpec get() { return graph::ChannelSpec::matching(std::string(P.view())); }
-};
-template <std::uint32_t N, fixed_string L> struct ChannelSpecOf<channels<N, L>> {
-    static graph::ChannelSpec get() { return graph::ChannelSpec::fixed(N, std::string(L.view())); }
-};
-}  // namespace detail
-
-// ---- ports ---------------------------------------------------------------------------------------------
-
-template <class Spec>
-struct audio_in {
-    static constexpr auto rp_kind = graph::PortKind::Audio;
-    static constexpr auto rp_dir  = graph::PortDir::In;
-    using rp_channels = Spec;
-    const float* const* channels = nullptr;
-    std::uint32_t       channel_count = 0;
-    const float* operator[](std::uint32_t c) const { return channels[c]; }
-};
-
-template <class Spec>
-struct audio_out {
-    static constexpr auto rp_kind = graph::PortKind::Audio;
-    static constexpr auto rp_dir  = graph::PortDir::Out;
-    using rp_channels = Spec;
-    float* const* channels = nullptr;
-    std::uint32_t channel_count = 0;
-    float* operator[](std::uint32_t c) const { return channels[c]; }
-};
-
-// An events or bytes output's queue capacity (graph::QueueCapacity): the most messages, and payload bytes, one block
-// carries. A part left 0 takes the default (graph::kDefaultQueueMessages / kDefaultQueueBytes). It rides on the
-// port's type, rp::bytes_out::sized<rp::capacity{.bytes = 65536}>, or adjust_layout sets it from settings. A wire
-// carries its source's queue, so only an output has one: an input has no `sized` (naming it is a compile error),
-// and a capacity adjust_layout sets on one is refused with the node's settings.
-struct capacity {
-    std::uint32_t messages = 0;
-    std::uint32_t bytes = 0;
-};
-
-template <graph::PortKind Kind>
-struct queue_in {
-    static constexpr auto rp_kind = Kind;
-    static constexpr auto rp_dir  = graph::PortDir::In;
-    const rp_queue* queue = nullptr;
-    std::span<const rp_message> messages() const { return {queue->items, queue->count}; }
-    std::span<const std::uint8_t> data(const rp_message& m) const { return graph::messageData(*queue, m); }
-};
-
-template <graph::PortKind Kind, capacity C = capacity{}>
-struct queue_out {
-    static_assert(C.messages <= graph::kMaxQueueMessages && C.bytes <= graph::kMaxQueueBytes,
-                  "a queue capacity is at most graph::kMaxQueueMessages messages and graph::kMaxQueueBytes bytes");
-    static constexpr auto     rp_kind = Kind;
-    static constexpr auto     rp_dir  = graph::PortDir::Out;
-    static constexpr capacity rp_capacity = C;
-    template <capacity N> using sized = queue_out<Kind, N>;
-    rp_queue* queue = nullptr;
-    bool push(std::uint32_t frame, std::span<const std::uint8_t> data, std::uint32_t flags = 0) const {
-        return graph::pushMessage(*queue, frame, data, flags);
-    }
-};
-
-using events_in  = queue_in<graph::PortKind::Events>;
-using events_out = queue_out<graph::PortKind::Events>;
-using bytes_in   = queue_in<graph::PortKind::Bytes>;
-using bytes_out  = queue_out<graph::PortKind::Bytes>;
-
-struct control_in {
-    static constexpr auto rp_kind = graph::PortKind::Control;
-    static constexpr auto rp_dir  = graph::PortDir::In;
-    const rp_control_queue* queue = nullptr;
-    double start() const { return queue->start; }
-    std::span<const rp_control_change> changes() const { return {queue->changes, queue->count}; }
-};
-
-struct control_out {
-    static constexpr auto rp_kind = graph::PortKind::Control;
-    static constexpr auto rp_dir  = graph::PortDir::Out;
-    rp_control_queue* queue = nullptr;
-    bool push(std::uint32_t frame, double value) const { return graph::pushControl(*queue, frame, value); }
-};
-
-template <fixed_string Type>
-struct resource_in {
-    static constexpr auto rp_kind = graph::PortKind::Resource;
-    static constexpr auto rp_dir  = graph::PortDir::In;
-    static constexpr std::string_view rp_resource_type() { return Type.view(); }
-    rp_resource* const* slot = nullptr;
-    const rp_resource* get() const { return slot ? *slot : nullptr; }
-};
-
-// A resource input the node WRITES (a record buffer). At most one write port may be bound to a given
-// mutable resource, and readers must be wired from the writer's output so the planner orders them.
-template <fixed_string Type>
-struct resource_write {
-    static constexpr auto rp_kind = graph::PortKind::Resource;
-    static constexpr auto rp_dir  = graph::PortDir::In;
-    static constexpr bool rp_write = true;
-    static constexpr std::string_view rp_resource_type() { return Type.view(); }
-    rp_resource* const* slot = nullptr;
-    rp_resource* get() const { return slot ? *slot : nullptr; }
-};
-
-template <fixed_string Type>
-struct resource_out {
-    static constexpr auto rp_kind = graph::PortKind::Resource;
-    static constexpr auto rp_dir  = graph::PortDir::Out;
-    static constexpr std::string_view rp_resource_type() { return Type.view(); }
-    rp_resource** slot = nullptr;
-    void set(rp_resource* r) const { *slot = r; }
-};
-
-struct link {
-    static constexpr auto rp_kind = graph::PortKind::Link;
-    static constexpr auto rp_dir  = graph::PortDir::Link;
-};
-
-// A TOP-LEVEL settings field that names a resource: the document writes "res:N" and the node reads the bytes without
-// ever touching the filesystem (docs/modular-graph-phase-c3-design.md section 4). The authoring layer takes the pin when
-// it creates the instance and drops it when the instance is destroyed, both on the control thread (the same pin
-// docs/modular-graph-design.md section 2.5 gives a node that references a resource from its settings). A field whose ref
-// moved is a REBUILD, which node.hpp decides itself: the fresh instance pins what it names and the old pin drops with the
-// instance the last plan ran. So a resource changes under a node only by a rebuild, and no pin is ever taken or dropped
-// on the audio thread.
-//
-// Create pins before any hook that sees the instance's settings runs, so `on_settings`, `activate`, `process`, `step` and
-// `finish` read a resolved handle (and a prepare that keeps the refs copies the instance's pins into the settings object
-// it installs). The hooks that run before create has pinned anything (`validate`, `adjust_layout`, `rebuild_key`,
-// `extra_memory`, `query_memory`) see only `id` (and `empty()`): a node must not read `bytes()` or `audio()` there. `needs_rebuild`
-// sees handles in both objects, and is skipped for a ref that moved, which is a rebuild on its own.
 //
-//   struct Settings { rp::resource_ref<"rom"> rom; };   // JSON: {"rom": "res:7"}; absent: the node names none
-//
-// The type is the resource type an id must have (the host's putResource makes "rom", "sram", "state"); a resource
-// of another type, an unknown or dropped id, or a host with no table refuses the create with the field's name.
-template <fixed_string Type>
-struct resource_ref {
-    // rfl reads and writes the ref as the plain string the document carries, never the handle it resolves to, and its
-    // schema is that string inline (the rfl::parsing::Parser specialization at the end of this file). The id is held
-    // as the number, so a settings object owns nothing on the heap for it.
-    static constexpr std::string_view resource_type() { return Type.view(); }
-
-    std::uint32_t      id = 0;              // N of the document's "res:N" (0: the document names none)
-    const rp_resource* resource = nullptr;  // resolved by the host layer at create, and again at each prepare
-
-    bool empty() const { return id == 0; }
-    // The document's spelling of the id, for an error message: "res:N", or empty for none.
-    std::string ref() const { return id ? "res:" + std::to_string(id) : std::string(); }
-
-    // The payload of a bytes resource (every type but "audio-buffer"), or null when the field names none.
-    const rp_bytes* bytes() const {
-        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") != 0
-                   ? static_cast<const rp_bytes*>(resource->payload)
-                   : nullptr;
-    }
-    // The payload of an "audio-buffer" resource, or null when the field names none or names another type.
-    const rp_audio_buffer* audio() const {
-        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") == 0
-                   ? static_cast<const rp_audio_buffer*>(resource->payload)
-                   : nullptr;
-    }
-};
-
-// ---- params ----------------------------------------------------------------------------------------------
-
-struct range {
-    double min = 0.0, max = 1.0, init = 0.0;
-};
-
-namespace detail {
-struct param_base {
-    const rp_control_queue* queue = nullptr;
-    // The value at block start, and this block's frame-stamped changes (sample-accurate nodes walk them).
-    double start() const { return queue->start; }
-    std::span<const rp_control_change> changes() const { return {queue->changes, queue->count}; }
-    double latest() const { return graph::controlEnd(*queue); }
-};
-}  // namespace detail
-
-template <fixed_string Name, range R = range{}, fixed_string Unit = "">
-struct float_param : detail::param_base {
-    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max, "a param range needs min <= init <= max");
-    static graph::ParamDesc rp_param(std::string id) {
-        return {std::move(id), std::string(Name.view()), graph::ParamType::Float, R.min, R.max, R.init, {},
-                std::string(Unit.view())};
-    }
-};
-
-template <fixed_string Name, range R = range{}, fixed_string Unit = "">
-struct int_param : detail::param_base {
-    static_assert(R.min <= R.max && R.min <= R.init && R.init <= R.max && R.min >= -2147483648.0 &&
-                      R.max <= 2147483647.0 && R.min == static_cast<double>(static_cast<int>(R.min)) &&
-                      R.max == static_cast<double>(static_cast<int>(R.max)) &&
-                      R.init == static_cast<double>(static_cast<int>(R.init)),
-                  "an int param's range is whole numbers with min <= init <= max, within int");
-    static graph::ParamDesc rp_param(std::string id) {
-        return {std::move(id), std::string(Name.view()), graph::ParamType::Int, R.min, R.max, R.init, {},
-                std::string(Unit.view())};
-    }
-    // The value at block start, rounded to the nearest int (half away from zero); NaN reads as the declared
-    // default and anything beyond int saturates. It is NOT clamped to the declared range: a wire can carry
-    // any double and adjust_layout may widen the range, so a node clamps to its own resolved range where
-    // that matters (Delay does).
-    int value() const {
-        const double v = start();
-        if (!(v == v)) return static_cast<int>(R.init);
-        if (v <= -2147483648.0) return std::numeric_limits<int>::min();
-        if (v >= 2147483647.0) return std::numeric_limits<int>::max();
-        return static_cast<int>(v + (v < 0 ? -0.5 : 0.5));
-    }
-};
-
-template <fixed_string Name, bool Default = false>
-struct bool_param : detail::param_base {
-    static graph::ParamDesc rp_param(std::string id) {
-        return {std::move(id), std::string(Name.view()), graph::ParamType::Bool, 0.0, 1.0, Default ? 1.0 : 0.0, {}, {}};
-    }
-    bool value() const { return start() >= 0.5; }  // NaN reads as false
-};
-
-namespace detail {
-// Enumerator names become kebab-case labels - MidiSync -> "midi-sync", HighPassTPDF -> "high-pass-tpdf",
-// N8Fifo -> "n8-fifo" - so C++ and TS enums share one label convention in saved state.
-inline std::string kebabCase(std::string_view name) {
-    std::string out;
-    auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
-    auto lowerOrDigit = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
-    for (std::size_t i = 0; i < name.size(); ++i) {
-        const char c = name[i];
-        if (i > 0 && upper(c)) {
-            const bool afterLower = lowerOrDigit(name[i - 1]);
-            const bool acronymEnd = upper(name[i - 1]) && i + 1 < name.size() && lowerOrDigit(name[i + 1]) &&
-                                    !(name[i + 1] >= '0' && name[i + 1] <= '9');
-            if (afterLower || acronymEnd) out += '-';
-        }
-        out += upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
-    }
-    return out;
-}
-
-template <class E, E Value>
-constexpr bool isEnumerator() {
-    for (const auto& entry : rfl::get_enumerator_array<E>())
-        if (entry.second == Value) return true;
-    return false;
-}
-}  // namespace detail
-
-// An enum param: labels are the enumerator names in kebab-case, the runtime value is the index, rounded
-// and clamped so any control can drive it (NaN reads as the default).
-template <class E, fixed_string Name, E Default>
-struct enum_param : detail::param_base {
-    static constexpr auto kEnumerators = rfl::get_enumerator_array<E>();
-    static_assert(kEnumerators.size() > 0, "an enum param needs at least one enumerator");
-    static_assert(detail::isEnumerator<E, Default>(), "an enum param's default must be one of its enumerators");
-
-    static graph::ParamDesc rp_param(std::string id) {
-        graph::ParamDesc d{std::move(id), std::string(Name.view()), graph::ParamType::Enum, 0.0, 0.0, 0.0, {}, {}};
-        for (const auto& [label, e] : kEnumerators) {
-            if (e == Default) d.def = static_cast<double>(d.values.size());
-            d.values.push_back(detail::kebabCase(label));
-        }
-        d.max = static_cast<double>(d.values.size() - 1);
-        return d;
-    }
-    E value() const {
-        const double v = start();
-        if (!(v == v)) return Default;
-        const auto last = kEnumerators.size() - 1;
-        std::size_t i = 0;
-        if (v >= static_cast<double>(last)) i = last;
-        else if (v > 0.0) i = static_cast<std::size_t>(v + 0.5);
-        return kEnumerators[i].second;
-    }
-};
-
-// ---- the block -------------------------------------------------------------------------------------------
-
-struct Block {
-    const rp_process& raw;
-    std::uint32_t frames() const { return raw.frames; }
-    const rp_transport& transport() const { return *raw.transport; }
-    // Ports and params beyond the reflected ones (appended by adjust_layout) are reached by index here.
-    const rp_port_view& input(std::uint32_t i) const { return raw.inputs[i]; }
-    const rp_port_view& output(std::uint32_t i) const { return raw.outputs[i]; }
-    const rp_control_queue& param(std::uint32_t i) const { return *raw.params[i]; }
-};
-
-// ---- reflection glue ---------------------------------------------------------------------------------------
-
-namespace detail {
-
-struct NoSettings {};
-struct NoPorts {};
-
-template <class T> struct SettingsOf { using type = NoSettings; };
-template <class T> requires requires { typename T::Settings; } struct SettingsOf<T> { using type = typename T::Settings; };
-
-template <class T> constexpr bool reshapesOf() {
-    if constexpr (requires { T::reshapes; }) return T::reshapes; else return false;
-}
-template <class T> constexpr bool statelessPrepareOf() {
-    if constexpr (requires { T::stateless_prepare; }) return T::stateless_prepare; else return false;
-}
-
-// What rebuild_key returns, or NoKey for a type without one. Detected by the call, as needs_rebuild is in prepareFn,
-// so the key the instance keeps and the key prepare compares are found the same way.
-struct NoKey {};
-template <class T> struct RebuildKeyOf { using type = NoKey; };
-template <class T>
-    requires requires(const typename SettingsOf<T>::type& s) { T::rebuild_key(s); }
-struct RebuildKeyOf<T> {
-    using type = std::remove_cvref_t<decltype(T::rebuild_key(std::declval<const typename SettingsOf<T>::type&>()))>;
-};
-template <class T> inline constexpr bool kHasRebuildKey = !std::is_same_v<typename RebuildKeyOf<T>::type, NoKey>;
-
-template <class T> auto& inputsOf(T& t) { if constexpr (requires { t.inputs; }) return t.inputs; else { static NoPorts none; return none; } }
-template <class T> auto& outputsOf(T& t) { if constexpr (requires { t.outputs; }) return t.outputs; else { static NoPorts none; return none; } }
-template <class T> auto& paramsOf(T& t) { if constexpr (requires { t.params; }) return t.params; else { static NoPorts none; return none; } }
-
-// The one place fields are walked (design 4.7's fields_of seam): swapping reflect-cpp's field reflection
-// for another one (standalone PFR) touches only these four.
-template <class S> using FieldsOf = typename rfl::named_tuple_t<S>::Fields;
-template <class S> inline constexpr std::size_t kFieldCount = rfl::num_fields<S>;
-template <class S, class F>
-void forEachField(S& s, F&& f) noexcept {
-    rfl::to_view(s).apply([&](auto& field) { f(*field.value()); });
-}
-// The same walk with each field's JSON name, for the settings fields that name a resource: each refuses with a reason
-// that names the field. Not `noexcept`, unlike forEachField, whose bodies only bind ports and params: what runs inside
-// this one builds std::strings and the pins it takes, so an allocation that fails there has to reach the catch around
-// a create or a prepare, which turns it into that node's refusal. A `noexcept` here makes it a terminate instead.
-template <class S, class F>
-void forEachNamedField(S& s, F&& f) {
-    rfl::to_view(s).apply([&](auto&... fields) { (f(std::string_view(fields.name()), *fields.value()), ...); });
-}
-
-enum class Side { Inputs, Outputs };
-
-template <Side W, class Field>
-graph::PortDesc portDesc() {
-    using P = std::remove_cvref_t<typename Field::Type>;
-    if constexpr (W == Side::Inputs) static_assert(P::rp_dir != graph::PortDir::Out, "a port in `inputs` must be an input or a link");
-    else static_assert(P::rp_dir == graph::PortDir::Out, "a port in `outputs` must be an output");
-    graph::PortDesc d;
-    d.id = std::string(Field::name());
-    d.kind = P::rp_kind;
-    d.dir = P::rp_dir;
-    if constexpr (requires { typename P::rp_channels; }) d.channels = ChannelSpecOf<typename P::rp_channels>::get();
-    if constexpr (requires { P::rp_resource_type(); }) d.resourceType = std::string(P::rp_resource_type());
-    if constexpr (requires { P::rp_write; }) d.write = P::rp_write;
-    if constexpr (requires { P::rp_capacity; }) {
-        if constexpr (P::rp_capacity.messages != 0) d.capacity.messages = P::rp_capacity.messages;
-        if constexpr (P::rp_capacity.bytes != 0) d.capacity.bytes = P::rp_capacity.bytes;
-    }
-    return d;
-}
-
-template <class Field>
-graph::ParamDesc paramDesc() {
-    using P = std::remove_cvref_t<typename Field::Type>;
-    return P::rp_param(std::string(Field::name()));
-}
-
-template <Side W, class S, std::size_t... I>
-void appendPorts(std::vector<graph::PortDesc>& out, std::index_sequence<I...>) {
-    (out.push_back(portDesc<W, rfl::tuple_element_t<I, FieldsOf<S>>>()), ...);
-}
-
-template <class S, std::size_t... I>
-void appendParams(std::vector<graph::ParamDesc>& out, std::index_sequence<I...>) {
-    (out.push_back(paramDesc<rfl::tuple_element_t<I, FieldsOf<S>>>()), ...);
-}
-
-template <Side W, class S>
-void appendPortsOf(std::vector<graph::PortDesc>& out) {
-    if constexpr (!std::is_same_v<S, NoPorts>) appendPorts<W, S>(out, std::make_index_sequence<kFieldCount<S>>{});
-}
-
-template <class S>
-void appendParamsOf(std::vector<graph::ParamDesc>& out) {
-    if constexpr (!std::is_same_v<S, NoPorts>) appendParams<S>(out, std::make_index_sequence<kFieldCount<S>>{});
-}
-
-template <class T>
-graph::Layout reflectedLayout() {
-    using In  = std::remove_cvref_t<decltype(inputsOf(std::declval<T&>()))>;
-    using Out = std::remove_cvref_t<decltype(outputsOf(std::declval<T&>()))>;
-    using Par = std::remove_cvref_t<decltype(paramsOf(std::declval<T&>()))>;
-    graph::Layout l;
-    appendPortsOf<Side::Inputs, In>(l.ports);
-    appendPortsOf<Side::Outputs, Out>(l.ports);
-    appendParamsOf<Par>(l.params);
-    return l;
-}
-
-// Reflected ports and params bind by position, so adjust_layout may patch their specs and append, but each
-// reflected one must keep its place, id, kind and direction. Throws (which every caller reports as invalid
-// settings) when it did not.
-inline void checkReflectedPrefix(const graph::Layout& reflected, const graph::Layout& adjusted) {
-    auto side = [](const graph::Layout& l, bool outputs) {
-        std::vector<const graph::PortDesc*> v;
-        for (const auto& p : l.ports)
-            if ((p.dir == graph::PortDir::Out) == outputs) v.push_back(&p);
-        return v;
-    };
-    for (const bool outputs : {false, true}) {
-        const auto want = side(reflected, outputs);
-        const auto got = side(adjusted, outputs);
-        for (std::size_t i = 0; i < want.size(); ++i)
-            if (i >= got.size() || got[i]->id != want[i]->id || got[i]->kind != want[i]->kind || got[i]->dir != want[i]->dir)
-                throw std::logic_error("adjust_layout moved, removed or re-kinded reflected port \"" + want[i]->id + "\"");
-    }
-    // An enum's labels too: enum_param::value() maps the index back through its compile-time list.
-    for (std::size_t i = 0; i < reflected.params.size(); ++i)
-        if (i >= adjusted.params.size() || adjusted.params[i].id != reflected.params[i].id ||
-            adjusted.params[i].type != reflected.params[i].type ||
-            (reflected.params[i].type == graph::ParamType::Enum && adjusted.params[i].values != reflected.params[i].values))
-            throw std::logic_error("adjust_layout moved, removed, re-typed or relabelled reflected param \"" + reflected.params[i].id + "\"");
-}
-
-// Inputs first, then outputs, each in declaration order - adjust_layout appends after the reflected ones.
-template <class T>
-graph::Layout layoutFor([[maybe_unused]] const typename SettingsOf<T>::type& settings) {
-    const graph::Layout reflected = reflectedLayout<T>();
-    graph::Layout l = reflected;
-    if constexpr (requires(graph::Layout& lr) { T::adjust_layout(lr, settings); }) T::adjust_layout(l, settings);
-    std::stable_partition(l.ports.begin(), l.ports.end(),
-                          [](const graph::PortDesc& p) { return p.dir != graph::PortDir::Out; });
-    checkReflectedPrefix(reflected, l);
-    // A capacity adjust_layout set from settings is refused here, so it is the settings that are invalid.
-    for (const auto& p : l.ports)
-        if (auto why = graph::checkCapacity(p)) throw std::invalid_argument(*why);
-    return l;
-}
-
-template <class S>
-void bindPorts([[maybe_unused]] S& ports, [[maybe_unused]] const rp_port_view* views) noexcept {
-    if constexpr (!std::is_same_v<S, NoPorts>) {
-        std::uint32_t i = 0;
-        forEachField(ports, [&](auto& port) {
-            using P = std::remove_cvref_t<decltype(port)>;
-            const rp_port_view& v = views[i++];
-            if constexpr (P::rp_kind == graph::PortKind::Audio) {
-                port.channels = v.u.audio.channels;
-                port.channel_count = v.u.audio.channel_count;
-            } else if constexpr (P::rp_kind == graph::PortKind::Events || P::rp_kind == graph::PortKind::Bytes) {
-                port.queue = v.u.queue;
-            } else if constexpr (P::rp_kind == graph::PortKind::Control) {
-                port.queue = v.u.control;
-            } else if constexpr (P::rp_kind == graph::PortKind::Resource) {
-                port.slot = v.u.resource;
-            }
-        });
-    }
-}
-
-template <class S>
-void bindParams([[maybe_unused]] S& params, [[maybe_unused]] rp_control_queue* const* queues) noexcept {
-    if constexpr (!std::is_same_v<S, NoPorts>) {
-        std::uint32_t i = 0;
-        forEachField(params, [&](auto& param) { param.queue = queues[i++]; });
-    }
-}
-
-// ---- compile-time checks of the optional hooks ---------------------------------------------------------------
-
-// A hook is a function; a data member that happens to share its name is not one.
-template <class P>
-inline constexpr bool kIsFunction = std::is_member_function_pointer_v<P> || std::is_function_v<std::remove_pointer_t<P>>;
-
-template <class T>
-constexpr bool checkHooks() {
-    using S = typename SettingsOf<T>::type;
-    static_assert(requires(T& t, const Block& b) { { t.process(b) } noexcept; },
-                  "a node needs `process(const rp::Block&) noexcept`");
-    static_assert(requires(T& t, const Block& b) { { t.process(b) } -> std::same_as<void>; } ||
-                      requires(T& t, const Block& b) { { t.process(b) } -> std::same_as<rp_status>; },
-                  "process returns void or rp_status");
-    if constexpr (requires { requires kIsFunction<decltype(&T::validate)>; })
-        static_assert(requires(const S& s) { { T::validate(s) } -> std::same_as<std::optional<std::string>>; },
-                      "validate must be `static std::optional<std::string> validate(const Settings&)`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::adjust_layout)>; })
-        static_assert(requires(graph::Layout& l, const S& s) { T::adjust_layout(l, s); },
-                      "adjust_layout must be `static void adjust_layout(rp::graph::Layout&, const Settings&)`");
-    // stateless_prepare against needs_rebuild is checked in prepareFn, where it is called.
-    if constexpr (requires { requires kIsFunction<decltype(&T::needs_rebuild)>; })
-        static_assert(requires(const S& s) { { T::needs_rebuild(s, s) } -> std::same_as<bool>; },
-                      "needs_rebuild must be `static bool needs_rebuild(const Settings&, const Settings&)`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::rebuild_key)>; }) {
-        static_assert(requires(const S& s) { T::rebuild_key(s); },
-                      "rebuild_key must be `static Key rebuild_key(const Settings&)`");
-        // Only a key the call really gives has a type to check: a signature near-miss falls back to NoKey here.
-        if constexpr (requires(const S& s) { T::rebuild_key(s); }) {
-            using K = typename RebuildKeyOf<T>::type;
-            static_assert(!std::is_void_v<K> && std::copy_constructible<K> && std::equality_comparable<K>,
-                          "rebuild_key's Key must be copyable and compare with ==");
-        }
-    }
-    static_assert(!(kHasRebuildKey<T> && requires(const S& s) { T::needs_rebuild(s, s); }),
-                  "a node has needs_rebuild or rebuild_key, not both: the key already decides what rebuilds");
-    if constexpr (requires { requires kIsFunction<decltype(&T::extra_memory)>; })
-        static_assert(requires(const S& s) { { T::extra_memory(s) } -> std::same_as<std::size_t>; },
-                      "extra_memory must be `static std::size_t extra_memory(const Settings&)`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::attach)>; })
-        static_assert(requires(T& t, std::span<std::byte> m) { { t.attach(m) } noexcept; },
-                      "attach must be `void attach(std::span<std::byte>) noexcept`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::on_settings)>; })
-        static_assert(requires(T& t, const S& s) { { t.on_settings(s) } noexcept; },
-                      "on_settings must be `void on_settings(const Settings&) noexcept`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::bind_host)>; })
-        static_assert(requires(T& t, const rp_host* h) { { t.bind_host(h) } noexcept; },
-                      "bind_host must be `void bind_host(const rp_host*) noexcept`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::activate)>; })
-        static_assert(requires(T& t) { { t.activate(1.0, 1u) } -> std::same_as<bool>; } ||
-                          requires(T& t) { { t.activate(1.0, 1u) } -> std::same_as<void>; },
-                      "activate must be `bool activate(double, std::uint32_t)` or return void");
-    if constexpr (requires { requires kIsFunction<decltype(&T::deactivate)>; })
-        static_assert(requires(T& t) { t.deactivate(); }, "deactivate must be `void deactivate()`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::ports_changed)>; })
-        static_assert(requires(T& t, std::span<const std::uint8_t> c) { { t.ports_changed(c) } noexcept; },
-                      "ports_changed must be `void ports_changed(std::span<const std::uint8_t>) noexcept`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::set_peers)>; })
-        static_assert(requires(T& t, std::span<rp_node* const> p) { { t.set_peers(p) } noexcept; },
-                      "set_peers must be `void set_peers(std::span<rp_node* const>) noexcept`");
-    if constexpr (requires { requires kIsFunction<decltype(&T::get_interface)>; })
-        static_assert(
-            requires(T& t, std::string_view id) { { t.get_interface(id) } noexcept -> std::same_as<const void*>; },
-            "get_interface must be `const void* get_interface(std::string_view) noexcept`");
-    constexpr bool anyStep = requires { requires kIsFunction<decltype(&T::prepare)>; } ||
-                             requires { requires kIsFunction<decltype(&T::step)>; } ||
-                             requires { requires kIsFunction<decltype(&T::finish)>; };
-    if constexpr (anyStep) {
-        static_assert(requires(T& t, const Block& b) { { t.prepare(b) } noexcept; { t.finish(b) } noexcept; },
-                      "rp.step/1 needs `prepare(const rp::Block&) noexcept` and `finish(const rp::Block&) noexcept`");
-        static_assert(requires(T& t, std::uint32_t f) { { t.step(f) } noexcept -> std::same_as<bool>; } ||
-                          requires(T& t, std::uint32_t f) { { t.step(f) } noexcept -> std::same_as<rp_step_result>; },
-                      "rp.step/1 needs `step(std::uint32_t) noexcept` returning bool or rp_step_result");
-    }
-    return true;
-}
-
-// ---- the ABI table ---------------------------------------------------------------------------------------------
-
-// One pin an instance holds on a resource its settings name, and how to drop it: the host's table, by id. A field
-// that names no resource still holds its place, so the pins line up with the settings' resource fields in order.
-struct ResourceHold {
-    const rp_host_resources* service = nullptr;
-    std::uint32_t            named = 0;   // the id the settings named (0: none), so prepare can tell whether it moved
-    std::uint32_t            id = 0;      // the id pinned: equal to `named` while a pin is held, 0 after drop
-    const rp_resource*       resource = nullptr;
-
-    ResourceHold() = default;
-    ResourceHold(const ResourceHold&) = delete;
-    ResourceHold& operator=(const ResourceHold&) = delete;
-    ResourceHold(ResourceHold&& other) noexcept
-        : service(std::exchange(other.service, nullptr)),
-          named(other.named),
-          id(std::exchange(other.id, 0)),
-          resource(std::exchange(other.resource, nullptr)) {}
-    ResourceHold& operator=(ResourceHold&& other) noexcept {
-        if (this != &other) {
-            drop();
-            service = std::exchange(other.service, nullptr);
-            named = other.named;
-            id = std::exchange(other.id, 0);
-            resource = std::exchange(other.resource, nullptr);
-        }
-        return *this;
-    }
-    ~ResourceHold() { drop(); }
-
-    void drop() noexcept {
-        if (service && id) service->release(service->ctx, id);
-        service = nullptr;
-        id = 0;
-        resource = nullptr;
-    }
-};
-
-template <class T>
-struct Instance : rp_node {
-    // The resources this instance's settings name, pinned at create (ResourceHold). Declared first so they are
-    // released after the node, which may read one of them in its destructor.
-    std::vector<ResourceHold>     resources;
-    T                             node;
-    typename SettingsOf<T>::type* settings = nullptr;
-    std::string                   layoutJson;  // as created, which prepare compares against; empty if it reshapes
-    std::span<std::byte>          extra;
-    // The rebuild_key of the settings it was created with, which prepare compares against (empty without the
-    // hook). create is its only writer: a commit cannot change it, since prepare answers rebuild for a different
-    // key.
-    std::optional<typename RebuildKeyOf<T>::type> key;
-};
-
-inline void copyError(rp_error* err, std::string_view what) {
-    if (!err) return;
-    const std::size_t n = what.size() < sizeof err->message - 1 ? what.size() : sizeof err->message - 1;
-    std::memcpy(err->message, what.data(), n);
-    err->message[n] = '\0';
-}
-
-inline std::string_view settingsText(const rp_settings* s) {
-    if (!s || !s->data || s->size == 0) return "{}";
-    return {s->data, s->size};
-}
-
-inline rfl::Unexpected<rfl::Error> settingsError(std::string what) {
-    return rfl::Unexpected<rfl::Error>(rfl::Error(std::move(what)));
-}
-
-template <class T>
-rfl::Result<typename SettingsOf<T>::type> parseSettings(const rp_settings* s) {
-    using S = typename SettingsOf<T>::type;
-    if (s && s->encoding != RP_SETTINGS_JSON) return settingsError("settings encoding " + std::to_string(s->encoding) + " is not supported; this node reads JSON");
-    if constexpr (std::is_same_v<S, NoSettings>) {
-        auto g = graph::parseJson(settingsText(s));
-        if (!g || !g->to_object()) return settingsError("settings must be a JSON object");
-        return S{};
-    } else {
-        auto parsed = rfl::json::read<S, rfl::DefaultIfMissing>(settingsText(s));
-        if constexpr (requires(const S& st) { T::validate(st); }) {
-            if (parsed) {
-                if (auto why = T::validate(*parsed)) return settingsError(*why);
-            }
-        }
-        return parsed;
-    }
-}
-
-template <class T>
-std::size_t extraBytes([[maybe_unused]] const typename SettingsOf<T>::type& s) {
-    if constexpr (requires { T::extra_memory(s); }) return T::extra_memory(s);
-    else return 0;
-}
-
-constexpr std::size_t alignUp(std::size_t n, std::size_t a) { return (n + a - 1) / a * a; }
-
-template <class T>
-std::size_t extraOffset() { return alignUp(sizeof(Instance<T>), alignof(std::max_align_t)); }
-
-// ---- resources named by settings (docs/modular-graph-phase-c3-design.md section 4) ------------------------
-
-template <class T>
-struct IsResourceRef : std::false_type {};
-template <fixed_string T>
-struct IsResourceRef<resource_ref<T>> : std::true_type {};
-
-template <class Field, class F>
-void resourceField(std::string_view name, Field& field, F&& f) {
-    using P = std::remove_cvref_t<Field>;
-    if constexpr (IsResourceRef<P>::value) f(name, field);
-}
-
-// A settings struct's resource fields, in declaration order, each with its JSON name: the order create pins them in
-// and prepare compares them by. Only settings fields are walked this way; a port or param of the same type is not.
-template <class S, class F>
-void forEachResourceField(S& s, F&& f) {
-    forEachNamedField(s, [&](std::string_view name, auto& field) { resourceField(name, field, f); });
-}
-
-// Pin every resource `s` names into `inst`, resolving each field's handle, in field order. False, with the reason in
-// *why, for a host that offers no table, an id that is unknown or dropped, or a resource
-// of another type than the field declares (a "rom" where the document names an sram).
-template <class T>
-bool acquireResources(Instance<T>& inst, const rp_host* host, typename SettingsOf<T>::type& s, std::string* why) {
-    bool any = false;
-    forEachResourceField(s, [&](std::string_view, auto&) { any = true; });
-    if (!any) return true;  // a node that names none never looks the service up
-    const auto* service =
-        host ? static_cast<const rp_host_resources*>(host->get_interface(host, RP_HOST_RESOURCES_INTERFACE)) : nullptr;
-    bool ok = true;
-    forEachResourceField(s, [&](std::string_view name, auto& field) {
-        ResourceHold hold;
-        hold.named = field.id;
-        inst.resources.push_back(std::move(hold));
-        if (!ok || field.empty()) return;  // the document names none here
-        ResourceHold& held = inst.resources.back();
-        if (!service) {
-            *why = std::string(name) + ": the host offers no resource table";
-            ok = false;
-            return;
-        }
-        rp_error err{};
-        const rp_resource* r =
-            service->acquire(service->ctx, field.id, std::string(field.resource_type()).c_str(), &err);
-        if (!r) {
-            // The service's reason continues the sentence: "rom: \"res:7\" is not a live resource".
-            *why = std::string(name) + ": " + graph::jsonString(field.ref()) + " " +
-                   (err.message[0] ? std::string(err.message) : std::string("cannot be used"));
-            ok = false;
-            return;
-        }
-        held.service = service;
-        held.id = field.id;
-        held.resource = r;
-        field.resource = r;
-    });
-    return ok;
-}
-
-// Put the instance's held handles into the resource fields of `s`, which prepare parsed from a new document, and
-// report whether they are the same resources: a ref that moved is a rebuild, since the fresh instance pins what it
-// names and this one keeps reading what it holds until its plan is reclaimed. Reads only what create fixed (the
-// pins), never the committed settings, so a type whose prepare is stateless may still call it.
-template <class T>
-bool resolveResources(Instance<T>& inst, typename SettingsOf<T>::type& s) {
-    bool same = true;
-    std::size_t i = 0;
-    forEachResourceField(s, [&](std::string_view, auto& field) {
-        if (!same) return;
-        if (i >= inst.resources.size() || inst.resources[i].named != field.id) {
-            same = false;
-            return;
-        }
-        field.resource = inst.resources[i].resource;
-        ++i;
-    });
-    return same && i == inst.resources.size();
-}
-
-template <class T> const rp_node_vtable* vtable();
-
-template <class T>
-Instance<T>* self(rp_node* n) noexcept { return static_cast<Instance<T>*>(n); }
-
-template <class T>
-std::size_t layoutForFn(const rp_node_type*, const rp_settings* s, char* out, std::size_t outSize, rp_error* err) {
-    try {
-        auto parsed = parseSettings<T>(s);
-        if (!parsed) {
-            copyError(err, parsed.error().what());
-            return 0;
-        }
-        const std::string json = graph::layoutJson(layoutFor<T>(*parsed));
-        if (out && json.size() < outSize) {
-            std::memcpy(out, json.data(), json.size());
-            out[json.size()] = '\0';
-        }
-        return json.size();
-    } catch (const std::exception& e) {
-        copyError(err, e.what());
-    } catch (...) {
-        copyError(err, "settings threw");
-    }
-    return 0;
-}
-
-template <class T>
-bool activateFn([[maybe_unused]] rp_node* n, [[maybe_unused]] double sampleRate,
-                [[maybe_unused]] std::uint32_t maxFrames, [[maybe_unused]] rp_error* err) {
-    if constexpr (!requires(T& t) { t.activate(sampleRate, maxFrames); }) {
-        return true;
-    } else {
-        try {
-            if constexpr (std::is_same_v<decltype(self<T>(n)->node.activate(sampleRate, maxFrames)), bool>) {
-                if (!self<T>(n)->node.activate(sampleRate, maxFrames)) {
-                    copyError(err, "activate failed");
-                    return false;
-                }
-            } else {
-                self<T>(n)->node.activate(sampleRate, maxFrames);
-            }
-            return true;
-        } catch (const std::exception& e) {
-            copyError(err, e.what());
-            return false;
-        } catch (...) {
-            copyError(err, "activate threw");
-            return false;
-        }
-    }
-}
-
-template <class T>
-void deactivateFn([[maybe_unused]] rp_node* n) {
-    if constexpr (requires(T& t) { t.deactivate(); }) {
-        try {
-            self<T>(n)->node.deactivate();
-        } catch (...) {
-            // Nothing to report through: deactivate has no error channel, and it must not unwind the host.
-        }
-    }
-}
-
-template <class T>
-void portsChangedFn([[maybe_unused]] rp_node* n, [[maybe_unused]] const std::uint8_t* connected,
-                    [[maybe_unused]] std::uint32_t count) noexcept {
-    if constexpr (requires(T& t, std::span<const std::uint8_t> c) { t.ports_changed(c); })
-        self<T>(n)->node.ports_changed(std::span<const std::uint8_t>(connected, count));
-}
-
-template <class T>
-void bindAll(T& node, const rp_process* p) noexcept {
-    bindPorts(inputsOf(node), p->inputs);
-    bindPorts(outputsOf(node), p->outputs);
-    bindParams(paramsOf(node), p->params);
-}
-
-template <class T>
-rp_status processFn(rp_node* n, const rp_process* p) noexcept {
-    T& node = self<T>(n)->node;
-    bindAll(node, p);
-    const Block block{*p};
-    if constexpr (std::is_same_v<decltype(node.process(block)), rp_status>) return node.process(block);
-    else {
-        node.process(block);
-        return RP_OK;
-    }
-}
-
-// Compares against the instance's extra memory, its rebuild_key and, unless it reshapes, its layout, none of which
-// changes after create; a node that reshapes never compares layouts, since the planner takes a reshape that keeps
-// the layout as a swap. Only needs_rebuild reads the instance's current settings, which is safe because a host never
-// prepares while a commit for the same instance may be in flight: GraphHost waits for a published commit to be
-// adopted before it plans, but only for commits from types without RP_TYPE_STATELESS_PREPARE (awaitsAdoption). A C++
-// node without stateless_prepare is such a type; a stateless_prepare type (every script node) is not waited for,
-// which is why that flag rules needs_rebuild out. The rule is asserted on the call itself, so no form of
-// needs_rebuild this would call (a template, an overload set, a callable static member) slips past it. A
-// rebuild_key decides the same thing from the key create kept, so a type that rebuilds on a key may prepare
-// statelessly.
-template <class T>
-rp_prepare_result prepareFn(rp_node* n, const rp_settings* s, void** prepared, rp_error* err) {
-    using S = typename SettingsOf<T>::type;
-    try {
-        auto parsed = parseSettings<T>(s);
-        if (!parsed) {
-            copyError(err, parsed.error().what());
-            return RP_PREPARE_INVALID;
-        }
-        auto* inst = self<T>(n);
-        bool rebuild = extraBytes<T>(*parsed) != inst->extra.size();
-        if constexpr (!reshapesOf<T>())
-            rebuild = rebuild || graph::layoutJson(layoutFor<T>(*parsed)) != inst->layoutJson;
-        if constexpr (kHasRebuildKey<T>) {
-            // create engages it for a keyed type, so this only ever fires if the invariant broke.
-            if (!inst->key) throw std::logic_error("a keyed instance was created without its key");
-            rebuild = rebuild || !(T::rebuild_key(*parsed) == *inst->key);
-        }
-        // A resource field that moved is a rebuild whatever the type's key says, and the one that stays puts this
-        // instance's pins into the prepared settings (nothing is pinned or dropped here: only create and destroy do
-        // that, both on the control thread). Resolved before needs_rebuild, and that hook is skipped on a moved ref, so
-        // it never sees a settings object whose handle is null: a node that reads a resource there would otherwise
-        // rebuild on every prepare.
-        const bool resourcesMoved = !resolveResources(*inst, *parsed);
-        if constexpr (requires(const S& a) { T::needs_rebuild(a, a); }) {
-            static_assert(!statelessPrepareOf<T>(),
-                          "a stateless_prepare node cannot have needs_rebuild, which reads its current settings");
-            if (!resourcesMoved) rebuild = rebuild || T::needs_rebuild(*inst->settings, *parsed);
-        }
-        rebuild = rebuild || resourcesMoved;
-        if (rebuild) return RP_PREPARE_REBUILD;
-        *prepared = new S(std::move(*parsed));
-        if constexpr (reshapesOf<T>()) return RP_PREPARE_RESHAPE;
-        else return RP_PREPARE_SWAP;
-    } catch (const std::exception& e) {
-        copyError(err, e.what());
-        return RP_PREPARE_INVALID;
-    } catch (...) {
-        copyError(err, "settings threw");
-        return RP_PREPARE_INVALID;
-    }
-}
-
-template <class T>
-void commitFn(rp_node* n, void* prepared, void** old) noexcept {
-    using S = typename SettingsOf<T>::type;
-    auto* inst = self<T>(n);
-    *old = inst->settings;
-    inst->settings = static_cast<S*>(prepared);
-    if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
-}
-
-template <class T>
-void releaseFn(rp_node*, void* old) noexcept {
-    delete static_cast<typename SettingsOf<T>::type*>(old);
-}
-
-template <class T> void stepPrepare(rp_node* n, const rp_process* p) noexcept {
-    T& node = self<T>(n)->node;
-    bindAll(node, p);
-    node.prepare(Block{*p});
-}
-template <class T> rp_step_result stepStep(rp_node* n, std::uint32_t target) noexcept {
-    T& node = self<T>(n)->node;
-    if constexpr (std::is_same_v<decltype(node.step(target)), bool>) return node.step(target) ? RP_STEP_DONE : RP_STEP_BELOW;
-    else return node.step(target);
-}
-template <class T> void stepFinish(rp_node* n, const rp_process* p) noexcept {
-    T& node = self<T>(n)->node;
-    bindAll(node, p);
-    node.finish(Block{*p});
-}
-template <class T> void linkSetPeers(rp_node* n, rp_node* const* peers, std::uint32_t count) noexcept {
-    self<T>(n)->node.set_peers(std::span<rp_node* const>(peers, count));
-}
-
-template <class T>
-const void* interfaceFn([[maybe_unused]] rp_node* n, const char* id) noexcept {
-    [[maybe_unused]] const std::string_view want(id);
-    if constexpr (requires(T& t, const Block& b, std::uint32_t f) { t.prepare(b); t.step(f); t.finish(b); }) {
-        static constexpr rp_step_interface kStep{&stepPrepare<T>, &stepStep<T>, &stepFinish<T>};
-        if (want == RP_STEP_INTERFACE) return &kStep;
-    }
-    if constexpr (requires(T& t, std::span<rp_node* const> peers) { t.set_peers(peers); }) {
-        static constexpr rp_link_interface kLink{&linkSetPeers<T>};
-        if (want == RP_LINK_INTERFACE) return &kLink;
-    }
-    if constexpr (requires(T& t, std::string_view w) { t.get_interface(w); })
-        return self<T>(n)->node.get_interface(want);
-    else
-        return nullptr;
-}
-
-template <class T>
-const rp_node_vtable* vtable() {
-    static constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
-                                            &processFn<T>,  &prepareFn<T>,    &commitFn<T>,
-                                            &releaseFn<T>,  &interfaceFn<T>};
-    return &kVtable;
-}
-
-template <class T> constexpr const char* nameOf() { if constexpr (requires { T::name; }) return T::name; else return T::id; }
-template <class T> constexpr const char* categoryOf() { if constexpr (requires { T::category; }) return T::category; else return ""; }
-template <class T> constexpr const char* versionOf() { if constexpr (requires { T::version; }) return T::version; else return "1.0.0"; }
-template <class T> constexpr bool heapOf() { if constexpr (requires { T::heap; }) return T::heap; else return false; }
-template <class T> constexpr bool threadsOf() { if constexpr (requires { T::threads; }) return T::threads; else return false; }
-// At most one node of the type in a plan (the planner refuses a second): a type that binds a host-owned device sets it.
-template <class T> constexpr bool uniqueOf() {
-    if constexpr (requires { T::unique; }) return T::unique; else return false;
-}
-template <class T> constexpr std::uint32_t typeFlags() {
-    std::uint32_t flags = 0;
-    if constexpr (statelessPrepareOf<T>()) flags |= RP_TYPE_STATELESS_PREPARE;
-    return flags;
-}
-
-// An empty descriptor when building it throws (adjust_layout on default settings, say), which the registry
-// refuses with a reason rather than letting the exception reach it.
-template <class T>
-const char* descriptorFn(const rp_node_type*) {
-    static const std::string kJson = []() -> std::string {
-        try {
-            using S = typename SettingsOf<T>::type;
-            graph::TypeDescriptor d;
-            d.id = T::id;
-            d.version = versionOf<T>();
-            d.name = nameOf<T>();
-            d.category = categoryOf<T>();
-            d.heap = heapOf<T>();
-            d.threads = threadsOf<T>();
-            d.unique = uniqueOf<T>();
-            d.layout = layoutFor<T>(S{});
-            if constexpr (!std::is_same_v<S, NoSettings>) d.settingsSchema = rfl::json::to_schema<S, rfl::DefaultIfMissing>();
-            return graph::descriptorJson(d);
-        } catch (...) {
-            return std::string();
-        }
-    }();
-    return kJson.c_str();
-}
-
-template <class T>
-bool queryMemoryFn(const rp_node_type*, const rp_settings* s, rp_memory_req* out, rp_error* err) {
-    try {
-        auto parsed = parseSettings<T>(s);
-        if (!parsed) {
-            copyError(err, parsed.error().what());
-            return false;
-        }
-        const std::size_t extra = extraBytes<T>(*parsed);
-        if (extra > std::numeric_limits<std::size_t>::max() - extraOffset<T>()) {
-            copyError(err, "extra memory request is too large");
-            return false;
-        }
-        out->size = extra ? extraOffset<T>() + extra : sizeof(Instance<T>);
-        out->align = alignof(Instance<T>) > alignof(std::max_align_t) ? alignof(Instance<T>) : alignof(std::max_align_t);
-        return true;
-    } catch (const std::exception& e) {
-        copyError(err, e.what());
-        return false;
-    } catch (...) {
-        copyError(err, "settings threw");
-        return false;
-    }
-}
-
-template <class T>
-rp_node* createFn(const rp_node_type*, [[maybe_unused]] const rp_host* host, void* memory, const rp_settings* s,
-                  rp_error* err) {
-    using S = typename SettingsOf<T>::type;
-    Instance<T>* inst = nullptr;
-    try {
-        auto parsed = parseSettings<T>(s);
-        if (!parsed) {
-            copyError(err, parsed.error().what());
-            return nullptr;
-        }
-        // Everything that can throw on the node's behalf (adjust_layout, rebuild_key) runs before the instance exists.
-        // Only a node that does not reshape keeps its layout, for prepare to compare against.
-        std::string layout;
-        if constexpr (reshapesOf<T>()) (void)layoutFor<T>(*parsed);
-        else layout = graph::layoutJson(layoutFor<T>(*parsed));
-        std::optional<typename RebuildKeyOf<T>::type> key;
-        if constexpr (kHasRebuildKey<T>) key.emplace(T::rebuild_key(*parsed));
-        const std::size_t extra = extraBytes<T>(*parsed);
-        inst = new (memory) Instance<T>{};
-        inst->vt = vtable<T>();
-        inst->layoutJson = std::move(layout);
-        inst->key = std::move(key);
-        inst->settings = new S(std::move(*parsed));
-        if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
-        // The resources the settings name are pinned before any hook sees the settings, so on_settings and activate
-        // read the handles; a resource that cannot be pinned fails the create, and with it the document.
-        std::string refused;
-        if (!acquireResources<T>(*inst, host, *inst->settings, &refused)) {
-            copyError(err, refused);
-        } else {
-            if (extra) {
-                inst->extra = std::span<std::byte>(static_cast<std::byte*>(memory) + extraOffset<T>(), extra);
-                std::memset(inst->extra.data(), 0, extra);
-                if constexpr (requires(T& t, std::span<std::byte> e) { t.attach(e); }) inst->node.attach(inst->extra);
-            }
-            if constexpr (requires(T& t, const S& st) { t.on_settings(st); }) inst->node.on_settings(*inst->settings);
-            return inst;
-        }
-    } catch (const std::exception& e) {
-        copyError(err, e.what());
-    } catch (...) {
-        copyError(err, "create threw");
-    }
-    // An allocation failure, or a resource that would not pin, after the instance exists: leave nothing behind.
-    if (inst) {
-        delete inst->settings;
-        inst->~Instance<T>();
-    }
-    return nullptr;
-}
-
-// The node goes first: it may hold a pointer to its settings until its own destructor has run.
-template <class T>
-void destroyFn(rp_node* n) {
-    auto* inst = self<T>(n);
-    auto* settings = inst->settings;
-    inst->~Instance<T>();
-    delete settings;
-}
-
-}  // namespace detail
-
-// The ABI table for a C++ node type. One per T, built on first use; every optional hook's signature is
-// checked here, at compile time.
-template <class T>
-const rp_node_type* node_type() {
+// Two builds (docs/modular-graph-phase-e1-design.md section 6). The desktop's, the default, is all of the above. A
+// device-mode build (-DRP_DEVICE, with no exceptions and no RTTI) is the portable core alone: the wrappers, rp::Block,
+// the reflection, the hooks a block or an instance's lifecycle calls, and create / destroy, with the settings read
+// from msgpack into the instance itself. A device runs a plan the desktop planned and validated, so its build has no
+// descriptor, no layout, no JSON and no prepare / commit, and never calls validate or adjust_layout: a node source
+// that is built for the device puts those two (and whatever only they use) inside `#ifndef RP_DEVICE`, since they name
+// desktop types (std::string, rp::graph::Layout). A type built for the device declares neither `heap` nor `threads`,
+// and its Settings holds only numbers, bools, enums (a fixed underlying type, no operator|), rp::choice, rp::text,
+// rp::list and rp::resource_ref; either is checked at compile time, on the desktop too, whose default build compiles
+// every `device` package entry in device mode as well (cmake/packages.cmake). An enum_param works on both builds.
+// The files: node_core.hpp (both builds), node_device.hpp (RP_DEVICE), node_desc.hpp (the desktop).
+
+#include "graph/node_core.hpp"
+#ifdef RP_DEVICE
+#include "graph/node_device.hpp"
+#else
+#include "graph/node_desc.hpp"
+#endif
+
+RP_NAMESPACE_BEGIN
+
+// The ABI table for a C++ node type: one per T, a constant (so a package's list of them needs no run-time
+// initialisation); every optional hook's signature is checked here, at compile time.
+template <class T>
+constexpr const rp_node_type* node_type() {
     static_assert(detail::checkHooks<T>());
-    static constexpr rp_node_type kType{T::id, &detail::descriptorFn<T>, &detail::layoutForFn<T>,
-                                        &detail::queryMemoryFn<T>, &detail::createFn<T>, &detail::destroyFn<T>,
-                                        detail::typeFlags<T>()};
-    return &kType;
+#ifdef RP_DEVICE
+    static_assert(detail::DeviceTypeCheck<T>::ok);
+#else
+    static_assert(detail::checkDescriptionHooks<T>());
+#endif
+    return &detail::kNodeType<T>;
 }
 
 // A coupled peer as its concrete type, or null when the peer is a different node type.
 template <class T>
 T* peer_as(rp_node* n) {
-    if (!n || n->vt != detail::vtable<T>()) return nullptr;
+    if (!n || n->vt != &detail::kVtable<T>) return nullptr;
     return &detail::self<T>(n)->node;
 }
 
-}  // namespace rp
-
-// A resource_ref is the plain string the document carries, read and written as one, and its schema is a string
-// INLINE rather than a $defs entry. rfl names a $defs entry after the C++ type, and a class template over a
-// fixed_string has no spelling compilers agree on (GCC writes the argument's type, Clang drops it), so the default
-// would put a different key into the descriptor under each compiler; a Settings struct is a named type for the same
-// reason (the hook list above).
-template <class R, class W, rp::fixed_string Type, class ProcessorsType>
-    requires rfl::parsing::AreReaderAndWriter<R, W, rp::resource_ref<Type>>
-struct rfl::parsing::Parser<R, W, rp::resource_ref<Type>, ProcessorsType> {
-    using InputVarType = typename R::InputVarType;
-    using String = Parser<R, W, std::string, ProcessorsType>;
-
-    // The empty string is "none" (id 0); anything else must be "res:N", the one spelling of an id parseResourceRef
-    // accepts. A ref that is not is refused here, with the string it carried, where the settings are read.
-    static Result<rp::resource_ref<Type>> read(const R& r, const InputVarType& var) noexcept {
-        auto text = String::read(r, var);
-        if (!text) return rfl::Unexpected(text.error());
-        rp::resource_ref<Type> out;
-        if (text->empty()) return out;
-        const auto id = rp::graph::parseResourceRef(*text);
-        if (!id)
-            return rfl::Unexpected(
-                rfl::Error(rp::graph::jsonString(*text) + " is not a resource reference (res:N)"));
-        out.id = *id;
-        return out;
-    }
-    template <class P>
-    static void write(const W& w, const rp::resource_ref<Type>& field, const P& parent) {
-        String::write(w, field.ref(), parent);
-    }
-    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
-        return String::to_schema(definitions);
-    }
-};
+RP_NAMESPACE_END
+
+#if defined(RP_DEVICE) && defined(__GLIBCXX__)
+// What a device-mode build must not carry: the standard library's heap and exception headers, and reflect-cpp's JSON.
+// The check runs here, at the end of graph/node.hpp, so it sees what the translation unit included BEFORE this header
+// (the node source's own includes, the kernels' headers) and what the SDK itself includes, and nothing included after
+// it. It is an early, readable error, not the guarantee: the device build's link trap (malloc, operator new,
+// __cxa_throw and the std::__throw_* family undefined) is. Each name is the header's own include guard in libstdc++,
+// the device toolchain's library (the desktop's device-mode test builds with whichever library its compiler has, and
+// only libstdc++'s guards are checked).
+#if defined(_GLIBCXX_STRING) || defined(_GLIBCXX_VECTOR) || defined(_GLIBCXX_MAP) || defined(_GLIBCXX_OPTIONAL) || \
+    defined(_GLIBCXX_STDEXCEPT) || defined(__EXCEPTION__) || defined(_GLIBCXX_IOSTREAM) ||                      \
+    defined(_GLIBCXX_FUNCTIONAL) || defined(_GLIBCXX_MEMORY) || defined(_GLIBCXX_RANDOM)
+#error "a device-mode build includes a heap or exception header: <string>, <vector>, <memory>, <random>, ... (above)"
+#endif
+#if defined(RFL_JSON_HPP_) || defined(YYJSON_H)
+#error "a device-mode build includes reflect-cpp's JSON or yyjson"
+#endif
+#endif
diff --git a/packages/node-sdk/native/src/graph/rp_node.h b/packages/node-sdk/native/src/graph/rp_node.h
index b631aecc9..8dc771520 100644
--- a/packages/node-sdk/native/src/graph/rp_node.h
+++ b/packages/node-sdk/native/src/graph/rp_node.h
@@ -22,7 +22,10 @@
  * type per definition a bundle registers, all behind the same functions).
  *
  * Every rp_node_vtable entry is required: a node with nothing to do supplies a no-op. get_interface may
- * return null for every id.
+ * return null for every id. The one exception is a device-mode build of a C++ node (graph/node.hpp under RP_DEVICE):
+ * a device runs a plan the desktop made and never changes its settings or describes a type, so its rp_node_type's
+ * descriptor and layout_for and its vtable's prepare_settings, commit_settings and release_settings are null. Such a
+ * type is never registered with a host that plans.
  *
  * Result codes are fixed-width integers rather than enum types: an enum's size is implementation-defined
  * (-fshort-enums makes it one byte on some embedded toolchains), and every value here crosses the ABI.
@@ -63,11 +66,16 @@ enum { RP_LOG_ERROR = 0, RP_LOG_WARNING = 1, RP_LOG_INFO = 2, RP_LOG_DEBUG = 3 }
 /* A control-thread call that fails writes a NUL-terminated reason here. */
 typedef struct rp_error { char message[256]; } rp_error;
 
-enum { RP_SETTINGS_JSON = 0u };
+/* How a settings object is spelt. JSON is a JSON object, what a graph document carries. MSGPACK is the same object as
+ * a msgpack map keyed by field name (strings as str, numbers as ints or floats as the JSON had them, arrays, nested
+ * maps, bools, nil), what a device reads from a plan blob (docs/modular-graph-phase-e1-design.md section 6). A C++ node
+ * built with graph/node.hpp reads both on the desktop and MSGPACK alone in a device-mode build; empty data (size 0)
+ * is the empty object in either. */
+enum { RP_SETTINGS_JSON = 0u, RP_SETTINGS_MSGPACK = 1u };
 typedef struct rp_settings {
     const char* data;     /* bytes, not NUL-terminated; UTF-8 text when the encoding is JSON */
     size_t      size;
-    uint32_t    encoding; /* RP_SETTINGS_JSON on the desktop */
+    uint32_t    encoding; /* RP_SETTINGS_* */
 } rp_settings;
 
 typedef struct rp_memory_req { size_t size; size_t align; } rp_memory_req;
@@ -361,6 +369,8 @@ typedef struct rp_link_interface {
 
 #ifdef __cplusplus
 }
+/* Where the C++ SDK's names live in a device-mode build: declared before any C++ header opens rp::graph. */
+#include "graph/sdk_namespace.hpp"
 #endif
 
 #endif /* RP_NODE_H */
diff --git a/packages/node-sdk/native/src/graph/settings_types.hpp b/packages/node-sdk/native/src/graph/settings_types.hpp
index 18b3aa4c7..503f080c3 100644
--- a/packages/node-sdk/native/src/graph/settings_types.hpp
+++ b/packages/node-sdk/native/src/graph/settings_types.hpp
@@ -2,41 +2,54 @@
 
 // Fixed-capacity settings types (docs/modular-graph-phase-e1-design.md section 5). A node meant for the device keeps
 // every settings field inline: no allocation to read, copy or destroy one. node.hpp includes this header, so a node
-// names them as rp::text and rp::list.
+// names them as rp::text, rp::list and rp::choice.
 //
 //   struct Settings {
-//       rp::text<16>                      layout = "stereo";   // JSON: "stereo"
-//       rp::list<std::uint32_t, 16>       counts = {1, 1};     // JSON: [1, 1]
+//       rp::text<16>                      layout = "stereo";              // JSON: "stereo"
+//       rp::list<std::uint32_t, 16>       counts = {1, 1};                // JSON: [1, 1]
+//       rp::choice<"audio", "control">    kind = decltype(kind)::make<"audio">();   // JSON: "audio"
 //   };
 //
-// Both are read and written as the JSON the std::string and std::vector they stand in for were, and their JSON Schema
-// is that type's (a string, an array of the element's schema), so a descriptor does not change when a field moves to
-// them. A JSON value over the bound is a settings error; reflect-cpp names the field it was reading in the message.
-// Reading builds a std::string or std::vector first, which is why the JSON side is a desktop one: the device reads
-// the same fields from msgpack (design section 6), and the types themselves allocate nothing.
+// Each is read and written as the JSON the type it stands in for was (std::string, std::vector, rfl::Literal), and its
+// JSON Schema is that type's, so a descriptor does not change when a field moves to one. A JSON value over the bound is
+// a settings error; reflect-cpp names the field it was reading in the message. The JSON side is settings_json.hpp, a
+// desktop one, since reading builds a std::string or std::vector first; the device reads the same fields from msgpack
+// (settings_msgpack.hpp, design section 6), and the types themselves allocate nothing. This header is portable: it is
+// part of both builds of node.hpp.
 
 #include <cstddef>
 #include <cstdint>
-#include <cstdlib>
+#include <cstring>
 #include <initializer_list>
-#include <map>
 #include <span>
-#include <string>
 #include <string_view>
 #include <type_traits>
-#include <vector>
+#ifndef RP_DEVICE
+#include <string>
+#endif
 
-#include <rfl/Result.hpp>
-#include <rfl/json.hpp>
-#include <rfl/parsing/Parser.hpp>
+#include "graph/rp_node.h"
+#include "graph/sdk_namespace.hpp"
 
-namespace rp {
+RP_NAMESPACE_BEGIN
+
+// A string as a template argument: a port's channel layout, a param's name, a resource type, a choice's labels.
+template <std::size_t N>
+struct fixed_string {
+    char v[N]{};
+    constexpr fixed_string(const char (&s)[N]) {  // NOLINT(google-explicit-constructor)
+        for (std::size_t i = 0; i < N; ++i) v[i] = s[i];
+    }
+    constexpr std::string_view view() const { return {v, N - 1}; }
+};
 
 namespace detail {
-// Called by the consteval constructors when a literal or a braced list is over its type's bound. It is not constexpr,
-// so evaluating it is a compile error where the default is written; a run-time value goes through assign /
-// push_back, which answer false instead.
-[[noreturn]] inline void capacityExceeded() { std::abort(); }
+// Called by the consteval constructors when a literal or a braced list is over its type's bound, and by choice's
+// consteval names when a label is none of its labels. Each is declared and never defined, and not constexpr, so
+// evaluating one is a compile error where the default is written, and no build ever carries a call to it; a run-time
+// value goes through assign / push_back, which answer false instead.
+void capacityExceeded();
+void notALabel();
 }  // namespace detail
 
 // Up to N bytes of UTF-8 stored inline. The bound is bytes, not code points, and nothing checks the encoding: a
@@ -72,7 +85,9 @@ public:
     constexpr std::size_t size() const { return size_; }
     constexpr bool        empty() const { return size_ == 0; }
     constexpr const char* data() const { return data_; }
-    std::string           str() const { return std::string(view()); }
+#ifndef RP_DEVICE
+    std::string           str() const { return std::string(view()); }  // the desktop's, which builds descriptors
+#endif
 
     friend constexpr bool operator==(const text& a, std::string_view b) { return a.view() == b; }
 
@@ -138,56 +153,113 @@ private:
     size_type size_ = 0;
 };
 
-}  // namespace rp
-
-// A text is the JSON string it holds, and its schema is the string's.
-template <class R, class W, std::size_t N, class ProcessorsType>
-    requires rfl::parsing::AreReaderAndWriter<R, W, rp::text<N>>
-struct rfl::parsing::Parser<R, W, rp::text<N>, ProcessorsType> {
-    using InputVarType = typename R::InputVarType;
-    using String = Parser<R, W, std::string, ProcessorsType>;
-
-    static Result<rp::text<N>> read(const R& r, const InputVarType& var) noexcept {
-        auto s = String::read(r, var);
-        if (!s) return rfl::Unexpected(s.error());
-        rp::text<N> out;
-        if (!out.assign(*s))
-            return rfl::Unexpected(rfl::Error("a string of " + std::to_string(s->size()) + " bytes is over the " +
-                                              std::to_string(N) + " the field holds"));
-        return out;
+// One of a fixed set of labels, stored as its index: a settings enum whose labels are what a document carries
+// ("low-pass", "high-pass-tpdf"), which a C++ enumerator cannot spell. It stands in for rfl::Literal, whose interface
+// it keeps (value() is the label's index, make<"label">() and value_of<"label">() name one at compile time), and is
+// read and written as the same JSON string with the same schema; it holds one byte (two past 255 labels) and no
+// string.
+template <fixed_string... Labels>
+class choice {
+public:
+    static_assert(sizeof...(Labels) > 0 && sizeof...(Labels) < 65536, "a choice has 1 to 65535 labels");
+    using value_type = std::conditional_t<(sizeof...(Labels) <= 255), std::uint8_t, std::uint16_t>;
+
+    static constexpr std::size_t size() { return sizeof...(Labels); }
+    // Label i, in declaration order.
+    static constexpr std::string_view label(std::size_t i) {
+        constexpr std::string_view kLabels[] = {Labels.view()...};
+        return kLabels[i];
     }
-    template <class P>
-    static void write(const W& w, const rp::text<N>& field, const P& parent) {
-        String::write(w, field.str(), parent);
+    // The index of `name`, or size() when it is none of the labels.
+    static constexpr std::size_t index_of(std::string_view name) {
+        for (std::size_t i = 0; i < size(); ++i)
+            if (label(i) == name) return i;
+        return size();
     }
-    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
-        return String::to_schema(definitions);
+    template <fixed_string Name>
+    static consteval value_type value_of() {
+        if (index_of(Name.view()) == size()) detail::notALabel();
+        return static_cast<value_type>(index_of(Name.view()));
     }
-};
+    template <fixed_string Name>
+    static consteval choice make() {
+        choice c;
+        c.value_ = value_of<Name>();
+        return c;
+    }
+
+    constexpr choice() = default;  // the first label
 
-// A list is the JSON array its elements make, and its schema is the array's. The elements are read as a std::vector
-// would be, so each one reports its own error exactly as it did when the field was one.
-template <class R, class W, class T, std::size_t N, class ProcessorsType>
-    requires rfl::parsing::AreReaderAndWriter<R, W, rp::list<T, N>>
-struct rfl::parsing::Parser<R, W, rp::list<T, N>, ProcessorsType> {
-    using InputVarType = typename R::InputVarType;
-    using Vector = Parser<R, W, std::vector<T>, ProcessorsType>;
-
-    static Result<rp::list<T, N>> read(const R& r, const InputVarType& var) noexcept {
-        auto v = Vector::read(r, var);
-        if (!v) return rfl::Unexpected(v.error());
-        if (v->size() > N)
-            return rfl::Unexpected(rfl::Error("an array of " + std::to_string(v->size()) + " elements is over the " +
-                                              std::to_string(N) + " the field holds"));
-        rp::list<T, N> out;
-        for (const T& e : *v) out.push_back(e);
-        return out;
+    constexpr value_type       value() const { return value_; }
+    constexpr std::string_view name() const { return label(value_); }
+
+    // Set by label. False, with the value unchanged, when `name` is none of the labels.
+    constexpr bool assign(std::string_view name) {
+        const std::size_t i = index_of(name);
+        if (i == size()) return false;
+        value_ = static_cast<value_type>(i);
+        return true;
     }
-    template <class P>
-    static void write(const W& w, const rp::list<T, N>& field, const P& parent) {
-        Vector::write(w, std::vector<T>(field.begin(), field.end()), parent);
+    // Set by index. False, with the value unchanged, past the last label.
+    constexpr bool assign_index(std::size_t i) {
+        if (i >= size()) return false;
+        value_ = static_cast<value_type>(i);
+        return true;
     }
-    static schema::Type to_schema(std::map<std::string, schema::Type>* definitions) {
-        return Vector::to_schema(definitions);
+
+    friend constexpr bool operator==(const choice& a, const choice& b) { return a.value_ == b.value_; }
+
+private:
+    value_type value_ = 0;
+};
+
+// A TOP-LEVEL settings field that names a resource: the document writes "res:N" and the node reads the bytes without
+// ever touching the filesystem (docs/modular-graph-phase-c3-design.md section 4). The authoring layer takes the pin
+// when it creates the instance and drops it when the instance is destroyed, both on the control thread (the same pin
+// docs/modular-graph-design.md section 2.5 gives a node that references a resource from its settings). A field whose
+// ref moved is a REBUILD, which node.hpp decides itself: the fresh instance pins what it names and the old pin drops
+// with the instance the last plan ran. So a resource changes under a node only by a rebuild, and no pin is ever taken
+// or dropped on the audio thread.
+//
+// Create pins before any hook that sees the instance's settings runs, so `on_settings`, `activate`, `process`, `step`
+// and `finish` read a resolved handle (and a prepare that keeps the refs copies the instance's pins into the settings
+// object it installs). The hooks that run before create has pinned anything (`validate`, `adjust_layout`,
+// `rebuild_key`, `extra_memory`, `query_memory`) see only `id` (and `empty()`): a node must not read `bytes()` or
+// `audio()` there. `needs_rebuild` sees handles in both objects, and is skipped for a ref that moved, which is a
+// rebuild on its own.
+//
+//   struct Settings { rp::resource_ref<"rom"> rom; };   // JSON: {"rom": "res:7"}; absent: the node names none
+//
+// The type is the resource type an id must have (the host's putResource makes "rom", "sram", "state"); a resource
+// of another type, an unknown or dropped id, or a host with no table refuses the create with the field's name.
+template <fixed_string Type>
+struct resource_ref {
+    // Read and written as the plain string the document carries, never the handle it resolves to (settings_json.hpp
+    // on the desktop, settings_msgpack.hpp on either build). The id is held as the number, so a settings object owns
+    // nothing on the heap for it.
+    static constexpr std::string_view resource_type() { return Type.view(); }
+
+    std::uint32_t      id = 0;              // N of the document's "res:N" (0: the document names none)
+    const rp_resource* resource = nullptr;  // resolved by the host layer at create, and again at each prepare
+
+    bool empty() const { return id == 0; }
+#ifndef RP_DEVICE
+    // The document's spelling of the id, for an error message: "res:N", or empty for none.
+    std::string ref() const { return id ? "res:" + std::to_string(id) : std::string(); }
+#endif
+
+    // The payload of a bytes resource (every type but "audio-buffer"), or null when the field names none.
+    const rp_bytes* bytes() const {
+        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") != 0
+                   ? static_cast<const rp_bytes*>(resource->payload)
+                   : nullptr;
+    }
+    // The payload of an "audio-buffer" resource, or null when the field names none or names another type.
+    const rp_audio_buffer* audio() const {
+        return resource && resource->payload && std::strcmp(resource->type, "audio-buffer") == 0
+                   ? static_cast<const rp_audio_buffer*>(resource->payload)
+                   : nullptr;
     }
 };
+
+RP_NAMESPACE_END
````

Run: `sed -n '8355,10013p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0: each refused source now fails in device mode with its own reason on an `error:` line, `HeapType.cpp` with `is never built for the device`, `PointerSettings.cpp` with `a device node's Settings holds only` and `HeapHeader.cpp` with `a heap or exception header` (the reasons `tests.cmake` names; the first is `DeviceTypeCheck`'s "a node type that declares `heap` or `threads` is never built for the device"), so the configure passes them.

- [ ] **Step 3: The nodes, the dither and the desktop's device checks**

`CoreNodes.cpp` and `DspNodes.cpp` wrap each node's `validate` and `adjust_layout`, and the helpers only they use, in
one `#ifndef RP_DEVICE`; `AddKind`, `BiquadType` and `DitherAlgorithm` become `rp::choice`; `std::size` stays,
`<iterator>`, `<vector>` and `rfl/Literal.hpp` go; the whole DPCM node and its package entry are desktop-only (it
holds a vector), and `std::construct_at` becomes placement new (`<memory>` pulls `<exception>`). `Mt19937.hpp` is the
standard engine without `<random>`, which `Dither.hpp` now holds, with the standard's 10000th draw `static_assert`ed
in `Dither.cpp`. `cmake/packages.cmake`'s `retroplug_package()` calls `_retroplug_device_check` for each `device`
entry's target, an object library compiled in device mode in the default build that nothing links, and
`warnings.cmake` puts it, the device-mode objects and the new binary on the `/W4 /WX` list.

Create `packages/dsp/native/src/dsp/Mt19937.hpp`:

````cpp
#pragma once

// The 32-bit Mersenne Twister, std::mt19937's engine and nothing else: the same seeding and the same sequence, which
// the standard specifies to the bit (its 10000th draw from the default seed is 4123659995, held in Dither.cpp). The
// dither kernel draws its TPDF noise from it. It is its own type rather than std::mt19937 because <random> brings
// <string>, <vector> and <iosfwd> with it (the distributions and seed_seq need them), which a device build of the
// kernel must not include; the state is the same 624 words and a position, held inline, so it allocates nothing and
// is trivially destructible.

#include <cstddef>
#include <cstdint>

namespace rp::dsp {

class Mt19937 {
public:
    using result_type = std::uint32_t;
    static constexpr std::uint32_t default_seed = 5489u;

    constexpr Mt19937() { seed(default_seed); }
    constexpr explicit Mt19937(std::uint32_t value) { seed(value); }

    constexpr void seed(std::uint32_t value) {
        state_[0] = value;
        for (std::uint32_t i = 1; i < kN; ++i)
            state_[i] = 1812433253u * (state_[i - 1] ^ (state_[i - 1] >> 30)) + i;
        index_ = kN;
    }

    constexpr std::uint32_t operator()() {
        if (index_ >= kN) twist();
        std::uint32_t y = state_[index_++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }

    static constexpr std::uint32_t min() { return 0; }
    static constexpr std::uint32_t max() { return 0xffffffffu; }

private:
    static constexpr std::uint32_t kN = 624;
    static constexpr std::uint32_t kM = 397;

    constexpr void twist() {
        for (std::uint32_t i = 0; i < kN; ++i) {
            const std::uint32_t y = (state_[i] & 0x80000000u) | (state_[(i + 1) % kN] & 0x7fffffffu);
            state_[i] = state_[(i + kM) % kN] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
        }
        index_ = 0;
    }

    std::uint32_t state_[kN] = {};
    std::size_t   index_ = kN;
};

}  // namespace rp::dsp
````

Run: `mkdir -p packages/dsp/native/src/dsp; sed -n '10036,10094p' docs/modular-graph-phase-e1-plan.md > packages/dsp/native/src/dsp/Mt19937.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/dsp/native/src/dsp/Mt19937.hpp`
Expected: `c4d783f4e6de2e50569a7cf14cdee4e29c0ec004`: the blob f34b117a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/cmake/packages.cmake b/cmake/packages.cmake
index e71b05bbb..1e6c65b12 100644
--- a/cmake/packages.cmake
+++ b/cmake/packages.cmake
@@ -59,6 +59,7 @@ function(retroplug_package name)
     set(_targets "")
     set(_entries "")
     set(_device "")
+    set(_deviceTargets "")
     math(EXPR _last "${_count} - 1")
     foreach(_i RANGE ${_last})
         string(JSON _target ERROR_VARIABLE _err GET "${_native}" ${_i} target)
@@ -88,6 +89,7 @@ function(retroplug_package name)
         list(APPEND _entries ${_entry})
         if(_isDevice)
             list(APPEND _device ${_entry})
+            list(APPEND _deviceTargets ${_target})
         endif()
     endforeach()
     list(REMOVE_DUPLICATES _targets)
@@ -101,6 +103,58 @@ function(retroplug_package name)
     if(EXISTS "${_dir}/native/CMakeLists.txt")
         add_subdirectory("${_dir}/native" "${CMAKE_BINARY_DIR}/packages/${name}")
     endif()
+    list(REMOVE_DUPLICATES _deviceTargets)
+    foreach(_target IN LISTS _deviceTargets)
+        _retroplug_device_check(${name} ${_target})
+    endforeach()
+endfunction()
+
+# _retroplug_device_check(<package> <target>): the desktop build's check of a `device` entry. An entry the manifest
+# marks `device` is one a device product compiles, so the desktop's default build compiles its target's sources a
+# second time, as the device does (graph/node.hpp under RP_DEVICE, no exceptions, no RTTI), into an object library
+# nothing links: <target>-device. A node type whose settings a device cannot hold, one declaring heap or threads, and
+# a description hook left outside `#ifndef RP_DEVICE` (node.hpp's header comment) then fail the desktop build, on
+# every platform CI builds, not only a device toolchain's. The manifest's flag is what keys it, rather than a constant
+# on each node type, because only a device-mode compile can see a hook that is not guarded, and the flag already says
+# which targets a device compiles. It copies the target's sources, definitions, options and include directories; a
+# source a generator expression picks, or a C source, is refused here rather than half-copied, and a device entry's
+# target must exist when retroplug_package() runs (a package's own native/CMakeLists.txt defines it).
+function(_retroplug_device_check package target)
+    if(NOT TARGET ${target})
+        message(FATAL_ERROR "retroplug_package(${package}): ${target}, a device entry's target, is not a target")
+    endif()
+    get_target_property(_sources ${target} SOURCES)
+    get_target_property(_sourceDir ${target} SOURCE_DIR)
+    set(_absolute "")
+    foreach(_source IN LISTS _sources)
+        # A source the device compile could not take as it stands: one a generator expression chooses (it would need
+        # evaluating per configuration), or C (graph/node.hpp is C++, and a C file is no node source).
+        if(_source MATCHES "^\\$<")
+            message(FATAL_ERROR "retroplug_package(${package}): ${target}'s source ${_source} is a generator "
+                                "expression, which its device check (_retroplug_device_check) does not take")
+        endif()
+        if(_source MATCHES "\\.c$")
+            message(FATAL_ERROR "retroplug_package(${package}): ${target}'s source ${_source} is C, which its device "
+                                "check (_retroplug_device_check) does not take")
+        endif()
+        if(NOT IS_ABSOLUTE "${_source}")
+            set(_source "${_sourceDir}/${_source}")
+        endif()
+        list(APPEND _absolute "${_source}")
+    endforeach()
+    add_library(${target}-device OBJECT ${_absolute})
+    set_target_properties(${target}-device PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
+    # The target's own definitions, options (/bigobj on MSVC among them, retroplug_package_library's) and include
+    # directories, read when the build is generated, so a package that adds one after this ran is still copied; then
+    # the device mode on top.
+    target_compile_definitions(${target}-device PRIVATE $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS> RP_DEVICE)
+    target_compile_options(${target}-device PRIVATE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS>)
+    if(NOT MSVC)
+        target_compile_options(${target}-device PRIVATE -fno-exceptions -fno-rtti)
+    endif()
+    target_include_directories(${target}-device PRIVATE $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>)
+    target_link_libraries(${target}-device PRIVATE retroplug-node-sdk-headers retroplug-reflect-headers)
+    set_property(GLOBAL APPEND PROPERTY RP_DEVICE_CHECK_TARGETS ${target}-device)
 endfunction()
 
 # retroplug_package_library(<target> <sources>...): a package's static library, with the settings every one of them
diff --git a/packages/core/native/src/graph/nodes/CoreNodes.cpp b/packages/core/native/src/graph/nodes/CoreNodes.cpp
index 41eec046f..62e3f6e15 100644
--- a/packages/core/native/src/graph/nodes/CoreNodes.cpp
+++ b/packages/core/native/src/graph/nodes/CoreNodes.cpp
@@ -4,10 +4,6 @@
 #include <array>
 #include <cmath>
 #include <cstring>
-#include <iterator>
-#include <vector>
-
-#include <rfl/Literal.hpp>
 
 #include "graph/node.hpp"
 
@@ -21,7 +17,7 @@ namespace core {
 // A join or split names at most kMaxPorts ports (the validators enforce it).
 constexpr std::uint32_t kMaxPorts = 16;
 
-using AddKind = rfl::Literal<"audio", "control">;
+using AddKind = rp::choice<"audio", "control">;
 
 struct AddSettings {
     std::uint32_t inputs = 2;
@@ -78,6 +74,9 @@ namespace {
 
 using namespace core;
 
+// What validate and adjust_layout share: the desktop's, since a device runs plans the desktop validated and laid out
+// (node.hpp's header comment).
+#ifndef RP_DEVICE
 PortDesc audioPort(std::string id, PortDir dir, ChannelSpec spec) {
     PortDesc p;
     p.id = std::move(id);
@@ -117,6 +116,7 @@ std::uint32_t sumOf(const rp::list<std::uint32_t, kMaxPorts>& v) {  // validated
     for (auto c : v) n += c;
     return n;
 }
+#endif  // RP_DEVICE
 
 // Emit a control value at a frame, skipping one equal to what the stream already holds.
 struct ControlWriter {
@@ -140,6 +140,7 @@ struct Add {
     static constexpr const char* category = "core/combine";
     using Settings = AddSettings;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) { return checkCount(s.inputs, "inputs"); }
 
     static void adjust_layout(Layout& l, const Settings& s) {
@@ -153,6 +154,7 @@ struct Add {
         if (controlKind) l.ports.push_back(queuePort("out", PortDir::Out, PortKind::Control));
         else l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::matching("in1")));
     }
+#endif
 
     bool          control = false;
     std::uint32_t inputCount = 2;
@@ -215,6 +217,7 @@ struct Merge {
     static constexpr const char* category = "core/combine";
     using Settings = MergeSettings;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) { return checkCount(s.inputs, "inputs"); }
 
     static void adjust_layout(Layout& l, const Settings& s) {
@@ -222,6 +225,7 @@ struct Merge {
             l.ports.push_back(queuePort("in" + std::to_string(k), PortDir::In, PortKind::Events));
         l.ports.push_back(queuePort("out", PortDir::Out, PortKind::Events));
     }
+#endif
 
     std::uint32_t inputCount = 2;
     void on_settings(const Settings& s) noexcept { inputCount = s.inputs; }
@@ -257,6 +261,7 @@ struct Join {
     static constexpr const char* category = "core/channels";
     using Settings = JoinSettings;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannelList(s.inputs, "inputs")) return e;
         return checkLayout(s.layout, sumOf(s.inputs));
@@ -268,6 +273,7 @@ struct Join {
                                         ChannelSpec::fixed(s.inputs[k], layoutForCount(s.inputs[k]))));
         l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(sumOf(s.inputs), s.layout.str())));
     }
+#endif
 
     void process(const rp::Block& b) noexcept {
         const rp_audio_view& out = b.output(0).u.audio;
@@ -286,6 +292,7 @@ struct Split {
     static constexpr const char* category = "core/channels";
     using Settings = SplitSettings;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannelList(s.outputs, "outputs")) return e;
         return checkLayout(s.layout, sumOf(s.outputs));
@@ -297,6 +304,7 @@ struct Split {
             l.ports.push_back(audioPort("out" + std::to_string(k + 1), PortDir::Out,
                                         ChannelSpec::fixed(s.outputs[k], layoutForCount(s.outputs[k]))));
     }
+#endif
 
     void process(const rp::Block& b) noexcept {
         const rp_audio_view& in = b.input(0).u.audio;
@@ -317,6 +325,7 @@ struct Downmix {
     static constexpr const char* category = "core/channels";
     using Settings = DownmixSettings;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         const auto from = layoutChannels(s.from);
         if (!from) return "unknown layout " + jsonString(s.from);
@@ -332,6 +341,7 @@ struct Downmix {
         l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(from, s.from.str())));
         l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(to, s.to.str())));
     }
+#endif
 
     bool toStereo = false;  // 5.1 -> stereo; every other downmix is to mono
     void on_settings(const Settings& s) noexcept { toStereo = s.to == "stereo"; }
@@ -364,6 +374,7 @@ struct Relabel {
     static constexpr const char* category = "core/channels";
     using Settings = RelabelSettings;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannels(s.channels, "relabel")) return e;
         return checkLayout(s.layout, s.channels);
@@ -373,6 +384,7 @@ struct Relabel {
         l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(s.channels, "any")));
         l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(s.channels, s.layout.str())));
     }
+#endif
 
     void process(const rp::Block& b) noexcept {
         const rp_audio_view& in = b.input(0).u.audio;
@@ -384,8 +396,10 @@ struct Relabel {
 
 // ---- rp.core.delay ----------------------------------------------------------------------------------------------
 
+#ifndef RP_DEVICE  // validate's bounds
 constexpr std::uint32_t kMaxDelaySamples = 1u << 24;      // keeps the int Time param in range
 constexpr std::uint64_t kMaxDelayRingSamples = 1ull << 26;  // 256 MiB of float across every channel
+#endif
 
 struct Delay {
     static constexpr const char* id = "rp.core.delay";
@@ -397,6 +411,7 @@ struct Delay {
     struct { rp::audio_out<rp::match<"in">> out; } outputs;
     struct { rp::int_param<"Time", rp::range{0, 48000, 0}, "samples"> time; } params;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannels(s.channels, "a delay")) return e;
         if (s.maxSamples < 1 || s.maxSamples > kMaxDelaySamples)
@@ -410,6 +425,7 @@ struct Delay {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
         l.params[0].max = static_cast<double>(s.maxSamples);
     }
+#endif
 
     // One ring of maxSamples + 1 per channel, in host-supplied memory: no heap, so it runs on the Pico.
     static std::size_t extra_memory(const Settings& s) {
@@ -463,6 +479,7 @@ struct Constant {
     struct { rp::control_out out; } outputs;
     struct { rp::float_param<"Value", rp::range{0, 1, 0}> value; } params;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (!std::isfinite(s.min) || !std::isfinite(s.max)) return std::string("min and max must be finite numbers");
         if (!(s.min <= s.max)) return std::string("min must not exceed max");
@@ -474,6 +491,7 @@ struct Constant {
         l.params[0].max = s.max;
         l.params[0].def = std::clamp(0.0, s.min, s.max);
     }
+#endif
 
     // The output follows the param: its value from frame 0 (the block-start value, or a frame-0 change),
     // then each later change at its own frame, skipping any that would not change the output.
@@ -499,6 +517,7 @@ struct BufferPlayer {
     struct { rp::resource_in<"audio-buffer"> sample; } inputs;
     struct { rp::audio_out<rp::stereo> out; } outputs;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannels(s.channels, "a player")) return e;
         return checkLayout(s.layout, s.channels);
@@ -506,6 +525,7 @@ struct BufferPlayer {
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[1].channels = ChannelSpec::fixed(s.channels, s.layout.str());
     }
+#endif
 
     bool               loop = true;
     std::uint32_t      position = 0;
@@ -542,10 +562,12 @@ struct BufferRecorder {
     struct { rp::resource_out<"audio-buffer"> buffer; } outputs;
     struct { rp::bool_param<"Record", true> record; } params;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a recorder"); }
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
     }
+#endif
 
     std::uint32_t      position = 0;
     bool               recording = false;   // Record as the last frame saw it
diff --git a/packages/dsp/native/src/dsp/Dither.cpp b/packages/dsp/native/src/dsp/Dither.cpp
index 31a1030ea..e1fb3fea6 100644
--- a/packages/dsp/native/src/dsp/Dither.cpp
+++ b/packages/dsp/native/src/dsp/Dither.cpp
@@ -16,6 +16,15 @@ static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<floa
 
 namespace {
 
+// The standard's check of mt19937 ([rand.predef]): the 10000th draw of a default-constructed engine. Held here so the
+// engine the kit's noise comes from is the one the standard specifies, draw for draw.
+constexpr bool tenThousandthDrawIsTheStandards() {
+    Mt19937 engine;
+    for (int i = 0; i < 9999; ++i) engine();
+    return engine() == 4123659995u;
+}
+static_assert(tenThousandthDrawIsTheStandards(), "Mt19937 is not the standard's mt19937");
+
 // One salt per TPDF algorithm: FNV-1a 64 of the algorithm's name, folded to 32 bits. A stable constant rather than
 // the enum's ordinal, so the order `DitherAlgorithm` (or the kit effect's `DitherType`) declares its algorithms in
 // cannot re-seed a kit that uses one of them.
@@ -25,7 +34,7 @@ constexpr std::uint32_t kShapedTPDFSalt   = 0x262f0818u; // fold(fnv1a64("Shaped
 // A uniform in [0, 1) from raw `mt19937` output, 24 bits at a time: `(x >> 8) * 2^-24`. The standard specifies
 // mt19937's sequence, so this draw is the same on every host, where `std::uniform_real_distribution`'s is not (the
 // three standard libraries disagree). A multiple of 2^-24 is exact in binary32, so the value is too.
-float uniform01(std::mt19937& rng) noexcept {
+float uniform01(Mt19937& rng) noexcept {
     return static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
 }
 
@@ -82,7 +91,7 @@ void Dither::init(DitherAlgorithm algo, int bitDepth, std::uint32_t seed) noexce
     prevDither = shapingState = 0.0f;
     err1 = err2 = 0.0f;
     error = 0.0f;
-    std::fill(std::begin(past), std::end(past), 0.0f);
+    for (float& e : past) e = 0.0f;
     pastCount = 0;
 }
 
diff --git a/packages/dsp/native/src/dsp/Dither.hpp b/packages/dsp/native/src/dsp/Dither.hpp
index 8be8c712a..ada5e022e 100644
--- a/packages/dsp/native/src/dsp/Dither.hpp
+++ b/packages/dsp/native/src/dsp/Dither.hpp
@@ -5,8 +5,9 @@
 // Six algorithms. `None` is plain rounding, which is what the kit path does when no dither is set; the other five are
 // the kit effect's, ported from the legacy build's AudioDithering helpers. Their arithmetic is the kit path's to the
 // bit: a code is `round(clamp(value, 0, 2^bits - 1))` of the input scaled by `(v + 1) * (2^bits - 1) / 2` plus that
-// algorithm's error or noise term. The two TPDF algorithms draw their noise from an `mt19937` seeded once, at `init`,
-// so the codes are a function of the input, the algorithm, the bit depth and the seed alone.
+// algorithm's error or noise term. The two TPDF algorithms draw their noise from an mt19937 (Mt19937.hpp, the engine
+// std::mt19937 is) seeded once, at `init`, so the codes are a function of the input, the algorithm, the bit depth and
+// the seed alone.
 //
 // Streaming: every algorithm's state is a few floats plus the noise generator (the error-diffusion forms keep their
 // past errors, one for ErrorDiffusion, two for Sierra Lite and four for JJN, rather than a padded copy of the buffer),
@@ -14,7 +15,8 @@
 
 #include <cstddef>
 #include <cstdint>
-#include <random>
+
+#include "dsp/Mt19937.hpp"
 
 namespace rp::dsp {
 
@@ -47,7 +49,7 @@ struct Dither {
     float           maxValue  = 15.0f;  // 2^bits - 1
     float           halfScale = 7.5f;   // maxValue / 2
 
-    std::mt19937 rng;                  // the TPDF noise, seeded at init
+    Mt19937      rng;                  // the TPDF noise, seeded at init
     float        prevDither   = 0.0f;  // HighPassTPDF
     float        shapingState = 0.0f;  // HighPassTPDF
     float        err1 = 0.0f, err2 = 0.0f;  // ShapedTPDF: the last two quantisation errors, newest first
diff --git a/packages/dsp/native/src/graph/nodes/DspNodes.cpp b/packages/dsp/native/src/graph/nodes/DspNodes.cpp
index 9b6e538ca..e1f3fe5f7 100644
--- a/packages/dsp/native/src/graph/nodes/DspNodes.cpp
+++ b/packages/dsp/native/src/graph/nodes/DspNodes.cpp
@@ -4,17 +4,16 @@
 #include <cmath>
 #include <cstddef>
 #include <cstdint>
-#include <iterator>
-#include <memory>
-#include <optional>
+#include <new>
 #include <span>
-#include <stdexcept>
-#include <string>
 #include <string_view>
 #include <type_traits>
+#ifndef RP_DEVICE  // what validate, adjust_layout and the DPCM node use: the desktop's
+#include <optional>
+#include <stdexcept>
+#include <string>
 #include <vector>
-
-#include <rfl/Literal.hpp>
+#endif
 
 #include "dsp/Biquad.hpp"
 #include "dsp/Dither.hpp"
@@ -35,9 +34,9 @@ namespace dsp {
 // The eight filters and the six dither forms, labelled as every other enum in a document is: the kebab-case of the
 // kernel enumerator it names (node.hpp's enum_param uses the same convention for a param), in the order the kernel
 // declares them, so a label's index is its enumerator's value (the static_asserts below hold that).
-using BiquadType = rfl::Literal<"low-pass", "high-pass", "band-pass", "band-stop", "peak", "low-shelf", "high-shelf",
-                                 "all-pass">;
-using DitherAlgorithm = rfl::Literal<"none", "high-pass-tpdf", "shaped-tpdf", "error-diffusion", "jjn", "sierra-lite">;
+using BiquadType = rp::choice<"low-pass", "high-pass", "band-pass", "band-stop", "peak", "low-shelf", "high-shelf",
+                              "all-pass">;
+using DitherAlgorithm = rp::choice<"none", "high-pass-tpdf", "shaped-tpdf", "error-diffusion", "jjn", "sierra-lite">;
 
 static_assert(BiquadType::value_of<"low-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::LowPass) &&
                   BiquadType::value_of<"high-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::HighPass) &&
@@ -159,11 +158,13 @@ struct Gain {
     // gain outside the range rather than letting the planner clamp it silently.
     struct { rp::float_param<"Gain", rp::range{-100.0, 100.0, 1.0}> gain; } params;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a gain"); }
 
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
     }
+#endif
 
     rp::dsp::Gain kernel;
     float         applied = 1.0f;  // the gain the last block ended at
@@ -253,11 +254,13 @@ struct Biquad {
         rp::float_param<"Gain", rp::range{-96.0, 96.0, 0.0}, "dB">           gainDb;  // Peak and the shelves only
     } params;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a biquad"); }
 
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
     }
+#endif
 
     static std::size_t extra_memory(const Settings& s) {
         return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Biquad);
@@ -282,7 +285,7 @@ struct Biquad {
         // because Biquad is trivially destructible: the static_assert above holds that.
         auto* const first = reinterpret_cast<rp::dsp::Biquad*>(mem.data());
         const std::size_t count = mem.size() / sizeof(rp::dsp::Biquad);
-        for (std::size_t i = 0; i < count; ++i) std::construct_at(first + i);
+        for (std::size_t i = 0; i < count; ++i) ::new (static_cast<void*>(first + i)) rp::dsp::Biquad();
         filters = std::span<rp::dsp::Biquad>(first, count);
     }
 
@@ -376,6 +379,7 @@ struct Dither {
     struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
     struct { rp::audio_out<rp::match<"in">> out; } outputs;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannels(s.channels, "a dither")) return e;
         if (s.bits < rp::dsp::kMinDitherBits || s.bits > rp::dsp::kMaxDitherBits)
@@ -387,6 +391,7 @@ struct Dither {
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
     }
+#endif
 
     static std::size_t extra_memory(const Settings& s) {
         return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Dither);
@@ -414,7 +419,7 @@ struct Dither {
         // destructible, which it is with its noise generator held inline.
         auto* const first = reinterpret_cast<rp::dsp::Dither*>(mem.data());
         const std::size_t count = mem.size() / sizeof(rp::dsp::Dither);
-        for (std::size_t i = 0; i < count; ++i) std::construct_at(first + i);
+        for (std::size_t i = 0; i < count; ++i) ::new (static_cast<void*>(first + i)) rp::dsp::Dither();
         states = std::span<rp::dsp::Dither>(first, count);
     }
 
@@ -494,6 +499,7 @@ struct Normalize {
 
     struct { rp::audio_out<rp::channels<2, "any">> out; } outputs;
 
+#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) {
         if (auto e = checkChannels(s.channels, "a normalise")) return e;
         if (s.buffer.empty()) return "a normalise names an audio buffer";
@@ -502,6 +508,7 @@ struct Normalize {
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
     }
+#endif
 
     const rp_audio_buffer* buffer = nullptr;  // pinned by the instance, so it outlives every block
     float                  gain = 1.0f;       // the setting, as the last commit left it
@@ -550,6 +557,10 @@ struct Normalize {
 
 // ---- rp.dsp.dpcm --------------------------------------------------------------------------------------------------
 
+// The DPCM node declares `heap`, so it is never built for the device (node_device.hpp): the planner refuses it on the
+// Pico, and a device build leaves it out of the package.
+#ifndef RP_DEVICE
+
 // risa's DMC encoder as a node: at activate, on the control thread, the kernel (src/dsp/Dpcm.hpp) runs over the whole
 // bound buffer into storage this instance owns, and every block after that streams one delta bit per frame as an audio
 // level: +1 for a one, -1 for a zero, silence past the buffer's end. The bits are the risa bank's payload rather than
@@ -638,6 +649,7 @@ struct Dpcm {
         position = n < available ? at + n : total;
     }
 };
+#endif  // RP_DEVICE
 
 }  // namespace
 
@@ -647,7 +659,9 @@ const rp_package* dspPackage() {
         rp::node_type<Biquad>(),
         rp::node_type<Dither>(),
         rp::node_type<Normalize>(),
+#ifndef RP_DEVICE
         rp::node_type<Dpcm>(),
+#endif
     };
     static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp","version":"1.0.0"})",
                                      static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
diff --git a/packages/native/cmake/warnings.cmake b/packages/native/cmake/warnings.cmake
index db6d69681..b5b6ce6f4 100644
--- a/packages/native/cmake/warnings.cmake
+++ b/packages/native/cmake/warnings.cmake
@@ -41,7 +41,12 @@ if(MSVC)
         # The workspace packages' targets (cmake/packages.cmake), defined in their own directories: a target's options
         # can be set from any directory, so the list stays here, where every one of our targets is named.
         retroplug-node-sdk retroplug-graph-exec retroplug-graph retroplug-core-nodes
-        retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes retroplug-dsp-resample-nodes)
+        retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes retroplug-dsp-resample-nodes
+        # The device build of the node SDK, tested on the desktop (cmake/tests.cmake).
+        retroplug-device-mode-objects retroplug-device-mode-test)
+    # And each device entry's device-mode compile (cmake/packages.cmake's _retroplug_device_check).
+    get_property(_rp_device_checks GLOBAL PROPERTY RP_DEVICE_CHECK_TARGETS)
+    list(APPEND _rp_werror_targets ${_rp_device_checks})
     foreach(_t IN LISTS _rp_werror_targets)
         if(TARGET ${_t})
             target_compile_options(${_t} PRIVATE ${_rp_warn_flags})
````

Run: `sed -n '10104,10631p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0. The one warning this build prints is the known `-Wformat-truncation` in `plugin/PluginDSP.cpp:186`, which recompiles here because the SDK headers changed (the stage's first build at this point printed it, `s3-build.log`: `PluginDSP.cpp:186:59: warning:`); it is pre-existing, so leave it. Among the build's lines are `Built target retroplug-dsp-nodes-device` and `Built target retroplug-core-nodes-device`: both device checks build in the default build.

Run: `cmake --build build --target retroplug-device-mode-test retroplug-graph-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (2258 assertions in 6 test cases)`.

Run: `build/bin/retroplug-graph-test`
Expected: `All tests passed (356435 assertions in 282 test cases)`: Task 2's 279 cases, `[fields]`, `[fields][enums]` and the `Mt19937` sweep, the descriptor goldens unchanged.

- [ ] **Step 4: Show that the twin comparison bites**

The twin table is what proves the device build renders what the desktop renders. Apply this temporary mutation, which
makes the msgpack decode scale every float it reads by 1.0001, and the device-mode test must fail; then reverse it.

````patch
diff --git a/packages/node-sdk/native/src/graph/settings_msgpack.hpp b/packages/node-sdk/native/src/graph/settings_msgpack.hpp
--- a/packages/node-sdk/native/src/graph/settings_msgpack.hpp
+++ b/packages/node-sdk/native/src/graph/settings_msgpack.hpp
@@ -159,7 +159,7 @@
         double       d = 0.0;
         if (r.readInt(v)) d = v.negative ? static_cast<double>(v.i) : static_cast<double>(v.u);
         else if (!r.readFloat(d)) return fail(why, "Could not cast to double.");
-        out = static_cast<F>(d);
+        out = static_cast<F>(d * 1.0001);
     } else if constexpr (std::is_enum_v<F>) {
         // The enumerator's name, or a number in the string, as reflect-cpp's string_to_enum reads one.
         std::string_view s;
````

Run: `sed -n '10658,10669p' docs/modular-graph-phase-e1-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake --build build --target retroplug-device-mode-test -j$(nproc)`
Expected: exit 0.

Run: `build/bin/retroplug-device-mode-test`
Expected: fails: the twin case `every portable rp.core and rp.dsp type renders, built for the device, what the desktop's build renders` among the failures, naming its table row `normalize, gain and channels moved` (the stage's run of this mutation: `a decode that scaled floats by 1.0001 fails "normalize, gain and channels moved"`); a case that compares a float decoded from msgpack with JSON's may fail beside it.

Run: `sed -n '10658,10669p' docs/modular-graph-phase-e1-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `packages/node-sdk/native/src/graph/settings_msgpack.hpp` is the task's file again (`git diff` of it shows only the task's own change, if the task changes it at all). Then `cmake --build build --target retroplug-device-mode-test -j$(nproc)` and `build/bin/retroplug-device-mode-test`: `All tests passed (2258 assertions in 6 test cases)` again.

- [ ] **Step 5: Show that an unguarded hook fails the desktop build**

The user's rule is one `#ifndef RP_DEVICE` around a device-capable node's desktop hooks, and the desktop build
enforces it. Apply this temporary mutation, which takes the guard off the buffer recorder's `validate` and
`adjust_layout`, and the recorder's device check must fail to compile; then reverse it.

````patch
diff --git a/packages/core/native/src/graph/nodes/CoreNodes.cpp b/packages/core/native/src/graph/nodes/CoreNodes.cpp
--- a/packages/core/native/src/graph/nodes/CoreNodes.cpp
+++ b/packages/core/native/src/graph/nodes/CoreNodes.cpp
@@ -562,12 +562,10 @@
     struct { rp::resource_out<"audio-buffer"> buffer; } outputs;
     struct { rp::bool_param<"Record", true> record; } params;
 
-#ifndef RP_DEVICE
     static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a recorder"); }
     static void adjust_layout(Layout& l, const Settings& s) {
         l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
     }
-#endif
 
     std::uint32_t      position = 0;
     bool               recording = false;   // Record as the last frame saw it
````

Run: `sed -n '10691,10706p' docs/modular-graph-phase-e1-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake --build build --target retroplug-core-nodes-device -j$(nproc)`
Expected: fails compiling `CoreNodes.cpp` in device mode: the hooks name what the device build does not declare, the helper `checkChannels` and the desktop's `std::optional` among them, and the layout is only declared there, so among the errors is `invalid use of incomplete type 'struct rp::device::graph::Layout'` (the stage's words for an `adjust_layout` left outside its guard). The desktop's own build of the file still compiles: only the device check sees it. (Predicted from the code.)

Run: `sed -n '10691,10706p' docs/modular-graph-phase-e1-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `packages/core/native/src/graph/nodes/CoreNodes.cpp` is the task's file again (`git diff` of it shows only the task's own change, if the task changes it at all). Then `cmake --build build --target retroplug-core-nodes-device -j$(nproc)` exits 0 again.

- [ ] **Step 6: Show that a refused source must stay refused**

The three refused sources are held at every configure, so a device check that stopped firing fails the configure
rather than going unnoticed. Apply this temporary mutation, which makes `HeapType.cpp`'s type declare no heap, so it
compiles in device mode, and the configure must stop; then reverse it.

````patch
diff --git a/packages/native/test/device-mode/refused/HeapType.cpp b/packages/native/test/device-mode/refused/HeapType.cpp
--- a/packages/native/test/device-mode/refused/HeapType.cpp
+++ b/packages/native/test/device-mode/refused/HeapType.cpp
@@ -4,7 +4,7 @@
 namespace {
 struct Heavy {
     static constexpr const char* id = "x.heavy";
-    static constexpr bool        heap = true;
+    static constexpr bool        heap = false;
     void process(const rp::Block&) noexcept {}
 };
 }  // namespace
````

Run: `sed -n '10725,10736p' docs/modular-graph-phase-e1-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake -S . -B build`
Expected: the configure stops with `CMake Error at packages/native/cmake/tests.cmake:375 (message):` and `test/device-mode/refused/HeapType.cpp compiled in device mode; it must not` (the stage flipped `heap = true` to false and the configure failed, its report says).

Run: `sed -n '10725,10736p' docs/modular-graph-phase-e1-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `packages/native/test/device-mode/refused/HeapType.cpp` is the task's file again (`git diff` of it shows only the task's own change, if the task changes it at all). Then `cmake -S . -B build` exits 0 again.

- [ ] **Step 7: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 and no warning in the prototype's log (the one known warning, `-Wformat-truncation` in `plugin/PluginDSP.cpp`, appears only when that file recompiles).

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 174 test file(s) passed (jobs=16)`: the descriptor conformance and `buildGraph`'s goldens inside it pass, unchanged.

Run: `pnpm test:plugin`
Expected: all 11 binaries exit 0, `# 11 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 313 | 302 passed | 11 skipped`; graph-test `All tests passed (356435 assertions in 282 test cases)`; `retroplug-device-mode-test` `All tests passed (2258 assertions in 6 test cases)`; audio-test `All tests passed (1234835 assertions in 108 test cases)`; launchpad `All tests passed (144 assertions in 29 test cases)`; the small six at their own counts (midi 35 / 19, dynparams 24 / 7, plugin 21 / 6, lottie 7 / 1, classid 10 / 3, watcher 11 / 3).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1712608 assertions in 313 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: the same 167 files, with `# 144 case(s) SKIPPED in 65 file(s)` and the skip check passing: CI's view. Nothing in E1 adds a skip, so the skip baseline does not move.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed (jobs=16)`.

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` (fakes only, no hardware).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, then `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa renders match the golden's hashes`, then 5 passed, 0 skipped, 0 failed.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed.

Run: `bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'`
Expected: exit 0: `render-menu` and `render-badge` 1 passed each, then `# 7 test file(s) passed (jobs=16)`.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-bliptoaster && mkdir -p /tmp/e1-bliptoaster/build /tmp/e1-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/e1-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/e1-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/e1-bliptoaster/retroplug-cli/ && cd /tmp/e1-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/e1-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed. The engine's nodes read the same JSON, so a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-nesvj && mkdir -p /tmp/e1-nesvj/build /tmp/e1-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/e1-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/e1-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/e1-nesvj/retroplug-cli/ && cd /tmp/e1-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/e1-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed. The engine's nodes read the same JSON, so a consumer's suite does not change.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 8: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc)`
Expected: exit 0: the ThreadSanitizer tree's two binaries, built from this task's tree (a stale binary proves nothing).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag; the list is the verification script's), then `All tests passed (1698301 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), and no ThreadSanitizer report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test -j$(nproc)`
Expected: exit 0: the AddressSanitizer tree's three binaries.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (1680309 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356435 assertions in 282 test cases)`, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-device-mode-test -j$(nproc)`
Expected: exit 0.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test`
Expected: `All tests passed (2258 assertions in 6 test cases)`, no AddressSanitizer or LeakSanitizer report (the report's `| ASan | device-mode test | 2258 / 6 |`, its first fix pass; the second changed no device-mode case).

- [ ] **Step 9: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M cmake/packages.cmake
 m deps/sameboy
 M package.json
 M packages/core/native/src/graph/nodes/CoreNodes.cpp
 M packages/dsp/native/src/dsp/Dither.cpp
 M packages/dsp/native/src/dsp/Dither.hpp
 M packages/dsp/native/src/graph/nodes/DspNodes.cpp
 M packages/native/cmake/test-binary.cmake
 M packages/native/cmake/tests.cmake
 M packages/native/cmake/warnings.cmake
 M packages/native/test/graph/Dsp.test.cpp
 M packages/node-sdk/native/CMakeLists.txt
 M packages/node-sdk/native/src/graph/Descriptor.hpp
 M packages/node-sdk/native/src/graph/GraphDocument.cpp
 M packages/node-sdk/native/src/graph/node.hpp
 M packages/node-sdk/native/src/graph/rp_node.h
 M packages/node-sdk/native/src/graph/settings_types.hpp
 M packages/retroplug/scripts/run-plugin-tests.mjs
?? packages/dsp/native/src/dsp/Mt19937.hpp
?? packages/native/test/device-mode/DeviceMode.test.cpp
?? packages/native/test/device-mode/DeviceModeEntries.hpp
?? packages/native/test/device-mode/DeviceModeNodes.cpp
?? packages/native/test/device-mode/EchoNode.hpp
?? packages/native/test/device-mode/refused/HeapHeader.cpp
?? packages/native/test/device-mode/refused/HeapType.cpp
?? packages/native/test/device-mode/refused/PointerSettings.cpp
?? packages/native/test/graph/Fields.test.cpp
?? packages/node-sdk/native/src/graph/JsonToMsgpack.cpp
?? packages/node-sdk/native/src/graph/JsonToMsgpack.hpp
?? packages/node-sdk/native/src/graph/PortTypes.hpp
?? packages/node-sdk/native/src/graph/ResourceId.hpp
?? packages/node-sdk/native/src/graph/enums.hpp
?? packages/node-sdk/native/src/graph/fields.hpp
?? packages/node-sdk/native/src/graph/msgpack_reader.hpp
?? packages/node-sdk/native/src/graph/node_core.hpp
?? packages/node-sdk/native/src/graph/node_desc.hpp
?? packages/node-sdk/native/src/graph/node_device.hpp
?? packages/node-sdk/native/src/graph/sdk_namespace.hpp
?? packages/node-sdk/native/src/graph/settings_json.hpp
?? packages/node-sdk/native/src/graph/settings_msgpack.hpp
````

````bash
git add -- cmake/packages.cmake package.json packages/core/native/src/graph/nodes/CoreNodes.cpp \
  packages/dsp/native/src/dsp/Dither.cpp packages/dsp/native/src/dsp/Dither.hpp \
  packages/dsp/native/src/dsp/Mt19937.hpp packages/dsp/native/src/graph/nodes/DspNodes.cpp \
  packages/native/cmake/test-binary.cmake packages/native/cmake/tests.cmake packages/native/cmake/warnings.cmake \
  packages/native/test/device-mode/DeviceMode.test.cpp packages/native/test/device-mode/DeviceModeEntries.hpp \
  packages/native/test/device-mode/DeviceModeNodes.cpp packages/native/test/device-mode/EchoNode.hpp \
  packages/native/test/device-mode/refused/HeapHeader.cpp packages/native/test/device-mode/refused/HeapType.cpp \
  packages/native/test/device-mode/refused/PointerSettings.cpp packages/native/test/graph/Dsp.test.cpp \
  packages/native/test/graph/Fields.test.cpp packages/node-sdk/native/CMakeLists.txt \
  packages/node-sdk/native/src/graph/Descriptor.hpp packages/node-sdk/native/src/graph/GraphDocument.cpp \
  packages/node-sdk/native/src/graph/JsonToMsgpack.cpp packages/node-sdk/native/src/graph/JsonToMsgpack.hpp \
  packages/node-sdk/native/src/graph/PortTypes.hpp packages/node-sdk/native/src/graph/ResourceId.hpp \
  packages/node-sdk/native/src/graph/enums.hpp packages/node-sdk/native/src/graph/fields.hpp \
  packages/node-sdk/native/src/graph/msgpack_reader.hpp packages/node-sdk/native/src/graph/node.hpp \
  packages/node-sdk/native/src/graph/node_core.hpp packages/node-sdk/native/src/graph/node_desc.hpp \
  packages/node-sdk/native/src/graph/node_device.hpp packages/node-sdk/native/src/graph/rp_node.h \
  packages/node-sdk/native/src/graph/sdk_namespace.hpp packages/node-sdk/native/src/graph/settings_json.hpp \
  packages/node-sdk/native/src/graph/settings_msgpack.hpp packages/node-sdk/native/src/graph/settings_types.hpp \
  packages/retroplug/scripts/run-plugin-tests.mjs
git commit -F - <<'EOF'
feat(node-sdk): a device build of node.hpp, and settings as msgpack

graph/node.hpp is built two ways. Under RP_DEVICE it is the portable core alone (node_core.hpp, node_device.hpp):
the port and param wrappers, Block, the compile-time reflection, the hook checks, enum_param, and an instance that
owns its settings in place, read from msgpack, with a fixed array of resource pins; descriptor, layout_for and the
settings swap are null, nothing catches, and the build refuses the heap and exception headers, reflect-cpp's JSON
and yyjson. The desktop's build (node_desc.hpp) keeps the description layer, JSON settings and the heap-settings
swap. A device type that declares heap or threads, or whose Settings holds anything but numbers, bools, enums,
rp::choice, rp::text, rp::list and rp::resource_ref, is a static_assert naming it, and the desktop's default build
compiles every `device` package entry in device mode too, so an unguarded description hook fails there as well.

RP_SETTINGS_MSGPACK is read on both builds by one allocation-free decode over the compile-time field list (our own
reader: reflect-cpp's needs msgpack-c, a heap zone and std::string errors), matched by name with JSON's leniency and
refusals, in JSON's words. settingsToMsgpack converts canonical JSON settings, keeping each number the kind its text
spells. The field walk is reflect-cpp's internal core with our own name extraction, held to rfl::fields by a
graph-test case; enum names come from a small heap-free scan held to reflect-cpp's at compile time. rp::choice
replaces rfl::Literal in the core and DSP settings, the dither kernel's std::mt19937 becomes the same engine without
<random>, and the core and DSP nodes guard their desktop hooks; descriptors do not change.

retroplug-device-mode-test, the eleventh test:plugin binary, builds rp.core and rp.dsp in device mode beside the
desktop's (in rp::device, an inline namespace) and holds them to the desktop's output bit for bit through real plans
over ragged blocks, with no allocation in any device-mode call; the configure holds what a device build refuses.
EOF
````

---

### Task 4: the RP2350 device check, with a link trap on the heap and on exceptions

Design section 7 and section 9's fourth commit. `rp-device-check` (`packages/graph-runtime/device-check/`) is a Pico
SDK firmware for the RP2350, its own CMake project on the SDK's toolchain (`RP_DEVICE_BUILD`), built in
`build-device/`. It is a DEVICE product of `device-check/packages.cmake`'s list (`core dsp`): `cmake/packages.cmake`
learns a device build, in which a package defines its device targets alone and compiles them as a device does, and
DEVICE products, which take each package's `device` entries alone. For each portable type the image creates it from
its default settings as msgpack in a static 400 KiB arena, activates it, runs one 64-frame block through the executor
and destroys it, and prints one result line on the UART. The cases come from `rp-device-check-cases`, a desktop DEVICE
product of the same list that reads each type's defaults through a new desktop SDK interface,
`rp.sdk.settings-json/1`, and writes `DeviceCheckCases.hpp`, a build artifact. The image links with `--wrap` on 78
symbols and defines none of them, so a heap call or a throw anywhere in what it links is a link error naming the
object; a probe that allocates must fail to link on every run. `retroplug-device-mode-test` runs the same check on the
desktop over the device-mode objects. CI's linux-x64 job builds the image; nothing is flashed or run in E1.

Two of the stage's findings shape it. The runtime is the SDK's own less `pico_malloc` alone: `pico_malloc` defines the
wrapped malloc family, so a call to malloc would resolve to its wrapper and the trap would fire only at newlib's
`malloc.o`, while stdio over the UART and the SDK's printf link under the full trap (the stage's first build failed at
newlib's `findfp.o` from the default panic's `puts` in an image with no stdio, not from a heap inside stdio). And
nothing portable reaches the heap: the image links `core` and `dsp` under the full trap and it names nothing in them;
the stage proved the trap's reach with a `new int(7)` in `main` and a `new std::vector<float>` in the gain kernel, each
failing naming its object, and the probe now proves it on every run.

**Files:**

- Create: `packages/graph-runtime/device-check/CMakeLists.txt`, `packages/graph-runtime/device-check/DeviceCheck.cpp`, `packages/graph-runtime/device-check/DeviceCheck.hpp`, `packages/graph-runtime/device-check/cases/CMakeLists.txt`, `packages/graph-runtime/device-check/cases/main.cpp`, `packages/graph-runtime/device-check/check-trap.cmake`, `packages/graph-runtime/device-check/main.cpp`, `packages/graph-runtime/device-check/packages.cmake`, `packages/graph-runtime/device-check/trap-probe.cpp`, `packages/native/test/device-mode/DeviceCheck.test.cpp`, `tools/build-device-check.sh`
- Modify: `.github/workflows/build.yml`, `.gitignore`, `CMakeLists.txt`, `cmake/packages.cmake`, `package.json`, `packages/dsp/native/CMakeLists.txt`, `packages/graph-runtime/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`, `packages/native/cmake/warnings.cmake`, `packages/native/test/device-mode/DeviceModeEntries.hpp`, `packages/node-sdk/native/CMakeLists.txt`, `packages/node-sdk/native/src/graph/node_desc.hpp`

- [ ] **Step 1: A desktop interface for a node's settings as JSON**

The case generator needs each portable type's default settings, and the ABI has no way to get them (the descriptor
schema has no defaults, and adding them would move goldens). `node_desc.hpp` gains `rp::graph::kSettingsJsonInterface`
(`"rp.sdk.settings-json/1"`) and `SettingsJsonInterface::write`, which writes an instance's settings back as JSON,
every field; every desktop `node.hpp` type answers it through `get_interface`, one string compare at plan time. A
device-mode build, which has no JSON writer, does not.

````diff
diff --git a/packages/node-sdk/native/src/graph/node_desc.hpp b/packages/node-sdk/native/src/graph/node_desc.hpp
index ccc1a5ade..4c984055a 100644
--- a/packages/node-sdk/native/src/graph/node_desc.hpp
+++ b/packages/node-sdk/native/src/graph/node_desc.hpp
@@ -35,6 +35,23 @@
 
 namespace rp {
 
+namespace graph {
+
+// The settings an instance holds, written back as JSON: every field, a default as much as a value its document gave.
+// An interface of the SDK's rather than the ABI's: every node graph/node.hpp builds for the desktop answers it through
+// get_interface, and a device-mode build, which has no JSON writer, does not; nor does a type written against
+// rp_node.h by hand. What reads it is desktop tooling: the device check's case generator
+// (packages/graph-runtime/device-check) writes each portable type's default settings through it, and converts them to
+// the msgpack a device reads. [control], and not while a commit_settings for the same instance may be in flight.
+inline constexpr const char* kSettingsJsonInterface = "rp.sdk.settings-json/1";
+struct SettingsJsonInterface {
+    // The JSON, NUL-terminated into out[0..capacity) when it fits; its length without the NUL either way, so a caller
+    // whose buffer is too small learns the size and asks again (as layout_for answers). 0 if writing it failed.
+    std::size_t (*write)(rp_node* node, char* out, std::size_t capacity);
+};
+
+}  // namespace graph
+
 namespace detail {
 
 // The two reflect-cpp field wrappers the field walk does not follow (fields.hpp), marked so the walk refuses them.
@@ -476,10 +493,34 @@ void releaseFn(rp_node*, void* old) noexcept {
     delete static_cast<typename SettingsOf<T>::type*>(old);
 }
 
+template <class T>
+std::size_t writeSettingsFn(rp_node* n, char* out, std::size_t capacity) noexcept {
+    using S = typename SettingsOf<T>::type;
+    try {
+        std::string json = "{}";
+        if constexpr (!std::is_same_v<S, NoSettings>) json = rfl::json::write(*self<T>(n)->settings);
+        if (json.size() < capacity) {
+            std::memcpy(out, json.data(), json.size());
+            out[json.size()] = '\0';
+        }
+        return json.size();
+    } catch (...) {
+        return 0;
+    }
+}
+
+// The portable core's interfaces (rp.step/1, rp.link/1, the node's own), and the desktop's settings writer.
+template <class T>
+const void* desktopInterfaceFn(rp_node* n, const char* id) noexcept {
+    static constexpr graph::SettingsJsonInterface kSettings{&writeSettingsFn<T>};
+    if (std::string_view(id) == graph::kSettingsJsonInterface) return &kSettings;
+    return interfaceFn<T>(n, id);
+}
+
 template <class T>
 inline constexpr rp_node_vtable kVtable{&activateFn<T>, &deactivateFn<T>, &portsChangedFn<T>,
                                         &processFn<T>,  &prepareFn<T>,    &commitFn<T>,
-                                        &releaseFn<T>,  &interfaceFn<T>};
+                                        &releaseFn<T>,  &desktopInterfaceFn<T>};
 
 // An empty descriptor when building it throws (adjust_layout on default settings, say), which the registry
 // refuses with a reason rather than letting the exception reach it.
````

Run: `sed -n '10984,11047p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake --build build --target retroplug-graph-test retroplug-device-mode-test -j$(nproc)`
Expected: exit 0.

Run: `bash -c 'build/bin/retroplug-graph-test | tail -2; build/bin/retroplug-device-mode-test | tail -2'`
Expected: `All tests passed (356435 assertions in 282 test cases)` and `All tests passed (2258 assertions in 6 test cases)`: Task 3's counts, unchanged.

- [ ] **Step 2: The device build in the package CMake**

`cmake/packages.cmake` finds the packages relative to itself (the device project includes it from another source dir),
records each package's device targets, skips the desktop's `-device` checks in a device build, and there builds each
package library as a device compiles `node.hpp` (`RP_DEVICE`, `-fno-exceptions -fno-rtti`, the options the device
project names in `RP_DEVICE_COMPILE_OPTIONS`, not position-independent). `retroplug_product` takes `DEVICE`: the
listed packages' `device` entries and their targets alone, a package with none refused, the registration filled per
call into a zero-initialised array (an initialised one would need a guard), and the product kept out of the engine's
golden table. In a device build `node-sdk`'s `retroplug-node-sdk` is an INTERFACE of headers with reflect-cpp's
include root from `RP_REFLECTCPP_INCLUDE_DIR`, `graph-runtime` defines the executor alone, and `dsp` stops before
r8brain (its file reordered so the resampler's targets come after `retroplug-dsp-nodes`; no target changes). Nothing
in the desktop build changes.

````diff
diff --git a/cmake/packages.cmake b/cmake/packages.cmake
index 1e6c65b12..ceff6f08c 100644
--- a/cmake/packages.cmake
+++ b/cmake/packages.cmake
@@ -20,7 +20,12 @@
 # naming it, but a Linux module links and fails only when it is loaded. So the configure checks it instead, for every
 # binary: retroplug_check_products() refuses one that links the engine's libraries and is not a product.
 
-set(RP_PACKAGES_DIR "${CMAKE_SOURCE_DIR}/packages")
+# The device build (docs/modular-graph-phase-e1-design.md section 7) includes this file too: the RP2350 firmware
+# packages/graph-runtime/device-check, its own CMake project on the Pico SDK's toolchain, sets RP_DEVICE_BUILD before it
+# does. Then a package's native/CMakeLists.txt defines its device targets alone, retroplug_package_library() compiles
+# them as a device compiles graph/node.hpp, and a product is a DEVICE product (retroplug_product() says what that is).
+# The packages are found from this file, not from the including project's source dir, so either project finds them.
+get_filename_component(RP_PACKAGES_DIR "${CMAKE_CURRENT_LIST_DIR}/../packages" ABSOLUTE)
 set(RP_PRODUCTS_DIR "${CMAKE_BINARY_DIR}/packages/products")
 
 # retroplug_package(<name>): reads packages/<name>/package.json's manifest and, when the package has a
@@ -93,20 +98,24 @@ function(retroplug_package name)
         endif()
     endforeach()
     list(REMOVE_DUPLICATES _targets)
+    list(REMOVE_DUPLICATES _deviceTargets)
 
     set_property(GLOBAL APPEND PROPERTY RP_PACKAGES ${name})
     set_property(GLOBAL APPEND PROPERTY RP_PACKAGE_ENTRIES ${_entries})
     set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_TARGETS ${_targets})
     set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_ENTRIES ${_entries})
     set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_ENTRIES ${_device})
+    set_property(GLOBAL PROPERTY RP_PACKAGE_${name}_DEVICE_TARGETS ${_deviceTargets})
 
     if(EXISTS "${_dir}/native/CMakeLists.txt")
         add_subdirectory("${_dir}/native" "${CMAKE_BINARY_DIR}/packages/${name}")
     endif()
-    list(REMOVE_DUPLICATES _deviceTargets)
-    foreach(_target IN LISTS _deviceTargets)
-        _retroplug_device_check(${name} ${_target})
-    endforeach()
+    # A device build compiles every target as a device does already, so it has nothing to check twice.
+    if(NOT RP_DEVICE_BUILD)
+        foreach(_target IN LISTS _deviceTargets)
+            _retroplug_device_check(${name} ${_target})
+        endforeach()
+    endif()
 endfunction()
 
 # _retroplug_device_check(<package> <target>): the desktop build's check of a `device` entry. An entry the manifest
@@ -161,23 +170,40 @@ endfunction()
 # shares. Position-independent, as each is linked into the plugin's shared module, and /bigobj on MSVC: reflect-cpp's
 # templates instantiate inside any translation unit that includes graph/node.hpp, enough COMDAT sections to pass COFF's
 # 2^16 limit (C1128), which packages/native sets directory-wide for its own and a package's directory does not inherit.
+#
+# In a device build it is the device's library instead: graph/node.hpp's device mode (RP_DEVICE), no exceptions and no
+# RTTI, and the code-generation options the device project names in RP_DEVICE_COMPILE_OPTIONS (the Pico SDK's, which
+# its own sources compile with), on top of what the device's toolchain adds to every compile; not
+# position-independent, as a firmware image links it statically.
 function(retroplug_package_library target)
     add_library(${target} STATIC ${ARGN})
-    set_target_properties(${target} PROPERTIES
-        CXX_STANDARD 20
-        CXX_STANDARD_REQUIRED ON
-        POSITION_INDEPENDENT_CODE ON)
+    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
+    if(RP_DEVICE_BUILD)
+        target_compile_definitions(${target} PRIVATE RP_DEVICE)
+        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>"
+                               ${RP_DEVICE_COMPILE_OPTIONS})
+        return()
+    endif()
+    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
     if(MSVC)
         target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
     endif()
 endfunction()
 
-# retroplug_product(<product> PACKAGES <package>... [TARGETS <target>...]): declares a product and the packages it
-# carries, in registration order. Generates build/packages/products/<product>.cpp, which defines the product's
+# retroplug_product(<product> PACKAGES <package>... [TARGETS <target>...] [DEVICE]): declares a product and the packages
+# it carries, in registration order. Generates build/packages/products/<product>.cpp, which defines the product's
 # rp_register_packages(), and adds it to each of TARGETS (default: the target named <product>) with the packages'
 # targets linked. TARGETS is for one product built as several binaries: the plugin's formats, each its own module.
+#
+# DEVICE makes it a device product: it takes each package's `device` entries alone, and their targets, and a package
+# with none is refused, since a device product that names one would carry a node type the device cannot run (as the
+# planner refuses one on the Pico). A device build's products are all device products: the RP2350 check, whose targets
+# are device-mode libraries. A desktop build may declare one too, which links the desktop's builds of the same entries:
+# the device check's case generator, which lists what the device registers. A device product constructs no engine, so
+# it is not in the engine's registration golden (retroplug_product_table); retroplug-device-mode-test holds what a
+# device registers instead.
 function(retroplug_product product)
-    cmake_parse_arguments(P "" "" "PACKAGES;TARGETS" ${ARGN})
+    cmake_parse_arguments(P "DEVICE" "" "PACKAGES;TARGETS" ${ARGN})
     if(P_UNPARSED_ARGUMENTS)
         message(FATAL_ERROR "retroplug_product(${product}): unrecognised argument(s): ${P_UNPARSED_ARGUMENTS}")
     endif()
@@ -189,22 +215,38 @@ function(retroplug_product product)
     if(NOT P_PACKAGES)
         message(FATAL_ERROR "retroplug_product(${product}): PACKAGES is required")
     endif()
+    if(RP_DEVICE_BUILD AND NOT P_DEVICE)
+        message(FATAL_ERROR "retroplug_product(${product}): a device build declares DEVICE products alone")
+    endif()
     if(NOT P_TARGETS)
         set(P_TARGETS ${product})
     endif()
     get_property(_products GLOBAL PROPERTY RP_PRODUCTS)
-    if(product IN_LIST _products)
+    get_property(_deviceProducts GLOBAL PROPERTY RP_DEVICE_PRODUCTS)
+    if(product IN_LIST _products OR product IN_LIST _deviceProducts)
         message(FATAL_ERROR "retroplug_product(${product}): the product is declared twice")
     endif()
     get_property(_known GLOBAL PROPERTY RP_PACKAGES)
     set(_entries "")
     set(_targets "")
+    set(_leftOut "")
     foreach(_p IN LISTS P_PACKAGES)
         if(NOT _p IN_LIST _known)
             message(FATAL_ERROR "retroplug_product(${product}): no package `${_p}` (retroplug_package() adds one)")
         endif()
         get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_ENTRIES)
         get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_TARGETS)
+        if(P_DEVICE)
+            set(_all ${_e})
+            get_property(_e GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
+            get_property(_t GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_TARGETS)
+            if(NOT _e)
+                message(FATAL_ERROR "retroplug_product(${product}): package `${_p}` has no `device` entry "
+                                    "(${RP_PACKAGES_DIR}/${_p}/package.json), so a device product cannot carry it")
+            endif()
+            list(REMOVE_ITEM _all ${_e})
+            list(APPEND _leftOut ${_all})
+        endif()
         # Every package's targets exist by the first product (the packages are added before packages/native, whose
         # retroplug-backend comes before its first product), so a name that is not one is a manifest typo, refused here
         # rather than left to the linker as a bare -l flag.
@@ -223,6 +265,17 @@ function(retroplug_product product)
     list(LENGTH _entries _n)
     if(_n EQUAL 0)
         set(_body "    *count = 0;\n    return nullptr;\n")
+    elseif(P_DEVICE)
+        # Filled on every call rather than initialised: a static initialised by calls needs a guard
+        # (__cxa_guard_acquire), which a device image does not link (the device check's link trap refuses one).
+        # Every call writes the same pointers, and a device product calls it once.
+        set(_body "    static const rp_package* packages[${_n}];\n")
+        set(_i 0)
+        foreach(_e IN LISTS _entries)
+            string(APPEND _body "    packages[${_i}] = ${_e}();\n")
+            math(EXPR _i "${_i} + 1")
+        endforeach()
+        string(APPEND _body "    *count = ${_n};\n    return packages;\n")
     else()
         set(_calls "")
         foreach(_e IN LISTS _entries)
@@ -232,10 +285,18 @@ function(retroplug_product product)
         string(APPEND _body "    *count = ${_n};\n    return packages;\n")
     endif()
     list(JOIN P_PACKAGES " " _listed)
+    set(_kind "")
+    if(P_DEVICE)
+        set(_kind ", a device product,")
+        if(_leftOut)
+            list(JOIN _leftOut " " _leftOut)
+            string(APPEND _listed ";\n// their entries that are not `device`, left out: ${_leftOut}")
+        endif()
+    endif()
     set(_src "${RP_PRODUCTS_DIR}/${product}.cpp")
     file(GENERATE OUTPUT "${_src}" CONTENT
-"// Generated by retroplug_product(${product}) in cmake/packages.cmake from the manifests of the packages it lists:
-// ${_listed}. Do not edit; edit the product's PACKAGES or a manifest.
+"// Generated by retroplug_product(${product})${_kind} in cmake/packages.cmake from the manifests of the packages it
+// lists: ${_listed}. Do not edit; edit the product's PACKAGES or a manifest.
 
 #include \"graph/Packages.hpp\"
 
@@ -250,6 +311,11 @@ ${_body}}
         target_sources(${_t} PRIVATE "${_src}")
         target_link_libraries(${_t} PRIVATE ${_targets})
     endforeach()
+    if(P_DEVICE)
+        set_property(GLOBAL APPEND PROPERTY RP_DEVICE_PRODUCTS ${product})
+        set_property(GLOBAL PROPERTY RP_DEVICE_PRODUCT_${product}_ENTRIES ${_entries})
+        return()
+    endif()
     set_property(GLOBAL APPEND PROPERTY RP_PRODUCTS ${product})
     set_property(GLOBAL APPEND PROPERTY RP_PRODUCT_TARGETS ${P_TARGETS})
     set_property(GLOBAL PROPERTY RP_PRODUCT_${product}_ENTRIES ${_entries})
diff --git a/packages/dsp/native/CMakeLists.txt b/packages/dsp/native/CMakeLists.txt
index c7fd23aa6..8dd6c3104 100644
--- a/packages/dsp/native/CMakeLists.txt
+++ b/packages/dsp/native/CMakeLists.txt
@@ -21,6 +21,25 @@ retroplug_package_library(retroplug-dsp-kernels
     ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dpcm.cpp)
 target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
 
+# --- retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad, dither, normalize and dpcm,
+# each a node around the kernels above (docs/modular-graph-phase-d1-design.md section 5). A target of its own rather
+# than a source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the CLI, the
+# test hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying.
+# It links retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or
+# RTTI, and it stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and
+# runs these nodes with only the graph library and the kernels.
+retroplug_package_library(retroplug-dsp-nodes
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
+target_include_directories(retroplug-dsp-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
+target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-node-sdk PRIVATE retroplug-dsp-kernels)
+
+# The device build (RP_DEVICE_BUILD, cmake/packages.cmake) stops here: it has no r8brain, so it defines none of the
+# resampler's targets below. rp.dsp.resample's manifest entry is not `device`, and a device product takes the device
+# entries alone.
+if(RP_DEVICE_BUILD)
+    return()
+endif()
+
 # r8brain's static members are vendored code, so they get a target of their own rather than riding in one of ours:
 # /W4 /WX is scoped to RetroPlug's own targets (packages/native/cmake/warnings.cmake), which this one is deliberately
 # not on, and GNU/Clang get the same `-w` a vendored dep gets (retroplug_silence_dep_warnings). Its one source includes
@@ -49,18 +68,6 @@ target_include_directories(retroplug-dsp-resample
 # saying which.
 target_link_libraries(retroplug-dsp-resample PRIVATE Threads::Threads retroplug-r8brain-base)
 
-# --- retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad, dither, normalize and dpcm,
-# each a node around the kernels above (docs/modular-graph-phase-d1-design.md section 5). A target of its own rather
-# than a source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the CLI, the
-# test hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying.
-# It links retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or
-# RTTI, and it stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and
-# runs these nodes with only the graph library and the kernels.
-retroplug_package_library(retroplug-dsp-nodes
-    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
-target_include_directories(retroplug-dsp-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
-target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-node-sdk PRIVATE retroplug-dsp-kernels)
-
 # --- retroplug-dsp-resample-nodes: the rp.dsp.resample package (src/graph/nodes/DspResampleNodes.cpp), the resampler
 # as a node, apart from retroplug-dsp-nodes because its kernel is retroplug-dsp-resample: r8brain, and with it the
 # heap, pthreads and RTTI. A build for a target that cannot take those (the Pico's) links retroplug-dsp-nodes and
diff --git a/packages/graph-runtime/native/CMakeLists.txt b/packages/graph-runtime/native/CMakeLists.txt
index 16309c4f0..e090a7f48 100644
--- a/packages/graph-runtime/native/CMakeLists.txt
+++ b/packages/graph-runtime/native/CMakeLists.txt
@@ -10,6 +10,12 @@ retroplug_package_library(retroplug-graph-exec
 target_include_directories(retroplug-graph-exec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
 target_link_libraries(retroplug-graph-exec PUBLIC retroplug-node-sdk-headers)
 
+# A device plans nothing (the desktop planned and validated the plan before export), so the device build
+# (RP_DEVICE_BUILD, cmake/packages.cmake) takes the executor alone.
+if(RP_DEVICE_BUILD)
+    return()
+endif()
+
 # retroplug-graph: everything the control thread needs to turn a graph document into a plan - the registry, resources,
 # the planner, GraphHost - plus the node types the runtime implements itself (rp.host.audio-out, the manifest's
 # rp_host_package).
diff --git a/packages/node-sdk/native/CMakeLists.txt b/packages/node-sdk/native/CMakeLists.txt
index 3f26f6855..d507f366b 100644
--- a/packages/node-sdk/native/CMakeLists.txt
+++ b/packages/node-sdk/native/CMakeLists.txt
@@ -22,9 +22,24 @@ target_include_directories(retroplug-node-sdk-headers INTERFACE ${CMAKE_CURRENT_
 # nothing. SYSTEM for the reason packages/native/cmake/warnings.cmake marks reflect-cpp so: its templates instantiate
 # in our translation units, and our warning level is not its. The device-mode test's objects and every package's
 # device check (cmake/packages.cmake) take it, and the device build will.
+# The device build (cmake/packages.cmake, RP_DEVICE_BUILD) has no reflectcpp target: it names the include root of the
+# same pinned reflect-cpp in RP_REFLECTCPP_INCLUDE_DIR (packages/graph-runtime/device-check/CMakeLists.txt finds it).
 add_library(retroplug-reflect-headers INTERFACE)
-target_include_directories(retroplug-reflect-headers SYSTEM INTERFACE
-    $<TARGET_PROPERTY:reflectcpp,INTERFACE_INCLUDE_DIRECTORIES>)
+if(RP_DEVICE_BUILD)
+    target_include_directories(retroplug-reflect-headers SYSTEM INTERFACE ${RP_REFLECTCPP_INCLUDE_DIR})
+else()
+    target_include_directories(retroplug-reflect-headers SYSTEM INTERFACE
+        $<TARGET_PROPERTY:reflectcpp,INTERFACE_INCLUDE_DIRECTORIES>)
+endif()
+
+# In the device build retroplug-node-sdk is the device's SDK, which is headers alone: the include root and
+# reflect-cpp's. Every package's library links retroplug-node-sdk, so each builds against the half of the SDK its build
+# compiles.
+if(RP_DEVICE_BUILD)
+    add_library(retroplug-node-sdk INTERFACE)
+    target_link_libraries(retroplug-node-sdk INTERFACE retroplug-node-sdk-headers retroplug-reflect-headers)
+    return()
+endif()
 
 # retroplug-node-sdk: the description library. Links reflect-cpp (settings parsing, JSON Schema), and holds the JSON to
 # msgpack settings converter (graph/JsonToMsgpack.hpp), the desktop half of RP_SETTINGS_MSGPACK.
````

Run: `sed -n '11073,11355p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0.

- [ ] **Step 3: The check's portable run and its case generator**

`DeviceCheck.hpp` and `DeviceCheck.cpp` are the check itself, portable and heap-free: for each registered type find
its case, `query_memory`, `create` into the arena, `activate`, bind its ports and params into a one-step plan,
`ports_changed`, one block through `runBlock`, `deactivate`, `destroy`, each failure a distinct `Step` naming the type
(a resource input is handed the host's one stereo buffer, `res:1`). `device-check/packages.cmake` holds the one
package list. `cases/` is `rp-device-check-cases`, a desktop DEVICE product of that list (`EXCLUDE_FROM_ALL`, on the
`/W4 /WX` list): it creates each type with no settings, reads the defaults back through `rp.sdk.settings-json/1`,
converts them with `settingsToMsgpack` and checks the round trip, completes `rp.dsp.normalize` with `{"buffer":
"res:1"}` (its own `validate` refuses its defaults), resolves the layout as a planner would for an unwired node, and
writes one header of constant arrays naming the entries it was made for. The root adds its directory once the packages
are added.

Create `packages/graph-runtime/device-check/DeviceCheck.hpp`:

````cpp
#pragma once

// The device check (docs/modular-graph-phase-e1-design.md section 7): every node type a product registers, created
// from its default settings as msgpack into memory the caller owns, activated, run for one block through the executor
// and destroyed, with no heap and no exceptions. rp-device-check runs it on the RP2350 (main.cpp, beside this file),
// where the image's link trap is what proves the path allocates and throws nothing; retroplug-device-mode-test runs
// the same code on the desktop over the device-mode builds of the same packages, where it is what proves the path
// works and where the counting allocator stands in for the trap.
//
// It is a host, not a node: it reaches every type through the C ABI alone, as E3's arena executor will, and it plans
// nothing. What a planner would have worked out (each port's kind and channel count, each queue's capacity, each
// param's default) comes with the settings, from the desktop's build of the type (the case generator in cases/).

#include <cstddef>
#include <cstdint>

#include "graph/rp_node.h"

namespace rp::device_check {

// One port of a type's layout for its default settings, resolved as a planner would for a node with no wires: an
// unwired "any" input is mono, a match output takes its port's count.
struct PortCase {
    std::uint32_t kind = RP_PORT_AUDIO;  // rp_port_kind
    bool          output = false;
    std::uint32_t channels = 0;  // audio
    std::uint32_t messages = 0;  // events and bytes: the queue's capacity
    std::uint32_t bytes = 0;
};

// A registered type's case: its default settings as the msgpack a device reads, and the layout they give.
struct TypeCase {
    const char*          id = nullptr;
    const std::uint8_t*  settings = nullptr;
    std::uint32_t        settingsSize = 0;
    const PortCase*      ports = nullptr;
    std::uint32_t        portCount = 0;
    const double*        params = nullptr;  // each param's default, in layout order
    std::uint32_t        paramCount = 0;
};

// Where a run stopped. Ok is the only pass; anything else names the type (by its index in registration order, across
// packages) and what it failed.
enum class Step : std::uint8_t {
    Ok,
    NoCase,       // a registered type with no case: the cases were generated from another registration
    CaseUnused,   // a case no registered type took
    QueryMemory,  // query_memory refused the settings
    Arena,        // the instance, its buffers and its queues do not fit the arena
    Create,       // create refused the settings
    Activate,
    Process,      // the block faulted the node
};

struct Result {
    Step          step = Step::Ok;
    std::uint32_t type = 0;  // the type's index, in registration order
    const char*   id = nullptr;
    std::uint32_t types = 0;          // how many types ran to the end (on a pass, every one registered)
    std::size_t   instanceBytes = 0;  // the largest query_memory size among the types reached, the failed one included
    std::size_t   arenaBytes = 0;     // the most of the arena one type used, instance, buffers and queues together
    rp_error      error{};            // the type's own reason, where it gave one
};

// The one resource the check's host holds: a stereo audio buffer, res:1, which a case names where a type's defaults
// must name a resource and name none (the case generator's completions). Its frames are written by run(), so a type
// reads a signal from it, and a recorder may write it.
inline constexpr std::uint32_t kBufferResource = 1;
inline constexpr std::uint32_t kBufferChannels = 2;
inline constexpr std::uint32_t kBufferFrames = 256;

// What rp-device-check gives run(): one block of kBlockFrames, in an arena of kArenaBytes. The arena is sized for the
// largest default instance, rp.core.delay's (a second of stereo at 48 kHz, 384,000 bytes of delay line of its own),
// with room for its ports. retroplug-device-mode-test runs the check in an arena of the same size on the desktop, where
// pointers and max_align_t are wider, so a run that fits there fits the device's.
inline constexpr std::uint32_t kBlockFrames = 64;
inline constexpr std::size_t   kArenaBytes = 400 * 1024;

// The host every type is created under: no heap (the types it runs declare none), no guards, and a resource table of
// the one buffer above. The case generator creates the desktop's builds of the types under it too.
const rp_host& host();

// Runs every type of every package in `packages` (rp_register_packages()'s list) against its case, one block of
// `frames` frames at 48 kHz each, with `arena` reused from one type to the next.
Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames);

}  // namespace rp::device_check
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '11383,11470p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/DeviceCheck.hpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/DeviceCheck.hpp`
Expected: `350b8da0e48add218ab07b4bb31f64f6378da976`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/DeviceCheck.cpp`:

````cpp
#include "DeviceCheck.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

#include "graph/Executor.hpp"
#include "graph/Plan.hpp"

namespace rp::device_check {

namespace {

constexpr double kSampleRate = 48000.0;
// A control stream's capacity: the planner's (PlanStorage.hpp's kStreamChanges), so a node sees the queue it would.
constexpr std::uint32_t kControlChanges = 64;

// Bump allocation over the caller's memory, started afresh for each type. Everything a type's run needs comes from it
// (its instance, its port views, its buffers and queues), so the check holds nothing of its own beyond the stack.
struct Arena {
    std::byte*  base;
    std::size_t size;
    std::size_t used = 0;

    void* raw(std::size_t bytes, std::size_t align) {
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base) + used;
        const std::uintptr_t aligned = (start + (align - 1)) & ~std::uintptr_t(align - 1);
        const std::size_t    at = used + static_cast<std::size_t>(aligned - start);
        if (at > size || bytes > size - at) return nullptr;
        used = at + bytes;
        std::memset(base + at, 0, bytes);
        return base + at;
    }
    template <class T>
    T* take(std::size_t count) {
        return static_cast<T*>(raw(count * sizeof(T), alignof(T)));
    }
};

// The buffer behind res:1. Static rather than in the arena, as a resource outlives the nodes that pin it.
float                 gBufferData[kBufferChannels][kBufferFrames];
float*                gBufferChannels[kBufferChannels] = {gBufferData[0], gBufferData[1]};
rp_audio_buffer       gBuffer{kBufferChannels, kBufferFrames, kSampleRate, gBufferChannels};
rp_resource           gResource{"audio-buffer", &gBuffer};

const rp_resource* acquireResource(void*, std::uint32_t id, const char* type, rp_error* err) {
    if (id == kBufferResource && (!type || std::strcmp(type, gResource.type) == 0)) return &gResource;
    // The ABI's acquire fills err (rp_node.h), and node.hpp always passes one; a host that is handed none says nothing.
    if (err) std::strcpy(err->message, "the device check's host holds res:1, an audio buffer, and nothing else");
    return nullptr;
}
bool releaseResource(void*, std::uint32_t id) { return id == kBufferResource; }
const rp_host_resources kResources{nullptr, &acquireResource, &releaseResource};

void ignoreLog(void*, int, const char*) {}
const void* hostInterface(const rp_host*, const char* id) {
    return std::strcmp(id, RP_HOST_RESOURCES_INTERFACE) == 0 ? &kResources : nullptr;
}
const rp_host kHost{RP_ABI_VERSION, nullptr, nullptr, nullptr, &ignoreLog, &hostInterface};

// The input signal every audio input and the buffer carry: small, deterministic, and not silence, so the kernels run
// on numbers.
float signal(std::uint32_t frame, std::uint32_t channel) {
    return static_cast<float>((frame * 7u + channel * 13u) % 64u) / 256.0f - 0.125f;
}

const TypeCase* findCase(const char* id, const TypeCase* cases, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        if (std::strcmp(cases[i].id, id) == 0) return &cases[i];
    return nullptr;
}

rp_control_queue* controlQueue(Arena& a, double start) {
    auto* q = a.take<rp_control_queue>(1);
    auto* changes = a.take<rp_control_change>(kControlChanges);
    if (!q || !changes) return nullptr;
    q->start = start;
    q->changes = changes;
    q->capacity = kControlChanges;
    return q;
}

rp_queue* messageQueue(Arena& a, std::uint32_t messages, std::uint32_t bytes) {
    auto* q = a.take<rp_queue>(1);
    auto* items = a.take<rp_message>(messages);
    auto* arena = a.take<std::uint8_t>(bytes);
    if (!q || !items || !arena) return nullptr;
    q->items = items;
    q->capacity = messages;
    q->bytes = arena;
    q->bytes_capacity = bytes;
    return q;
}

// One port's view, with the buffers or queue behind it. An input's audio carries the signal; an output's starts
// silent.
bool bindPort(Arena& a, const PortCase& p, rp_port_view& view, std::uint32_t frames) {
    view.kind = p.kind;
    switch (p.kind) {
        case RP_PORT_AUDIO: {
            auto* channels = a.take<float*>(p.channels);
            if (!channels) return false;
            for (std::uint32_t c = 0; c < p.channels; ++c) {
                channels[c] = a.take<float>(frames);
                if (!channels[c]) return false;
                if (!p.output)
                    for (std::uint32_t i = 0; i < frames; ++i)
                        channels[c][i] = signal(i, c);
            }
            view.u.audio.channels = channels;
            view.u.audio.channel_count = p.channels;
            return true;
        }
        case RP_PORT_EVENTS:
        case RP_PORT_BYTES:
            view.u.queue = messageQueue(a, p.messages, p.bytes);
            return view.u.queue != nullptr;
        case RP_PORT_CONTROL:
            view.u.control = controlQueue(a, 0.0);
            return view.u.control != nullptr;
        case RP_PORT_RESOURCE:
            // An input is handed the host's buffer, as a wire from a resource output would hand it one, so a player
            // plays it and a recorder writes it; an output's slot starts empty, for the node to fill.
            view.u.resource = a.take<rp_resource*>(1);
            if (view.u.resource && !p.output) *view.u.resource = &gResource;
            return view.u.resource != nullptr;
        case RP_PORT_LINK:
        default:
            return true;
    }
}

// Binds the type's ports and params into a one-step plan and runs one block of it. False if the arena ran out.
bool runOneBlock(Arena& a, rp_node* node, const TypeCase& c, std::uint32_t frames, bool* faulted) {
    std::uint32_t inputs = 0, outputs = 0, queues = 0, controls = c.paramCount;
    for (std::uint32_t i = 0; i < c.portCount; ++i) {
        const PortCase& p = c.ports[i];
        (p.output ? outputs : inputs) += 1;
        if (p.output && (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES)) ++queues;
        if (p.output && p.kind == RP_PORT_CONTROL) ++controls;
    }
    auto* in = a.take<rp_port_view>(inputs);
    auto* out = a.take<rp_port_view>(outputs);
    auto* params = a.take<rp_control_queue*>(c.paramCount);
    auto* toClear = a.take<graph::OutputQueue>(queues);
    auto* toRoll = a.take<rp_control_queue*>(controls);
    auto* connected = a.take<std::uint8_t>(outputs);
    if (!in || !out || !params || !toClear || !toRoll || !connected) return false;

    std::uint32_t nIn = 0, nOut = 0, nClear = 0, nRoll = 0;
    for (std::uint32_t i = 0; i < c.portCount; ++i) {
        const PortCase& p = c.ports[i];
        rp_port_view&   view = p.output ? out[nOut++] : in[nIn++];
        if (!bindPort(a, p, view, frames)) return false;
        if (!p.output) continue;
        if (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES) toClear[nClear++] = graph::OutputQueue{view.u.queue};
        if (p.kind == RP_PORT_CONTROL) toRoll[nRoll++] = view.u.control;
    }
    for (std::uint32_t k = 0; k < c.paramCount; ++k) {
        params[k] = controlQueue(a, c.params[k]);
        if (!params[k]) return false;
        toRoll[nRoll++] = params[k];
    }

    // Every output wired, so the node writes each one as it would in a plan that reads it.
    for (std::uint32_t o = 0; o < outputs; ++o) connected[o] = 1;
    node->vt->ports_changed(node, connected, outputs);

    std::atomic<bool>          fault{false};
    std::atomic<std::uint32_t> faults{0}, drops{0};
    graph::PlanStep            step;
    step.node = node;
    step.process = rp_process{0, nullptr, in, inputs, out, outputs, params, c.paramCount};
    step.faulted = &fault;
    const graph::PlanView plan{std::span<graph::PlanStep>(&step, 1),
                               std::span<const graph::OutputQueue>(toClear, queues),
                               std::span<rp_control_queue* const>(toRoll, controls),
                               {},
                               &faults,
                               &drops};
    const rp_transport transport{kSampleRate, 120.0, 0.0, RP_TRANSPORT_PLAYING};
    graph::runBlock(plan, frames, transport);
    *faulted = fault.load(std::memory_order_relaxed);
    return true;
}

Step runType(const rp_node_type& type, const TypeCase& c, Arena& a, std::uint32_t frames, Result& r) {
    const rp_settings settings{reinterpret_cast<const char*>(c.settings), c.settingsSize, RP_SETTINGS_MSGPACK};
    rp_memory_req req{};
    if (!type.query_memory(&type, &settings, &req, &r.error)) return Step::QueryMemory;
    if (req.size > r.instanceBytes) r.instanceBytes = req.size;
    void* memory = a.raw(req.size, req.align ? req.align : alignof(std::max_align_t));
    if (!memory) return Step::Arena;
    rp_node* node = type.create(&type, &kHost, memory, &settings, &r.error);
    if (!node) return Step::Create;
    if (!node->vt->activate(node, kSampleRate, frames, &r.error)) {
        type.destroy(node);
        return Step::Activate;
    }
    bool       faulted = false;
    const bool bound = runOneBlock(a, node, c, frames, &faulted);
    node->vt->deactivate(node);
    type.destroy(node);
    if (!bound) return Step::Arena;
    return faulted ? Step::Process : Step::Ok;
}

}  // namespace

const rp_host& host() { return kHost; }

Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames) {
    for (std::uint32_t c = 0; c < kBufferChannels; ++c)
        for (std::uint32_t i = 0; i < kBufferFrames; ++i) gBufferData[c][i] = signal(i, c);
    Result        r;
    std::size_t   matched = 0;
    std::uint32_t index = 0;
    for (std::size_t p = 0; p < packageCount; ++p) {
        for (std::uint32_t t = 0; t < packages[p]->node_type_count; ++t, ++index) {
            const rp_node_type& type = *packages[p]->node_types[t];
            r.type = index;
            r.id = type.id;
            const TypeCase* c = findCase(type.id, cases, caseCount);
            if (!c) {
                r.step = Step::NoCase;
                return r;
            }
            ++matched;
            Arena a{static_cast<std::byte*>(arena), arenaBytes};
            r.step = runType(type, *c, a, frames, r);
            if (a.used > r.arenaBytes) r.arenaBytes = a.used;
            if (r.step != Step::Ok) return r;
            ++r.types;
        }
    }
    // Each case's id is a different type's, so a case left over is one no registered type took.
    if (matched != caseCount) {
        r.step = Step::CaseUnused;
        r.type = index;
        r.id = nullptr;
        return r;
    }
    r.id = nullptr;
    return r;
}

}  // namespace rp::device_check
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '11482,11730p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/DeviceCheck.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/DeviceCheck.cpp`
Expected: `9af68748852ef529fc752c3571ce33919ca40b4f`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/packages.cmake`:

````cmake
# The packages the device check carries, in registration order: the one list both of its builds read. The image
# (CMakeLists.txt) registers their `device` entries as a device builds them, and the case generator (cases/) registers
# the same entries as the desktop builds them, to write the cases the image runs. The generated cases header carries
# the entries it was made for, and the image refuses, at compile time, a header made for another list.
#
# retroplug-device-mode-test holds the list to the manifests: every package with a `device` entry must be on it, so a
# package that becomes device-capable is device-checked from then on.
set(RP_DEVICE_CHECK_PACKAGES core dsp)
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '11742,11749p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/packages.cmake`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/packages.cmake`
Expected: `10c6488712e47d0ac978ceed608f4249e2ce717a`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/cases/CMakeLists.txt`:

````cmake
# rp-device-check-cases (main.cpp): the desktop half of the device check (docs/modular-graph-phase-e1-design.md section
# 7), which writes the header the device image is built against: every type the device builds, its default settings as
# msgpack and the layout they give. The root CMakeLists adds this directory once the packages it lists are added.
#
# A device product of the desktop build (cmake/packages.cmake) over the device check's own list (../packages.cmake), so
# it registers the same device entries the image does, built for the desktop, which can write their defaults and their
# layouts. The header names those entries (RP_DEVICE_CHECK_ENTRIES), and the image refuses one made for others. Built
# by name: tools/build-device-check.sh builds and runs it before configuring the image, and retroplug-device-mode-test
# runs the device check on the desktop against its output.
include(${CMAKE_CURRENT_SOURCE_DIR}/../packages.cmake)
add_executable(rp-device-check-cases EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../DeviceCheck.cpp)
set_target_properties(rp-device-check-cases PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
retroplug_product(rp-device-check-cases PACKAGES ${RP_DEVICE_CHECK_PACKAGES} DEVICE)
get_property(_rp_entries GLOBAL PROPERTY RP_DEVICE_PRODUCT_rp-device-check-cases_ENTRIES)
list(JOIN _rp_entries " " _rp_entries)
target_compile_definitions(rp-device-check-cases PRIVATE "RP_DEVICE_CHECK_ENTRIES=\"${_rp_entries}\"")
# graph/Packages.hpp, the graph-runtime include root's, and node-sdk's descriptor parser and msgpack converter; the
# device check's own source for the host it creates the types under, the one the device creates them under too.
target_link_libraries(rp-device-check-cases PRIVATE retroplug-graph-exec retroplug-node-sdk)
target_include_directories(rp-device-check-cases PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
````

Run: `mkdir -p packages/graph-runtime/device-check/cases; sed -n '11761,11782p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/cases/CMakeLists.txt`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/cases/CMakeLists.txt`
Expected: `4e37d3a59505487b1025d8b6110071534f9a7f82`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/cases/main.cpp`:

````cpp
// rp-device-check-cases: the device check's case generator (docs/modular-graph-phase-e1-design.md section 7). A desktop
// tool, and a device product of the desktop build: its rp_register_packages() is the device entries of the packages the
// device check lists, built for the desktop (packages/graph-runtime/device-check/cases/CMakeLists.txt). For each type
// a device builds of those, it writes the type's default settings, read back from a desktop instance created with no
// settings (graph::kSettingsJsonInterface) and converted to the msgpack a device reads (graph::settingsToMsgpack), and
// the layout those settings give, resolved for a node with no wires, into one header of constant arrays
// (DeviceCheck.hpp's TypeCase), with the package entries it was made for (kEntries, which the image holds its own
// product's to). The device image is built against that header and nothing else of the desktop's build.
//
// Which types a device builds is the types' own declaration: a device entry registers, in device mode, each of its
// types that declares neither `heap` nor `threads` (node_device.hpp refuses to build one that does), so the generator
// leaves those out and names them in the header. retroplug-device-mode-test holds the device-mode registration to the
// same rule and to this header.
//
// Usage: rp-device-check-cases <out.hpp>. The header is rewritten only when its text changes, so a desktop rebuild
// that moves no default rebuilds no device image.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "DeviceCheck.hpp"
#include "graph/Descriptor.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/Packages.hpp"
#include "graph/node.hpp"

namespace {

using rp::graph::ChannelSpec;
using rp::graph::Layout;
using rp::graph::PortDesc;
using rp::graph::PortDir;
using rp::graph::PortKind;

[[noreturn]] void fail(const std::string& why) {
    std::fprintf(stderr, "rp-device-check-cases: %s\n", why.c_str());
    std::exit(1);
}

// What a type's defaults lack before it can be created, merged over them: a type whose own validate refuses its
// defaults (a buffer it must read, which no default can name) gets the device check host's one resource here. A type
// whose defaults are refused with no entry fails the generator, naming it.
struct Completion {
    std::string_view id;
    std::string_view json;
};
constexpr Completion kCompletions[] = {
    {"rp.dsp.normalize", R"({"buffer":"res:1"})"},
};

std::string_view completionOf(std::string_view id) {
    for (const Completion& c : kCompletions)
        if (c.id == id) return c.json;
    return {};
}

// A desktop instance of the type, created from `settings` in its own memory, for as long as the object lives.
struct Made {
    const rp_node_type* type;
    rp_memory_req       req{};
    void*               memory = nullptr;
    rp_node*            node = nullptr;

    Made(const rp_node_type* t, const rp_settings& settings) : type(t) {
        rp_error err{};
        if (!t->query_memory(t, &settings, &req, &err))
            fail(std::string(t->id) + ": its defaults are refused (" + err.message +
                 "); give it a completion (kCompletions)");
        // An alignment of 0 asks for none in particular, as DeviceCheck.cpp reads it; align_val_t(0) is undefined.
        if (req.align == 0) req.align = alignof(std::max_align_t);
        memory = ::operator new(req.size, std::align_val_t(req.align));
        node = t->create(t, &rp::device_check::host(), memory, &settings, &err);
        if (!node) fail(std::string(t->id) + ": create: " + err.message);
    }
    ~Made() {
        if (node) type->destroy(node);
        ::operator delete(memory, std::align_val_t(req.align));
    }

    std::string settingsJson() const {
        const auto* writer = static_cast<const rp::graph::SettingsJsonInterface*>(
            node->vt->get_interface(node, rp::graph::kSettingsJsonInterface));
        if (!writer) fail(std::string(type->id) + ": answers no " + rp::graph::kSettingsJsonInterface);
        std::string json(256, '\0');
        std::size_t n = writer->write(node, json.data(), json.size());
        if (n >= json.size()) {
            json.assign(n + 1, '\0');
            n = writer->write(node, json.data(), json.size());
        }
        if (n == 0 || n >= json.size()) fail(std::string(type->id) + ": its settings would not write");
        json.resize(n);
        return json;
    }
};

std::uint32_t channelsOf(const Layout& layout, const PortDesc& port, int depth = 0) {
    if (depth > static_cast<int>(layout.ports.size())) fail("a port's \"match\" specs form a loop");
    switch (port.channels.mode) {
        case ChannelSpec::Mode::Fixed:
            return port.channels.count;
        case ChannelSpec::Mode::Any:
            return 1;  // an unwired "any" input is mono, as the planner resolves one
        case ChannelSpec::Mode::Match: {
            const PortDesc* other = rp::graph::findPort(layout, port.channels.match);
            if (!other) fail("port " + port.id + " matches " + port.channels.match + ", which is not a port");
            return channelsOf(layout, *other, depth + 1);
        }
    }
    return 0;
}

std::uint32_t kindOf(PortKind kind) {
    switch (kind) {
        case PortKind::Audio: return RP_PORT_AUDIO;
        case PortKind::Events: return RP_PORT_EVENTS;
        case PortKind::Bytes: return RP_PORT_BYTES;
        case PortKind::Control: return RP_PORT_CONTROL;
        case PortKind::Link: return RP_PORT_LINK;
        case PortKind::Resource: return RP_PORT_RESOURCE;
    }
    return RP_PORT_AUDIO;
}

const char* kindName(std::uint32_t kind) {
    switch (kind) {
        case RP_PORT_AUDIO: return "RP_PORT_AUDIO";
        case RP_PORT_EVENTS: return "RP_PORT_EVENTS";
        case RP_PORT_BYTES: return "RP_PORT_BYTES";
        case RP_PORT_CONTROL: return "RP_PORT_CONTROL";
        case RP_PORT_LINK: return "RP_PORT_LINK";
        default: return "RP_PORT_RESOURCE";
    }
}

// A C++ comment line may not hold a newline, and a JSON string may: write any control character as an escape.
std::string commentSafe(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        if (static_cast<unsigned char>(ch) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
            out += buf;
        } else {
            out += ch;
        }
    }
    return out;
}

std::string typeCase(const rp_node_type* type, std::size_t index, std::ostringstream& arrays) {
    const std::string id = type->id;
    // The defaults: a desktop instance created with no settings (or its completion alone), written back whole.
    const std::string_view completion = completionOf(id);
    const rp_settings      start{completion.data(), completion.size(), RP_SETTINGS_JSON};
    const std::string      json = Made(type, start).settingsJson();
    auto converted = rp::graph::settingsToMsgpack(json);
    if (!converted) fail(id + ": " + converted.error().what());
    const std::vector<std::uint8_t>& msgpack = *converted;

    // The blob read back by the desktop's own msgpack decode must give the same settings, so a field the conversion
    // or the decode would lose fails the generator rather than the device.
    const rp_settings blob{reinterpret_cast<const char*>(msgpack.data()), msgpack.size(), RP_SETTINGS_MSGPACK};
    const std::string back = Made(type, blob).settingsJson();
    if (back != json) fail(id + ": its defaults read back from msgpack as " + back + ", not " + json);

    const rp_settings asJson{json.data(), json.size(), RP_SETTINGS_JSON};
    rp_error          err{};
    std::string       layoutText(4096, '\0');
    std::size_t       n = type->layout_for(type, &asJson, layoutText.data(), layoutText.size(), &err);
    if (n >= layoutText.size()) {
        layoutText.assign(n + 1, '\0');
        n = type->layout_for(type, &asJson, layoutText.data(), layoutText.size(), &err);
    }
    if (n == 0) fail(id + ": layout_for: " + err.message);
    layoutText.resize(n);
    auto layout = rp::graph::parseLayout(layoutText);
    if (!layout) fail(id + ": its layout: " + layout.error().what());

    arrays << "\n// " << id << ": " << commentSafe(json) << "\n";
    arrays << "inline constexpr std::uint8_t kSettings" << index << "[] = {";
    for (std::size_t i = 0; i < msgpack.size(); ++i) {
        arrays << (i % 16 == 0 ? "\n    " : " ") << "0x";
        static const char kHex[] = "0123456789abcdef";
        arrays << kHex[msgpack[i] >> 4] << kHex[msgpack[i] & 0xf] << ",";
    }
    arrays << "\n};\n";
    if (!layout->ports.empty()) {
        arrays << "inline constexpr PortCase kPorts" << index << "[] = {\n";
        for (const PortDesc& p : layout->ports) {
            const std::uint32_t kind = kindOf(p.kind);
            const bool          output = p.dir == PortDir::Out;
            const std::uint32_t channels = p.kind == PortKind::Audio ? channelsOf(*layout, p) : 0;
            const bool          queue = p.kind == PortKind::Events || p.kind == PortKind::Bytes;
            arrays << "    {" << kindName(kind) << ", " << (output ? "true" : "false") << ", " << channels << ", "
                   << (queue ? p.capacity.messageCount() : 0) << ", " << (queue ? p.capacity.byteCount() : 0)
                   << "},  // " << p.id << "\n";
        }
        arrays << "};\n";
    }
    if (!layout->params.empty()) {
        arrays << "inline constexpr double kParams" << index << "[] = {";
        for (const auto& param : layout->params) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", param.def);
            arrays << buf << ",";
        }
        arrays << "};\n";
    }
    std::ostringstream row;
    row << "    {\"" << id << "\", kSettings" << index << ", sizeof kSettings" << index << ", "
        << (layout->ports.empty() ? "nullptr, 0" : "kPorts" + std::to_string(index) + ", std::size(kPorts" +
                                                         std::to_string(index) + ")")
        << ", "
        << (layout->params.empty() ? "nullptr, 0" : "kParams" + std::to_string(index) + ", std::size(kParams" +
                                                          std::to_string(index) + ")")
        << "},\n";
    return row.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) fail("usage: rp-device-check-cases <out.hpp>");
    std::ostringstream arrays, rows, leftOut;
    std::size_t        index = 0;
    for (const rp_package* package : rp::graph::productPackages()) {
        for (std::uint32_t t = 0; t < package->node_type_count; ++t) {
            const rp_node_type* type = package->node_types[t];
            auto                d = rp::graph::parseDescriptor(type->descriptor(type));
            if (!d) fail(std::string(type->id) + ": its descriptor: " + d.error().what());
            if (d->heap || d->threads) {
                leftOut << "//   " << type->id << " (declares " << (d->heap ? "heap" : "threads") << ")\n";
                continue;
            }
            rows << typeCase(type, index++, arrays);
        }
    }
    std::ostringstream out;
    out << "#pragma once\n\n"
           "// Generated by rp-device-check-cases (packages/graph-runtime/device-check/cases/main.cpp) from the\n"
           "// desktop's build of the device check's packages: every type a device builds, its default settings as\n"
           "// msgpack and the layout they give. Do not edit.\n";
    if (!leftOut.str().empty())
        out << "//\n// Left out, as no device builds a type that declares heap or threads:\n" << leftOut.str();
    out << "\n#include <cstdint>\n#include <iterator>\n\n#include \"DeviceCheck.hpp\"\n\n"
           "namespace rp::device_check {\n\n"
           "// The package entries the cases were made for, in registration order; the image checks its own\n"
           "// against it.\n"
           "inline constexpr const char kEntries[] = \"" RP_DEVICE_CHECK_ENTRIES "\";\n"
        << arrays.str() << "\ninline constexpr TypeCase kCases[] = {\n"
        << rows.str() << "};\n\n}  // namespace rp::device_check\n";

    const std::string text = out.str();
    {
        std::ifstream existing(argv[1], std::ios::binary);
        const std::string current((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
        if (existing && current == text) return 0;
    }
    std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
    file << text;
    if (!file) fail(std::string("cannot write ") + argv[1]);
    return 0;
}
````

Run: `mkdir -p packages/graph-runtime/device-check/cases; sed -n '11794,12065p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/cases/main.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/cases/main.cpp`
Expected: `aba5e732b126a7d68a22221e29567615776803c6`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/CMakeLists.txt b/CMakeLists.txt
index b4c918eb7..762d5a750 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -364,6 +364,9 @@ retroplug_package(dsp)             # the kernels, the rp.dsp nodes and rp.dsp.re
 # The node types not yet carved into packages of their own (sameboy, mesen, host.io, n8, launchpad) register from
 # packages/native, the engine's tree, whose manifest names their entries; its targets are defined below.
 retroplug_package(native)
+# The device check's case generator, a desktop tool listing what the RP2350 check registers (a device product of the
+# packages above); the check itself is packages/graph-runtime/device-check, a Pico SDK project of its own.
+add_subdirectory(packages/graph-runtime/device-check/cases "${CMAKE_BINARY_DIR}/device-check/cases")
 
 # The packages a desktop product carries (retroplug_product), every one today; a product that differs lists its own.
 # The ORDER is load-bearing: it is the order the products register in, which the registration golden holds
diff --git a/packages/native/cmake/warnings.cmake b/packages/native/cmake/warnings.cmake
index b5b6ce6f4..1ae137542 100644
--- a/packages/native/cmake/warnings.cmake
+++ b/packages/native/cmake/warnings.cmake
@@ -43,7 +43,9 @@ if(MSVC)
         retroplug-node-sdk retroplug-graph-exec retroplug-graph retroplug-core-nodes
         retroplug-dsp-kernels retroplug-dsp-resample retroplug-dsp-nodes retroplug-dsp-resample-nodes
         # The device build of the node SDK, tested on the desktop (cmake/tests.cmake).
-        retroplug-device-mode-objects retroplug-device-mode-test)
+        retroplug-device-mode-objects retroplug-device-mode-test
+        # The device check's case generator (packages/graph-runtime/device-check/cases).
+        rp-device-check-cases)
     # And each device entry's device-mode compile (cmake/packages.cmake's _retroplug_device_check).
     get_property(_rp_device_checks GLOBAL PROPERTY RP_DEVICE_CHECK_TARGETS)
     list(APPEND _rp_werror_targets ${_rp_device_checks})
````

Run: `sed -n '12075,12103p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0.

Run: `cmake --build build --target rp-device-check-cases -j$(nproc)`
Expected: exit 0.

Run: `build/device-check/cases/rp-device-check-cases /tmp/e1-cases.hpp`
Expected: exit 0 and nothing printed (a type whose defaults are refused with no completion would stop it, naming the type).

Run: `grep -c '^    {"rp\.' /tmp/e1-cases.hpp; grep -n 'kEntries\|declares heap' /tmp/e1-cases.hpp`
Expected: `14` (the ten `rp.core` types and `rp.dsp`'s gain, biquad, dither and normalize; the last case line of the tip build's header is `{"rp.dsp.normalize", kSettings13` ...), then these three lines, as the tip build's header has them:

````text
7:// Left out, as no device builds a type that declares heap or threads:
8://   rp.dsp.dpcm (declares heap)
19:inline constexpr const char kEntries[] = "rp_core_package rp_dsp_package";
````

Then `rm /tmp/e1-cases.hpp`.

- [ ] **Step 4: The device check in the device-mode test**

`DeviceCheck.test.cpp` (`[device-check]`, four cases) runs the check on the desktop over the device-mode objects,
against the header the generator writes for the image: each manifest device entry registers, in device mode, exactly
its types that declare neither heap nor threads; the check carries every device entry and its cases list their device
types, in order; the run passes in the image's arena size with nothing allocated or freed and every case reached; and
it stops at a type that fails, naming it (no case, a case no type takes, settings the type refuses, an arena too
small). `tests.cmake` now takes the device-mode sources, include directories, definitions and entry renames from the
manifests (every `<target>-device` check), generates `device-entries.cpp` listing every device entry with both its
builds, and runs `rp-device-check-cases` into `build/device-check/DeviceCheckCases.hpp` (generated, never committed)
for the binary. `DeviceModeEntries.hpp` gains `DeviceEntry`.

Create `packages/native/test/device-mode/DeviceCheck.test.cpp`:

````cpp
// The device check (packages/graph-runtime/device-check, docs/modular-graph-phase-e1-design.md section 7) on the
// desktop. The RP2350 image is built and linked in CI (its link trap is the proof that it allocates and throws
// nothing) but run nowhere before E3, so the run itself is held here: the same DeviceCheck.cpp, over the device-mode
// builds of the same packages (retroplug-device-mode-objects), against the same header of cases
// (rp-device-check-cases' output), in an arena of the image's size.
//
// - Which types a device registers is the types' own word: each `device` entry of a manifest registers, in device
//   mode, exactly the types of its desktop build that declare neither heap nor threads, in the same order. A type left
//   out of the device build by its source but portable, or a heap type built anyway, fails here.
// - The device check carries every package with a `device` entry (its list is device-check/packages.cmake), and its
//   cases, generated for the entries they name, list exactly those entries' device types, in order.
// - The check passes, every type created from its defaults as msgpack, activated, run for one block and destroyed, in
//   the image's arena, with no allocation (the counting allocator stands in for the image's trap).
// - It is not vacuous: a missing case, a case no type takes, settings a type refuses and an arena too small each stop
//   it, naming the type.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "DeviceCheck.hpp"
#include "DeviceCheckCases.hpp"
#include "DeviceModeEntries.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/Descriptor.hpp"

// Generated (device-entries.cpp): every manifest's `device` entries, with both builds.
std::vector<DeviceEntry> deviceEntries();

namespace {

using rp::device_check::Result;
using rp::device_check::Step;
using rp::device_check::TypeCase;

std::vector<std::string> idsOf(const rp_package* p) {
    std::vector<std::string> ids;
    for (std::uint32_t i = 0; i < p->node_type_count; ++i) ids.emplace_back(p->node_types[i]->id);
    return ids;
}

// The device-mode builds of the image's packages, as its rp_register_packages() lists them.
std::vector<const rp_package*> devicePackages();

Result runCheck(const std::vector<const rp_package*>& packages, const std::vector<TypeCase>& cases,
                std::size_t arenaBytes = rp::device_check::kArenaBytes) {
    std::vector<std::max_align_t> arena(arenaBytes / sizeof(std::max_align_t) + 1);
    return rp::device_check::run(packages.data(), packages.size(), cases.data(), cases.size(), arena.data(), arenaBytes,
                                 rp::device_check::kBlockFrames);
}

const std::vector<TypeCase> kAllCases(std::begin(rp::device_check::kCases), std::end(rp::device_check::kCases));

// The device types of a device entry's desktop build: its types that declare neither heap nor threads, in order.
std::vector<std::string> portableIds(const DeviceEntry& e, std::size_t* leftOut = nullptr) {
    std::vector<std::string> portable;
    const rp_package*        desktop = e.desktop();
    for (std::uint32_t i = 0; i < desktop->node_type_count; ++i) {
        const rp_node_type* t = desktop->node_types[i];
        auto                d = rp::graph::parseDescriptor(t->descriptor(t));
        REQUIRE(d);
        if (d->heap || d->threads) {
            if (leftOut) ++*leftOut;
        } else {
            portable.emplace_back(t->id);
        }
    }
    return portable;
}

// The entries the cases header names (the image's product), in its order, each as the manifests list it; an entry no
// manifest marks `device` is left out here, and the test that reads the header refuses it.
std::vector<DeviceEntry> checkedEntries() {
    std::vector<DeviceEntry> out;
    std::istringstream       words(rp::device_check::kEntries);
    const auto               all = deviceEntries();
    for (std::string w; words >> w;)
        for (const DeviceEntry& e : all)
            if (w == e.entry) out.push_back(e);
    return out;
}

std::vector<const rp_package*> devicePackages() {
    std::vector<const rp_package*> packages;
    for (const DeviceEntry& e : checkedEntries()) packages.push_back(e.device());
    return packages;
}

}  // namespace

TEST_CASE("a device entry registers its types that declare neither heap nor threads", "[device-mode][device-check]") {
    const std::vector<DeviceEntry> entries = deviceEntries();
    REQUIRE(entries.size() >= 2);  // rp.core and rp.dsp at least
    std::size_t leftOut = 0;
    for (const DeviceEntry& e : entries) {
        INFO(e.package << ": " << e.entry);
        CHECK(idsOf(e.device()) == portableIds(e, &leftOut));
    }
    CHECK(leftOut >= 1);  // rp.dsp.dpcm, which declares heap
}

TEST_CASE("the device check carries every device entry, and its cases list their device types",
          "[device-mode][device-check]") {
    std::vector<std::string> named;
    std::istringstream       words(rp::device_check::kEntries);
    for (std::string w; words >> w;) named.push_back(w);
    const std::vector<DeviceEntry> entries = deviceEntries();
    for (const DeviceEntry& e : entries) {
        INFO("the package " << e.package << " has a device entry, " << e.entry
                            << ", which the device check does not carry: add the package to "
                               "packages/graph-runtime/device-check/packages.cmake");
        CHECK(std::find(named.begin(), named.end(), e.entry) != named.end());
    }
    std::vector<std::string> registered;
    for (const std::string& n : named) {
        const auto e = std::find_if(entries.begin(), entries.end(), [&](const DeviceEntry& d) { return n == d.entry; });
        INFO("the cases name " << n << ", which no manifest marks `device`");
        REQUIRE(e != entries.end());
        const std::vector<std::string> portable = portableIds(*e);
        registered.insert(registered.end(), portable.begin(), portable.end());
    }
    std::vector<std::string> cases;
    for (const TypeCase& c : kAllCases) cases.emplace_back(c.id);
    CHECK(cases == registered);
}

TEST_CASE("the device check runs every device type from its default settings, in the image's arena, allocating "
          "nothing",
          "[device-mode][device-check]") {
    const std::vector<const rp_package*> packages = devicePackages();
    // The arena is made before counting starts, as the image's is a static.
    std::vector<std::max_align_t> arena(rp::device_check::kArenaBytes / sizeof(std::max_align_t) + 1);

    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    const Result r = rp::device_check::run(packages.data(), packages.size(), kAllCases.data(), kAllCases.size(),
                                           arena.data(), rp::device_check::kArenaBytes, rp::device_check::kBlockFrames);
    const auto counted = rp::test::endCountingAllocations();
    INFO("stopped at " << (r.id ? r.id : "-") << ", step " << static_cast<int>(r.step) << ": " << r.error.message);
    CHECK(r.step == Step::Ok);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(r.types == kAllCases.size());
    // The largest instance, with its ports, fitted the image's arena (an instance whose defaults outgrow it fails the
    // run with Step::Arena above). No type's size is pinned here: kArenaBytes says which one sizes it.
    CHECK(r.instanceBytes > 0);
    CHECK(r.arenaBytes >= r.instanceBytes);
    CHECK(r.arenaBytes <= rp::device_check::kArenaBytes);
}

TEST_CASE("the device check stops at the type that fails, and names it", "[device-mode][device-check]") {
    const std::vector<const rp_package*> packages = devicePackages();
    REQUIRE(kAllCases.size() >= 2);

    SECTION("a registered type with no case") {
        std::vector<TypeCase> cases(kAllCases.begin(), kAllCases.end() - 1);
        const Result          r = runCheck(packages, cases);
        CHECK(r.step == Step::NoCase);
        CHECK(std::string(r.id) == kAllCases.back().id);
        CHECK(r.type == kAllCases.size() - 1);
    }
    SECTION("a case no registered type takes") {
        std::vector<TypeCase> cases = kAllCases;
        TypeCase              extra = cases.front();
        extra.id = "rp.devtest.nobody";
        cases.push_back(extra);
        CHECK(runCheck(packages, cases).step == Step::CaseUnused);
    }
    SECTION("settings the type refuses") {
        std::vector<TypeCase> cases = kAllCases;
        static constexpr std::uint8_t kNotAMap[] = {0x92, 0x01, 0x02};  // [1, 2]
        cases[1].settings = kNotAMap;
        cases[1].settingsSize = sizeof kNotAMap;
        const Result r = runCheck(packages, cases);
        CHECK(r.step == Step::QueryMemory);
        CHECK(std::string(r.id) == kAllCases[1].id);
        CHECK(std::string(r.error.message) == "settings must be a msgpack map");
    }
    SECTION("an arena too small for an instance and its ports") {
        const Result r = runCheck(packages, kAllCases, 64);
        CHECK(r.step == Step::Arena);
        CHECK(std::string(r.id) == kAllCases.front().id);
    }
}
````

Run: `mkdir -p packages/native/test/device-mode; sed -n '12144,12330p' docs/modular-graph-phase-e1-plan.md > packages/native/test/device-mode/DeviceCheck.test.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/native/test/device-mode/DeviceCheck.test.cpp`
Expected: `a892406e182ad9e80d61f810258842f03f79fa89`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index ab8ca7dfe..26f587fc2 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -316,15 +316,58 @@ retroplug_add_test_binary(
 # device-mode code only through the C ABI, as a device's host would. MSVC keeps its /EHsc there: a second /EH option on
 # one command line is the D9025 warning AGENTS.md describes, and the GCC and Clang builds are where -fno-exceptions is
 # proved.
+#
+# Which sources and entries are the device's is the manifests' word (cmake/packages.cmake): the objects compile the
+# sources of every `device` entry's target (each one's <target>-device check holds the same list), and each device
+# entry is renamed <entry>_device_mode. device-entries.cpp, generated below, lists every device entry with both its
+# builds, which is what the test holds a device registration to.
+get_property(_rp_device_targets GLOBAL PROPERTY RP_DEVICE_CHECK_TARGETS)
+set(_rp_device_sources "")
+set(_rp_device_includes "")
+set(_rp_device_definitions "")
+set(_rp_device_owners "")
+foreach(_t IN LISTS _rp_device_targets)
+    get_target_property(_s ${_t} SOURCES)
+    list(APPEND _rp_device_sources ${_s})
+    string(REGEX REPLACE "-device$" "" _owner ${_t})
+    list(APPEND _rp_device_owners ${_owner})
+    list(APPEND _rp_device_includes $<TARGET_PROPERTY:${_owner},INCLUDE_DIRECTORIES>)
+    list(APPEND _rp_device_definitions $<TARGET_PROPERTY:${_owner},COMPILE_DEFINITIONS>)
+endforeach()
+set(_rp_device_renames "")
+set(_rp_device_rows "")
+set(_rp_device_decls "")
+get_property(_rp_packages GLOBAL PROPERTY RP_PACKAGES)
+foreach(_p IN LISTS _rp_packages)
+    get_property(_entries GLOBAL PROPERTY RP_PACKAGE_${_p}_DEVICE_ENTRIES)
+    foreach(_e IN LISTS _entries)
+        list(APPEND _rp_device_renames ${_e}=${_e}_device_mode)
+        string(APPEND _rp_device_decls
+               "extern \"C\" const rp_package* ${_e}(void);\nextern \"C\" const rp_package* ${_e}_device_mode(void);\n")
+        string(APPEND _rp_device_rows "        {\"${_p}\", \"${_e}\", &${_e}, &${_e}_device_mode},\n")
+    endforeach()
+endforeach()
+file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/device-mode/device-entries.cpp" CONTENT
+"// Generated by packages/native/cmake/tests.cmake from the packages' manifests: every `device` entry, with the
+// desktop's build of it and the device-mode build retroplug-device-mode-objects renames. Do not edit.
+
+#include <vector>
+
+#include \"DeviceModeEntries.hpp\"
+
+${_rp_device_decls}
+std::vector<DeviceEntry> deviceEntries() {
+    return {
+${_rp_device_rows}    };
+}
+")
+
 add_library(retroplug-device-mode-objects OBJECT EXCLUDE_FROM_ALL
-    ${CMAKE_SOURCE_DIR}/packages/core/native/src/graph/nodes/CoreNodes.cpp
-    ${CMAKE_SOURCE_DIR}/packages/dsp/native/src/graph/nodes/DspNodes.cpp
+    ${_rp_device_sources}
     ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode/DeviceModeNodes.cpp)
 set_target_properties(retroplug-device-mode-objects PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
-target_compile_definitions(retroplug-device-mode-objects PRIVATE
-    RP_DEVICE
-    rp_core_package=rp_core_package_device_mode
-    rp_dsp_package=rp_dsp_package_device_mode)
+target_compile_definitions(retroplug-device-mode-objects PRIVATE ${_rp_device_definitions} RP_DEVICE
+                                                                  ${_rp_device_renames})
 if(NOT MSVC)
     target_compile_options(retroplug-device-mode-objects PRIVATE -fno-exceptions -fno-rtti)
 endif()
@@ -332,10 +375,19 @@ endif()
 # and the packages' own source roots for their headers and the kernels'.
 target_link_libraries(retroplug-device-mode-objects PRIVATE retroplug-node-sdk-headers retroplug-reflect-headers)
 target_include_directories(retroplug-device-mode-objects PRIVATE
-    ${CMAKE_SOURCE_DIR}/packages/core/native/src
-    ${CMAKE_SOURCE_DIR}/packages/dsp/native/src
+    ${_rp_device_includes}
     ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode)
 
+# The device check (packages/graph-runtime/device-check) run on the desktop over the device-mode objects, against the
+# header the case generator writes for the RP2350 image: the same cases, the same code, the same arena size.
+set(_rp_device_check_dir ${CMAKE_SOURCE_DIR}/packages/graph-runtime/device-check)
+add_custom_command(
+    OUTPUT  ${CMAKE_BINARY_DIR}/device-check/DeviceCheckCases.hpp
+    COMMAND rp-device-check-cases ${CMAKE_BINARY_DIR}/device-check/DeviceCheckCases.hpp
+    DEPENDS rp-device-check-cases
+    COMMENT "rp-device-check-cases: the device check's cases"
+    VERBATIM)
+
 # What a device-mode build refuses, held at every configure (GCC and Clang): each source under
 # test/device-mode/refused must fail to compile as a device compiles it, and fail with its own reason on one of the
 # compiler's error lines, so a check that stopped firing (a static_assert gone, the heap-header #error gone) fails the
@@ -389,11 +441,16 @@ endif()
 retroplug_add_test_binary(
     NAME     retroplug-device-mode-test
     SOURCES  test/device-mode/DeviceMode.test.cpp
+             test/device-mode/DeviceCheck.test.cpp
              test/graph/CountingAllocator.cpp          # replaces operator new / delete for this binary
+             ${_rp_device_check_dir}/DeviceCheck.cpp   # the device check, as a host: desktop-built, C ABI only
+             ${CMAKE_BINARY_DIR}/device-check/DeviceCheckCases.hpp
+             ${CMAKE_BINARY_DIR}/device-mode/device-entries.cpp
              $<TARGET_OBJECTS:retroplug-device-mode-objects>
     LIBS     retroplug-graph
-             retroplug-core-nodes                      # the desktop's rp.core, the device's twin
-             retroplug-dsp-nodes                       # and rp.dsp
+             ${_rp_device_owners}                      # the desktop's build of every device entry, the device's twin
              retroplug-dsp-kernels                     # the kernels, which both builds of rp.dsp call
     INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test          # graph/CountingAllocator.hpp
-             ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode)
+             ${CMAKE_CURRENT_SOURCE_DIR}/test/device-mode
+             ${_rp_device_check_dir}                   # DeviceCheck.hpp
+             ${CMAKE_BINARY_DIR}/device-check)         # DeviceCheckCases.hpp
diff --git a/packages/native/test/device-mode/DeviceModeEntries.hpp b/packages/native/test/device-mode/DeviceModeEntries.hpp
index d48651af6..41b47dceb 100644
--- a/packages/native/test/device-mode/DeviceModeEntries.hpp
+++ b/packages/native/test/device-mode/DeviceModeEntries.hpp
@@ -17,6 +17,16 @@ const rp_package* rp_dsp_package_device_mode(void);
 const rp_package* rp_devtest_package_device_mode(void);
 }
 
+// A package's `device` entry as its manifest names it, with both builds of it: the desktop's (`desktop`) and the
+// device-mode objects' (`device`, the entry renamed <entry>_device_mode). The generated device-entries.cpp lists every
+// one (packages/native/cmake/tests.cmake), in the order the packages are added.
+struct DeviceEntry {
+    const char*         package;
+    const char*         entry;
+    rp_package_entry_fn desktop;
+    rp_package_entry_fn device;
+};
+
 // The settings an echo node decoded, as its on_settings copied them: get_interface(kEchoInterface) answers one.
 inline constexpr const char* kEchoInterface = "rp.devtest.echo/1";
 
````

Run: `sed -n '12340,12471p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `cmake -S . -B build`
Expected: exit 0.

Run: `cmake --build build --target retroplug-device-mode-test -j$(nproc)`
Expected: exit 0: the cases tool runs first and writes the header.

Run: `build/bin/retroplug-device-mode-test "[device-check]"`
Expected: all 4 test cases pass (the design's four `[device-check]` cases; no log records this tag's assertion total alone).

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (2317 assertions in 10 test cases)`.

- [ ] **Step 5: Show that the check's package list is held to the manifests**

A package that becomes device-capable must be device-checked from then on. Apply this temporary mutation, which drops
`dsp` from the check's list, and the second `[device-check]` case must fail; then reverse it. The edit re-runs the
configure, and the cases tool rewrites the header for `core` alone.

````patch
diff --git a/packages/graph-runtime/device-check/packages.cmake b/packages/graph-runtime/device-check/packages.cmake
--- a/packages/graph-runtime/device-check/packages.cmake
+++ b/packages/graph-runtime/device-check/packages.cmake
@@ -5,4 +5,4 @@
 #
 # retroplug-device-mode-test holds the list to the manifests: every package with a `device` entry must be on it, so a
 # package that becomes device-capable is device-checked from then on.
-set(RP_DEVICE_CHECK_PACKAGES core dsp)
+set(RP_DEVICE_CHECK_PACKAGES core)
````

Run: `sed -n '12496,12504p' docs/modular-graph-phase-e1-plan.md | git apply`
Expected: nothing: the mutation applies.

Run: `cmake --build build --target retroplug-device-mode-test -j$(nproc)`
Expected: exit 0 (the configure re-runs and the header is regenerated).

Run: `build/bin/retroplug-device-mode-test "[device-check]"`
Expected: fails in exactly one case, `the device check carries every device entry, and its cases list their device types`, at the `dsp` entry, with the message `the package dsp has a device entry, rp_dsp_package, which the device check does not carry: add the package to packages/graph-runtime/device-check/packages.cmake` (the report's `"add the package to packages/graph-runtime/device-check/packages.cmake"`); the other three pass over `core` alone. (Predicted from the test.)

Run: `sed -n '12496,12504p' docs/modular-graph-phase-e1-plan.md | git apply -R`
Expected: nothing: the same range as above, reversed, so `packages/graph-runtime/device-check/packages.cmake` is the task's file again (`git diff` of it shows only the task's own change, if the task changes it at all). Then `cmake --build build --target retroplug-device-mode-test -j$(nproc)` and `build/bin/retroplug-device-mode-test`: `All tests passed (2317 assertions in 10 test cases)` again.

- [ ] **Step 6: The image, the link trap and `pnpm device:check`**

`device-check/CMakeLists.txt` is the Pico SDK project: `PICO_BOARD` pico2, `PICO_PLATFORM` rp2350-arm-s, the SDK's
toolchain; reflect-cpp at the pin rpcpp's `CPMAddPackage` line names (0.26.2: `RP_REFLECTCPP_SOURCE_DIR` must be that
version, or a shallow FetchContent clone of the tag); `cmake/packages.cmake` in device mode with the package libraries
on the SDK's code-generation flags; `retroplug_product(rp-device-check PACKAGES ${RP_DEVICE_CHECK_PACKAGES} DEVICE)`;
`rp-device-runtime` and `rp-device-stdlib` derived from `pico_runtime` and `pico_stdlib` less `pico_malloc` (stopping
if that name is gone, warning if the SDK is not 2.1.1), stdio over the UART on and USB off, `pico_minimize_runtime`
for size; the trap list (the malloc family, `sbrk`, the guards, and every `new`, `delete`, `__cxa_` throw entry and
`std::__throw_*` read with `nm` from the multilib's `libstdc++.a`, each its own `--wrap`, the configure stopping if an
expected one is missing and re-deriving if the library changes); a link map; and `check-trap.cmake` after the link.
`main.cpp` runs the check over `rp_register_packages()` in the static arena, `static_assert`s that the cases header
was made for its own entries, and prints `rp-device-check: pass, <n> types, arena <b> bytes` or the failing step.
`trap-probe.cpp` is the positive control. `tools/build-device-check.sh` builds the cases tool and writes the header,
configures and builds the image (Release by default), builds the probe and stops unless its link fails naming its own
object for `__wrap__Znwj` and `__wrap_malloc`, and prints the sizes by owner from the map. `pnpm device:check` runs
it; `.gitignore` gains `/build-device/`.

Create `packages/graph-runtime/device-check/CMakeLists.txt`:

````cmake
# rp-device-check (docs/modular-graph-phase-e1-design.md section 7): a Pico SDK firmware for the RP2350 that runs every
# portable node type of `core` and `dsp`, built in device mode (RP_DEVICE, no exceptions, no RTTI), from its default
# settings as msgpack, and links with a trap on the heap and on exceptions. Its own CMake project on the SDK's
# toolchain, built in a build dir of its own, never part of the desktop's build/: tools/build-device-check.sh
# configures and builds it (pnpm device:check), and CI's linux-x64 job runs that script.
#
# Inputs, both given by the script:
#   PICO_SDK_PATH          the Pico SDK (2.1.1; no submodules needed)
#   RP_DEVICE_CHECK_CASES  the header rp-device-check-cases wrote (cases/main.cpp): each type's default settings as
#                          msgpack and its layout. The one thing this project takes from the desktop's build.
#   RP_REFLECTCPP_SOURCE_DIR  optional: a checkout of reflect-cpp at the pinned version; without it, it is fetched.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()
if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s)
endif()
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "rp-device-check: PICO_SDK_PATH (\"${PICO_SDK_PATH}\") is not a Pico SDK")
endif()
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "The Pico SDK" FORCE)
# Before project(): it picks the SDK's toolchain (arm-none-eabi, the RP2350's Cortex-M33 flags, the FPU on).
include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(rp_device_check C CXX ASM)
pico_sdk_init()

get_filename_component(RP_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# --- reflect-cpp, header-only here: the field reflection graph/fields.hpp compiles against (nothing of it is linked).
# The version is the desktop's pin, read from where the desktop pins it (rpcpp's CPMAddPackage), so the two builds
# cannot drift apart. A checkout the caller names (the desktop build's, which CI has already fetched) must be that
# version; otherwise a shallow clone of its tag.
file(READ "${RP_REPO}/deps/dpf.js/deps/rpcpp/CMakeLists.txt" _rp_rpcpp)
string(REGEX MATCH "GITHUB_REPOSITORY[ \t]+getml/reflect-cpp[ \t\r\n]+VERSION[ \t]+([0-9.]+)" _rp_pin "${_rp_rpcpp}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "rp-device-check: no reflect-cpp pin in deps/dpf.js/deps/rpcpp/CMakeLists.txt")
endif()
set(_rp_reflect_version "${CMAKE_MATCH_1}")
if(RP_REFLECTCPP_SOURCE_DIR)
    file(READ "${RP_REFLECTCPP_SOURCE_DIR}/CMakeLists.txt" _rp_reflect)
    string(REGEX MATCH "project\\(reflectcpp VERSION ([0-9.]+)" _rp_found "${_rp_reflect}")
    if(NOT CMAKE_MATCH_1 STREQUAL _rp_reflect_version)
        message(FATAL_ERROR "rp-device-check: ${RP_REFLECTCPP_SOURCE_DIR} is reflect-cpp ${CMAKE_MATCH_1}, and the "
                            "desktop pins ${_rp_reflect_version}")
    endif()
    set(_rp_reflect_dir "${RP_REFLECTCPP_SOURCE_DIR}")
else()
    include(FetchContent)
    # SOURCE_SUBDIR names a directory the checkout does not have, so the clone is made available without its own
    # CMakeLists being added: the headers are all this build takes.
    FetchContent_Declare(rp_reflectcpp
        GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
        GIT_TAG        v${_rp_reflect_version}
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  rp-headers-only)
    FetchContent_MakeAvailable(rp_reflectcpp)
    set(_rp_reflect_dir "${rp_reflectcpp_SOURCE_DIR}")
endif()
set(RP_REFLECTCPP_INCLUDE_DIR "${_rp_reflect_dir}/include")

# --- the packages, in device mode (cmake/packages.cmake): each defines its device targets alone, compiled as a device
# compiles graph/node.hpp, with the SDK's code-generation flags (each function and object in a section of its own, so
# --gc-sections drops what the image never calls; no unwind tables; no __cxa_atexit), as the image's own sources have
# them. node-sdk and graph-runtime are what every package and the check build on; the list is packages.cmake's.
include("${CMAKE_CURRENT_SOURCE_DIR}/packages.cmake")
set(RP_DEVICE_BUILD ON)
set(RP_DEVICE_COMPILE_OPTIONS
    "$<TARGET_PROPERTY:pico_standard_link,INTERFACE_COMPILE_OPTIONS>"
    "$<TARGET_PROPERTY:pico_cxx_options,INTERFACE_COMPILE_OPTIONS>")
include("${RP_REPO}/cmake/packages.cmake")
retroplug_package(node-sdk)
retroplug_package(graph-runtime)
foreach(_rp_package IN LISTS RP_DEVICE_CHECK_PACKAGES)
    retroplug_package(${_rp_package})
endforeach()

if(NOT RP_DEVICE_CHECK_CASES OR NOT EXISTS "${RP_DEVICE_CHECK_CASES}")
    message(FATAL_ERROR "rp-device-check: RP_DEVICE_CHECK_CASES names no header (\"${RP_DEVICE_CHECK_CASES}\"); "
                        "tools/build-device-check.sh writes one with the desktop's rp-device-check-cases")
endif()
# Copied in, so a new header re-runs the configure and rebuilds what includes it.
configure_file("${RP_DEVICE_CHECK_CASES}" "${CMAKE_BINARY_DIR}/generated/DeviceCheckCases.hpp" COPYONLY)

# --- the runtime: the SDK's pico_stdlib with pico_malloc taken out, and nothing else. pico_malloc is the one library
# of the SDK the trap cannot have: it DEFINES the wrapped malloc family (its own --wrap, a heap behind it), so with it
# in the link a call to malloc resolves to its wrapper, the trap fires only further down, at newlib's malloc.o, naming
# that rather than the object that called it, and check-trap.cmake fails the build on the wrapper's definition. The
# rest links under the full trap: stdio over the UART and pico_printf among them (the SDK wraps puts and printf to its
# own implementation, which allocates nothing), and stdio over USB too (TinyUSB's device CDC, scratch-built against
# SDK 2.1.1 with its submodules; this image leaves it out, as CI's SDK has none).
#
# Both lists are read from the SDK's own targets rather than copied, so an SDK that adds a runtime library brings it
# here too: pico_runtime's sources, link options and libraries less pico_malloc (rp-device-runtime), and pico_stdlib's
# with that in place of pico_runtime (rp-device-stdlib). A list that no longer holds the name it replaces stops the
# configure, as the SDK has moved under it.
if(NOT PICO_SDK_VERSION_STRING STREQUAL "2.1.1")
    message(WARNING "rp-device-check: built and checked against Pico SDK 2.1.1, and this is ${PICO_SDK_VERSION_STRING}")
endif()
function(_rp_derive_library name from drop)
    get_target_property(_libraries ${from} INTERFACE_LINK_LIBRARIES)
    if(NOT drop IN_LIST _libraries)
        message(FATAL_ERROR "rp-device-check: ${from} no longer links ${drop} (it links ${_libraries}); the Pico SDK's "
                            "runtime has changed, and ${name} must be derived from it again")
    endif()
    list(REMOVE_ITEM _libraries ${drop})
    list(APPEND _libraries ${ARGN})
    get_target_property(_sources ${from} INTERFACE_SOURCES)
    get_target_property(_options ${from} INTERFACE_LINK_OPTIONS)
    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE ${_libraries})
    if(_sources)
        target_sources(${name} INTERFACE ${_sources})
    endif()
    if(_options)
        target_link_options(${name} INTERFACE ${_options})
    endif()
endfunction()
_rp_derive_library(rp-device-runtime pico_runtime pico_malloc)
_rp_derive_library(rp-device-stdlib pico_stdlib pico_runtime rp-device-runtime)

# --- the link trap. Every symbol through which code reaches the heap or throws is wrapped, and no __wrap_ symbol is
# defined, so a reference to any of them in what the image links is an undefined __wrap_ reference: a link error
# naming the object (and the function) that made it. Every object is compiled with a section a function, so
# --gc-sections (pico_standard_link) means it names what the image keeps, not every function an object happens to
# hold. The C heap is newlib's, and newlib reaches it through its reentrant _r forms as well as the plain ones, so both
# are listed, and sbrk, the break under it; operator new and delete, the C++ runtime's throw path and the
# std::__throw_* helpers the standard headers call are read from the device's own libstdc++ (every variant of each its
# multilib defines), so a variant is never missed for being spelt differently. The function-local static guards
# (__cxa_guard_*) are wrapped too: libsupc++ implements them with a throw, and a device image has no business with
# them (the device product's registration is written to need none).
set(_rp_trapped
    malloc calloc realloc free memalign aligned_alloc posix_memalign valloc pvalloc reallocf reallocarray
    malloc_usable_size
    _malloc_r _calloc_r _realloc_r _free_r _memalign_r _valloc_r _pvalloc_r _reallocf_r _malloc_usable_size_r
    sbrk _sbrk _sbrk_r
    __cxa_guard_acquire __cxa_guard_release __cxa_guard_abort)
separate_arguments(_rp_cxx_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_rp_cxx_flags} -print-file-name=libstdc++.a
                OUTPUT_VARIABLE _rp_libstdcxx OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT IS_ABSOLUTE "${_rp_libstdcxx}" OR NOT EXISTS "${_rp_libstdcxx}")
    message(FATAL_ERROR "rp-device-check: no libstdc++.a for these flags (${CMAKE_CXX_FLAGS}): \"${_rp_libstdcxx}\"")
endif()
# A toolchain upgraded in place re-runs the configure, so the list is read from the library the link uses.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rp_libstdcxx}")
execute_process(COMMAND ${CMAKE_NM} -g --defined-only "${_rp_libstdcxx}" OUTPUT_VARIABLE _rp_nm)
set(_rp_cxx_names
    "_Znw[A-Za-z0-9_]+" "_Zna[A-Za-z0-9_]+"  # operator new and new[]
    "_Zdl[A-Za-z0-9_]+" "_Zda[A-Za-z0-9_]+"  # operator delete and delete[]
    "_ZSt[0-9]+__throw_[A-Za-z0-9_]+"        # std::__throw_*
    "__cxa_(throw|rethrow|allocate_exception|allocate_dependent_exception|free_exception|free_dependent_exception)"
    "__cxa_(init_primary_exception|throw_bad_array_new_length|bad_cast|bad_typeid)")
list(JOIN _rp_cxx_names "|" _rp_cxx_names)
string(REGEX MATCHALL "[ \t][TW] (${_rp_cxx_names})\n" _rp_cxx "${_rp_nm}")
list(TRANSFORM _rp_cxx REPLACE "^[ \t][TW] ([^\n]+)\n$" "\\1")
list(REMOVE_DUPLICATES _rp_cxx)
foreach(_rp_required _Znwj _ZdlPv __cxa_throw __cxa_allocate_exception _ZSt17__throw_bad_allocv)
    if(NOT _rp_required IN_LIST _rp_cxx)
        message(FATAL_ERROR "rp-device-check: ${_rp_libstdcxx} defines no ${_rp_required}; the trap would miss it")
    endif()
endforeach()
list(APPEND _rp_trapped ${_rp_cxx})
list(LENGTH _rp_trapped _rp_trapped_count)
message(STATUS "rp-device-check: the link trap wraps ${_rp_trapped_count} symbols")
set(_rp_trap_list "${CMAKE_BINARY_DIR}/trapped-symbols.txt")
list(JOIN _rp_trapped "\n" _rp_trapped_lines)
file(WRITE "${_rp_trap_list}" "${_rp_trapped_lines}\n")

# An executable of this project as the device builds it: the device-mode flags, the runtime and the trap. One --wrap a
# symbol: -Wl, splits at commas, so a joined list would hand the linker the symbols as input files.
function(_rp_device_executable target)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_compile_definitions(${target} PRIVATE RP_DEVICE)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
    target_link_libraries(${target} PRIVATE rp-device-stdlib)
    list(TRANSFORM _rp_trapped PREPEND "LINKER:--wrap=" OUTPUT_VARIABLE _wrap)
    target_link_options(${target} PRIVATE ${_wrap})
endfunction()

_rp_device_executable(rp-device-check main.cpp DeviceCheck.cpp)
target_include_directories(rp-device-check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated")
# The device check's product: the device entries of packages.cmake's list, which the case generator's product lists
# too. The image's entries reach main.cpp, which refuses a cases header made for other ones.
retroplug_product(rp-device-check PACKAGES ${RP_DEVICE_CHECK_PACKAGES} DEVICE)
get_property(_rp_entries GLOBAL PROPERTY RP_DEVICE_PRODUCT_rp-device-check_ENTRIES)
list(JOIN _rp_entries " " _rp_entries)
target_compile_definitions(rp-device-check PRIVATE "RP_DEVICE_CHECK_ENTRIES=\"${_rp_entries}\"")
target_link_libraries(rp-device-check PRIVATE retroplug-graph-exec)
target_link_options(rp-device-check PRIVATE "LINKER:-Map=${CMAKE_BINARY_DIR}/rp-device-check.map")
# The result goes out on the UART (stdio, GP0 at 115200), which also keeps stdio and printf in the image the trap
# judges. pico_minimize_runtime drops the runtime services the check uses none of (the default alarm pool, the FPGA
# check, the auto-initialised mutexes), for size: the trap needs none of it dropped, as stdio is linked, so the default
# panic prints through the SDK's puts. (Without pico_stdio a panic's puts is newlib's, whose buffers come from malloc;
# that is the image to avoid, and why stdio is not left out to save its size.)
pico_enable_stdio_uart(rp-device-check 1)
pico_enable_stdio_usb(rp-device-check 0)
pico_minimize_runtime(rp-device-check INCLUDE FLOAT DOUBLE PRINTF PANIC)

# None of the wrapped symbols may be defined either: a library that defined one (pico_malloc defines the malloc
# family's) would turn the trap into a heap. Checked on the linked image, which fails the build when one is.
add_custom_command(TARGET rp-device-check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DIMAGE=$<TARGET_FILE:rp-device-check> -DTRAPPED=${_rp_trap_list}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check-trap.cmake"
    VERBATIM)

# The trap's positive control: an image built exactly as rp-device-check is, whose one source allocates with new and
# with malloc. It must NOT link, and its link error must name its own object for both: tools/build-device-check.sh
# builds it on every run and fails unless it fails that way, so a trap that stopped reaching the link (an option moved
# to a target nothing links, a toolchain that ignores --wrap) fails CI rather than passing everything.
_rp_device_executable(rp-device-trap-probe trap-probe.cpp)
set_target_properties(rp-device-trap-probe PROPERTIES EXCLUDE_FROM_ALL ON)
pico_enable_stdio_uart(rp-device-trap-probe 1)
pico_enable_stdio_usb(rp-device-trap-probe 0)
pico_minimize_runtime(rp-device-trap-probe INCLUDE FLOAT DOUBLE PRINTF PANIC)
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '12540,12759p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/CMakeLists.txt`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/CMakeLists.txt`
Expected: `202d27611d5b39d120083cf228b921dc84e0134c`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/main.cpp`:

````cpp
// rp-device-check (docs/modular-graph-phase-e1-design.md section 7): the RP2350 image that runs the device check over
// every type its product registers (CMakeLists.txt: packages.cmake's list, as a device builds it), and the proof, at
// link time, that what it links allocates and throws nothing: the image links with a trap on the heap and on
// exceptions (CMakeLists.txt), so a use of either anywhere in its code is a link error naming the object that pulled
// it in.
//
// It reports the outcome three ways: one line on stdio (the UART, GP0 at 115200), gDeviceCheck for a debugger, and the
// board's LED, lit and steady for a pass and for a failure a count of blinks, the failed Step's value, repeated after
// a pause. E3 runs this check's successor on the rig; E1 builds it and runs nothing.

#include <cstddef>
#include <cstdio>
#include <string_view>

#include "DeviceCheck.hpp"
#include "DeviceCheckCases.hpp"
#include "graph/Packages.hpp"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/platform.h"
#include "pico/stdio.h"

// The cases were generated for one list of package entries, and this image registers its product's: they must be the
// same list, or a type would meet a case made for another (CMakeLists.txt passes the product's entries).
static_assert(std::string_view(RP_DEVICE_CHECK_ENTRIES) == rp::device_check::kEntries,
              "DeviceCheckCases.hpp was generated for other package entries than this image registers: regenerate it "
              "with rp-device-check-cases from the same packages.cmake (tools/build-device-check.sh)");

namespace {

alignas(16) std::byte gArena[rp::device_check::kArenaBytes];

void led(bool on) { gpio_put(PICO_DEFAULT_LED_PIN, on); }

}  // namespace

// The outcome, where a debugger finds it.
volatile rp::device_check::Step gDeviceCheckStep = rp::device_check::Step::Ok;
rp::device_check::Result        gDeviceCheck;

int main() {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    std::size_t                    count = 0;
    const rp_package* const* const packages = rp_register_packages(&count);
    gDeviceCheck = rp::device_check::run(packages, count, rp::device_check::kCases, std::size(rp::device_check::kCases),
                                         gArena, sizeof gArena, rp::device_check::kBlockFrames);
    gDeviceCheckStep = gDeviceCheck.step;
    if (gDeviceCheck.step == rp::device_check::Step::Ok)
        std::printf("rp-device-check: pass, %u types, arena %u bytes\n", static_cast<unsigned>(gDeviceCheck.types),
                    static_cast<unsigned>(gDeviceCheck.arenaBytes));
    else
        std::printf("rp-device-check: step %u at type %u (%s): %s\n", static_cast<unsigned>(gDeviceCheck.step),
                    static_cast<unsigned>(gDeviceCheck.type), gDeviceCheck.id ? gDeviceCheck.id : "-",
                    gDeviceCheck.error.message);

    const unsigned blinks = static_cast<unsigned>(gDeviceCheck.step);
    for (;;) {
        if (blinks == 0) {
            led(true);
            busy_wait_ms(1000);
            continue;
        }
        for (unsigned i = 0; i < blinks; ++i) {
            led(true);
            busy_wait_ms(200);
            led(false);
            busy_wait_ms(200);
        }
        busy_wait_ms(1000);
    }
}
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '12771,12844p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/main.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/main.cpp`
Expected: `ad10ccc1a254394efd3d03257629c21fd27f7e0b`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/check-trap.cmake`:

````cmake
# The link trap's second half (CMakeLists.txt): run on the linked image, it fails the build if the image defines the
# wrapper of any trapped symbol. --wrap turns each reference into a reference to __wrap_<symbol>, which only an
# undefined wrapper makes a link error; a library that defines one (the Pico SDK's pico_malloc defines the malloc
# family's, with a heap behind them) would let every use through.
#
# cmake -DNM=<nm> -DIMAGE=<elf> -DTRAPPED=<file, one symbol a line> -P check-trap.cmake
execute_process(COMMAND "${NM}" --defined-only "${IMAGE}" OUTPUT_VARIABLE _defined RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "check-trap: ${NM} could not read ${IMAGE}")
endif()
file(STRINGS "${TRAPPED}" _trapped)
set(_found "")
foreach(_symbol IN LISTS _trapped)
    string(FIND "${_defined}" " __wrap_${_symbol}\n" _at)
    if(NOT _at EQUAL -1)
        list(APPEND _found "__wrap_${_symbol}")
    endif()
endforeach()
if(_found)
    list(JOIN _found ", " _found)
    message(FATAL_ERROR "check-trap: ${IMAGE} defines ${_found}, so the link trap lets those uses through. Whatever "
                        "defines them must leave the image (see the rp-device-runtime comment in CMakeLists.txt).")
endif()
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '12856,12878p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/check-trap.cmake`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/check-trap.cmake`
Expected: `6c3fccaf0af13cde072d23280b6276653d6d651a`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/graph-runtime/device-check/trap-probe.cpp`:

````cpp
// rp-device-trap-probe: the link trap's positive control (CMakeLists.txt). Built as rp-device-check is, it allocates
// with operator new and with malloc, so it must fail to link, naming this object for __wrap__Znwj and __wrap_malloc;
// tools/build-device-check.sh fails unless it does. Never an image.

#include <cstdlib>

int* volatile  gProbeNew;
void* volatile gProbeMalloc;

int main() {
    gProbeNew = new int(1);
    gProbeMalloc = std::malloc(16);
    return 0;
}
````

Run: `mkdir -p packages/graph-runtime/device-check; sed -n '12890,12903p' docs/modular-graph-phase-e1-plan.md > packages/graph-runtime/device-check/trap-probe.cpp`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/graph-runtime/device-check/trap-probe.cpp`
Expected: `7bf1c1e6187df57d03db74deebc8d781ec3beb07`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `tools/build-device-check.sh`:

````bash
#!/usr/bin/env bash
#
# Builds rp-device-check, the RP2350 image that runs every portable rp.core and rp.dsp node type in device mode with a
# link trap on the heap and on exceptions (packages/graph-runtime/device-check, docs/modular-graph-phase-e1-design.md
# section 7), and prints what it costs in flash and RAM. CI's linux-x64 job runs it after the desktop build; nothing is
# flashed or run (E3 runs the check's successor on the rig).
#
#   1. Builds the case generator (rp-device-check-cases) in the desktop build, which must be configured (./build.sh),
#      and writes the cases header: each type's default settings as msgpack and the layout they give.
#   2. Configures the image's own CMake project, on the Pico SDK's toolchain, in a build dir of its own, and builds it.
#      A link error there naming __wrap_<symbol> is the trap: the object it names reached the heap or a throw.
#   3. Builds the trap's positive control, rp-device-trap-probe, which allocates with new and malloc, and stops unless
#      its link FAILS naming its own object for __wrap__Znwj and __wrap_malloc: the proof, on every run, that the trap
#      reaches the link.
#   4. Prints the image's allocated sections (arm-none-eabi-size -A) and its flash and RAM by owner, from the map.
#
# Usage:  tools/build-device-check.sh            (pnpm device:check)
#   PICO_SDK_PATH         the Pico SDK (2.1.1); when unset, /workspaces/pico-sdk, the devcontainer's. A path that is
#                         set and is not an SDK stops the run. The check needs none of the SDK's submodules.
#   RP_DESKTOP_BUILD_DIR  the configured desktop build (default build/)
#   RP_DEVICE_BUILD_DIR   the image's build dir (default build-device/)
#   RP_DEVICE_BUILD_TYPE  default Release (the SDK's default: -O3, NDEBUG); MinSizeRel for -Os

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DESKTOP="${RP_DESKTOP_BUILD_DIR:-$REPO_DIR/build}"
OUT="${RP_DEVICE_BUILD_DIR:-$REPO_DIR/build-device}"
TYPE="${RP_DEVICE_BUILD_TYPE:-Release}"
JOBS="$(nproc 2>/dev/null || echo 4)"

die() { echo "build-device-check: $*" >&2; exit 1; }

# An SDK named explicitly is the one used, or the run stops: a wrong path never quietly builds against another SDK.
if [ -n "${PICO_SDK_PATH:-}" ]; then
    sdk=$PICO_SDK_PATH
    [ -e "$sdk/pico_sdk_init.cmake" ] || die "PICO_SDK_PATH ($sdk) is not a Pico SDK"
else
    sdk=/workspaces/pico-sdk
    [ -e "$sdk/pico_sdk_init.cmake" ] || die "no Pico SDK: set PICO_SDK_PATH (the default, $sdk, is not one)"
fi
command -v arm-none-eabi-g++ >/dev/null ||
    die "no arm-none-eabi-g++ (apt: gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib)"
[ -f "$DESKTOP/CMakeCache.txt" ] || die "$DESKTOP is not a configured desktop build (run ./build.sh first)"

echo "== the cases (desktop: rp-device-check-cases)"
cmake --build "$DESKTOP" --target rp-device-check-cases -j"$JOBS"
mkdir -p "$OUT/cases"
"$DESKTOP/device-check/cases/rp-device-check-cases" "$OUT/cases/DeviceCheckCases.hpp"

# reflect-cpp at the desktop's pin: the desktop build's own checkout when it has one, which saves a clone (the image's
# CMakeLists checks its version against the pin either way, and clones the pinned tag without it).
reflect="$(sed -n 's/^reflectcpp_SOURCE_DIR:STATIC=//p' "$DESKTOP/CMakeCache.txt")"
generator=()
command -v ninja >/dev/null && [ ! -f "$OUT/CMakeCache.txt" ] && generator=(-G Ninja)

echo "== the image (Pico SDK $sdk, $TYPE)"
cmake -S "$REPO_DIR/packages/graph-runtime/device-check" -B "$OUT" ${generator[@]+"${generator[@]}"} \
    -DPICO_SDK_PATH="$sdk" -DCMAKE_BUILD_TYPE="$TYPE" \
    -DRP_DEVICE_CHECK_CASES="$OUT/cases/DeviceCheckCases.hpp" \
    ${reflect:+-DRP_REFLECTCPP_SOURCE_DIR="$reflect"}
cmake --build "$OUT" -j"$JOBS"

echo "== the trap's positive control (rp-device-trap-probe must not link)"
probe_log="$OUT/trap-probe.log"
if cmake --build "$OUT" --target rp-device-trap-probe -j"$JOBS" >"$probe_log" 2>&1; then
    die "rp-device-trap-probe LINKED: the link trap is not reaching the image's link ($probe_log)"
fi
for symbol in __wrap__Znwj __wrap_malloc; do
    grep -q "trap-probe\.cpp\.o: in function .main.:" "$probe_log" && grep -q "undefined reference to .$symbol'" "$probe_log" ||
        die "rp-device-trap-probe failed, but not with its own object's reference to $symbol ($probe_log)"
done
grep -E "trap-probe\.cpp\.o: in function|undefined reference" "$probe_log" | sed 's/^.*ld: //; s/^/  /'

ELF="$OUT/rp-device-check.elf"
MAP="$OUT/rp-device-check.map"
[ -f "$ELF" ] || die "no $ELF"

echo
echo "== $ELF: allocated sections"
arm-none-eabi-size -A "$ELF" | awk 'NR <= 2 || ($2 > 0 && $3 > 0)'

echo "== by owner (bytes; from the link map: flash is code and constants, data is initialised RAM, also in flash)"
awk '
function hex(s,    i, n) {
    n = 0
    s = tolower(substr(s, 3))
    for (i = 1; i <= length(s); i++) n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
    return n
}
/^Linker script and memory map/ { on = 1; next }
!on { next }
/^\.[A-Za-z_]/ { out = $1; pending = ""; next }
/^ \*fill\*/ { next }
/^ \.[^ ]+$/ { pending = $1; next }
/^ / {
    if ($1 ~ /^0x/ && pending != "") { size = $2; file = $3 }
    else if ($2 ~ /^0x/ && $3 ~ /^0x/) { size = $3; file = $4 }
    else { pending = ""; next }
    pending = ""
    if (file == "" || size == "0x0") next
    if (out == ".text" || out == ".rodata" || out == ".binary_info" || out == ".ARM.exidx" || out == ".flash_end")
        mem = 1
    else if (out == ".data") mem = 2
    else if (out == ".bss" || out == ".ram_vector_table" || out == ".uninitialized_data" || out ~ /^\.scratch_/) mem = 3
    else next
    owner = "toolchain (libc, libm, libgcc)"
    if (file ~ /libretroplug-core-nodes/) owner = "core: the rp.core nodes"
    else if (file ~ /libretroplug-dsp-nodes/) owner = "dsp: the rp.dsp nodes"
    else if (file ~ /libretroplug-dsp-kernels/) owner = "dsp: the kernels"
    else if (file ~ /libretroplug-graph-exec/) owner = "graph-runtime: the executor"
    else if (file ~ /rp-device-check\.dir\/(main|DeviceCheck)\.cpp/ || file ~ /\/packages\/products\//)
        owner = "the check (its cases and arena)"
    # The SDK sources compile into the image (CMakeFiles/rp-device-check.dir/<their path>); its own libraries build
    # under pico-sdk/.
    else if (file ~ /rp-device-check\.dir\// || file ~ /^pico-sdk\//) owner = "the Pico SDK runtime"
    total[owner, mem] += hex(size); owners[owner] = 1
}
END {
    n = split("core: the rp.core nodes|dsp: the rp.dsp nodes|dsp: the kernels|graph-runtime: the executor|" \
              "the check (its cases and arena)|the Pico SDK runtime|toolchain (libc, libm, libgcc)", order, "|")
    printf "  %-34s %9s %9s %9s\n", "", "flash", "data", "bss"
    for (i = 1; i <= n; i++) {
        o = order[i]
        printf "  %-34s %9d %9d %9d\n", o, total[o, 1], total[o, 2], total[o, 3]
        f += total[o, 1]; d += total[o, 2]; b += total[o, 3]
    }
    printf "  %-34s %9d %9d %9d\n", "total", f, d, b
}' "$MAP"
````

Run: `mkdir -p tools; sed -n '12915,13044p' docs/modular-graph-phase-e1-plan.md > tools/build-device-check.sh`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object tools/build-device-check.sh`
Expected: `641964c7423c32a125300e6bab3334beb86d6245`: the blob 48f3611a9 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Run: `chmod +x tools/build-device-check.sh`
Expected: nothing: the script is executable, as the commit records it (mode 100755).

Run: `test -x tools/build-device-check.sh`
Expected: exit 0.

````diff
diff --git a/.gitignore b/.gitignore
index 9583622d4..5e82ec46a 100644
--- a/.gitignore
+++ b/.gitignore
@@ -6,6 +6,8 @@
 /build-asan/
 # profiling build dir (tools/run-profile.sh)
 /build-prof/
+# the RP2350 device check's build dir (tools/build-device-check.sh)
+/build-device/
 # aarch64 cross-build dirs + sysroot (leftover build artifacts)
 /build-arm/
 /build-arm-prof/
diff --git a/package.json b/package.json
index c49fa1673..41a6a0ee8 100644
--- a/package.json
+++ b/package.json
@@ -25,6 +25,7 @@
     "test:cli-ts": "node scripts/cmake-build.js retroplug-cli && tools/run-cli-ts-tests.sh",
     "sdl:smoke": "node scripts/cmake-build.js retroplug-sdl && tools/run-sdl-smoke.sh",
     "sdl:pipewire": "node scripts/cmake-build.js retroplug-sdl && tools/run-sdl-pipewire.sh",
+    "device:check": "tools/build-device-check.sh",
     "screenshot": "node scripts/cmake-build.js retroplug-jack && tools/run-standalone.sh",
     "profile": "tools/run-profile.sh stats",
     "validate": "node scripts/cmake-build.js retroplug-clap retroplug-vst3 && tools/validate-plugins.sh",
````

Run: `sed -n '13060,13084p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm device:check`
Expected: exit 0, after the cases tool, the image's configure (`-- rp-device-check: the link trap wraps 78 symbols`; the SDK's own `TinyUSB submodule has not been initialized` warning is harmless, the check uses no TinyUSB) and its build. Then the probe's failure, these lines (each indented two spaces) under `== the trap's positive control (rp-device-trap-probe must not link)`:

````text
CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

Then the image's sections (`.text` 58248, `.rodata` 4852, `.data` 1848, `.bss` 412472) and the table by owner, these figures:

````text
flash data bss
core: the rp.core nodes 22002 0 0
dsp: the rp.dsp nodes 14027 0 0
dsp: the kernels 4424 0 0
graph-runtime: the executor 1404 0 0
the check (its cases and arena) 6326 40 411937
the Pico SDK runtime 15593 96 373
toolchain (libc, libm, libgcc) 1380 1708 432
total 65156 1844 412742
````

(spaced into columns in the output). `core`, `dsp` and the executor take no static RAM; the check's bss is the 400 KiB arena and the `res:1` buffer. `git status --short` still shows no `build-device/`, which `.gitignore` now lists.

Run: `RP_DEVICE_BUILD_TYPE=MinSizeRel RP_DEVICE_BUILD_DIR=/tmp/e1-device-os pnpm device:check`
Expected: exit 0, the probe failing as above, and the table's last line `total 46167 2236 413058` (its first row `core: the rp.core nodes 13832 0 0`). Then `rm -rf /tmp/e1-device-os`.

Run: `RP_DEVICE_BUILD_TYPE=Debug RP_DEVICE_BUILD_DIR=/tmp/e1-device-dbg pnpm device:check`
Expected: exit 0: a Debug image links under the full trap too, the SDK's `__assert_func` printing through its own printf, and the table's last line `total 65691 2236 413054`. Then `rm -rf /tmp/e1-device-dbg`.

- [ ] **Step 7: The CI step, and a dry run of it**

CI's linux-x64 job installs `gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`, clones the
Pico SDK's 2.1.1 tag with no submodules into `$RUNNER_TEMP`, and runs `pnpm device:check` with `PICO_SDK_PATH` set,
after the desktop build (reflect-cpp comes from it, at the desktop's pin). The dry run below is that step in a clean
environment: no `/usr/local/bin`, no picotool, a fresh shallow SDK.

````diff
diff --git a/.github/workflows/build.yml b/.github/workflows/build.yml
index 4be08c5e7..258fb6d92 100644
--- a/.github/workflows/build.yml
+++ b/.github/workflows/build.yml
@@ -116,6 +116,26 @@ jobs:
       # every platform and it rides along in the artifact below; `pnpm sdl:smoke` above then proves
       # it RUNS, on this job only. It is deliberately absent from release.yml's packaging lists.
 
+      # The device build (docs/modular-graph-phase-e1-design.md section 7): rp-device-check, the RP2350 image that runs
+      # every portable rp.core and rp.dsp node type in device mode, linked with a trap on the heap and on exceptions,
+      # so a node that allocates or throws on the device fails this step naming the object, and a probe that allocates
+      # must fail to link, proving the trap still reaches the link. Built, never run (E3 runs its successor on the
+      # rig). The Pico SDK is pinned to the tag the devcontainer's is, and the check needs none of
+      # its submodules; reflect-cpp comes from the desktop build above, at the desktop's pin. x86 linux alone carries
+      # it: the image is the same from any host.
+      - name: Install the device toolchain (arm-none-eabi, Pico SDK 2.1.1)
+        run: |
+          set -eux
+          sudo apt-get install -yqq --no-install-recommends \
+            gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
+          git clone --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk.git "$RUNNER_TEMP/pico-sdk"
+          arm-none-eabi-g++ --version
+
+      - name: Device build (rp-device-check, the link trap)
+        env:
+          PICO_SDK_PATH: ${{ runner.temp }}/pico-sdk
+        run: pnpm device:check
+
       - name: Install plugin-format validators
         run: |
           set -eux
````

Run: `sed -n '13129,13159p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `bash -c 'rm -rf /tmp/e1-ci && mkdir -p /tmp/e1-ci && git clone -q --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk.git /tmp/e1-ci/pico-sdk'`
Expected: exit 0: a fresh SDK at 2.1.1, no submodules.

Run: `env -i HOME=$HOME PATH=/usr/bin:/bin PICO_SDK_PATH=/tmp/e1-ci/pico-sdk RP_DEVICE_BUILD_DIR=/tmp/e1-ci/build-device bash tools/build-device-check.sh`
Expected: exit 0, with `PICO_SDK_PATH is /tmp/e1-ci/pico-sdk`, `-- rp-device-check: the link trap wraps 78 symbols`, the probe's three lines as in Step 6 and the same table, its last line `total 65156 1844 412742` (the stage's dry run, its paths under `/tmp/e1pro/s4fci`). The SDK's TinyUSB warning is the only configure noise. Then `rm -rf /tmp/e1-ci`.

- [ ] **Step 8: Run the suites**

Run: `cmake --build build -j$(nproc)`
Expected: exit 0 and no warning in the prototype's log (the one known warning, `-Wformat-truncation` in `plugin/PluginDSP.cpp`, appears only when that file recompiles).

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 174 test file(s) passed (jobs=16)`: the descriptor conformance and `buildGraph`'s goldens inside it pass, unchanged.

Run: `pnpm test:plugin`
Expected: all 11 binaries exit 0, `# 11 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 313 | 302 passed | 11 skipped`; graph-test `All tests passed (356435 assertions in 282 test cases)`; `retroplug-device-mode-test` `All tests passed (2317 assertions in 10 test cases)`; audio-test `All tests passed (1234835 assertions in 108 test cases)`; launchpad `All tests passed (144 assertions in 29 test cases)`; the small six at their own counts (midi 35 / 19, dynparams 24 / 7, plugin 21 / 6, lottie 7 / 1, classid 10 / 3, watcher 11 / 3).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1713095 assertions in 313 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: the same 167 files, with `# 144 case(s) SKIPPED in 65 file(s)` and the skip check passing: CI's view. Nothing in E1 adds a skip, so the skip baseline does not move.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed (jobs=16)`.

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` (fakes only, no hardware).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, then `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa renders match the golden's hashes`, then 5 passed, 0 skipped, 0 failed.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: 5 passed, 0 skipped, 0 failed.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed.

Run: `bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'`
Expected: exit 0: `render-menu` and `render-badge` 1 passed each, then `# 7 test file(s) passed (jobs=16)`.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-bliptoaster && mkdir -p /tmp/e1-bliptoaster/build /tmp/e1-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/e1-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/e1-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/e1-bliptoaster/retroplug-cli/ && cd /tmp/e1-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/e1-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed. The desktop products are untouched by the device build.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-nesvj && mkdir -p /tmp/e1-nesvj/build /tmp/e1-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/e1-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/e1-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/e1-nesvj/retroplug-cli/ && cd /tmp/e1-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/e1-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed. The desktop products are untouched by the device build.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 9: The sanitizers**

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc)`
Expected: exit 0: the ThreadSanitizer tree's two binaries, built from this task's tree (a stale binary proves nothing).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag; the list is the verification script's), then `All tests passed (1698163 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), and no ThreadSanitizer report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test -j$(nproc)`
Expected: exit 0: the AddressSanitizer tree's three binaries.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (1680370 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356435 assertions in 282 test cases)`, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-device-mode-test -j$(nproc)`
Expected: exit 0. From this task on the ASan tree builds its own `rp-device-check-cases` with ASan, runs it to write the header (LeakSanitizer on, so a leak there would fail the build) and then the binary: the run on the prototype's tip, whose code is this tree's, logged `Built target rp-device-check-cases` and `Built target retroplug-device-mode-test` with no leak or error line (`/tmp/e1plan/asan-tip-build.log`).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test`
Expected: `All tests passed (2317 assertions in 10 test cases)`, no AddressSanitizer or LeakSanitizer report: the device check's run over the device-mode objects among them (the prototype tip's run, `/tmp/e1plan/asan-tip-run.log`).

- [ ] **Step 10: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M .github/workflows/build.yml
 M .gitignore
 M CMakeLists.txt
 M cmake/packages.cmake
 m deps/sameboy
 M package.json
 M packages/dsp/native/CMakeLists.txt
 M packages/graph-runtime/native/CMakeLists.txt
 M packages/native/cmake/tests.cmake
 M packages/native/cmake/warnings.cmake
 M packages/native/test/device-mode/DeviceModeEntries.hpp
 M packages/node-sdk/native/CMakeLists.txt
 M packages/node-sdk/native/src/graph/node_desc.hpp
?? packages/graph-runtime/device-check/CMakeLists.txt
?? packages/graph-runtime/device-check/DeviceCheck.cpp
?? packages/graph-runtime/device-check/DeviceCheck.hpp
?? packages/graph-runtime/device-check/cases/CMakeLists.txt
?? packages/graph-runtime/device-check/cases/main.cpp
?? packages/graph-runtime/device-check/check-trap.cmake
?? packages/graph-runtime/device-check/main.cpp
?? packages/graph-runtime/device-check/packages.cmake
?? packages/graph-runtime/device-check/trap-probe.cpp
?? packages/native/test/device-mode/DeviceCheck.test.cpp
?? tools/build-device-check.sh
````

````bash
git add -- .github/workflows/build.yml .gitignore CMakeLists.txt cmake/packages.cmake package.json \
  packages/dsp/native/CMakeLists.txt packages/graph-runtime/device-check/CMakeLists.txt \
  packages/graph-runtime/device-check/DeviceCheck.cpp packages/graph-runtime/device-check/DeviceCheck.hpp \
  packages/graph-runtime/device-check/cases/CMakeLists.txt packages/graph-runtime/device-check/cases/main.cpp \
  packages/graph-runtime/device-check/check-trap.cmake packages/graph-runtime/device-check/main.cpp \
  packages/graph-runtime/device-check/packages.cmake packages/graph-runtime/device-check/trap-probe.cpp \
  packages/graph-runtime/native/CMakeLists.txt packages/native/cmake/tests.cmake \
  packages/native/cmake/warnings.cmake packages/native/test/device-mode/DeviceCheck.test.cpp \
  packages/native/test/device-mode/DeviceModeEntries.hpp packages/node-sdk/native/CMakeLists.txt \
  packages/node-sdk/native/src/graph/node_desc.hpp tools/build-device-check.sh
git commit -F - <<'EOF'
build(device): the RP2350 device check, with a link trap on the heap and on exceptions

rp-device-check (packages/graph-runtime/device-check) is a Pico SDK firmware for the RP2350, its own CMake project on
the SDK's toolchain, built in a build dir of its own. It is a device product of device-check/packages.cmake's list
(core dsp), the one list the image and its case generator both read: cmake/packages.cmake gains a device build
(RP_DEVICE_BUILD), in which each package's native/CMakeLists.txt defines its device targets alone and
retroplug_package_library() compiles them in device mode with the SDK's code-generation flags, and DEVICE products,
which take the manifests' `device` entries alone, refuse a package that has none, and register without a static guard.
Its main runs every type the product registers from its default settings as msgpack: query_memory, create into a
static arena, activate, one block through runBlock, destroy, and reports the outcome on the UART, in a global and on
the LED.

The image links with --wrap on the malloc family (newlib's _r forms and sbrk too), every operator new and delete, the
C++ runtime's throw path, the std::__throw_* helpers and the static guards, the C++ names read from the device's own
libstdc++, and defines none of the wrappers (a post-link check fails the build if anything does). rp-device-trap-probe,
built the same way and allocating with new and malloc, must fail to link naming its own object; the build script fails
unless it does, so every run proves the trap reaches the link. The runtime is the SDK's pico_stdlib with pico_malloc
taken out, both lists read from the SDK's own targets; stdio over the UART and printf link under the trap.

The cases come from rp-device-check-cases, a desktop tool and a device product of the desktop build: each portable
type's defaults read back from a desktop instance through a new SDK interface (rp.sdk.settings-json/1), converted with
settingsToMsgpack and checked by reading them back, plus the layout they give and the entries they were made for,
written as one header the image refuses at compile time if it was made for other entries. A type whose own validate
refuses its defaults (rp.dsp.normalize) gets the check host's one resource, res:1. The types' own heap and threads
declarations decide what a device registers; retroplug-device-mode-test now derives its device-mode objects, their
entries and its links from the manifests, holds every device entry's device-mode registration to its desktop types
without heap or threads, holds the check to carry every device entry and its cases to list them, and runs
DeviceCheck.cpp on the desktop over the device-mode builds, in the image's arena, allocating nothing.

tools/build-device-check.sh (pnpm device:check) builds the cases and the image and prints its sections and its flash
and RAM by owner; CI's linux-x64 job installs arm-none-eabi and the Pico SDK 2.1.1 and runs it. reflect-cpp is taken at
the desktop's pin, read from rpcpp's CPMAddPackage: the desktop build's checkout when the script finds one, a shallow
clone of the tag otherwise.
EOF
````

---

### Task 5: the docs say what E1 built

Design section 9's fifth commit and section 12. The E1 design's status line says built; sections 1 to 10 are corrected
in place where the build made them false (`SpscRing`'s home, the manifest as an object or a list with a `device` flag
per entry, `rp::choice`, enums with limits, the node author's one `#ifndef RP_DEVICE` and what enforces it, the split
headers, our own msgpack reader, `rp.sdk.settings-json/1`, DEVICE products, the trap's 78 symbols and the runtime it
leaves in, the CI step, the device-mode test's ten cases and the refused-source checks); section 11 is marked answered;
and section 12 records the build per commit, answers the prototype questions with the sizes, and lists what was found.
The main design's Phase E outline becomes E1 to E4 with the decisions behind it, and its library table and package
split follow. The specs, `AGENTS.md` and the older designs that named a moved path follow. No code changes.

**Files:**

- Modify: `AGENTS.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-b3a-design.md`, `docs/modular-graph-phase-c1-design.md`, `docs/modular-graph-phase-c3-design.md`, `docs/modular-graph-phase-d1-design.md`, `docs/modular-graph-phase-d2-design.md`, `docs/modular-graph-phase-e1-design.md`, `spec/00-overview.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/13-subsystems.md`, `spec/README.md`

- [ ] **Step 1: The design's record**

The E1 design gets its status line, sections 1 to 10 corrected in place, section 11 answered and section 12 "As
built": 12.1 to 12.4 per commit, 12.5 the prototype questions with the sizes table, 12.6 what was found (the delay's
default, the Windows run owed, the per-enum compile cost, Catch2, clang 18's CTAD, the debug `__assert_func`, the
sanitizers).

````diff
diff --git a/docs/modular-graph-phase-e1-design.md b/docs/modular-graph-phase-e1-design.md
index 9d39d7603..b9d3d14ca 100644
--- a/docs/modular-graph-phase-e1-design.md
+++ b/docs/modular-graph-phase-e1-design.md
@@ -1,6 +1,7 @@
 # Phase E1: a portable node core and the package boundary
 
-**Date:** 2026-11-03 · **Branch:** `feature/modularize` · **Status:** designed, not built
+**Date:** 2026-11-03 · **Branch:** `feature/modularize` · **Status:** built (E1, five commits); section 12 records the
+build, answers section 11 and carries what was left, and sections 1 to 10 are corrected in place
 
 Phase E is the Pico runtime: the C++ nodes, the TypeScript nodes and the device packs RetroPlug builds run unchanged in
 the desktop products and on the RP2350, and the Pico box becomes a graph host that keeps everything it does today. E1 is
@@ -11,12 +12,12 @@ split it now begins is the main design's section 11.
 
 ## 1. Before E1
 
-- **The executor is portable; the node authoring layer is not.** `retroplug-graph-exec` (`src/graph/Executor.cpp`) has
-  no heap, no exceptions and no dependency beyond libc. A C++ node is written as a plain struct and turned into the C
-  ABI table by `src/graph/node.hpp`, whose block path is clean, but whose control side is desktop code: an instance
-  heap-allocates its `Settings` (`new S`), keeps its layout as a `std::string` and its resource pins in a
-  `std::vector`, every control entry is wrapped in `try` / `catch`, and settings are parsed from JSON by reflect-cpp
-  (`rfl::json`, over yyjson).
+- **The executor is portable; the node authoring layer is not.** `retroplug-graph-exec` (`Executor.cpp`) has no heap,
+  no exceptions and no dependency beyond libc. A C++ node is written as a plain struct and turned into the C ABI table
+  by `node.hpp` (both under `packages/native/src/graph/` before E1), whose block path is clean, but whose control side
+  is desktop code: an instance heap-allocates its `Settings` (`new S`), keeps its layout as a `std::string` and its
+  resource pins in a `std::vector`, every control entry is wrapped in `try` / `catch`, and settings are parsed from
+  JSON by reflect-cpp (`rfl::json`, over yyjson).
 - **Settings use heap types.** The core nodes' settings hold `std::string` (`layout = "stereo"`, `downmix`'s `from` and
   `to`) and `std::vector<std::uint32_t>` (`join`'s inputs, `split`'s outputs); `resource_ref` keeps the document's
   `"res:N"` as a `std::string`; `validate` answers `std::optional<std::string>`.
@@ -39,11 +40,11 @@ split it now begins is the main design's section 11.
 | Where does N8 support live? | **A pack of its own** (E2) | Its nodes, Edio, link and lifecycle leave the core; a SNES flash-cart pack follows the same template |
 | How many Edio implementations? | **One, in C++**, in the N8 pack, wrapped for TS (E2) | `edio.ts`'s protocol and `edio.c` retire; the byte decoders may stay TS |
 | TS nodes on the Pico? | **Yes, on MicroQuickJS** (E4) | `defineNode` and the nodes the box runs move to its subset |
-| How do settings reach a node on the device? | **msgpack in the plan blob, decoded at plan load** | `RP_SETTINGS_MSGPACK`; lenient by field name, as JSON is; no change for node authors (section 6) |
-| Strings and lists in a portable node's settings? | **Fixed-capacity types**, `rp::text<N>` and `rp::list<T, N>` | The same JSON shape; no allocation on the device ever; the arena is sized exactly (section 5) |
+| How do settings reach a node on the device? | **msgpack in the plan blob, decoded at plan load** | `RP_SETTINGS_MSGPACK`; lenient by field name, as JSON is; a node's source changes only by one `#ifndef RP_DEVICE` around its desktop hooks (section 6) |
+| Strings, lists and choices in a portable node's settings? | **Fixed-capacity types**, `rp::text<N>`, `rp::list<T, N>` and `rp::choice` | The same JSON shape; no allocation on the device ever; the arena is sized exactly (section 5) |
 | What is a package? | **A workspace package per feature**, `packages/<name>/{native,ts}` with a manifest | Each product lists its packages and gets a generated `rp_register_packages()` (section 4) |
 | Where is the SDK boundary? | **`node-sdk`, `graph-runtime`, then node packages** | A pack depends on `node-sdk` alone (section 4) |
-| How is portability proved? | **A CI cross-compile with a link trap**, and device mode tested on the desktop | Heap use or a throw in portable code is a link error; on-device runs start at E2 and E3 (section 7) |
+| How is portability proved? | **A CI cross-compile with a link trap**, and device mode tested on the desktop | Heap use or a throw in portable code is a link error; a device run of the check starts at E3, when the box gets the plan loader; E2 runs the firmware with the pack's Edio on the rig, but no graph node (section 7) |
 
 ## 3. Phase E
 
@@ -69,38 +70,58 @@ E1 changes nothing on the Pico, so it has no hardware leg.
 ## 4. The packages
 
 A package is `packages/<name>/` with a `native/` part, a `ts/` part or both, and a `package.json` whose `retroplug`
-field is the manifest: the native CMake target, its entry function, the TS entry, and whether its node types build for
-the device. CMake reads the field with `string(JSON)`, so the manifest is the one source.
+field is the manifest: under `native`, the CMake target, its entry function, and whether its node types build for the
+device. CMake reads the field with `string(JSON)`, so the manifest is the one source. (A `ts` key will name the TS entry
+when a package has a `ts/` part; no E1 manifest has one, and nothing reads it yet.)
 
 ```json
 {
   "name": "@retroplug/dsp",
+  "version": "0.1.0",
   "private": true,
-  "retroplug": { "native": { "target": "rp-dsp", "entry": "rp_dsp_package", "device": true } }
+  "description": "The DSP kernels and the rp.dsp node types around them, and rp.dsp.resample (r8brain) as a package of its own.",
+  "retroplug": {
+    "native": [
+      { "target": "retroplug-dsp-nodes", "entry": "rp_dsp_package", "device": true },
+      { "target": "retroplug-dsp-resample-nodes", "entry": "rp_dsp_resample_package", "device": false }
+    ]
+  }
 }
 ```
 
 The entry is a unique function per package returning its `rp_package`. `rp_package_entry` stays reserved for a
-downloaded pack's dynamic library, so many statically linked packages never collide on it.
+downloaded pack's dynamic library, so many statically linked packages never collide on it. `retroplug.native` is an
+object or a list of them, one per `rp_package` the workspace package holds, each with its own `device` flag: `dsp`
+holds two, `rp.dsp` (`device: true`) and `rp.dsp.resample` (`device: false`, since it needs r8brain). A device product
+takes the `device` entries only, and a device entry's node types that declare `heap` or `threads` are compiled out of
+it (the planner's rule, `Target::Pico`). `node-sdk` has no entry: it is a plain library every package depends on.
 
 **E1's packages:**
 
 | Package | Holds | Depends on |
 |---|---|---|
-| `node-sdk` | `rp_node.h`, `node.hpp`, the description layer's support library (descriptor writer, canonical JSON, the graph document types `node.hpp` names), the msgpack settings decode, `rp::text` and `rp::list` | reflect-cpp (desktop) |
-| `graph-runtime` | the executor (`retroplug-graph-exec`, the part a device needs), the planner, the registry, the resource table, `GraphHost`, `SpscRing`, and the `rp.host` nodes the runtime implements itself (`rp.host.audio-out`) | `node-sdk` |
+| `node-sdk` | `rp_node.h`, `node.hpp` (split into the headers section 6 lists), the description layer's support library (descriptor writer, canonical JSON, the graph document types `node.hpp` names), the msgpack settings decode, `rp::text`, `rp::list` and `rp::choice`, and `SpscRing` | reflect-cpp (headers only on the device) |
+| `graph-runtime` | the executor (`retroplug-graph-exec`, the part a device needs), the planner, the registry, the resource table, `GraphHost`, and the `rp.host` nodes the runtime implements itself (`rp.host.audio-out`) | `node-sdk` |
 | `core` | the `rp.core` nodes | `node-sdk` |
-| `dsp` | the kernels (`src/dsp/`, with `util/Hash.hpp`) and the `rp.dsp` nodes, `resample` among them | `node-sdk`, r8brain |
+| `dsp` | the kernels (`packages/dsp/native/src/dsp/`, with `util/Hash.hpp`) and the `rp.dsp` nodes, `resample` among them | `node-sdk`, r8brain |
+
+`SpscRing` is in `node-sdk` rather than `graph-runtime`: native code outside the graph includes it (the N8, Launchpad
+and script-host links), and a pack depends on `node-sdk` alone, so it is the one home both `GraphHost` and a pack's
+device links can reach.
 
 A pack, ours in E2 or a third party's, depends on `node-sdk` and nothing else. `packages/native` keeps the engine, the
-systems, the hosts and every node type not yet carved. The files move with `git mv`, and every include path stays as it
-is (`#include "graph/node.hpp"` resolves from the package's include directory), so no node source changes for the move.
+systems, the hosts and every node type not yet carved; its own manifest lists those entries (`sameboy`, `mesen`,
+`host.io`, `n8`, `launchpad`) so that one mechanism registers everything, and each entry leaves it when its group is
+carved. The files move with `git mv`, and every include path stays as it is (`#include "graph/node.hpp"` resolves from
+the package's include directory), so no node source changes for the move.
 
 **Products.** Each product declares its packages, `retroplug_product(<target> PACKAGES core dsp ...)`, and CMake
-generates that product's `rp_register_packages()`, which replaces `Engine.cpp`'s hard-coded loop. The types not yet
-carved (`sameboy`, `mesen`, `host.io`, `n8`, `launchpad`) are registered through the same generated list from where they
-live until their own split. The offline pool keeps its own fixed subset (`core` and `dsp`), and the script-node pack
-keeps its own path: it is loaded at run time, not linked.
+generates that product's `rp_register_packages()`, which replaces `Engine.cpp`'s hard-coded loop; the order of the list
+is the registration order, and the golden holds it. The plugin is one product (`retroplug`) whose targets are every
+binary DPF builds from the plugin's libraries. A configure-time check (`retroplug_check_products`) stops the build when
+a binary links the engine and is no product, since a Linux module would otherwise fail only when loaded. A device
+product (`retroplug_product(... DEVICE)`, section 7) takes the device entries only. The offline pool keeps its own fixed
+subset (`core` and `dsp`), and the script-node pack keeps its own path: it is loaded at run time, not linked.
 
 ## 5. Fixed-capacity settings
 
@@ -110,71 +131,147 @@ A node meant for the device keeps every settings field fixed-size:
   error naming the field.
 - `rp::list<T, N>`: up to `N` elements of `T` inline. Read and written as a JSON array; a longer one is refused the same
   way.
+- `rp::choice<"a", "b", ...>` replaces `rfl::Literal` in a portable node's settings (`rfl/Literal.hpp` includes
+  `<string>` and `<vector>`). It keeps `Literal`'s interface (`value()`, `make<>`, `value_of<>`) and stores one byte;
+  its desktop parser delegates to the same `rfl::Literal`, so the JSON, the refusals and the schema are identical.
 - `rp::resource_ref<Type>` stores its resource id as a number. The desktop parses the document's `"res:N"` into it and
-  writes it back as the same string, so documents do not change.
-- Numbers, bools and enums, as now.
+  writes it back as the same string, so documents do not change. On the wire to the device it is that string too.
+- Numbers and bools, as now.
+- Enums, with limits. The device reads an enum by its enumerator name, or by a number in a string as reflect-cpp does
+  (`std::stoi`'s reading), through a scan of our own (`enums.hpp`, enchantum's technique, in the range -256 to 256).
+  The enum needs a fixed underlying type and no `operator|`; its enumerators must lie within the scanned range. A flag
+  enum (reflect-cpp reads those as names joined by `|`) and an unscoped enum without a fixed type are refused at
+  compile time, and so is an `rfl::config::enum_range<E>` that changes what the scan sees (the desktop holds the scan
+  to reflect-cpp's with a `static_assert` for every enum a portable `Settings` holds). A custom `rfl::Reflector<E>` is
+  not seen by that check or by the device decode, which reads enumerator names: a portable enum must not have one (a
+  rule, not a refusal; no node has one). `enum_param` is held to the same rules on the desktop too, which narrows what
+  it accepted before. Each portable enum costs about 55 to 95 ms of compile time per translation unit, for both scans
+  on the desktop.
 
 The device build `static_assert`s that a portable type's `Settings` holds only these, so a `std::string` slipping into
-one is a compile error with the type's name, not a link failure. A desktop-only node (anything declaring `heap`: the
-emulators, `rp.dsp.resample`, `rp.dsp.dpcm`) keeps whatever types it likes.
+one is a compile error with the type's name, not a link failure; so does a pointer, and so does a `heap` or `threads`
+type built for the device. A desktop-only node (anything declaring `heap`: the emulators, `rp.dsp.resample`,
+`rp.dsp.dpcm`) keeps whatever types it likes.
 
 The core nodes move to them: `join` and `split`'s counts become `rp::list<std::uint32_t, kMaxPorts>` (16, which they
 already enforce), and every `layout`, `from` and `to` becomes `rp::text<16>`. Their JSON Schemas are written as they
 are today (a string, an array of integers), so no descriptor golden moves and no document changes. `validate` stays a
-desktop hook, since the desktop validates every settings object before the device sees it.
+desktop hook, since the desktop validates every settings object before the device sees it; a node that builds for the
+device keeps it, and `adjust_layout`, inside one `#ifndef RP_DEVICE` (section 6). `AddKind`, `BiquadType` and
+`DitherAlgorithm` become `rp::choice`. A malformed `"res:N"` is now refused when the settings are parsed
+(`settings-invalid`) rather than when the node is created (`create-failed`), with the same text.
 
 ## 6. Device mode and msgpack
 
-**One header, built two ways.** A node's source includes `graph/node.hpp` and is written exactly as now. Compiled with
-`RP_DEVICE`, the header is the portable core only: the port and param wrappers, `Block`, the compile-time reflection of
-the `inputs`, `outputs` and `params` structs, the hook checks, `query_memory`, `create`, `destroy`, the instance
-vtable, and the msgpack decode. Everything that describes or validates is compiled out and its table entries are null:
-`descriptor`, `layout_for`, `prepare_settings` and `commit_settings`, `validate`, the JSON parse and the schema. The
-device never plans; the desktop planned and validated the plan before export.
+**One entry header, built two ways.** A node's source includes `graph/node.hpp` and reads its guide. Compiled with
+`RP_DEVICE`, the SDK is the portable core only. `node.hpp` is split by what each half is for:
+
+| Header | Holds | Built |
+|---|---|---|
+| `node_core.hpp` | the port and param wrappers, `Block`, the compile-time reflection of `inputs`, `outputs` and `params`, the hook checks, `ResourceHold` and the resource-field walk, the block-path entries | both |
+| `node_device.hpp` | `DeviceTypeCheck`, the in-place instance, the msgpack-only settings read, `query_memory`, `create`, `destroy`, the vtable with null `prepare`, `commit` and `release` | `RP_DEVICE` |
+| `node_desc.hpp` | the descriptor, `layout_for`, the JSON parse, `prepare_settings` and `commit_settings`, `validate`, the schema, the heap-settings instance | desktop |
+| `fields.hpp`, `enums.hpp`, `settings_types.hpp`, `settings_msgpack.hpp`, `msgpack_reader.hpp`, `PortTypes.hpp`, `ResourceId.hpp`, `sdk_namespace.hpp` | the field walk, the enum scan, the fixed-capacity types, the msgpack decode and reader, and the constants and names both builds share | both |
+| `settings_json.hpp` | the reflect-cpp parsers for `text`, `list`, `choice` and `resource_ref` | desktop |
+
+The portable boundary is a file boundary, which is easy to audit, where one file of interleaved `#if` blocks is not.
+The device build carries an `#error` (under libstdc++) when the translation unit has already included `<string>`,
+`<vector>`, `<map>`, `<optional>`, `<stdexcept>`, `<exception>`, `<iostream>`, `<functional>`, `<memory>`, `<random>`,
+`rfl/json` or yyjson; it sees only what is included before `node.hpp`, and the link trap is the real guard. The field
+walk is reflect-cpp's internal compile-time core (`num_fields`, `bind_to_tuple`) with our own name extraction, since
+its public `to_view` and `named_tuple_t` include heap headers; `rfl::Rename` and `rfl::Flatten` fields are refused at
+compile time, and a graph-test case holds the walk to `rfl::fields<S>()`.
+
+**A node author's rules.** A device-capable node's source is not written exactly as before, by the user's decision: it
+wraps its desktop-only hooks, `validate`, `adjust_layout` and the helpers only they use, in one `#ifndef RP_DEVICE`.
+`validate` returns `std::optional<std::string>` and `adjust_layout` takes a `graph::Layout`, desktop types the node's
+own source names, so the hooks cannot compile in device mode. Three things enforce the guard on every compiler: a
+forward declaration of `Layout` in device mode (an `adjust_layout` left outside fails with an incomplete type), a
+`DeviceTypeCheck` `static_assert` that no `validate` or `adjust_layout` is visible, and the `<target>-device` object
+libraries, which the desktop build compiles for every target of a manifest entry with `device: true` (the `RP_DEVICE`
+flag, `-fno-exceptions -fno-rtti` off MSVC, nothing linked), so an unguarded hook fails the desktop build and CI.
+Beyond that: settings hold the section 5 types only, a `heap` or `threads` type is not built for the device, and a
+source built both ways keeps its own names under `rp::graph`, `rp::detail` or an anonymous namespace. In device mode
+the SDK lives in an inline namespace `rp::device`, declared before any header opens `rp::graph`, so a node's own
+`namespace rp::graph` lands in `rp::device::graph` with no source change and the two builds link into one binary
+without an ODR clash. A package's `extern "C"` entries are renamed by the test's compile definitions.
 
 **A device instance owns its settings in place.** `query_memory` counts the `Settings` in the instance's own size, so
-`create` constructs them in the host's memory rather than with `new`; the resource pins are a fixed array sized by the
-number of resource fields; there is no layout string and no `try`. The desktop build keeps today's storage and its
-pointer swap at a commit.
-
-**`RP_SETTINGS_MSGPACK`.** A second value of `rp_settings.encoding`, accepted on both sides: the desktop's `create` and
-`query_memory` read either encoding, so the device's decode is exercised on the desktop from E1 on. The decode matches
-fields by name, keeps the default for a missing field and ignores an unknown one, as `rfl::DefaultIfMissing` does for
-JSON, so a blob written by a newer desktop still loads its known fields. The desktop gains a converter from a node's
-canonical JSON settings to msgpack (E3's `exportPlan` uses it). Whether reflect-cpp's msgpack reader builds with no
-exceptions and no heap is a prototype question (section 11); if it does not, the reader is our own, a small one over
-the same compile-time field list.
+`create` constructs them in the host's memory rather than with `new`: the resource pins, then the settings, then the
+node, so the node is destroyed first and the pins last. The pins are a fixed array sized by the number of resource
+fields (none for a type without any); there is no layout string and no `try`. The desktop build keeps today's storage
+and its pointer swap at a commit. The device's stack in `query_memory` and `create` holds the `Settings` and up to
+three 264-byte error buffers.
+
+**`RP_SETTINGS_MSGPACK`** (`rp_settings.encoding` is 1). It is accepted on both sides: the desktop's `create` and
+`query_memory` read either encoding for a type whose `Settings` is portable, so the device's decode is exercised on the
+desktop. A type with desktop settings (`rp.host.audio-out`, the emulators) and a script node read JSON alone. The reader
+is our own, about 530 lines over the compile-time field list, with no allocation and no exception (reflect-cpp's needs
+msgpack-c and a heap). It matches fields by name, keeps the default for a missing field and steps over an unknown one,
+as `rfl::DefaultIfMissing` does for JSON, so a blob written by a newer desktop still loads its known fields. It mirrors
+the JSON reader's rules (an unsigned field takes only a non-negative integer, a float field takes an integer or a
+float, a repeated key keeps the last) and its refusal wording, naming the field. The desktop's converter,
+`settingsToMsgpack(json)` in `node-sdk`, writes each number as the JSON spells it (an integer literal in its smallest
+form, anything with a fraction or exponent as float64); E3's `exportPlan` uses it. A desktop-only SDK interface,
+`rp.sdk.settings-json/1`, writes an instance's settings back as JSON, which every `node.hpp` type answers; the device
+check's case generator reads a type's defaults through it.
 
 ## 7. The device build
 
 **`rp-device-check`** is a small Pico SDK firmware, `packages/graph-runtime/device-check/`, built with `arm-none-eabi`
-against a pinned Pico SDK with no exceptions and no RTTI. It is a product like any other: its list is `core` and `dsp`,
-its registration is generated, and only the device-capable types are compiled, in `RP_DEVICE` mode (the manifest's
-`device` flag excludes the rest; one named anyway is refused, as the planner refuses it). Its `main`, for each portable
-type: the type's default settings as msgpack (generated at build time by a desktop tool from the type's defaults),
-`query_memory`, `create` into a static buffer, `activate`, one block through `runBlock`, `destroy`. CI builds it and
-does not run it; it is flashable, and E3 runs its successor on the rig.
-
-**The link trap.** The image links with `--wrap` on `malloc`, `calloc`, `realloc`, `free`, every `operator new` and
-`operator delete`, `__cxa_throw`, `__cxa_allocate_exception` and the `std::__throw_*` family, and defines none of the
-wrapped symbols, so any use in what it links is a link error naming the object that pulled it in. If the SDK's runtime
-needs `malloc` (its stdio, say), the image drops that part of the runtime; the trap is never loosened.
-
-**CI.** The linux-x64 job installs `gcc-arm-none-eabi` and the pinned Pico SDK and builds `rp-device-check`. The new
-desktop targets join the `/W4 /WX` list in `packages/native/cmake/warnings.cmake`.
+against the Pico SDK 2.1.1 (the devcontainer's tag) with no exceptions and no RTTI. It is a DEVICE product like any
+other: its list is `core` and `dsp` (`device-check/packages.cmake`, which the image and the case generator both
+include, and the generated header records, so they cannot drift), its registration is generated, and only the
+device-capable types are compiled, in `RP_DEVICE` mode. The device project is its own CMake build (`RP_DEVICE_BUILD`):
+`cmake/packages.cmake` finds the packages relative to itself, skips the desktop's `-device` objects, and builds the
+libraries with the SDK's code-generation flags. Its `main`, for each portable type: the type's default settings as
+msgpack, `query_memory`, `create` into a static 400 KiB arena, `activate`, one block of 64 frames through `runBlock`,
+`deactivate`, `destroy`, each failure a distinct `Step` naming the type; it prints one result line on the UART. The
+settings come from `rp-device-check-cases`, a desktop tool that is itself a DEVICE product of the desktop build: it
+creates each type with no settings, reads the defaults back through `rp.sdk.settings-json/1`, converts them with
+`settingsToMsgpack`, checks the round trip, resolves the layout as a planner would for an unwired node, and writes one
+header of constant arrays (with one completion table, `rp.dsp.normalize` needing `"buffer": "res:1"`, which the
+check's host serves). CI builds the image and does not run it; the ELF is flashable, and E3 is the first phase to run
+the check's successor on the rig, with plans loaded by the box.
+
+**The link trap.** The image links with `--wrap` on 78 symbols and defines none of the wrapped ones, so any use in
+what it links is a link error naming the object that pulled it in: the malloc family (plain, newlib's `_r` forms,
+aligned and `memalign` variants), `sbrk`, `__cxa_guard_*`, and every `operator new` and `operator delete`, `__cxa_`
+throw entry and `std::__throw_*` that the device multilib's own `libstdc++.a` defines (read with `nm` at configure,
+which stops if an expected one is missing). A post-link check fails the build if the image defines any `__wrap_`
+symbol. A positive control (`rp-device-trap-probe`, which does `new int(1)` and `std::malloc(16)`) is linked on every
+run of the script and must fail naming both. The runtime is the SDK's own with `pico_malloc` removed
+(`rp-device-runtime` and `rp-device-stdlib` are derived from `pico_runtime` and `pico_stdlib` at configure, and the
+configure stops if the name to remove is gone). Only `pico_malloc` has to leave: stdio over the UART links under the
+full trap, since the SDK wraps `printf` and `puts` to its own heap-free implementation. The image builds Release by
+default, so the Executor's asserts compile out; a Debug image links too.
+
+**CI.** The linux-x64 job installs `gcc-arm-none-eabi` (with newlib and its libstdc++), clones the Pico SDK 2.1.1 with
+no submodules and runs `pnpm device:check` (`tools/build-device-check.sh`), which builds the cases tool, the image and
+the probe, runs the trap proof and prints the sizes by owner. The new desktop targets join the `/W4 /WX` list in
+`packages/native/cmake/warnings.cmake`, the `-device` object libraries among them.
 
 ## 8. Tests
 
 - **`retroplug-device-mode-test`**, a new Catch2 binary (the eleventh in `pnpm test:plugin`), compiles the portable
-  sources with `RP_DEVICE` and `-fno-exceptions` on the desktop (Catch2's exceptions disabled). For every portable
-  `core` and `dsp` type over a table of settings: a device-mode node created from msgpack produces output bit-identical
-  to a desktop-mode node created from the same settings as JSON (same machine, so exactly); create, process and destroy
-  allocate nothing, counted by the existing counting allocator; the decode is lenient as JSON is (a missing field
-  defaults, an unknown one is ignored) and refuses an over-long `text` or `list` with the field's name.
-- **The settings types** in `retroplug-graph-test`: `rp::text` and `rp::list` read and write the same JSON as the
-  types they replace, a `resource_ref` round-trips `"res:N"`, and every bound is refused with its field.
+  sources with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` on the desktop, into an object library reached only
+  through the C ABI, as a device's host reaches it (Catch2 is one library built with exceptions, so its own
+  translation unit keeps them; MSVC keeps `/EHsc` on the objects). Ten cases: a device-mode type carries no
+  description and never re-plans; for every portable `core` and `dsp` type over a table of settings, a device-mode
+  node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, with
+  create, process and destroy allocating nothing (the counting allocator); the desktop reads msgpack and renders the
+  table as it does from JSON; the decode keeps a missing field's default and steps over an unknown one; it refuses
+  what JSON refuses, naming the field, in the same words; the converter writes each number as the JSON spells it; and
+  four `[device-check]` cases: each manifest device entry's types agree between the two builds, the image's cases list
+  exactly those, the run passes in the image's arena with no allocation, and it stops at a type that fails and names
+  it. A `try_compile` triple at every configure (GCC and Clang) requires a heap type, a pointer in `Settings` and a
+  heap header each to fail in device mode with its own reason.
+- **The settings types** in `retroplug-graph-test`: `[settings-types]` (`rp::text` and `rp::list` read and write the
+  same JSON as the types they replace, a `resource_ref` round-trips `"res:N"`, every bound is refused with its field),
+  `[fields]` (the field walk against `rfl::fields<S>()`) and `[enums]` (the enum scan against reflect-cpp's), and the
+  dither's `Mt19937` against `std::mt19937`.
 - **Registration**: each product's generated list registers exactly the type ids it registered before, against a
-  golden list per product.
+  golden list per product (`[packages]` in `retroplug-engine-graph-test`).
 - **Unchanged and run whole**: the descriptor goldens and conformance, `buildGraph`'s goldens, strict parity, the kit
   goldens and the render baseline under their require flags, `test:native`, `test:ui`, `test:node`, `test:cli-ts`,
   `check:docs`, `sdl:smoke`, the engine-graph binary in `build-tsan` and `build-asan`, the Reaper suite, and the
@@ -184,9 +281,9 @@ desktop targets join the `/W4 /WX` list in `packages/native/cmake/warnings.cmake
 
 Five commits, each building and passing on its own:
 
-1. **The packages.** `packages/node-sdk`, `graph-runtime`, `core` and `dsp` with their manifests; `src/graph` and
-   `src/dsp` moved into them; `retroplug_product()` and each product's generated `rp_register_packages()`; the
-   registration golden. No behaviour change.
+1. **The packages.** `packages/node-sdk`, `graph-runtime`, `core` and `dsp` with their manifests; the graph libraries
+   (`packages/native/src/graph`) and the kernels (`packages/native/src/dsp`) moved into them; `retroplug_product()`
+   and each product's generated `rp_register_packages()`; the registration golden. No behaviour change.
 2. **Fixed-capacity settings.** `rp::text`, `rp::list`, the numeric `resource_ref`; the core and DSP nodes' settings
    converted; the settings-type tests.
 3. **Device mode and msgpack.** `RP_DEVICE`, the in-place instance, `RP_SETTINGS_MSGPACK` and its decode, the JSON to
@@ -195,12 +292,14 @@ Five commits, each building and passing on its own:
    prototype answers recorded with the image's sizes.
 5. **Docs.** This design's as-built section; the main design's Phase E outline rewritten to E1 to E4 with section 2's
    decisions; spec/01, spec/02 and spec/06; spec/07 (E2 to E4 and the rest of the split as what is left); AGENTS.md (the
-   new binary, the device build, where `src/graph` and `src/dsp` went); every doc link to a moved path.
+   new binary, the device build, where the graph libraries and the kernels went); every doc link to a moved path.
 
 ## 10. Left beyond E1
 
 - The N8 pack and the single C++ Edio (E2).
-- `exportPlan`, the blob's format, the arena executor and the box as a graph host (E3).
+- `exportPlan`, the blob's format, the arena executor and the box as a graph host (E3); with it a device default for
+  `rp.core.delay` or a planner cap on the Pico (12.6), and a measure of the device's stack in `query_memory` and
+  `create`.
 - MicroQuickJS on the Pico (E4).
 - The rest of the package split (main design section 11): Mesen's per-console compile-out, per-product esbuild
   bundles, menus and the HD player onto extensions and views, LSDj, risa, smsggdj, mGB, BlipToaster and Launchpad as
@@ -209,9 +308,12 @@ Five commits, each building and passing on its own:
 - Bit-identity across targets: a float node on the Cortex-M33 can differ from the desktop (fused multiply-adds, newlib's
   `libm`), so E3's on-device comparisons hold events and integers exactly and audio to a tolerance, unless E3 decides on
   `-ffp-contract=off` everywhere.
+- A Windows run of `build.bat` and `test.bat` (12.6) and a macOS or libc++ run of the refused-source checks.
 
 ## 11. Prototype questions
 
+Answered in section 12.
+
 1. Does reflect-cpp's compile-time reflection (`to_view`, `named_tuple_t`, the enum names `enum_param` reads) build with
    no exceptions and no RTTI under the devcontainer's `arm-none-eabi` GCC?
 2. Does reflect-cpp's msgpack reader build with no heap and no exceptions, or is the reader our own?
@@ -219,3 +321,157 @@ Five commits, each building and passing on its own:
 4. What do `core` and `dsp` cost in flash and RAM in device mode? Recorded as E3's budget.
 5. Does any portable node reach a heap type through a kernel (a `std::vector` in a kernel's state, say), which the
    trap would name, and how is each answered?
+
+## 12. As built
+
+Five commits on `feature/modularize`: the packages, fixed-capacity settings, device mode and msgpack, the device build,
+and these docs. Parity bit for bit, the kit goldens, the render baseline, the descriptor goldens, `buildGraph`'s goldens
+and both consumer suites were untouched throughout, and nothing was re-recorded. The Reaper suite was not run by
+any commit; the acceptance run covers it. Sections 1 to 10 are corrected in
+place where the build made them false (the node author's rules, SpscRing's home, enums, the runtime the trap leaves
+in). The final counts: `retroplug-device-mode-test` ends at 10 cases, `retroplug-graph-test` at 282, and
+`retroplug-engine-graph-test` at 313 (the baseline's 311 and the two `[packages]` cases).
+
+### 12.1 The packages (commit 1)
+
+- **Manifests.** `packages/{node-sdk,graph-runtime,core,dsp}/package.json` carry the `retroplug` field;
+  `retroplug.native` is an object or a list, since `dsp` holds two `rp_package`s with different `device` flags.
+  `node-sdk` has no entry and no `device` flag: which half of it a build gets is chosen by the compile mode.
+  `graph-runtime` is `device: false` (`rp.host.audio-out` keeps a vector; the device host implements its own in E3).
+  `packages/native`'s own manifest lists the five not-yet-carved entries (target `retroplug-backend`), so one
+  mechanism registers everything.
+- **CMake.** `cmake/packages.cmake` holds `retroplug_package()` (reads the manifest with `string(JSON)`, adds
+  `packages/<name>/native`), `retroplug_package_library()` (static, C++20, PIC, `/bigobj`), `retroplug_product()` and
+  `retroplug_product_table()`, with configure-time FATAL checks (a missing manifest field, a duplicate or
+  non-identifier entry, an unknown package, a product declared twice, a `TARGETS` name that is no target). The root
+  names `RP_DESKTOP_PACKAGES core dsp graph-runtime native`, whose order reproduces the old loop (`graph-runtime`
+  follows `dsp` so that `rp.host` registers after `rp.dsp.resample`). Nine products carry it: the plugin (one product
+  with the DPF binaries as its targets, derived from what DPF built, so macOS's `retroplug-export` is included), the
+  host, the CLI, the SDL standalone, the Node addon (opt-in), and four test binaries.
+- **Link-time registration.** `Engine.cpp` references `rp_register_packages()` and each product's own object defines it;
+  GNU ld resolves an archive's undefined reference against a command-line object in any order. An executable that pulls
+  the engine in without being a product fails to link; a Linux module would fail only when loaded, so
+  `retroplug_check_products` stands in for the missing link error. The test binaries `retroplug-classid-test` and
+  `-watcher-test` never pull `Engine.o` in and are exempt.
+- **The golden** is `[packages]` in the engine-graph binary (`golden/product-registrations.json`, 26 ids per product):
+  it was recorded from a throwaway program at the design commit that printed the old hard-coded list, not from the new
+  code, so it could not agree with itself by construction.
+- **Moves.** All by `git mv`, no include changed; `cmake/graph.cmake` was deleted, its targets living in the packages'
+  CMakeLists. `retroplug-node-sdk-headers` is the include-only target the executor takes, so it needs no library.
+  `SpscRing` went to `node-sdk` (section 4). `util/Hash.hpp` went to `dsp`; `retroplug-core` takes only its include
+  directory, not a link edge, since it calls nothing in the kernels.
+
+### 12.2 Fixed-capacity settings (commit 2)
+
+- `rp::text<N>` (inline bytes and a length, an implicit `string_view`, a consteval literal constructor so a default that
+  does not fit fails to compile) and `rp::list<T, N>` (an inline array, a consteval initializer list; trivially copyable
+  elements only; the length is a byte up to 255 and a `uint16` above). Their reflect-cpp parsers delegate to
+  `std::string` and `std::vector`, so reads, writes and schemas are the replaced types'; an over-bound read is an error
+  naming the field.
+- `resource_ref` stores a number. A malformed `"res:007"` is refused at the settings parse (`settings-invalid`) where it
+  failed at create (`create-failed`); the mock backend refuses it first, in the same words.
+- `text<16>`: the longest layout any table accepts is `discrete(64)`, 12 bytes. The bound is `kMaxLayoutName`.
+
+### 12.3 Device mode and msgpack (commit 3)
+
+- **The split** is section 6's table. The portable boundary being a file boundary was chosen over interleaved `#if`
+  blocks; the param descriptor members became traits (`ParamDescOf`) so the wrappers have no `#if` inside.
+- **The node author's rules** (section 6) differ from what the design promised ("a node is written exactly as now"),
+  by the user's decision (`.superpowers/e-pico/decisions.md`): the one `#ifndef RP_DEVICE` around the desktop hooks,
+  `rp::choice` for `rfl::Literal`, and enums with limits. The `<target>-device` object libraries make the
+  guard enforced on every compiler the desktop build uses, and enums on the device (`enums.hpp`), with the
+  `enumMatchesReflect` `static_assert` for every enum a portable `Settings` holds. What it narrows: `enum_param` over an
+  unscoped enum without a fixed type or a flag enum compiled before and is now refused; an `rfl::config::enum_range<E>`
+  override is not honoured (the `static_assert` catches it); a custom `rfl::Reflector<E>` is not seen by the device
+  decode, which reads names (`rfl::internal::has_read_reflector<E>` would refuse it in one line; no node has one).
+- **Dither's `<random>`.** `std::mt19937` is heap-free but `<random>` includes `<string>`, `<vector>` and `<iosfwd>`.
+  `rp::dsp::Mt19937` (`Mt19937.hpp`) is the same engine, constexpr, with the standard's 10000th draw checked at compile
+  time and a graph-test sweep over 70 seeds; the kit goldens and parity did not move.
+- **Constant tables.** `node_type<T>()` is constexpr and `peer_as` compares with `&kVtable<T>`, so a package's
+  `kTypes[]` is constant-initialised and a device object has no `__cxa_guard_acquire` (libsupc++ implements it with a
+  throw).
+- **The converter's numbers** are as the JSON spells them, so a blob refuses what the JSON refuses (`{"u32": 2.0}` fails
+  in both).
+- **Refusing a source that must not build for the device**: at every configure `try_compile` requires `HeapType`,
+  `PointerSettings` and `HeapHeader` (under `test/device-mode/refused/`) each to fail with its own reason, matched on
+  the compiler's `error:` lines only. The heap-header check reads libstdc++'s include guards, so it is skipped under
+  libc++ (macOS), where the other two still run and the link trap stands in.
+- **Stack.** `query_memory` and `create` hold the `Settings` plus up to three 264-byte error buffers.
+
+### 12.4 The device build (commit 4)
+
+- **The pieces** are section 7's. Product-level decisions: the settings generator is a DEVICE product of the desktop
+  build; the cases and the image share `device-check/packages.cmake`; reflect-cpp's pin is read from rpcpp's
+  `CPMAddPackage` line (0.26.2) so the two builds cannot drift, with a FetchContent fallback; no UF2 (the ELF is the
+  artifact, and `pico_add_extra_outputs` would build picotool); `PICO_SDK_PATH` set but not an SDK is an error, the
+  fallback to `/workspaces/pico-sdk` applies only when unset; the build directory is `build-device/`.
+- **The trap** is 78 symbols, each its own `--wrap` (a comma-joined list is split by gcc into input files). The package
+  libraries compile with the SDK's code-generation flags (`-ffunction-sections -fdata-sections -fno-unwind-tables
+  -fno-use-cxa-atexit`), so the trap names what the image keeps.
+- **A new SDK interface**, `rp.sdk.settings-json/1`, exists because the ABI had no way to get a type's defaults and the
+  descriptor schema has none; adding either would have moved goldens. Every `node.hpp` type answers it (one string
+  compare on a plan-time `get_interface`).
+- **Not run:** the image is never flashed or run, so instance sizes on ARM are unmeasured; the x86-64 sizes are upper
+  bounds.
+
+### 12.5 The prototype questions, answered
+
+1. **Reflection builds with no exceptions and no RTTI on `arm-none-eabi` GCC 13.2.1, with the SDK's real flags
+   (`-mcpu=cortex-m33 -march=armv8-m.main+fp+dsp -mfloat-abi=softfp`).** But the public headers (`to_view`,
+   `named_tuple_t`) include `<iostream>`, `<string>`, `<vector>`, `<stdexcept>` and `<unordered_map>`, so the portable
+   core uses reflect-cpp's internal compile-time core (`num_fields`, `bind_to_tuple`, the fake-object name trick: only
+   `<tuple>`, `<array>`, `<utility>`, `<cassert>`) with a copy of its 20-line name extraction. The extraction needs a
+   deduced return type: a named `std::string_view` return makes GCC append the template arguments to the name.
+   `fields.hpp` is `#error` under `REFLECTCPP_USE_CPP26_REFLECTION`, and the `[fields]` case holds it to
+   `rfl::fields<S>()` across seven struct shapes.
+2. **The reader is our own.** reflect-cpp's msgpack needs msgpack-c (not in the tree), unpacks into a `msgpack_zone`
+   (malloc, 2048-byte chunks), reads `<istream>` and reports through `std::string`. `msgpack_reader.hpp` and
+   `settings_msgpack.hpp` are about 530 lines with no allocation or exception.
+3. **Only `pico_malloc` has to go.** It defines the wrapped malloc family, so a call to malloc resolves to the wrapper
+   and the trap fires further down, at newlib's `malloc.o`. Everything else links under the full trap, `pico_stdio`
+   (UART) and `pico_printf` included, because the SDK wraps `puts` and `printf` to its own heap-free implementation; a
+   scratch build that also linked `pico_stdio_usb` (TinyUSB) passed too. The first build's failure at newlib's
+   `findfp.o` came from the default panic's `puts` in an image with no `pico_stdio`, which is newlib's and allocates
+   its buffers; it was not a heap inside stdio. So the image keeps stdio and reports on the UART, and
+   `pico_minimize_runtime` is for size only.
+4. **The sizes** (RP2350 flags, link map by owner, bytes; stdio over the UART and printf in the image):
+
+   | Owner | Flash, Release (-O3) | Flash, MinSizeRel (-Os) | RAM (data / bss, Release) |
+   |---|---|---|---|
+   | core: `rp.core` nodes | 22,002 | 13,832 | 0 / 0 |
+   | dsp: `rp.dsp` nodes (decode included) | 14,027 | 8,615 | 0 / 0 |
+   | dsp: kernels | 4,424 | 2,944 | 0 / 0 |
+   | graph-runtime: executor | 1,404 | 1,078 | 0 / 0 |
+   | the check (cases, run, registration) | 6,326 | 5,100 | 40 / 411,937 (arena 409,600 and a 2,048-byte buffer) |
+   | Pico SDK runtime (stdio, printf) | 15,593 | 13,010 | 96 / 373 |
+   | toolchain (libc, libm, libgcc) | 1,380 | 1,588 | 1,708 / 432 |
+   | **total** | **65,156** | **46,167** | 1,844 / 412,742 |
+
+   Release's sections: `.text` 58,248, `.rodata` 4,852, `.data` 1,848, `.bss` 412,472. `core`, `dsp` and the executor
+   are 41,857 bytes of flash at -O3 (26,469 at -Os) and **no static RAM**: their RAM is per instance, from
+   `query_memory`.
+5. **No portable node reaches a heap type or a throw through a kernel**; the image links `core` and `dsp` under the full
+   trap and it names nothing in them. The one heap-bringing header was the dither's `<random>`. The DPCM kernel is
+   heap-free and sits in the device kernels archive but nothing references it, so it is not linked; its node holds a
+   vector and is desktop-only by its `heap` declaration. The trap's reach is proved: a `new int(7)` in `main` and a
+   `new std::vector<float>` in the gain kernel each fail naming the object, and the probe proves it on every run.
+
+### 12.6 What was found
+
+- **`rp.core.delay`'s default needs 384 KB per instance** (`maxSamples` 48,000, stereo; 384,104 bytes on x86-64), about
+  74% of the RP2350's 520 KB of RAM, and sizes the check's 400 KiB arena. E3 needs a device default or a planner cap.
+- **Windows and MSVC never built E1's device-mode targets.** The `-device` object libraries are in the default build
+  under `/W4 /WX`, so a Windows problem breaks `build.bat`'s plain build. The risks: the inline-namespace reopen,
+  `rp::choice::make<>` and `rp::text`'s literal constructor as consteval in default member initializers, the
+  template-lambda variable templates in `enums.hpp`, `__FUNCSIG__` in a consteval function, the copied `/bigobj`, and
+  `std::strcpy` in `DeviceCheck.cpp`. A `build.bat` and `test.bat` run is owed. libc++ runs two of the three
+  refused-source checks.
+- **Per-enum compile cost** is about 55 to 95 ms per int-underlying enum per translation unit (3 to 5 times
+  enchantum's), and the desktop pays both scans. If enums multiply, test validity with `enumNameFromSignature` directly
+  or batch values per signature.
+- **Catch2 cannot be built exception-free here** (one shared library), hence the object library of section 8.
+- **clang 18 rejects alias-template CTAD**, so the parsers spell `rfl::Unexpected<rfl::Error>(...)`.
+- **A debug `__assert_func`** in the SDK prints through its own printf and links under the trap; the Executor's asserts
+  compile out in Release.
+- **Sanitizers:** TSan and ASan runs of the engine-graph and graph-test binaries, and ASan of the device-mode test, are
+  clean.
````

Run: `sed -n '13379,13890p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

- [ ] **Step 2: The main design and the older designs**

The main design's status line, its section 5.6 library table rewritten for the packages, 5.8 (the Pico runtime:
msgpack, the fixed-capacity types, device mode, the link trap), the Phase E outline rewritten as E1 to E4 with the
decisions and a note that E1 is built, and section 11's package split (what E1 began, generated registration, the Edio
line pointing at E2). The D1 and D2 designs' prose names the kernels' and the nodes' new home, and the B3a, C1 and C3
designs' link-only lines that Task 1 left past the wrap column are re-broken before the link.

````diff
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 55bb003e1..a3dc8e807 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -1,6 +1,8 @@
 # Design: the modular node-graph engine
 
-**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B3b built, and Phases C1 and C2 (the rest of Phases C to E not yet built).
+**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phases A to D built (the graph libraries are in
+`packages/graph-runtime/` and `packages/node-sdk/`, the TS half in `packages/retroplug/src/graph/`), and Phase E1; E2
+to E4 not yet built.
 
 RetroPlug's long-term direction is a modular system in the spirit of Reaktor: emulated consoles (and
 the real hardware they can drive) appear as nodes in a graph alongside DSP effects and routing nodes.
@@ -816,18 +818,20 @@ format.
 
 ### 5.6 Library targets
 
-All of it lives in `packages/native/src/graph/`:
+The graph libraries are workspace packages since E1
+([modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md)):
 
 | Target | Contents | Depends on |
 |---|---|---|
-| `graph/rp_node.h` | the C ABI | nothing |
-| `retroplug-graph-exec` | the executor (`Executor`, `Plan.hpp`, `Queue.hpp`) | nothing (freestanding C++20; builds for the RP2350) |
-| `retroplug-graph` | canonical JSON, descriptors, the graph document, registry, resources, planner, `GraphHost`, the host nodes, and the `rp.core` node package | exec, reflect-cpp |
-| `graph/node.hpp` | the C++ authoring layer (header-only, on `retroplug-graph`'s include path) | reflect-cpp |
+| `graph/rp_node.h`, `Queue.hpp` (`node-sdk`) | the C ABI and the queue types | nothing |
+| `retroplug-graph-exec` (`graph-runtime`) | the executor (`Executor`, `Plan.hpp`) | nothing (freestanding C++20; builds for the RP2350) |
+| `retroplug-graph` (`graph-runtime`) | the registry, resources, planner, `GraphHost` and the host nodes | exec, `retroplug-node-sdk` |
+| `retroplug-node-sdk` (`node-sdk`) | canonical JSON, descriptors, the graph document, the settings types, the msgpack converter | reflect-cpp |
+| `graph/node.hpp` (`node-sdk`) | the C++ authoring layer, built for the desktop or, with `RP_DEVICE`, for the device (header-only) | reflect-cpp |
+| `retroplug-core-nodes` (`core`) | the `rp.core` node package | `node-sdk` |
 
 The `rp.core` package (`+`, `merge`, `join`, `split`, `downmix`, `relabel`, `delay`, `constant`,
-`buffer-player`, `buffer-recorder`) sits in `retroplug-graph` for now and moves into the `dsp` package
-when the package split happens.
+`buffer-player`, `buffer-recorder`) is the `core` workspace package; the DSP kernels and the `rp.dsp` nodes are `dsp`.
 
 None of these link a core, txiki, efsw or LVGL. They are the seed of the system-free "retroplug core".
 The TS half (`defineNode`, `defineExtension`, descriptors, `buildGraph`) lives in
@@ -852,9 +856,19 @@ is not; [modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md)).
   export applies the target's capability rules, so a `heap` node fails here, on the desktop.
 - The firmware sizes a static arena from the blob (node memory from `query_memory`, plus buffers) and
   runs the identical executor over node types compiled into the firmware.
-- The blob's settings encoding (likely msgpack, already in the tree via dpf.js) is the Pico phase's
-  decision; the ABI already carries an encoding tag (`rp_settings.encoding`).
-- TS nodes on the Pico run on MicroQuickJS within `check:portable`'s rules.
+- **Settings are msgpack** in the blob (`RP_SETTINGS_MSGPACK`, the second value of `rp_settings.encoding`),
+  decoded at plan load by a reader of our own that allocates nothing and throws nothing, and matches fields
+  by name as the JSON does. A portable node's settings hold only fixed-capacity types: `rp::text<N>`,
+  `rp::list<T, N>`, `rp::choice`, numbers, bools, enums within limits and numeric resource ids. Nothing on
+  the device allocates, and a device instance owns its `Settings` in the memory `query_memory` sized.
+- **Device mode.** The node-authoring header builds two ways: with `RP_DEVICE` it is the portable core
+  (block path, in-place instance, msgpack decode) with the descriptor and the JSON parse compiled out; a
+  device-capable node source wraps its desktop-only hooks (`validate`, `adjust_layout`) in one
+  `#ifndef RP_DEVICE`, which the desktop build checks for every package whose manifest says `device: true`.
+- **Proved by a link trap.** `rp-device-check` links every portable `rp.core` and `rp.dsp` type for the
+  RP2350 under `--wrap` on the heap, `sbrk`, the C++ allocation operators and the throw entry points, so any
+  use is a link error naming the object (E1; `pnpm device:check`, a CI step).
+- TS nodes on the Pico run on MicroQuickJS within `check:portable`'s rules (E4).
 
 ---
 
@@ -1243,14 +1257,15 @@ leaves (its section 11): P7's remainder, and a cart's battery reachable only at
   (`KitCompiler`, retired with its RPCs in D2; the goldens it seeded are still the oracle).
 - CLI `render` and background renders on the offline pool (as engine jobs; D3).
 
-D1 is built ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md), six commits): the kit path's DSP is a
-core-free library of block-streaming kernels (`src/dsp/`, `retroplug-dsp-kernels` + `retroplug-dsp-resample`), the kit
-compilers run it, and the graph has it as `rp.dsp.gain`, `.biquad`, `.dither` and `rp.dsp.resample`; TPDF dither is
-seeded from the sample it receives, so a kit compiles to the same bytes, and the kit baseline recorded 39 compiled
-banks. D2 is built too ([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md), seven commits): the
-offline job seam, `rp.dsp.normalize`, dither's `seedFrom` and `rp.dsp.dpcm`, and `packages/retroplug/src/kit/`, whose
-TypeScript compiler builds each pass as a document and packs the bank, held byte for byte to those 39 banks; native's
-compiler retired with its RPCs, and every host, the plugin included, compiles kits. D3 is built too
+D1 is built ([modular-graph-phase-d1-design.md](modular-graph-phase-d1-design.md), six commits): the kit path's DSP is
+a core-free library of block-streaming kernels (`packages/dsp/native/src/dsp/`, `retroplug-dsp-kernels` +
+`retroplug-dsp-resample`), the kit compilers run it, and the graph has it as `rp.dsp.gain`, `.biquad`, `.dither` and
+`rp.dsp.resample`; TPDF dither is seeded from the sample it receives, so a kit compiles to the same bytes, and the kit
+baseline recorded 39 compiled banks. D2 is built too
+([modular-graph-phase-d2-design.md](modular-graph-phase-d2-design.md), seven commits): the offline job seam,
+`rp.dsp.normalize`, dither's `seedFrom` and `rp.dsp.dpcm`, and `packages/retroplug/src/kit/`, whose TypeScript
+compiler builds each pass as a document and packs the bank, held byte for byte to those 39 banks; native's compiler
+retired with its RPCs, and every host, the plugin included, compiles kits. D3 is built too
 ([modular-graph-phase-d3-design.md](modular-graph-phase-d3-design.md), five commits): the CLI's `render` and the UI's
 background renders are engine jobs on the same pool, `RenderJobRegistry` and the `__rp_*Render` hooks are gone, an
 engine job's engine runs continuous 1024-frame blocks, and a render baseline records the bytes (a render's bits moved
@@ -1258,9 +1273,34 @@ once, deliberately).
 
 ### Phase E: the Pico runtime
 
-- `exportPlan` for a named target; the static-arena executor built for the RP2350.
-- First proven on the desktop with heap allocation forbidden, then on hardware, running a real plan with
-  a C node.
+Phase E makes the C++ nodes, the TypeScript nodes and the packs RetroPlug builds run unchanged in the desktop products
+and on the RP2350, and makes the Pico box a graph host that keeps everything it does today. It is four sub-phases
+([modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md) section 3 has the gates), taken on these
+decisions: **C++ on the device**, the same sources, no C rewrites; **msgpack settings** in the plan blob;
+**fixed-capacity settings types** (`rp::text`, `rp::list`) in portable nodes; **one C++ Edio**, in the N8 pack,
+wrapped for TS; **TS nodes on MicroQuickJS**; **a workspace package per feature** with a manifest and a generated
+registration per product; **`node-sdk`, `graph-runtime`, then node packages**, a pack depending on `node-sdk` alone;
+portability **proved by a CI cross-compile with a link trap**.
+
+- **E1: the portable node core and the package boundary.** The packages (`node-sdk`, `graph-runtime`,
+  `core`, `dsp`) and each product's generated `rp_register_packages()`; fixed-capacity settings; the node
+  header built for the desktop and the device with `RP_SETTINGS_MSGPACK`; `rp-device-check` and its link
+  trap. The Pico baseline is untouched.
+- **E2: the N8 pack.** `packages/n8`: the nodes, one C++ Edio (no exceptions, caller-supplied buffers, an
+  abstract transport: the desktop serial port, TinyUSB on the Pico, later Web Serial), the link and the
+  lifecycle (probe, menu boot, recovery); TS reaches Edio through an RPC facet. `edio.ts`'s protocol and
+  `pico/n8-host/edio.c` retire. A SNES flash-cart pack follows the same template. Gate: the desktop N8
+  tests, the real-hardware leg and the Pico baseline.
+- **E3: `exportPlan` and the box as a graph host.** `exportPlan` for `rp2350` (the blob, msgpack settings),
+  the static-arena executor, a desktop run with the heap forbidden; the box takes `rp.host.midi-in` from the
+  UART and the pack's `rp.n8.nes`, with today's bridge as its built-in default plan. Gate: the Pico baseline,
+  and a C++ node in the box's plan. It also decides the delay's device default and the float tolerance.
+- **E4: MicroQuickJS on the Pico.** A script host in the arena, `defineNode` made portable. Gate: a TS node
+  in the box's MIDI path on the rig.
+
+E1 is built ([modular-graph-phase-e1-design.md](modular-graph-phase-e1-design.md), five commits): the registration
+is generated, settings are msgpack with the fixed-capacity types, and the device image of `core` and `dsp` links
+under the trap (about 42 KB of flash at -O3 and no static RAM).
 
 ---
 
@@ -1320,20 +1360,22 @@ here so it is not lost.
 
 ### Package split
 
-- Carve `core` (the graph libraries, §5.6), `dsp`, `sameboy`, `mesen` (per console), `lsdj`, `mgb`,
-  `risa`, `smsggdj`, `bliptoaster`, `n8`, `launchpad` into packages, each with a native part, a TS part,
-  or both.
+- E1 began it: `node-sdk`, `graph-runtime`, `core` and `dsp` are workspace packages (`packages/<name>/{native,ts}`
+  with a `retroplug` manifest in `package.json`), and `packages/native`'s own manifest lists the types not
+  yet carved. Carve `sameboy`, `mesen` (per console), `lsdj`, `mgb`, `risa`, `smsggdj`, `bliptoaster`, `n8`
+  (E2) and `launchpad` the same way, each with a native part, a TS part, or both, and the TS halves of
+  `core` and `dsp`.
 - **Mesen** compiles all seven consoles into one 426-object archive (NES 6.6 MB, SNES 6.3 MB of objects,
   ...), and `Core/Shared/Emulator.cpp` plus `Core/Debugger/Debugger.cpp` reference every console, so the
   linker cannot drop any. Per-console compile-out is a `MESEN_ENABLE_<X>` patch to ~20 files plus a
   filtered glob (Mesen is vendored, not a submodule). NES-only removes roughly half the object bytes.
-- Generated `registerCoreBackends` / `rp_register_packages()` per product; `txiki` and `efsw` out of
+- `rp_register_packages()` is generated per product (E1); `registerCoreBackends` the same way; `txiki` and `efsw` out of
   `retroplug-backend` into opt-in libraries; LSDj kit DTOs out of `BackendTypes.hpp`; the risa DMC codec's
   dependency on LSDj files removed; per-product esbuild entries (today every bundle carries every cart).
 - Menus (`menuDefs.ts`'s 240 cart-specific lines, `TRACKER_UI`), catalogs, the HD player and the overlays
   move onto extensions and views.
-- The Edio protocol exists three times (TS source of truth, C++, Pico C), kept in sync by hand; a package
-  boundary is the moment to fix that.
+- The Edio protocol exists three times (TS source of truth, C++, Pico C), kept in sync by hand; E2 fixes
+  that with one C++ implementation in the N8 pack.
 
 ### Web host
 
diff --git a/docs/modular-graph-phase-b3a-design.md b/docs/modular-graph-phase-b3a-design.md
index 7d2ee7778..4cb852be0 100644
--- a/docs/modular-graph-phase-b3a-design.md
+++ b/docs/modular-graph-phase-b3a-design.md
@@ -104,7 +104,8 @@ cannot be read refuses the whole package, and a second `load` is refused, leavin
 Every type is served by the same functions, which the ABI could not tell apart: its type-level calls had no context
 (§10). So `descriptor`, `layout_for`, `query_memory` and `create` now take the `const rp_node_type*`
 they are called through, and `rp_node_type` gains a trailing `ctx`, null for a C++ node and never read by the host,
-which for a script type points at its definition's record ([rp_node.h](../packages/node-sdk/native/src/graph/rp_node.h)).
+which for a script type points at its definition's record
+([rp_node.h](../packages/node-sdk/native/src/graph/rp_node.h)).
 
 ### 2.3 Marshalling
 
@@ -191,7 +192,8 @@ instance, with the layout `layout_for` gives for the new settings.
   control or resource output whose id, kind and resource type match. The rest start afresh: a param at its default,
   its document value (if the document gives one) sent again, a control output at 0, a resource slot empty.
 - Feedback wires keep their last block, as they already do across a plan swap when a wire's key (its two
-  endpoints) and shape are unchanged ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). A queue
+  endpoints) and shape are unchanged
+  ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). A queue
   front whose source's capacity changed carries its block too while no audio thread runs, counting what the new
   capacity cannot hold as the source's drops, and starts empty while one does (the control thread may not read a
   front the audio thread is filling); a front whose channel count changed starts silent.
diff --git a/docs/modular-graph-phase-c1-design.md b/docs/modular-graph-phase-c1-design.md
index e5c85c5a8..78683e206 100644
--- a/docs/modular-graph-phase-c1-design.md
+++ b/docs/modular-graph-phase-c1-design.md
@@ -206,7 +206,8 @@ ring (release on push, acquire on adopt) carries the whole built pack with it.
      holds already, or a type id another package holds.
   3. The Engine replans its current document: the last request `setGraph` published, with its tap carried as it is
      (the request re-arms no tap, so its swap carries none: section 11's second question). The planner already
-     rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp)
+     rebuilds an instance whose registry entry changed
+     ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp)
      `reconcile`), so every node of the pack gets a fresh instance of the new code under the same node id, its JS state
      not carried over, while every other node keeps its instance. A document that places none of the old
      generation's types would plan as it runs, so it is not replanned. The replan leaves a core parked since that
diff --git a/docs/modular-graph-phase-c3-design.md b/docs/modular-graph-phase-c3-design.md
index d72997ec4..830a1de97 100644
--- a/docs/modular-graph-phase-c3-design.md
+++ b/docs/modular-graph-phase-c3-design.md
@@ -44,7 +44,8 @@ construct-time fields ... stay in the build spec until C3 builds cores in their
   and `adopt` (a project load: one build per system, then one publish). System ids are never reused, so every rebuild
   is a new id and a new node id `sys-<id>`. The load-time roles' `onConstruct` hooks fold asset overrides into
   `romBytes` (`lsdj-assets`, `risa-assets`, `bliptoaster-assets`) or seed an empty LSDj battery (`lsdj-sync`).
-- **Resources** exist in the graph layer ([ResourceTable.hpp](../packages/graph-runtime/native/src/graph/ResourceTable.hpp)):
+- **Resources** exist in the graph layer
+  ([ResourceTable.hpp](../packages/graph-runtime/native/src/graph/ResourceTable.hpp)):
   immutable byte blocks with one owner and a pin per plan binding, freed once the owner has dropped them and no pin is
   left, ids never reused. Nothing creates one over RPC yet, and no node reads one from its settings: main design
   section 2.5 planned that host service "with the first system node".
diff --git a/docs/modular-graph-phase-d1-design.md b/docs/modular-graph-phase-d1-design.md
index 26762f87d..518df33b4 100644
--- a/docs/modular-graph-phase-d1-design.md
+++ b/docs/modular-graph-phase-d1-design.md
@@ -77,11 +77,11 @@ unchanged, and `test-native/kit-baseline.test.ts` is their runner now (D2 design
 
 ## 4. The DSP library
 
-**The targets.** `retroplug-dsp-kernels` (`packages/native/src/dsp/`) is a static library linking the C++ standard
-library and nothing else: gain, peak and the normalising gain, the biquads, dither and quantise.
+**The targets.** `retroplug-dsp-kernels` (`src/dsp/`, in `packages/dsp/native/` since E1) is a static library linking
+the C++ standard library and nothing else: gain, peak and the normalising gain, the biquads, dither and quantise.
 `retroplug-dsp-resample` is `Resample.cpp`, taking r8brain's headers and not linking its library, and linking
-`Threads::Threads` for r8brain's filter-cache mutex, so a build with no resampler omits that target and sees neither the
-heap nor pthreads nor RTTI. r8brain's static members are `retroplug-r8brain-base`, a target of their own beside it
+`Threads::Threads` for r8brain's filter-cache mutex, so a build with no resampler omits that target and sees neither
+the heap nor pthreads nor RTTI. r8brain's static members are `retroplug-r8brain-base`, a target of their own beside it
 (`R8bBase.cpp`, built on the throwing allocator section 10 describes): that one translation unit is vendored r8brain
 code, so it keeps the compiler's default warning level rather than this tree's `/W4 /WX` on Windows, and gets the
 deps' `-w` on GNU/Clang. `retroplug-core` links both for the kit path, the node packages link them, and so does
diff --git a/docs/modular-graph-phase-d2-design.md b/docs/modular-graph-phase-d2-design.md
index feb908639..1faefe897 100644
--- a/docs/modular-graph-phase-d2-design.md
+++ b/docs/modular-graph-phase-d2-design.md
@@ -368,7 +368,7 @@ moved a kit byte or a parity baseline either.
   stays. CI's Catch2 step drops `RP_KIT_GOLDEN_REQUIRE_TARGET` with the suite it guarded, and the `test:native` step
   keeps it, because the TypeScript goldens still compare exact bytes there. The C++ seam test no longer holds the
   compiler's side of an empty window: `kit-compile`'s EMP slots and the edges golden do.
-- **7, docs.** This section, the specs, AGENTS.md and `docs/lsdj.md` carry the build (and `src/dsp/`'s nodes, the
+- **7, docs.** This section, the specs, AGENTS.md and `docs/lsdj.md` carry the build (and the `rp.dsp` nodes, the
   compiler and the seam describe what they do now rather than what they replaced).
 
 ### Section 10's questions, answered
````

Run: `sed -n '13908,14148p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

- [ ] **Step 3: The specs**

`spec/00` names the package rows; `spec/01` the packages and a device-build section; `spec/02` the moved paths, the
`graph.cmake` text and a new "Packages, generated registration and device mode" section; `spec/06` the targets table
(the new SDK, runtime, node, device-mode and device-check targets), the eleventh binary and the device-mode test, a
`device:check` row and "The device build" with its CI step; `spec/07` E1 built and E2 to E4, the rest of the split,
the Windows run owed, libc++'s two of three refused checks, the enum limits, the open CMake minors and the delay's RAM
under E3; `spec/13` and `spec/README.md` follow.

````diff
diff --git a/spec/00-overview.md b/spec/00-overview.md
index 0cbb066fa..7fa801294 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -91,7 +91,10 @@ system routed to one pair by its audio routing.
 |---|---|---|
 | [`packages/native/`](../packages/native) | The C++ host: the RPC services + their facets, `Engine`, `SystemFactory`/backends, the command/snapshot/release seams, the script host the TS nodes (the MIDI routing, the controller session and the protocol nodes) run in, the DPF plugin, and both test hosts. | The sole native tree. |
 | [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane and React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the protocol nodes that run them, and the UI. | The sole control-plane + UI tree. |
-| [`packages/native/src/`](../packages/native/src) | The **shared core**, compiled into the `retroplug-core` static lib and linked by the host: `SystemBase`, `SameBoySystem`, `Mesen{Nes,Gba,Sms}System`, the offline seam's sample decode (`SampleCache`), and the transport *primitives* (`SpscRing`, `FrameBufferTriple`, `MemorySnapshotTriple`). The node-graph executor and planner (`src/graph/`) and the DSP kernels (`src/dsp/`) are their own libs, `retroplug-graph-exec` / `retroplug-graph` and the `retroplug-dsp-*` targets, reached by the `rp.dsp` node packages. ROM classification, the LSDj sav codec and the kit compiler are TS-owned. | Shared across the host + CLI + test binaries. |
+| [`packages/native/src/`](../packages/native/src) | The **shared core**, compiled into the `retroplug-core` static lib and linked by the host: `SystemBase`, `SameBoySystem`, `Mesen{Nes,Gba,Sms}System`, the offline seam's sample decode (`SampleCache`), and the transport *primitives* (`FrameBufferTriple`, `MemorySnapshotTriple`; `SpscRing` lives in `node-sdk`). The node-graph executor, planner and node SDK and the DSP kernels are workspace packages of their own (the rows below), reached through `retroplug-graph-exec` / `retroplug-graph` and the `retroplug-dsp-*` targets. ROM classification, the LSDj sav codec and the kit compiler are TS-owned. | Shared across the host + CLI + test binaries. |
+| [`packages/node-sdk/`](../packages/node-sdk) | The node SDK (E1): the C ABI (`rp_node.h`), `node.hpp` built for the desktop or, with `RP_DEVICE`, for the RP2350, the descriptor and canonical-JSON support library, the fixed-capacity settings types (`rp::text`, `rp::list`, `rp::choice`), the msgpack settings decode and converter, and `SpscRing`. A node pack depends on this alone. | Shared by every package. |
+| [`packages/graph-runtime/`](../packages/graph-runtime) | The executor (`retroplug-graph-exec`, no heap, no exceptions), the planner, the registry, resources and `GraphHost` (`retroplug-graph`), and `device-check/`, the RP2350 image that proves the portable nodes link under a heap-and-exception trap. | Shared by every host. |
+| [`packages/core/`](../packages/core) / [`packages/dsp/`](../packages/dsp) | The `rp.core` mixing nodes; the DSP kernels and the `rp.dsp` nodes (`rp.dsp.resample` beside them, desktop only). Both build for the device. | Shared by every host. |
 
 The LSDj-persistence path is TS-owned. `retroplug-core` is a neutral static lib in the
 native tree; `retroplug-backend` wraps it with the `Engine` (which runs the systems on
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index d6f948f0e..155366c58 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -130,8 +130,8 @@ Three primitives, three directions:
 - **SnapshotRegistry** — audio → control, the ONE read door (§4).
 - **release ring**: audio → control, a replaced plan handed back so the audio thread never frees (§5).
 
-All three are built on the shared-core transport primitives:
-[`SpscRing`](../packages/node-sdk/native/src/transport/SpscRing.hpp),
+All three are built on [`SpscRing`](../packages/node-sdk/native/src/transport/SpscRing.hpp) (in `node-sdk`) and the
+shared-core transport primitives
 [`FrameBufferTriple`](../packages/native/src/transport/FrameBufferTriple.hpp), and
 [`MemorySnapshotTriple`](../packages/native/src/transport/MemorySnapshotTriple.hpp).
 
@@ -423,20 +423,41 @@ without the frame).
 
 ### The DSP library and its nodes
 
-`src/dsp/` is the kit path's gain, biquad, dither and resample as block-streaming kernels, in two targets:
-`retroplug-dsp-kernels` (core-free, the C++ standard library only) and `retroplug-dsp-resample` (r8brain, and with it
-the heap, pthreads and RTTI, so a build with no resampler omits it). r8brain's static members are a third target,
-`retroplug-r8brain-base`, beside the resampler: its one translation unit is vendored code (`R8bBase.cpp` includes
-r8brain's own `r8bbase.cpp`, on a throwing allocator), so it is kept off RetroPlug's own warning level.
-`retroplug-core` links neither (nothing of the core calls a kernel),
-and the graph has them as `rp.dsp` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and `rp.dsp.resample`,
-in targets of their own (`retroplug-dsp-nodes`, `retroplug-dsp-resample-nodes`) so `retroplug-graph` carries neither and
-a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone. A kit compile and a rendered patch run the
-same arithmetic through those nodes, so a patched kit and a rendered patch share one DSP. `rp.dsp.resample` takes an
-`audio-buffer` resource at that buffer's own rate and outputs at the plan's; every other node's ports carry the plan's
-one rate. A kit compile's output is reproducible: the TPDF dither seeds from a hash of the input it sees, held by the
-kit baseline ([06-build-test.md](06-build-test.md), [02-native-host.md](02-native-host.md), D1 design sections 4 and 5,
-D2 design section 11).
+`packages/dsp/native/src/dsp/` is the kit path's gain, biquad, dither and resample as block-streaming kernels, in two
+targets: `retroplug-dsp-kernels` (core-free, the C++ standard library only) and `retroplug-dsp-resample` (r8brain, and
+with it the heap, pthreads and RTTI, so a build with no resampler omits it). r8brain's static members are a third
+target, `retroplug-r8brain-base`, beside the resampler: its one translation unit is vendored code (`R8bBase.cpp`
+includes r8brain's own `r8bbase.cpp`, on a throwing allocator), so it is kept off RetroPlug's own warning level.
+`retroplug-core` links neither (nothing of the core calls a kernel), and the graph has them as `rp.dsp`
+(`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and `rp.dsp.resample`, in targets of their own
+(`retroplug-dsp-nodes`, `retroplug-dsp-resample-nodes`) so `retroplug-graph` carries neither and a Pico build links
+`retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone (the `dsp` package's manifest marks `rp.dsp` a device entry
+and `rp.dsp.resample` not). A kit compile and a rendered patch run the same arithmetic through those nodes, so a
+patched kit and a rendered patch share one DSP. `rp.dsp.resample` takes an `audio-buffer` resource at that buffer's
+own rate and outputs at the plan's; every other node's ports carry the plan's one rate. A kit compile's output is
+reproducible: the TPDF dither seeds from a hash of the input it sees, held by the kit baseline
+([06-build-test.md](06-build-test.md), [02-native-host.md](02-native-host.md), D1 design sections 4 and 5, D2 design
+section 11).
+
+### The packages and the device build
+
+Since E1 ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md)) the graph libraries are
+workspace packages, `packages/<name>/` with a `native/` part, a `ts/` part or both and a `retroplug` field in
+`package.json` naming its native CMake target, its entry function (a unique function returning the `rp_package`) and
+whether the entry builds for the device. The native packages are `node-sdk` (the C ABI and the authoring header, which
+a node pack depends on alone), `graph-runtime` (the executor, planner, registry and `GraphHost`), `core` (the
+`rp.core` nodes) and `dsp` (the kernels and the `rp.dsp` nodes); `packages/native`'s manifest lists the types not yet
+carved (`sameboy`, `mesen`, `host.io`, `n8`, `launchpad`). Each product (the plugin, `retroplug-host`, the CLI, the
+SDL standalone, the Node addon and four test binaries) declares its packages with `retroplug_product()` and gets a
+generated `rp_register_packages()` that the Engine reads in place of a hard-coded loop; a golden holds each product's
+registered type ids, and a configure-time check refuses a binary that links the engine without being a product.
+
+A portable node's settings hold only fixed-capacity types, and `node.hpp` builds two ways: the desktop's description,
+JSON and heap-settings half, and, with `RP_DEVICE`, a portable core whose instance owns its `Settings` in the host's
+memory and reads them from msgpack (`RP_SETTINGS_MSGPACK`). `rp-device-check` (`packages/graph-runtime/device-check/`,
+`pnpm device:check`) is the RP2350 image that runs every portable `rp.core` and `rp.dsp` type once, linked with
+`--wrap` on the heap and the exception entry points so that any allocation or throw is a link error. It is built, never
+run, in CI ([06-build-test.md](06-build-test.md)).
 
 ### The offline job pool
 
@@ -569,7 +590,7 @@ lost mutation.
 | [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), its one `SystemFactory`, the staged host MIDI, the host outputs and the registry; `buildSystem` / `destroySystem` / `setGraph` / `loadScriptPack`, `processBlock`, the command handler, `SyncTap` |
 | [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in packs, each two bare QuickJS runtimes of its own (the built-in pack first, more loaded and reloaded at runtime), and the audio runtimes' profiling |
 | [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
-| [`src/dsp/`](../packages/dsp/native/src/dsp) | the `rp.dsp` nodes' kernels, core-free: gain and peak, the normalising gain, the RBJ biquads, dither and quantise, the DMC encode, and the r8brain resampler |
+| [`packages/dsp/native/src/dsp/`](../packages/dsp/native/src/dsp) | the `rp.dsp` nodes' kernels, core-free: gain and peak, the normalising gain, the RBJ biquads, dither and quantise, the DMC encode, and the r8brain resampler |
 | [`DspNodes.cpp`](../packages/dsp/native/src/graph/nodes/DspNodes.cpp) / [`DspResampleNodes.cpp`](../packages/dsp/native/src/graph/nodes/DspResampleNodes.cpp) | the `rp.dsp` package (gain, biquad, dither, normalize, dpcm) and the `rp.dsp.resample` package beside it |
 | [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp) | the offline job pool: a document run to completion on a host of its own, or a render (an engine job: a `RenderHost` and `Engine` of its own), its inputs, outputs and completion (the request and completion types are in [`OfflineTypes.hpp`](../packages/native/src/host/engine/OfflineTypes.hpp), which `Engine.hpp` includes in place of the pool) |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 3b00ff024..64a27ed41 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -1106,42 +1106,43 @@ its **runtime** classes:
 | [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; its node gives it its knobs through value-guarded setters (`setModel`, `setHighpass`, the display group, `setSerialOutCapture`); its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
 | [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the one core with a [`ChannelTap`](../packages/native/src/system/ChannelTap.hpp) today (it implements the interface itself, `channelTap()` returning `this`), so it is the `SyncTap` target. The engine's tap and build check name no console: it forward-declares `ChannelTap` and includes no core header, and with C5's cart node it has no console check at all |
 | `SampleCache`, `util/MinizZip`, `EmbeddedRoms` | the offline seam's file decode (miniaudio, mono float at the file's own rate, the decode a kit compile reads its sources through), `zip`/`unzip`, and embedded-ROM resolution. `lsdj/Effects` is the kit path's reference arithmetic now, compiled into the two test binaries that hold the DSP nodes to it, not into the core |
-| `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |
+| `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the snapshot registry is built on (the command and release rings use `SpscRing`, which lives in `packages/node-sdk`) |
 
 The graph the engine runs on is not in `retroplug-core`: the executor, planner and
-[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp), with the `rp.core` mixing nodes and the
-`rp.host` package's `rp.host.audio-out`, are the `retroplug-graph` / `retroplug-graph-exec` libs
-(`src/graph/`), and the engine's own nodes (`src/host/graph/`: the system nodes, the host MIDI I/O, the cart's node
-and the control surface's two) and the script host (`src/host/script/`) compile into
-`retroplug-backend`.
+[`GraphHost`](../packages/graph-runtime/native/src/graph/GraphHost.hpp), with the `rp.host` package's
+`rp.host.audio-out`, are the `retroplug-graph` / `retroplug-graph-exec` libs (`packages/graph-runtime/`, over the node
+SDK in `packages/node-sdk/`); the `rp.core` mixing nodes are `retroplug-core-nodes` (`packages/core/`),
+and the engine's own nodes (`src/host/graph/`: the system nodes, the host MIDI I/O, the cart's node and the control
+surface's two) and the script host (`src/host/script/`) compile into `retroplug-backend`.
 
 ROM classification (the legacy build's C++ `RomSniffer`) and the LSDj sav codec are TS-owned - neither is in
 the C++ tree.
 
 ### The DSP targets and the `rp.dsp` packages
 
-The kit path's DSP and the graph's DSP nodes are the same kernels, in `src/dsp/`, split across five targets:
-`retroplug-dsp-kernels` (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode; the C++ standard
-library alone), `retroplug-dsp-resample` (the r8brain resampler, which brings the heap, pthreads and RTTI),
-`retroplug-r8brain-base` (r8brain's static members, one vendored translation unit compiled on an allocator that throws,
-with no target declared for the vendored library; kept off RetroPlug's own warning level), and the two node packages
-`retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and `retroplug-dsp-resample-nodes`
-(`rp.dsp.resample`), in [cmake/graph.cmake](../packages/dsp/native/CMakeLists.txt). `retroplug-core` links neither
-(nothing of the core calls a kernel), and `retroplug-graph` links neither; `retroplug-backend` links both packages
-and `Engine.cpp` registers them beside the other static packages, the offline job pool registering them in its own
-registry too. The split is load-bearing: `rp.dsp` never
-reaches r8brain, so a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone, leaves
-`dspResamplePackage()` out of its registration, and has neither the resampler nor r8brain in its binary, where the
-planner's `heap` refusal is only the second line (`rp.dsp.dpcm` declares `heap` and stays in the package, so the split
-is r8brain's link-time cost, not allocation). `rp.dsp.resample` reads an `audio-buffer` resource named by its
-settings at that buffer's rate and outputs at the plan's; a recorder and a resampler on one buffer in one plan are
-unsupported (the planner refuses a resource bound to a port reader when the same resource has a port writer, and a
-settings ref is not a binding it sees), so a driver fills a buffer before the plan that plays it. `rp.dsp.normalize`
-and `rp.dsp.dpcm` read a buffer the same way, and the dither's `seedFrom` names one too, so a kit-shaped document can
-name one buffer twice (the player's port and the settings ref). A rate change
-resumes where the output had reached, to within one frame, so a fresh r8brain run-in is heard from there rather than a
-gap. `rp.dsp.normalize` measures its peak over the channels it plays (the loudest of each played channel's peak), and
-`rp.dsp.dpcm` refuses a multi-channel buffer rather than mixing one.
+The kit path's DSP and the graph's DSP nodes are the same kernels, in `packages/dsp/native/src/dsp/`, split across
+five targets: `retroplug-dsp-kernels` (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode; the
+C++ standard library alone), `retroplug-dsp-resample` (the r8brain resampler, which brings the heap, pthreads and
+RTTI), `retroplug-r8brain-base` (r8brain's static members, one vendored translation unit compiled on an allocator that
+throws, with no target declared for the vendored library; kept off RetroPlug's own warning level), and the two node
+packages `retroplug-dsp-nodes` (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and
+`retroplug-dsp-resample-nodes` (`rp.dsp.resample`), in the `dsp` package's
+[CMakeLists.txt](../packages/dsp/native/CMakeLists.txt). `retroplug-core` links neither (nothing of the core calls a
+kernel), and `retroplug-graph` links neither; `retroplug-backend`'s products link both packages and `Engine.cpp`
+registers them, beside the other static packages, through the product's generated `rp_register_packages()`, the
+offline job pool registering them in its own registry too. The split is load-bearing: `rp.dsp` never reaches r8brain,
+so a Pico build links `retroplug-dsp-nodes` and `retroplug-dsp-kernels` alone, leaves `dspResamplePackage()` out of
+its registration, and has neither the resampler nor r8brain in its binary, where the planner's `heap` refusal is only
+the second line (`rp.dsp.dpcm` declares `heap` and stays in the package, so the split is r8brain's link-time cost, not
+allocation). `rp.dsp.resample` reads an `audio-buffer` resource named by its settings at that buffer's rate and
+outputs at the plan's; a recorder and a resampler on one buffer in one plan are unsupported (the planner refuses a
+resource bound to a port reader when the same resource has a port writer, and a settings ref is not a binding it
+sees), so a driver fills a buffer before the plan that plays it. `rp.dsp.normalize` and `rp.dsp.dpcm` read a buffer
+the same way, and the dither's `seedFrom` names one too, so a kit-shaped document can name one buffer twice (the
+player's port and the settings ref). A rate change resumes where the output had reached, to within one frame, so a
+fresh r8brain run-in is heard from there rather than a gap. `rp.dsp.normalize` measures its peak over the channels it
+plays (the loudest of each played channel's peak), and `rp.dsp.dpcm` refuses a multi-channel buffer rather than mixing
+one.
 
 Two of the nodes guard their state, because nothing between a plan and the host output rejects a non-finite sample:
 `rp.dsp.biquad` resets a channel's filter and writes that block as silence when the filter comes out of it non-finite
@@ -1163,6 +1164,35 @@ unchanged), unless `seedFrom` names a buffer, whose first channel's hash seeds t
 byte-identical because it refuses rather than recovers. No kit byte moves either way: the nodes are held to those
 kernels by their own tests.
 
+### Packages, generated registration and device mode
+
+The node SDK, the graph runtime and the `rp.core` and `rp.dsp` nodes are workspace packages since E1
+([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md));
+[cmake/packages.cmake](../cmake/packages.cmake) reads each `packages/<name>/package.json`'s `retroplug.native`
+manifest (an object or a list, one per `rp_package`, with a per-entry `device` flag) with `string(JSON)`.
+`retroplug_product(<target> PACKAGES ...)` generates that product's `rp_register_packages()` (declared in
+[`Packages.hpp`](../packages/graph-runtime/native/src/graph/Packages.hpp)), which `Engine.cpp` iterates in order; the
+order is load-bearing (`rp.host` after `rp.dsp.resample`) and the `[packages]` golden holds it. The plugin is one
+product whose targets are every binary DPF builds from the plugin's libraries; `retroplug_check_products` fails the
+configure for a binary that links the engine and is none, since a Linux module would only fail at load. The offline
+pool keeps its own fixed subset (`core` and `dsp`).
+
+`node.hpp` is the guide a node author reads and is built two ways. The portable half (`node_core.hpp` and
+`node_device.hpp`, with `fields.hpp`, `enums.hpp`, `settings_types.hpp`, `settings_msgpack.hpp` and
+`msgpack_reader.hpp`) compiles with `RP_DEVICE` and no exceptions or RTTI; the desktop half (`node_desc.hpp`,
+`settings_json.hpp`) holds the descriptor, `layout_for`, the JSON parse and the heap-settings instance. A device
+instance owns its `Settings` in the bytes `query_memory` counted, with the resource pins, and reads a msgpack blob
+(`rp_settings.encoding` is `RP_SETTINGS_MSGPACK`) with a reader of our own that matches fields by name, defaults a
+missing one and skips an unknown one; the desktop reads the same blob for a type whose settings are portable, and
+`settingsToMsgpack` converts a node's JSON settings. Settings use `rp::text<N>`, `rp::list<T, N>`, `rp::choice<...>`,
+numbers, bools, numeric `rp::resource_ref`s and enums with a fixed underlying type, no `operator|` and enumerators
+within -256 to 256 (the same limits hold for `enum_param`). A device-capable node source wraps `validate`,
+`adjust_layout` and the helpers only they use in one `#ifndef RP_DEVICE`; the desktop build compiles a
+`<target>-device` object library from every `device: true` target (the `RP_DEVICE` flags, nothing linked) so a missed
+guard, a pointer or non-portable `Settings`, or a `heap` node fails the desktop build. `rp.sdk.settings-json/1` is an
+SDK interface every `node.hpp` type answers, writing an instance's settings as JSON. The device image and the link
+trap are in [06-build-test.md](06-build-test.md).
+
 ### Kit reproducibility
 
 A kit compiles to the same bytes every time. The compiler is TypeScript (`packages/retroplug/src/kit/`): it builds
@@ -1275,7 +1305,10 @@ is one automatable parameter applied post-render across all 8 channels
 - [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp), [`OfflineJobs.hpp`](../packages/native/src/host/engine/OfflineJobs.hpp) / [`.cpp`](../packages/native/src/host/engine/OfflineJobs.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs, the registry and the offline job pool; `processBlock`, `setGraph`, `setParam`, `loadScriptPack`, `submitOfflineJob`, `drainOfflineJobs`, `cancelOfflineJob`, the command handler, `SyncTap`.
 - [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
 - [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes, with their settings and params), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host MIDI I/O nodes), [`N8Nodes.cpp`](../packages/native/src/host/graph/N8Nodes.cpp) (the cart's node), [`LaunchpadNodes.cpp`](../packages/native/src/host/graph/LaunchpadNodes.cpp) (the control surface's two), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
-- [`GraphHost.hpp`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/graph-runtime/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/node-sdk/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
+- [`GraphHost.hpp`](../packages/graph-runtime/native/src/graph/GraphHost.hpp) /
+  [`.cpp`](../packages/graph-runtime/native/src/graph/GraphHost.cpp),
+  [`rp_node.h`](../packages/node-sdk/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI
+  (`layout_for` per type).
 - [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
 - [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
 - [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime and its packs, + the compiler a pack's source and the tests' toys go through; [`src/script/`](../packages/retroplug/src/script) (`bundle.ts`, `register.ts`, `nodes/`): the bundle every Engine embeds, how it registers its nodes, and the script nodes themselves.
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index dbd4f9b1e..a2eb31f26 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -29,16 +29,24 @@ build produces the plugin (all formats) + the standalone.
 
 ## CMake targets
 
-All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt).
+Defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt) and, for the graph libraries, in the
+CMakeLists of [node-sdk](../packages/node-sdk/native/CMakeLists.txt),
+[graph-runtime](../packages/graph-runtime/native/CMakeLists.txt),
+[core](../packages/core/native/CMakeLists.txt) and [dsp](../packages/dsp/native/CMakeLists.txt) (registered by
+[cmake/packages.cmake](../cmake/packages.cmake)).
 
 | Target | Kind | Produces | In `ALL`? |
 |---|---|---|---|
 | `retroplug-core` | static lib | The shared emulator core: the system backends, the transport primitives, `SampleCache` (the offline seam's file decode) and the RPC-free utility the hosts need. `src/lsdj/` is test-reference code only now (the kit path's effect arithmetic, compiled into the two test binaries that hold the nodes to it). | yes (via consumers) |
+| `retroplug-node-sdk` / `retroplug-node-sdk-headers` / `retroplug-reflect-headers` | static lib / interface | The node SDK ([packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)): canonical JSON, descriptors, the graph document, the JSON to msgpack converter (the library); node-sdk's include root alone, which the executor and the launchpad and N8 tests take; reflect-cpp's headers alone, as `SYSTEM`, for the device-mode objects. | yes (via `retroplug-graph`) |
+| `retroplug-graph-exec` / `retroplug-graph` | static lib | The executor (no heap, no exceptions, builds for the RP2350) and, over it and the node SDK, the registry, planner, resources, `GraphHost` and the host nodes ([packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
+| `retroplug-core-nodes` | static lib | The `rp.core` node package ([packages/core](../packages/core/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
+| `retroplug-device-mode-objects` / `rp-device-check-cases` / `rp-device-check` | object lib / executable / firmware | The `RP_DEVICE` objects `retroplug-device-mode-test` links, built from every `<target>-device` check target; the desktop tool that writes the device check's cases (builds into `build/device-check/cases/`, not `build/bin`); and the RP2350 image `pnpm device:check` builds in its own CMake project (`build-device/`). The first two are `EXCLUDE_FROM_ALL`. | no |
 | `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the `rp.dsp` nodes run (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode); the C++ standard library alone. | yes (via the node packages; the two DSP test binaries link it directly) |
 | `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via the resample node package) |
 | `retroplug-r8brain-base` | static lib | r8brain's static members, one vendored translation unit (`R8bBase.cpp` includes `r8bbase.cpp`) compiled on a throwing allocator and linked into `retroplug-dsp-resample`. Kept off RetroPlug's `/W4 /WX` (vendored code keeps the compiler's default level) and given the deps' `-w` on GNU/Clang. | yes (via `retroplug-dsp-resample`) |
-| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in [cmake/graph.cmake](../packages/dsp/native/CMakeLists.txt), so `retroplug-graph` carries neither. | yes (via `retroplug-backend`) |
-| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/graph-runtime/native/CMakeLists.txt)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
+| `retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes` | static lib | The `rp.dsp` package (`rp.dsp.gain`, `.biquad`, `.dither`, `.normalize`, `.dpcm`) and the `rp.dsp.resample` package beside it, in the `dsp` package's [CMakeLists.txt](../packages/dsp/native/CMakeLists.txt), so `retroplug-graph` carries neither. `retroplug-dsp-nodes` and `retroplug-core-nodes` (the `rp.core` package, [packages/core](../packages/core/native/CMakeLists.txt)) each also get a `<target>-device` object library in `ALL` (the sources compiled with `RP_DEVICE`, nothing linked; on the `/W4 /WX` list) that fails the desktop build for an unguarded desktop hook. | yes (via `retroplug-backend`) |
+| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt), over the node SDK in [packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)) + the Engine and its graph nodes, the RPC services, the script host with its embedded script-node bundle, and the shared txiki host. Composed by every host. | yes (via consumers) |
 | `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
 | `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
 | `retroplug-sdl` | executable | `build/bin/retroplug-sdl` — the SDL2 standalone ([sdl-standalone.md](../docs/sdl-standalone.md)). Shipped in the CI artifact, deliberately **not** in releases. | yes |
@@ -49,7 +57,7 @@ All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.tx
 | `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
 | `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
 | `retroplug-render-host-test` | executable | A manual smoke of engine jobs on `rp::host::OfflineJobs` ([11-ui-rendering.md](11-ui-rendering.md)): `<job-json>` runs one render (and is `cmp`-identical to `retroplug-cli render`), `--parallel <j>...` runs N concurrently on one pool, `--cancel <j>` cancels mid-render. Not a CI step (`[engine-jobs]` carries that coverage). `EXCLUDE_FROM_ALL`. | no |
-| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` | executable | The ten Catch2 binaries `pnpm test:plugin` builds and runs. `EXCLUDE_FROM_ALL`. | no |
+| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` | executable | The eleven Catch2 binaries `pnpm test:plugin` builds and runs. `EXCLUDE_FROM_ALL`. | no |
 | `retroplug-n8-test` / `-n8-hwtest` | executable | Everdrive N8 protocol checks; `-hwtest` needs the physical console. `EXCLUDE_FROM_ALL`. | no |
 
 The plugin's identity is vendor-owned (not the DPF example namespace):
@@ -313,7 +321,7 @@ section 10) and these files hold what it writes.
 | **mock TS** | [`testing/mockBackend.ts`](../packages/retroplug/testing/mockBackend.ts) (in-memory, no native, no emulator) | `pnpm test` | [run-tests.mjs](../packages/retroplug/scripts/run-tests.mjs) on the `tjs` binary | `test/` |
 | **real host** | the real Backend RPC surface (fs/config/codec + a live `SameBoySystem` on the real engine graph, the protocol script nodes running in its script host) | `pnpm test:native` | [run-native-tests.mjs](../packages/retroplug/scripts/run-native-tests.mjs) on `retroplug-host` | `test-native/` |
 | **LVGL React UI** | the real UI bundle on a software LVGL display driven by `UiHarness` | `pnpm test:ui` | [run-ui-tests.mjs](../packages/retroplug/scripts/run-ui-tests.mjs) on `retroplug-ui-test` | `test-ui/` |
-| **C++ unit** | native classes directly, no TS in the loop | `pnpm test:plugin` | [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs) over ten Catch2 binaries | `packages/native/test/` |
+| **C++ unit** | native classes directly, no TS in the loop | `pnpm test:plugin` | [run-plugin-tests.mjs](../packages/retroplug/scripts/run-plugin-tests.mjs) over eleven Catch2 binaries | `packages/native/test/` |
 
 Each pnpm script first builds its runner via `cmake-build.js` (mock → `tjs-cli`; native →
 `retroplug-host`; UI → `retroplug-ui-test`), so a `pnpm test*`
@@ -362,9 +370,10 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest; C5's `test-native/capability.test.ts` and `test-native/stage-node-midi.test.ts`). |
 | `test:n8` | `retroplug-n8-test` + `retroplug-serial-test` | The Everdrive N8 protocol and serial checks, `[cart-jobs]` and `[cart-release]` among them since C5: the link's pause for a job, the SD card's battery file, an upload's stopped streaming, and the note-offs a leaving cart node's held notes get from the serial thread. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly, and two of its lazily built process tables (the NES game database, the GBA CPU's dispatch tables) taken from two threads at once (`[mesen-statics]`, whose ThreadSanitizer run means something only alone, as each is built once per process); the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | eleven Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`, `-device-mode-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`, the fixed-capacity settings types `[settings-types]`, the field walk `[fields]` and the enum scan `[enums]` among them), the node SDK built for the device (`retroplug-device-mode-test`, below), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly, and two of its lazily built process tables (the NES game database, the GBA CPU's dispatch tables) taken from two threads at once (`[mesen-statics]`, whose ThreadSanitizer run means something only alone, as each is built once per process); the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), and a node refusing a core built with another console's kind (a 12-way matrix over the four consoles, through a rig seam that reports another kind, the refusal giving the core back); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). E1's `retroplug-device-mode-test` ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md) section 8) compiles `rp.core` and `rp.dsp` with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an object library reached through the C ABI and holds ten cases: a device-mode node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, for every portable type over a table of settings, allocating nothing in create, process or destroy; the desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses what JSON refuses in the same words, naming the field; the JSON to msgpack converter; and four `[device-check]` cases that hold the image's cases to the manifests' device entries and run the whole check in the image's arena with no allocation. Its configure also runs three refused-source checks (a heap type, a pointer in `Settings` and a heap header must each fail in device mode with their own reason; the heap header is skipped under libc++). The engine-graph binary's `[packages]` holds each product's generated registration to a golden list. |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. In CI. |
+| `device:check` | `rp-device-check` | [tools/build-device-check.sh](../tools/build-device-check.sh): builds the cases generator in the desktop build, then the RP2350 image of every portable `rp.core` and `rp.dsp` node type (Pico SDK 2.1.1 via `PICO_SDK_PATH`, falling back to `/workspaces/pico-sdk` when unset; `arm-none-eabi-g++` 13; reflect-cpp's headers at the desktop's pin; `build-device/`) with a link trap on the heap and on exceptions (78 `--wrap`ped symbols), the trap probe (a program that does `new int(1)` and `std::malloc(16)`, which must FAIL to link naming both), and prints the sizes by owner. The image is built, never run. In CI (linux-x64). |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
 | `test:cli-ts` | `retroplug-cli` | Runs a directory of `.ts` tests through the CLI's own embedded stripper + harness — the consumer-kit path ([09-cli-debugging.md](09-cli-debugging.md)). In CI. |
 | `validate` | `-clap` + `-vst3` | `clap-validator` + `pluginval` against the built binaries via the shared [validate-plugins.sh](../tools/validate-plugins.sh). Fails if a validator is missing rather than skipping it. |
@@ -436,6 +445,23 @@ Pick the cheapest tier that actually exercises the code you touched. Trust-but-v
 | **DPF wrapper / plugin-format / state-restore behaviour** | `pnpm validate` | `clap-validator` + `pluginval` against the `.clap`/`.vst3`. Catches ABI / state-restore / threading regressions in the format adapters. |
 | **End-to-end inside a real DAW** | `pnpm reaper:mgb-smoke` | Instantiates `retroplug.vst3` in headless Reaper 7.x, plays mGB, renders audio. The first proof the plugin works in a host (not just the harness, which bypasses DPF). |
 
+### The device build
+
+`pnpm device:check` ([tools/build-device-check.sh](../tools/build-device-check.sh)) builds `rp-device-check`
+([packages/graph-runtime/device-check/](../packages/graph-runtime/device-check)), the RP2350 image that runs each
+portable `rp.core` and `rp.dsp` type once in `RP_DEVICE` mode from its default settings as msgpack, in a static 400
+KiB arena, reporting on the UART. It links with `--wrap` on 78 symbols (the malloc family, `sbrk`, `__cxa_guard_*`,
+every `operator new` / `delete` and `std::__throw_*` the device `libstdc++.a` defines, the throw entry points), so any
+heap use or throw in what it links is a link error naming the object; the Pico SDK's `pico_malloc` is the one part of
+the runtime left out (stdio over the UART links under the trap). The script also links a probe that allocates and
+requires it to fail naming `__wrap__Znwj` and `__wrap_malloc`, on every run, so the trap cannot silently stop reaching
+the link. It needs `arm-none-eabi-g++` and a Pico SDK (no submodules); the CI linux-x64 job installs
+`gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`, clones SDK 2.1.1 into `$RUNNER_TEMP` and
+runs the script. The image is not run: instance sizes on the device are unmeasured, and the first run of the check's
+successor on hardware is E3 (E2 runs the pack's Edio on the rig, but no graph node). Release sizes: core, dsp and the
+executor are about 42 KB of flash and no static RAM; the whole image is 65 KB flash and about 415 KB RAM (data and
+bss), almost all of it the arena.
+
 ### Sanitizer specifics
 
 [run-sanitizer.sh](../tools/run-sanitizer.sh) is **not** a pnpm script — run it
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index cc976f9a3..e2925662a 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -368,13 +368,14 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     the allocations left on the block thread, as C1/C4 list them above; C5 added none.
 - **D1 built: the kit baseline and the DSP library**
   ([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md), six commits): the kit path's DSP is
-  `src/dsp/` as block-streaming kernels (`retroplug-dsp-kernels`, plus `retroplug-dsp-resample` for r8brain), and
-  `lsdj/Effects`, `KitUtil` and `RisaDmcCodec` run those kernels. The graph has them as `rp.dsp.gain`, `.biquad`,
-  `.dither` and `rp.dsp.resample` (`retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes`, the resampler a package and
-  target of its own so a Pico build can leave it and r8brain out). TPDF dither is seeded from a hash of the sample it
-  sees, so a kit compiles to the same bytes, and `retroplug-audio-test`'s `[kit-baseline]` held 39 compiled banks to
-  a golden (the test-native runners hold them now; the next bullet). Strict parity held bit for bit at every commit,
-  and the baseline did not move by a hash through the kernel move. What it leaves:
+  `packages/dsp/native/src/dsp/` as block-streaming kernels (`retroplug-dsp-kernels`, plus `retroplug-dsp-resample`
+  for r8brain), and `lsdj/Effects`, `KitUtil` and `RisaDmcCodec` run those kernels. The graph has them as
+  `rp.dsp.gain`, `.biquad`, `.dither` and `rp.dsp.resample` (`retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes`,
+  the resampler a package and target of its own so a Pico build can leave it and r8brain out). TPDF dither is seeded
+  from a hash of the sample it sees, so a kit compiles to the same bytes, and `retroplug-audio-test`'s
+  `[kit-baseline]` held 39 compiled banks to a golden (the test-native runners hold them now; the next bullet). Strict
+  parity held bit for bit at every commit, and the baseline did not move by a hash through the kernel move. What it
+  leaves:
   - **D2**: audio buffers in and out of a document, a run-to-completion driver over `GraphHost`, normalise in a graph,
     the two-rate kit chain, and kit compiles as graphs behind `compileKit` / `compileDmc`, byte-identical to the
     baseline (as built, the TypeScript entry points of those names; the harness RPCs went with native's compiler, the
@@ -506,6 +507,41 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - **A GBA render case** is missing: without a BIOS the repo's GBA ROM renders exact silence.
   - **Cores print straight to the process.** Mesen's load log goes to fd 1 while the job's console lines arrive through
     the drain, so in the CLI the two can interleave differently than before.
+- **E1 built: a portable node core and the package boundary**
+  ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md), five commits): `node-sdk`,
+  `graph-runtime`, `core` and `dsp` are workspace packages with a manifest each, and every product's
+  `rp_register_packages()` is generated (the `[packages]` golden); portable nodes' settings are `rp::text`, `rp::list`,
+  `rp::choice` and numeric resource ids; `node.hpp` builds for the device (`RP_DEVICE`) and reads msgpack
+  (`RP_SETTINGS_MSGPACK`) with a reader of our own; `retroplug-device-mode-test` is the eleventh `test:plugin` binary;
+  and `pnpm device:check` links the RP2350 image of every portable `rp.core` and `rp.dsp` type under a trap on the heap
+  and on exceptions, a CI step. Nothing was re-recorded. What is left, in order:
+  - **E2: the N8 pack** (`packages/n8`: the nodes, one C++ Edio with an abstract transport, the link and the
+    lifecycle; `edio.ts`'s protocol and `pico/n8-host/edio.c` retire), gated on the desktop N8 tests, the real-hardware
+    leg and the Pico baseline.
+  - **E3: `exportPlan` for `rp2350`, the static-arena executor and the box as a graph host**, gated on the Pico baseline
+    and a C++ node in the box's plan. It must also settle `rp.core.delay`'s default, which needs 384 KB an instance
+    (74% of the RP2350's RAM) and sizes the check's arena: a device default or a planner cap. The device's stack in
+    `query_memory` and `create` (the `Settings` plus up to three 264-byte error buffers) is unmeasured, as are instance
+    sizes on ARM (the image is never run), and the float tolerance against the desktop is E3's decision.
+  - **E4: MicroQuickJS on the Pico**, a script host in the arena and `defineNode` made portable.
+  - **The rest of the package split** (the main design's section 11): `sameboy`, `mesen` (per console), `lsdj`, `mgb`,
+    `risa`, `smsggdj`, `bliptoaster` and `launchpad` as packages, the TS halves of `core` and `dsp`, per-product esbuild
+    bundles, downloadable packs.
+  - **Windows and MSVC have never built E1's targets.** The `<target>-device` object libraries are in the default build
+    under `/W4 /WX`, so a Windows problem breaks `build.bat` itself: a `build.bat` and `test.bat` run is owed. The risks
+    are the inline-namespace reopen for the device SDK, `rp::choice::make<>` and `rp::text`'s literal constructor as
+    consteval in default member initializers, `enums.hpp`'s template-lambda variable templates, `__FUNCSIG__` in a
+    consteval function, the copied `/bigobj`, and `std::strcpy` in `DeviceCheck.cpp`. The device objects keep MSVC's
+    `/EHsc`, so only the GCC and Clang builds prove the no-exceptions build.
+  - **libc++ runs two of the three refused-source checks** (the heap-header check reads libstdc++'s include guards and
+    is skipped on macOS); there the `static_assert`s and the link trap are the guards.
+  - **Enum limits.** An `rfl::config::enum_range<E>` override is not honoured and a custom `rfl::Reflector<E>` is not
+    seen by the device decode (`rfl::internal::has_read_reflector<E>` would refuse it in one line; no node has one);
+    each portable enum costs 55 to 95 ms of compile time a translation unit.
+  - **Hardening the product check.** `retroplug_check_products` looks at direct links to a fixed list of
+    carriers rather than computing the engine-carrying set outward from `retroplug-backend`; it runs at the end of
+    `packages/native`, not at the end of the root; its exempt names are not checked to exist; and the root's
+    `cmake_minimum_required` understates the 3.19 that `string(JSON)` and dpf.js need.
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
@@ -587,7 +623,8 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     the input node carries, or a merge of k runs, would bound it.
   - A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front:
     the block in flight is lost, and `droppedMessages` never counts it, since the carry and its count run
-    only while quiescent ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). No port changes
+    only while quiescent
+    ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). No port changes
     its capacity live today, and no document `buildGraph` builds holds a feedback wire: the Game Boy's `serialOut`,
     fixed, was the one source until LSDj's decoder moved after it.
   - `retroplug-engine-graph-test` replaces the global `operator new` with a counting allocator
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index eff12e2cd..76fe9c937 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -27,11 +27,12 @@ import/export paths are written once:
 - **`TRACKER_INTEGRATIONS`** — the registry that maps a system to its catalogs.
 - **`liveSav`** — reads the battery out of the *running* core rather than off disk, which is why
   the Songs menu acts on what the cart currently has.
-- **Kit compiles** are TypeScript ([`src/kit/`](../packages/retroplug/src/kit)): each slot is a chain of offline
-  graph jobs whose passes are `rp.dsp` documents (the effects and resample kernels in `src/dsp/`, which those nodes
-  wrap), and TypeScript packs the codes and bits and assembles the bank. TPDF dither seeds from a hash of its input, so
-  a kit compiles to the same bytes every time, and `test-native`'s `kit-baseline` and `kit-compile` hold the compiled
-  banks to their goldens (D1 and D2, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md),
+- **Kit compiles** are TypeScript ([`src/kit/`](../packages/retroplug/src/kit)): each slot is a chain of offline graph
+  jobs whose passes are `rp.dsp` documents (the effects and resample kernels in `packages/dsp/native/src/dsp/`, which
+  those nodes wrap), and TypeScript packs the codes and bits and assembles the bank. TPDF dither seeds from a hash of
+  its input, so a kit compiles to the same bytes every time, and `test-native`'s `kit-baseline` and `kit-compile` hold
+  the compiled banks to their goldens (D1 and D2,
+  [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md),
   [modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) section 11). Every host compiles a kit,
   the plugin included; the native compiler and its harness-facet RPCs are gone.
 
@@ -199,7 +200,8 @@ its serial-out decoder, `rp.lsdj.serial-out`, placed after the systems
 ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). What is left is
 [07-remaining-work.md](07-remaining-work.md) section 5.
 
-- Code: `packages/native/src/graph/` (C++), `packages/native/src/host/graph/` (the engine's nodes),
+- Code: `packages/node-sdk/`, `packages/graph-runtime/`, `packages/core/` and `packages/dsp/` (C++: the node SDK, the
+  executor and planner, the `rp.core` and `rp.dsp` nodes), `packages/native/src/host/graph/` (the engine's nodes),
   `packages/native/src/host/script/` (the script host), `packages/retroplug/src/graph/` (TS: the node and
   extension contracts, `buildGraph`, `DirectGraph`, the zod-free settings schema), `packages/retroplug/src/script/`
   (the script-node bundle and its nodes) and `packages/retroplug/src/engineGraph.ts`.
diff --git a/spec/README.md b/spec/README.md
index 974b9da50..f24be628b 100644
--- a/spec/README.md
+++ b/spec/README.md
@@ -1,8 +1,9 @@
 # RetroPlug — architecture spec
 
 This directory is the single source of truth for the RetroPlug architecture — the
-implementation in [`packages/native/`](../packages/native) (C++ host)
-and [`packages/retroplug/`](../packages/retroplug) (TypeScript + React/LVGL UI).
+implementation in [`packages/native/`](../packages/native) (C++ host), the graph libraries in the workspace packages
+[`node-sdk`](../packages/node-sdk), [`graph-runtime`](../packages/graph-runtime), [`core`](../packages/core) and
+[`dsp`](../packages/dsp), and [`packages/retroplug/`](../packages/retroplug) (TypeScript + React/LVGL UI).
 
 The guiding principle is one sentence:
 
````

Run: `sed -n '14167,14598p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

- [ ] **Step 4: AGENTS.md**

The layout line names the packages; the test-binary paragraph counts eleven `test:plugin` binaries and describes
`retroplug-device-mode-test`'s cases; a new bullet gives a portable node's rules (the one guard, the settings types,
no heap or threads type on the device, names under `rp::graph`, `rp::detail` or an anonymous namespace); and the
verification section names `pnpm device:check`. One emdash on a reflowed line of the spec bullet becomes " - ".

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 17591f56e..1db690c31 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -3,9 +3,10 @@
 Start with these — all short:
 
 - [README.md](README.md) — what RetroPlug is, how to build, project layout.
-- [spec/](spec/README.md) — the architecture (one doc per concern). The build lives
-  in `packages/native/` (C++ host: `src/` core + `src/host/` layer + `plugin/`) +
-  `packages/retroplug/` (TS/UI). The older **legacy** build has been removed;
+- [spec/](spec/README.md) - the architecture (one doc per concern). The build lives in `packages/native/` (C++ host:
+  `src/` core + `src/host/` layer + `plugin/`) + `packages/retroplug/` (TS/UI), with the graph libraries in the
+  workspace packages `packages/node-sdk/`, `packages/graph-runtime/`, `packages/core/` and `packages/dsp/` (formerly
+  `native/src/graph` and `native/src/dsp`). The older **legacy** build has been removed;
   [spec/07-remaining-work.md](spec/07-remaining-work.md) tracks the residual cleanup + feature gaps.
 - [docs/lsdj.md](docs/lsdj.md) — the LSDj domain reference + how to test LSDj sync /
   DAW timing / audio quality headlessly.
@@ -34,9 +35,9 @@ The rules below are the parts that don't fit those.
   linked into `retroplug-backend`; the watcher is opt-in via `enableWatching`, which
   only the plugin calls). `deps/catch2` is the C++ unit-test framework:
   it's `add_subdirectory`'d at the root (`EXCLUDE_FROM_ALL`) and linked by the
-  ten `test:plugin` binaries (`retroplug-plugin-test` / `-classid-test` / `-audio-test` /
+  eleven `test:plugin` binaries (`retroplug-plugin-test` / `-classid-test` / `-audio-test` /
   `-watcher-test` / `-dynparams-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` /
-  `-graph-test` / `-engine-graph-test`) as `Catch2::Catch2WithMain`. `retroplug-engine-graph-test`
+  `-graph-test` / `-engine-graph-test` / `-device-mode-test`) as `Catch2::Catch2WithMain`. `retroplug-engine-graph-test`
   holds the engine's graph to the cores it wraps: each system node renders what its core renders
   driven directly, bit for bit, its inputs, settings and params included (C2,
   [docs/modular-graph-phase-c2-design.md](docs/modular-graph-phase-c2-design.md): every setting reaches its core at
@@ -126,6 +127,26 @@ The rules below are the parts that don't fit those.
   `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`)
   plus its OWN `DistrhoPluginInfo.h`, building no plugin format at all, to drive
   `PluginExporter::reinitParameters` over a toy plugin.
+  `retroplug-device-mode-test` holds the node SDK built for the RP2350 to the desktop's: `rp.core` and `rp.dsp`
+  compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an object library reached through the C ABI (Catch2
+  itself keeps exceptions), ten cases. A device-mode node created from msgpack settings renders bit for bit what a
+  desktop-mode node renders from the same settings as JSON, with create, process and destroy allocating nothing; the
+  desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses what JSON
+  refuses in the same words, naming the field; the converter writes each number as the JSON spells it; and four
+  `[device-check]` cases hold the image's cases to the manifests' device entries and run the whole check in its arena
+  with no allocation. Its configure runs three refused-source checks (a heap type, a pointer in `Settings` and a heap
+  header must each fail in device mode with their own reason; libc++ skips the header one).
+- **The graph libraries are workspace packages, and portable nodes have rules.** `packages/<name>/package.json`'s
+  `retroplug` field is the manifest [cmake/packages.cmake](cmake/packages.cmake) reads; each product lists its
+  packages (`retroplug_product`) and gets a generated `rp_register_packages()` (the `[packages]` golden holds the type
+  ids; a configure check fails a binary that links the engine and is no product). A node meant for the Pico (a package
+  entry with `device: true`) includes `graph/node.hpp` and keeps its settings to `rp::text<N>`, `rp::list<T, N>`,
+  `rp::choice` (not `rfl::Literal`), numbers, bools, `rp::resource_ref` and enums (a fixed underlying type, no
+  `operator|`, enumerators within -256..256; `enum_param` too), and wraps `validate`, `adjust_layout` and the helpers
+  only they use in ONE `#ifndef RP_DEVICE`. The desktop build compiles a `<target>-device` object library for every
+  such target, so a missed guard or a non-portable `Settings` fails the ordinary build. A custom `rfl::Reflector<E>`
+  is not seen by the device decode. Details:
+  [docs/modular-graph-phase-e1-design.md](docs/modular-graph-phase-e1-design.md).
 - **`deps/sameboy` is patched at configure — a dirty working tree there is
   EXPECTED, not stray changes.** The per-channel (4-stem) Game Boy audio tap lives
   in `Core/apu.{c,h}` and ships as a tracked patch
@@ -343,7 +364,10 @@ The headless loop (the only path — legacy is gone) is documented in
 `test:native` (real host + cores), `test:ui` (LVGL React), `test:plugin` (Catch2
 C++ unit checks — the per-context window-hook routing, the class-id counter sync
 that keeps the DAW-hosted editor from rendering blank, and the DPF dynamic-parameter
-diff classifier), `screenshot`,
+diff classifier), `device:check` (`tools/build-device-check.sh` - builds `rp-device-check`, the RP2350 image of every
+portable `rp.core` / `rp.dsp` type, with `arm-none-eabi-g++` and the Pico SDK 2.1.1 (`PICO_SDK_PATH`), under a link
+trap on the heap and on exceptions (78 wrapped symbols) and a probe that must fail to link; built and never run, in
+CI on linux-x64; needs no hardware), `screenshot`,
 `reaper:editor` (`tools/run-reaper-editor.sh` — floats the hosted plugin editor in
 headless Reaper and asserts its LVGL snapshot rendered; the only check of on-screen
 editor rendering, not in CI), `reaper:editor-reopen` (`tools/run-reaper-editor-reopen.sh`
````

Run: `sed -n '14615,14683p' docs/modular-graph-phase-e1-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks. The stage's own run: `pnpm check:docs`: `check-doc-links: 78 file(s) clean`.

Run: `git diff --check`
Expected: no output: no trailing whitespace and no conflict markers.

Run: `git diff | grep "^+" | grep -c $'\u2014'`
Expected: `0`, and an exit status of 1, which is `grep -c`'s answer when nothing matches, not a failure: no added line carries an emdash (the report: `Emdash on added lines: 0`).

- [ ] **Step 5: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M AGENTS.md
 m deps/sameboy
 M docs/modular-graph-design.md
 M docs/modular-graph-phase-b3a-design.md
 M docs/modular-graph-phase-c1-design.md
 M docs/modular-graph-phase-c3-design.md
 M docs/modular-graph-phase-d1-design.md
 M docs/modular-graph-phase-d2-design.md
 M docs/modular-graph-phase-e1-design.md
 M spec/00-overview.md
 M spec/01-architecture.md
 M spec/02-native-host.md
 M spec/06-build-test.md
 M spec/07-remaining-work.md
 M spec/13-subsystems.md
 M spec/README.md
````

````bash
git add -- AGENTS.md docs/modular-graph-design.md docs/modular-graph-phase-b3a-design.md \
  docs/modular-graph-phase-c1-design.md docs/modular-graph-phase-c3-design.md docs/modular-graph-phase-d1-design.md \
  docs/modular-graph-phase-d2-design.md docs/modular-graph-phase-e1-design.md spec/00-overview.md \
  spec/01-architecture.md spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md \
  spec/13-subsystems.md spec/README.md
git commit -F - <<'EOF'
docs: E1 builds a portable node core and the package boundary

The E1 design gains its as-built section (the five commits' decisions, the
section 11 prototype questions answered with the image sizes, what the build
found) and is corrected in place where the build made it false: SpscRing lives
in node-sdk, a device-capable node wraps its desktop hooks in one
#ifndef RP_DEVICE, rp::choice replaces rfl::Literal, and enums work on the
device with limits.

The main design's Phase E outline becomes E1 to E4 with the decisions behind
it, its section 5.6 and 5.8 and the package split describe what E1 built, and
the specs and AGENTS.md carry the packages, generated registration, device mode,
the eleventh test:plugin binary, the device check and its CI step. spec/07 lists
E2 to E4 and the residuals (Windows and libc++, the delay's RAM, the package
check's hardening). Prose that named src/graph or src/dsp now names the
packages.
EOF
````

---

### Task 6: the acceptance run

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. **There is no hardware leg**: E1 changes nothing on the Pico and nothing is flashed; the
device image is built and its link trap proved, never run. The counts are the final verification on stage 4's tree,
whose code is identical to the final tip `d4d960b4e` (stage 5 changed docs only): its logs are `/tmp/e1pro/s4f-*.log`,
`s4f-summary.txt` lists its 30 legs, every one exit 0, and `s4f-devcheck.log` and `s4f-ci-dry.log` are the device
check and its CI dry run. A line no log records says so and asks for the count instead. No stage ran the Reaper
suite, so it runs here, as D3's acceptance ran it; the Windows, macOS and arm64 runs that are owed are stated in Step 9 and cannot run in
this container.

- [ ] **Step 1: The tree and the build**

Run: `git status --short`
Expected: only ` m deps/sameboy`.

Run: `git -C deps/sameboy status --short`
Expected: ` M Core/apu.c`, ` M Core/apu.h`, ` M Core/camera.c`, ` M Core/random.c` and ` M Core/sgb.c`: the two tracked patches, and nothing else.

Run: `cmake --build build -j$(nproc)`
Expected: a clean build of every target, the two `-device` object libraries among them (Task 5 changed Markdown only). Which `Built target` line comes last depends on the parallel schedule, so do not pin it.

Run: `git log --oneline -6`
Expected: six lines: Task 5's `docs: E1 builds a portable node core and the package boundary`, Task 4's `build(device): the RP2350 device check, with a link trap on the heap and on exceptions`, Task 3's `feat(node-sdk): a device build of node.hpp, and settings as msgpack`, Task 2's `feat(node-sdk): fixed-capacity settings types, and the core and DSP nodes use them`, Task 1's `refactor(graph): the graph libraries and the core and DSP nodes become workspace packages`, then `docs: the E1 implementation plan`. The design commit a27f53430 is the seventh.

- [ ] **Step 2: The suites, with and without fixtures**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 174 test file(s) passed (jobs=16)` (the baseline's count: the descriptor conformance and `buildGraph`'s goldens inside it unchanged).

Run: `pnpm test:plugin`
Expected: all 11 binaries exit 0, `# 11 plugin test binary(ies) passed (jobs=16)`; `retroplug-engine-graph-test` `test cases: 313 | 302 passed | 11 skipped`; graph-test `All tests passed (356435 assertions in 282 test cases)`; `retroplug-device-mode-test` `All tests passed (2317 assertions in 10 test cases)`; audio-test `All tests passed (1234835 assertions in 108 test cases)`; launchpad `All tests passed (144 assertions in 29 test cases)`; the small six at their own counts (midi 35 / 19, dynparams 24 / 7, plugin 21 / 6, lottie 7 / 1, classid 10 / 3, watcher 11 / 3). Report all eleven lines.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1713095 assertions in 313 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the baseline's 311 cases and `[packages]`'s two, the fixture-gated ones running.

Run: `pnpm test:n8`
Expected: `All tests passed (529 assertions in 57 test cases)` and `All tests passed (7 assertions in 1 test case)` for the serial binary: the baseline's.

Run: `RP_PARITY_REQUIRE_TARGET=1 RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, every case run, nothing re-recorded. `RP_PARITY_REQUIRE_TARGET=1` is what CI's linux-x64 job sets: on a host other than the recording one a case would fail rather than fall back to agreeing with itself. The tip's own run was without the flag on this host, the recording one; any case it fails is a finding. `git status` shows no parity baseline changed.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native parity`
Expected: 30 files pass with the skip check passing: CI's view. No E1 log records this run alone (its skips are inside the whole CI view below); report its `SKIPPED` line.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 167 native test file(s) passed (jobs=16)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: the same 167 files, with `# 144 case(s) SKIPPED in 65 file(s)` and the skip check clean: the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 41 UI test file(s) passed (jobs=16)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:ui`
Expected: the same 41 files (a skip does not remove a file); report which cases skipped without the fixtures, which no log records.

Run: `pnpm test:node`
Expected: `tests 13`, `pass 13`, `fail 0`: the Node addon, a product, registers its packages as before.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 79 file(s) clean`: the reports record 78, and this plan is the 79th tracked Markdown file `check-doc-links` walks.

- [ ] **Step 3: The goldens, and nothing re-recorded**

Run: `build/bin/retroplug-engine-graph-test "[packages]"`
Expected: `All tests passed (464 assertions in 2 test cases)`: every product registers the 26 ids the old loop did (Task 1's run; no product or package changed since).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 39 banks match the golden's hashes`, then `# 3 passed, 0 skipped, 0 failed`: the dither's `Mt19937` draws what `std::mt19937` drew.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, `# 4 lsdj renders match the golden's hashes`, `# 4 risa renders match the golden's hashes`, then `# 5 passed, 0 skipped, 0 failed`.

Run: `RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native render-baseline`
Expected: `# 5 fixture-free renders match the golden's hashes`, then `# 2 passed, 3 skipped, 0 failed`, the skip check passing: CI's share (the whole CI view's lines for this file).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed.

Run: `bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'`
Expected: exit 0, `pnpm test render` ending `# 7 test file(s) passed (jobs=16)`.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0.

Run: `git status --short`
Expected: only ` m deps/sameboy`: no parity baseline, kit golden, render golden, descriptor golden, `buildGraph` golden, registration golden or skip baseline moved during the run.

- [ ] **Step 4: The sanitizers**

Build each binary in its own tree first. The tag lists are the verification script's.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc)`
Expected: exit 0: the ThreadSanitizer tree's two binaries, built from this task's tree (a stale binary proves nothing).

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'` (`[n8]` is not an engine-graph tag; the list is the verification script's), then `All tests passed (1698163 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), and no ThreadSanitizer report.

Run: `TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-audio-test -j$(nproc)`
Expected: exit 0: the AddressSanitizer tree's three binaries.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8],[launchpad],[offline],[engine-jobs]"`
Expected: `No test cases matched '[n8]'`, then `All tests passed (1680370 assertions in 133 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's), no AddressSanitizer or LeakSanitizer report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356435 assertions in 282 test cases)`, no report.

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"`
Expected: `All tests passed (6 assertions in 3 test cases)`, no report.

Run: `cmake --build build-asan --target retroplug-device-mode-test -j$(nproc)`
Expected: exit 0. From this task on the ASan tree builds its own `rp-device-check-cases` with ASan, runs it to write the header (LeakSanitizer on, so a leak there would fail the build) and then the binary: the run on the prototype's tip, whose code is this tree's, logged `Built target rp-device-check-cases` and `Built target retroplug-device-mode-test` with no leak or error line (`/tmp/e1plan/asan-tip-build.log`).

Run: `ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test`
Expected: `All tests passed (2317 assertions in 10 test cases)`, no AddressSanitizer or LeakSanitizer report: the device check's run over the device-mode objects among them (the prototype tip's run, `/tmp/e1plan/asan-tip-run.log`).

- [ ] **Step 5: The device build**

Run: `rm -rf build-device`
Expected: nothing: the device check's build dir is gone, so the run below configures it afresh.

Run: `pnpm device:check`
Expected: exit 0, `-- rp-device-check: the link trap wraps 78 symbols`, the probe failing as Task 4 Step 6 quotes (`trap-probe.cpp.o: in function 'main'`, `__wrap__Znwj` at line 11 and `__wrap_malloc` at line 12), and the table by owner ending `total 65156 1844 412742` (its first row `core: the rp.core nodes 22002 0 0`, `dsp`'s nodes 14027 and kernels 4424, the executor 1404). Report the table.

Run: `RP_DEVICE_BUILD_TYPE=MinSizeRel RP_DEVICE_BUILD_DIR=/tmp/e1-device-os pnpm device:check`
Expected: exit 0, the table ending `total 46167 2236 413058`. Then `rm -rf /tmp/e1-device-os`.

Run: `bash -c 'rm -rf /tmp/e1-ci && mkdir -p /tmp/e1-ci && git clone -q --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk.git /tmp/e1-ci/pico-sdk'`
Expected: exit 0.

Run: `env -i HOME=$HOME PATH=/usr/bin:/bin PICO_SDK_PATH=/tmp/e1-ci/pico-sdk RP_DEVICE_BUILD_DIR=/tmp/e1-ci/build-device bash tools/build-device-check.sh`
Expected: exit 0: CI's step in a clean environment, `-- rp-device-check: the link trap wraps 78 symbols`, the probe proved and the table ending `total 65156 1844 412742`. Then `rm -rf /tmp/e1-ci`.

- [ ] **Step 6: A fresh tree**

A configure of a scratch build dir, so no stale cache in `build/` hides a path that moved or a target that is missing.
It configures the whole tree, the refused-source checks among it, and builds the binaries below only.

Run: `cmake -S . -B /tmp/e1-fresh`
Expected: exit 0 (the stage's fresh configure took about 18 s).

Run: `cmake --build /tmp/e1-fresh --target retroplug-graph-test retroplug-engine-graph-test retroplug-device-mode-test retroplug-launchpad-test retroplug-n8-test -j$(nproc)`
Expected: exit 0.

Run: `bash -c 'for t in retroplug-graph-test retroplug-device-mode-test retroplug-launchpad-test retroplug-n8-test; do /tmp/e1-fresh/bin/$t | tail -2; done; /tmp/e1-fresh/bin/retroplug-engine-graph-test "[packages]" | tail -2'`
Expected: graph-test 356435 / 282, device-mode 2317 / 10, launchpad 144 / 29, n8 529 / 57 (this tree's counts), and `[packages]`'s two cases passing over the eight products a default configure declares (Task 1's fresh tree logged `[packages]` 417 / 2; no product changed since). Then `rm -rf /tmp/e1-fresh`.

- [ ] **Step 7: The live hosts**

One at a time, with nothing rebuilding the plugin meanwhile. Every product's registration is generated now and the
plugin's is attached to the binaries DPF built, so the plugin hosts are where a missing registration would show: a
Linux module without one loads and fails. No stage ran the Reaper suite: report its line; a failure here is a real
finding about a plugin host rather than a missing figure. Make sure no Reaper or jackd of an earlier run is left
first.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after its eight `ok:` lines (ok: boot + non-blank render, ..., ok: idle reclaim).

Run: `node tools/author-rplg.js && node tools/author-nes-rplg.js && node tools/author-risa-rplg.js /workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes && node tools/author-sms-rplg.js && node tools/author-sms-rplg.js resources/roms/smsggdj_v0_45.gg && node tools/author-lsdj-rplg.js midi-metro /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js midi-drift /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js arduinoboy-metro /workspaces/resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb`
Expected: an `authored build/<name>.rplg.zip` line for each (eight). The suite's own fixture step reads `../resources/...`, which does not resolve from this worktree (D3's residual in [07-remaining-work.md](../spec/07-remaining-work.md)), so author them by hand with explicit ROM paths and run the suite with `RP_SUITE_NO_BUILD=1`, which skips its own build and every fixture, after the build above.

Run: `pgrep -a reaper; pgrep -a jackd; RP_SUITE_NO_BUILD=1 pnpm reaper:all`
Expected: no leftover process listed, then `ALL 15 REAPER CHECKS PASSED` (the suite prints the inconclusive form instead when a check skips). Report the line. If a render fails, run that one check alone once (its own `pnpm reaper:<name>`) and report both results: concurrency in this container has flaked before.

Run: `pnpm validate`
Expected: exit 0, its last line `summary: clap=0 vst3=0`: clap-validator and pluginval (strictness 5) load the plugin's CLAP and VST3 binaries, each of which now carries the product's generated registration object, as CI's linux-x64 job validates them. No E1 stage ran it; report its summary line.

- [ ] **Step 8: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched. Never edit `/workspaces/bliptoaster` or `/workspaces/nesvj`: both commands copy the suite to `/tmp`, run it
there and remove the copy, and both repositories' `git status --short` must stay empty.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-bliptoaster && mkdir -p /tmp/e1-bliptoaster/build /tmp/e1-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/e1-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/e1-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/e1-bliptoaster/retroplug-cli/ && cd /tmp/e1-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/e1-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`, its last file 49 passed with none skipped or failed. The CLI is a product whose registration is generated; a consumer sees no change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/e1-nesvj && mkdir -p /tmp/e1-nesvj/build /tmp/e1-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/e1-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/e1-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/e1-nesvj/retroplug-cli/ && cd /tmp/e1-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/e1-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`, its last file 23 passed with none skipped or failed. The CLI is a product whose registration is generated; a consumer sees no change.

Run: `git -C /workspaces/bliptoaster status --short && git -C /workspaces/nesvj status --short`
Expected: nothing printed: neither repository was edited.

- [ ] **Step 9: Not runnable here: the Windows, macOS and arm64 runs that are owed**

E1's device-mode targets have never met MSVC, and the `-device` object libraries are in the default build under `/W4
/WX`, so a Windows problem breaks `build.bat`'s plain build. On a Windows machine with this tree (the user's Windows
agent), run `build.bat` and then `test.bat` (all four suites), and report both: the risks are the inline-namespace
reopen (`sdk_namespace.hpp`), `rp::choice::make<>` and `rp::text`'s literal constructor as consteval in default member
initialisers, the template-lambda variable templates in `enums.hpp`, `__FUNCSIG__` in a consteval function (its MSVC
branch), the copied `/bigobj`, and `std::strcpy` in `DeviceCheck.cpp`. The refused-source checks never run on MSVC.
macOS (AppleClang with libc++) has not built E1 either: CI's macOS job builds `all`, so it compiles the two `-device`
object libraries and the device-mode objects with Apple clang and libc++, takes the Clang branches of `enums.hpp` and
`fields.hpp`, and runs the refused-source checks, of which under libc++ two are meant to run and `HeapHeader` is
skipped; no libc++ build has run them, and the prototype's clang was Linux clang 18 with libstdc++. CI's arm64 Linux
job (the same GCC 13) has not built E1 either. Their first run is CI's, on the user's push, so the acceptance records
all three (the design records the Windows run as owed too) as owed rather than passed: report them as such.

- [ ] **Step 10: The final tree**

Run: `git status --short`
Expected: only ` m deps/sameboy`; no scratch file and no golden or baseline changed. (`build-device/` is ignored.)

---

