# Phase E1: a portable node core and the package boundary

**Date:** 2026-11-03 · **Branch:** `feature/modularize` · **Status:** designed, not built

Phase E is the Pico runtime: the C++ nodes, the TypeScript nodes and the device packs RetroPlug builds run unchanged in
the desktop products and on the RP2350, and the Pico box becomes a graph host that keeps everything it does today. E1 is
its first step. It makes a C++ node's source build for the device as it builds for the desktop, with no heap and no
exceptions there, and it draws the package boundary that the N8 pack (E2) and any third party's pack will sit behind.
Phase E's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)); the package
split it now begins is the main design's section 11.

## 1. Before E1

- **The executor is portable; the node authoring layer is not.** `retroplug-graph-exec` (`src/graph/Executor.cpp`) has
  no heap, no exceptions and no dependency beyond libc. A C++ node is written as a plain struct and turned into the C
  ABI table by `src/graph/node.hpp`, whose block path is clean, but whose control side is desktop code: an instance
  heap-allocates its `Settings` (`new S`), keeps its layout as a `std::string` and its resource pins in a
  `std::vector`, every control entry is wrapped in `try` / `catch`, and settings are parsed from JSON by reflect-cpp
  (`rfl::json`, over yyjson).
- **Settings use heap types.** The core nodes' settings hold `std::string` (`layout = "stereo"`, `downmix`'s `from` and
  `to`) and `std::vector<std::uint32_t>` (`join`'s inputs, `split`'s outputs); `resource_ref` keeps the document's
  `"res:N"` as a `std::string`; `validate` answers `std::optional<std::string>`.
- **The ABI has one settings encoding.** `rp_settings.encoding` exists, and `RP_SETTINGS_JSON` is its only value.
- **Packages are a list in the engine.** `rp_package` and `NodeRegistry::addPackage` exist, but which packages a
  product has is a hard-coded loop in `Engine.cpp` (and a fixed subset in `OfflineJobs.cpp`): every product carries
  every node type. The workspace is split by language (`packages/native`, `packages/retroplug`), not by feature.
- **The planner already knows the Pico.** `Target::Pico` refuses a node type that declares `heap` or `threads`
  (`Planner.cpp`); `rp.dsp.resample` and `rp.dsp.dpcm` declare `heap`, every other core and DSP type plans there.
- **The Pico firmware is hand-written C** (`pico/n8-host`): its own Edio (`edio.c`, a third copy of the protocol beside
  `edio.ts` and `Edio.cpp`), UART MIDI in, the box's lifecycle. Its behaviour, verified on the rig on 2026-11-03, is
  the baseline Phase E must keep (section 3).

## 2. Decisions taken

| Question | Decision | Consequence |
|---|---|---|
| C or C++ on the device? | **C++**, the same sources | A node written once runs in a compiled app and on a microcontroller; no C rewrites |
| How is Phase E sequenced? | **E1 -> E2 -> E3 -> E4** (section 3) | The boundary and the portable core first, then N8 carved on them |
| Where does N8 support live? | **A pack of its own** (E2) | Its nodes, Edio, link and lifecycle leave the core; a SNES flash-cart pack follows the same template |
| How many Edio implementations? | **One, in C++**, in the N8 pack, wrapped for TS (E2) | `edio.ts`'s protocol and `edio.c` retire; the byte decoders may stay TS |
| TS nodes on the Pico? | **Yes, on MicroQuickJS** (E4) | `defineNode` and the nodes the box runs move to its subset |
| How do settings reach a node on the device? | **msgpack in the plan blob, decoded at plan load** | `RP_SETTINGS_MSGPACK`; lenient by field name, as JSON is; no change for node authors (section 6) |
| Strings and lists in a portable node's settings? | **Fixed-capacity types**, `rp::text<N>` and `rp::list<T, N>` | The same JSON shape; no allocation on the device ever; the arena is sized exactly (section 5) |
| What is a package? | **A workspace package per feature**, `packages/<name>/{native,ts}` with a manifest | Each product lists its packages and gets a generated `rp_register_packages()` (section 4) |
| Where is the SDK boundary? | **`node-sdk`, `graph-runtime`, then node packages** | A pack depends on `node-sdk` alone (section 4) |
| How is portability proved? | **A CI cross-compile with a link trap**, and device mode tested on the desktop | Heap use or a throw in portable code is a link error; on-device runs start at E2 and E3 (section 7) |

## 3. Phase E

| Sub-phase | Builds | Accepted by |
|---|---|---|
| **E1** (this design) | the packages and generated registration; fixed-capacity settings; `node.hpp` built two ways, `RP_SETTINGS_MSGPACK`; `rp-device-check` and its link trap | every desktop suite bit-identical, nothing re-recorded; the device image links |
| **E2** | `packages/n8`: the nodes, one C++ Edio (no exceptions, caller-supplied buffers, an abstract transport: the desktop serial port, TinyUSB on the Pico, later Web Serial), the link and the lifecycle (probe, menu boot, recovery); TS reaches Edio through an RPC facet; `edio.ts`'s protocol and `pico/n8-host/edio.c` retire and the firmware builds against the pack | the desktop N8 tests, the real-hardware leg, the Pico baseline |
| **E3** | `exportPlan` for `rp2350` (the blob, msgpack settings), the static-arena executor, a desktop run with the heap forbidden; the box becomes a graph host (`rp.host.midi-in` from the UART, the pack's `rp.n8.nes`) whose built-in default plan is today's bridge | the Pico baseline, and a C++ node in the box's plan |
| **E4** | MicroQuickJS on the Pico: a script host in the arena, `defineNode` made portable | a TS node in the box's MIDI path on the rig |

**The Pico baseline is a gate at E2, E3 and E4.** It is recorded in the worktree's gitignored
`.superpowers/e-pico/baseline.md` with its logs: `pico/n8-host/loop.sh` boots a ROM through the N8 menu with no PC;
`stress.sh` with BlipToaster loses nothing (bursts, a chord, running status and realtime bytes, SysEx pass-through, a
30 s soak, a 40,000-message stream, a power cycle under a live stream); a 2A03 A4 and C5 through the whole chain measure
on pitch on the L6; `faults.sh` recovers from random power cuts (7 of 8 on 2026-11-03, the eighth the known Zigbee
late-off quirk). What the box does that the gate covers: Edio over the PIO-USB host (status, device info, the menu's
`*t`, `*n`, `*s`, file put and get, writes to the cart FIFO, the expansion-volume register, the sniffer), MIDI in on
the UART interrupt with batched forwarding, SysEx pass-through, the status LED, the watchdog and recovery with backoff,
the ROM pick over SysEx `7D 50` saved on the SD card, and ROM staging in flash.

E1 changes nothing on the Pico, so it has no hardware leg.

## 4. The packages

A package is `packages/<name>/` with a `native/` part, a `ts/` part or both, and a `package.json` whose `retroplug`
field is the manifest: the native CMake target, its entry function, the TS entry, and whether its node types build for
the device. CMake reads the field with `string(JSON)`, so the manifest is the one source.

```json
{
  "name": "@retroplug/dsp",
  "private": true,
  "retroplug": { "native": { "target": "rp-dsp", "entry": "rp_dsp_package", "device": true } }
}
```

The entry is a unique function per package returning its `rp_package`. `rp_package_entry` stays reserved for a
downloaded pack's dynamic library, so many statically linked packages never collide on it.

**E1's packages:**

| Package | Holds | Depends on |
|---|---|---|
| `node-sdk` | `rp_node.h`, `node.hpp`, the description layer's support library (descriptor writer, canonical JSON, the graph document types `node.hpp` names), the msgpack settings decode, `rp::text` and `rp::list` | reflect-cpp (desktop) |
| `graph-runtime` | the executor (`retroplug-graph-exec`, the part a device needs), the planner, the registry, the resource table, `GraphHost`, `SpscRing`, and the `rp.host` nodes the runtime implements itself (`rp.host.audio-out`) | `node-sdk` |
| `core` | the `rp.core` nodes | `node-sdk` |
| `dsp` | the kernels (`src/dsp/`, with `util/Hash.hpp`) and the `rp.dsp` nodes, `resample` among them | `node-sdk`, r8brain |

A pack, ours in E2 or a third party's, depends on `node-sdk` and nothing else. `packages/native` keeps the engine, the
systems, the hosts and every node type not yet carved. The files move with `git mv`, and every include path stays as it
is (`#include "graph/node.hpp"` resolves from the package's include directory), so no node source changes for the move.

**Products.** Each product declares its packages, `retroplug_product(<target> PACKAGES core dsp ...)`, and CMake
generates that product's `rp_register_packages()`, which replaces `Engine.cpp`'s hard-coded loop. The types not yet
carved (`sameboy`, `mesen`, `host.io`, `n8`, `launchpad`) are registered through the same generated list from where they
live until their own split. The offline pool keeps its own fixed subset (`core` and `dsp`), and the script-node pack
keeps its own path: it is loaded at run time, not linked.

## 5. Fixed-capacity settings

A node meant for the device keeps every settings field fixed-size:

- `rp::text<N>`: up to `N` bytes of UTF-8 inline. Read and written as a JSON string; a longer value is a settings
  error naming the field.
- `rp::list<T, N>`: up to `N` elements of `T` inline. Read and written as a JSON array; a longer one is refused the same
  way.
- `rp::resource_ref<Type>` stores its resource id as a number. The desktop parses the document's `"res:N"` into it and
  writes it back as the same string, so documents do not change.
- Numbers, bools and enums, as now.

The device build `static_assert`s that a portable type's `Settings` holds only these, so a `std::string` slipping into
one is a compile error with the type's name, not a link failure. A desktop-only node (anything declaring `heap`: the
emulators, `rp.dsp.resample`, `rp.dsp.dpcm`) keeps whatever types it likes.

The core nodes move to them: `join` and `split`'s counts become `rp::list<std::uint32_t, kMaxPorts>` (16, which they
already enforce), and every `layout`, `from` and `to` becomes `rp::text<16>`. Their JSON Schemas are written as they
are today (a string, an array of integers), so no descriptor golden moves and no document changes. `validate` stays a
desktop hook, since the desktop validates every settings object before the device sees it.

## 6. Device mode and msgpack

**One header, built two ways.** A node's source includes `graph/node.hpp` and is written exactly as now. Compiled with
`RP_DEVICE`, the header is the portable core only: the port and param wrappers, `Block`, the compile-time reflection of
the `inputs`, `outputs` and `params` structs, the hook checks, `query_memory`, `create`, `destroy`, the instance
vtable, and the msgpack decode. Everything that describes or validates is compiled out and its table entries are null:
`descriptor`, `layout_for`, `prepare_settings` and `commit_settings`, `validate`, the JSON parse and the schema. The
device never plans; the desktop planned and validated the plan before export.

**A device instance owns its settings in place.** `query_memory` counts the `Settings` in the instance's own size, so
`create` constructs them in the host's memory rather than with `new`; the resource pins are a fixed array sized by the
number of resource fields; there is no layout string and no `try`. The desktop build keeps today's storage and its
pointer swap at a commit.

**`RP_SETTINGS_MSGPACK`.** A second value of `rp_settings.encoding`, accepted on both sides: the desktop's `create` and
`query_memory` read either encoding, so the device's decode is exercised on the desktop from E1 on. The decode matches
fields by name, keeps the default for a missing field and ignores an unknown one, as `rfl::DefaultIfMissing` does for
JSON, so a blob written by a newer desktop still loads its known fields. The desktop gains a converter from a node's
canonical JSON settings to msgpack (E3's `exportPlan` uses it). Whether reflect-cpp's msgpack reader builds with no
exceptions and no heap is a prototype question (section 11); if it does not, the reader is our own, a small one over
the same compile-time field list.

## 7. The device build

**`rp-device-check`** is a small Pico SDK firmware, `packages/graph-runtime/device-check/`, built with `arm-none-eabi`
against a pinned Pico SDK with no exceptions and no RTTI. It is a product like any other: its list is `core` and `dsp`,
its registration is generated, and only the device-capable types are compiled, in `RP_DEVICE` mode (the manifest's
`device` flag excludes the rest; one named anyway is refused, as the planner refuses it). Its `main`, for each portable
type: the type's default settings as msgpack (generated at build time by a desktop tool from the type's defaults),
`query_memory`, `create` into a static buffer, `activate`, one block through `runBlock`, `destroy`. CI builds it and
does not run it; it is flashable, and E3 runs its successor on the rig.

**The link trap.** The image links with `--wrap` on `malloc`, `calloc`, `realloc`, `free`, every `operator new` and
`operator delete`, `__cxa_throw`, `__cxa_allocate_exception` and the `std::__throw_*` family, and defines none of the
wrapped symbols, so any use in what it links is a link error naming the object that pulled it in. If the SDK's runtime
needs `malloc` (its stdio, say), the image drops that part of the runtime; the trap is never loosened.

**CI.** The linux-x64 job installs `gcc-arm-none-eabi` and the pinned Pico SDK and builds `rp-device-check`. The new
desktop targets join the `/W4 /WX` list in `packages/native/cmake/warnings.cmake`.

## 8. Tests

- **`retroplug-device-mode-test`**, a new Catch2 binary (the eleventh in `pnpm test:plugin`), compiles the portable
  sources with `RP_DEVICE` and `-fno-exceptions` on the desktop (Catch2's exceptions disabled). For every portable
  `core` and `dsp` type over a table of settings: a device-mode node created from msgpack produces output bit-identical
  to a desktop-mode node created from the same settings as JSON (same machine, so exactly); create, process and destroy
  allocate nothing, counted by the existing counting allocator; the decode is lenient as JSON is (a missing field
  defaults, an unknown one is ignored) and refuses an over-long `text` or `list` with the field's name.
- **The settings types** in `retroplug-graph-test`: `rp::text` and `rp::list` read and write the same JSON as the
  types they replace, a `resource_ref` round-trips `"res:N"`, and every bound is refused with its field.
- **Registration**: each product's generated list registers exactly the type ids it registered before, against a
  golden list per product.
- **Unchanged and run whole**: the descriptor goldens and conformance, `buildGraph`'s goldens, strict parity, the kit
  goldens and the render baseline under their require flags, `test:native`, `test:ui`, `test:node`, `test:cli-ts`,
  `check:docs`, `sdl:smoke`, the engine-graph binary in `build-tsan` and `build-asan`, the Reaper suite, and the
  BlipToaster and NESVJ suites from scratch copies. Nothing is re-recorded and no audio moves.

## 9. Landing

Five commits, each building and passing on its own:

1. **The packages.** `packages/node-sdk`, `graph-runtime`, `core` and `dsp` with their manifests; `src/graph` and
   `src/dsp` moved into them; `retroplug_product()` and each product's generated `rp_register_packages()`; the
   registration golden. No behaviour change.
2. **Fixed-capacity settings.** `rp::text`, `rp::list`, the numeric `resource_ref`; the core and DSP nodes' settings
   converted; the settings-type tests.
3. **Device mode and msgpack.** `RP_DEVICE`, the in-place instance, `RP_SETTINGS_MSGPACK` and its decode, the JSON to
   msgpack converter, `retroplug-device-mode-test`.
4. **The device build.** `rp-device-check`, the build-time settings generator, the link trap, the CI step; the
   prototype answers recorded with the image's sizes.
5. **Docs.** This design's as-built section; the main design's Phase E outline rewritten to E1 to E4 with section 2's
   decisions; spec/01, spec/02 and spec/06; spec/07 (E2 to E4 and the rest of the split as what is left); AGENTS.md (the
   new binary, the device build, where `src/graph` and `src/dsp` went); every doc link to a moved path.

## 10. Left beyond E1

- The N8 pack and the single C++ Edio (E2).
- `exportPlan`, the blob's format, the arena executor and the box as a graph host (E3).
- MicroQuickJS on the Pico (E4).
- The rest of the package split (main design section 11): Mesen's per-console compile-out, per-product esbuild
  bundles, menus and the HD player onto extensions and views, LSDj, risa, smsggdj, mGB, BlipToaster and Launchpad as
  packages, the TS halves of `core` and `dsp`.
- Downloadable native packs.
- Bit-identity across targets: a float node on the Cortex-M33 can differ from the desktop (fused multiply-adds, newlib's
  `libm`), so E3's on-device comparisons hold events and integers exactly and audio to a tolerance, unless E3 decides on
  `-ffp-contract=off` everywhere.

## 11. Prototype questions

1. Does reflect-cpp's compile-time reflection (`to_view`, `named_tuple_t`, the enum names `enum_param` reads) build with
   no exceptions and no RTTI under the devcontainer's `arm-none-eabi` GCC?
2. Does reflect-cpp's msgpack reader build with no heap and no exceptions, or is the reader our own?
3. Does the Pico SDK's runtime link under the trap without its stdio pulling in `malloc`?
4. What do `core` and `dsp` cost in flash and RAM in device mode? Recorded as E3's budget.
5. Does any portable node reach a heap type through a kernel (a `std::vector` in a kernel's state, say), which the
   trap would name, and how is each answered?
