# Phase E1: a portable node core and the package boundary

**Date:** 2026-11-03 · **Branch:** `feature/modularize` · **Status:** built (E1, five commits); section 12 records the
build, answers section 11 and carries what was left, and sections 1 to 10 are corrected in place

Phase E is the Pico runtime: the C++ nodes, the TypeScript nodes and the device packs RetroPlug builds run unchanged in
the desktop products and on the RP2350, and the Pico box becomes a graph host that keeps everything it does today. E1 is
its first step. It makes a C++ node's source build for the device as it builds for the desktop, with no heap and no
exceptions there, and it draws the package boundary that the N8 pack (E2) and any third party's pack will sit behind.
Phase E's outline is the main design's section 8 ([modular-graph-design.md](modular-graph-design.md)); the package
split it now begins is the main design's section 11.

## 1. Before E1

- **The executor is portable; the node authoring layer is not.** `retroplug-graph-exec` (`Executor.cpp`) has no heap,
  no exceptions and no dependency beyond libc. A C++ node is written as a plain struct and turned into the C ABI table
  by `node.hpp` (both under `packages/native/src/graph/` before E1), whose block path is clean, but whose control side
  is desktop code: an instance heap-allocates its `Settings` (`new S`), keeps its layout as a `std::string` and its
  resource pins in a `std::vector`, every control entry is wrapped in `try` / `catch`, and settings are parsed from
  JSON by reflect-cpp (`rfl::json`, over yyjson).
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
| How do settings reach a node on the device? | **msgpack in the plan blob, decoded at plan load** | `RP_SETTINGS_MSGPACK`; lenient by field name, as JSON is; a node's source changes only by one `#ifndef RP_DEVICE` around its desktop hooks (section 6) |
| Strings, lists and choices in a portable node's settings? | **Fixed-capacity types**, `rp::text<N>`, `rp::list<T, N>` and `rp::choice` | The same JSON shape; no allocation on the device ever; the arena is sized exactly (section 5) |
| What is a package? | **A workspace package per feature**, `packages/<name>/{native,ts}` with a manifest | Each product lists its packages and gets a generated `rp_register_packages()` (section 4) |
| Where is the SDK boundary? | **`node-sdk`, `graph-runtime`, then node packages** | A pack depends on `node-sdk` alone (section 4) |
| How is portability proved? | **A CI cross-compile with a link trap**, and device mode tested on the desktop | Heap use or a throw in portable code is a link error; a device run of the check starts at E3, when the box gets the plan loader; E2 runs the firmware with the pack's Edio on the rig, but no graph node (section 7) |

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
field is the manifest: under `native`, the CMake target, its entry function, and whether its node types build for the
device. CMake reads the field with `string(JSON)`, so the manifest is the one source. (A `ts` key will name the TS entry
when a package has a `ts/` part; no E1 manifest has one, and nothing reads it yet.)

```json
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
```

The entry is a unique function per package returning its `rp_package`. `rp_package_entry` stays reserved for a
downloaded pack's dynamic library, so many statically linked packages never collide on it. `retroplug.native` is an
object or a list of them, one per `rp_package` the workspace package holds, each with its own `device` flag: `dsp`
holds two, `rp.dsp` (`device: true`) and `rp.dsp.resample` (`device: false`, since it needs r8brain). A device product
takes the `device` entries only, and a device entry's node types that declare `heap` or `threads` are compiled out of
it (the planner's rule, `Target::Pico`). `node-sdk` has no entry: it is a plain library every package depends on.

**E1's packages:**

| Package | Holds | Depends on |
|---|---|---|
| `node-sdk` | `rp_node.h`, `node.hpp` (split into the headers section 6 lists), the description layer's support library (descriptor writer, canonical JSON, the graph document types `node.hpp` names), the msgpack settings decode, `rp::text`, `rp::list` and `rp::choice`, and `SpscRing` | reflect-cpp (headers only on the device) |
| `graph-runtime` | the executor (`retroplug-graph-exec`, the part a device needs), the planner, the registry, the resource table, `GraphHost`, and the `rp.host` nodes the runtime implements itself (`rp.host.audio-out`) | `node-sdk` |
| `core` | the `rp.core` nodes | `node-sdk` |
| `dsp` | the kernels (`packages/dsp/native/src/dsp/`, with `util/Hash.hpp`) and the `rp.dsp` nodes, `resample` among them | `node-sdk`, r8brain |

`SpscRing` is in `node-sdk` rather than `graph-runtime`: native code outside the graph includes it (the N8, Launchpad
and script-host links), and a pack depends on `node-sdk` alone, so it is the one home both `GraphHost` and a pack's
device links can reach.

A pack, ours in E2 or a third party's, depends on `node-sdk` and nothing else. `packages/native` keeps the engine, the
systems, the hosts and every node type not yet carved; its own manifest lists those entries (`sameboy`, `mesen`,
`host.io`, `n8`, `launchpad`) so that one mechanism registers everything, and each entry leaves it when its group is
carved. The files move with `git mv`, and every include path stays as it is (`#include "graph/node.hpp"` resolves from
the package's include directory), so no node source changes for the move.

**Products.** Each product declares its packages, `retroplug_product(<target> PACKAGES core dsp ...)`, and CMake
generates that product's `rp_register_packages()`, which replaces `Engine.cpp`'s hard-coded loop; the order of the list
is the registration order, and the golden holds it. The plugin is one product (`retroplug`) whose targets are every
binary DPF builds from the plugin's libraries. A configure-time check (`retroplug_check_products`) stops the build when
a binary links the engine and is no product, since a Linux module would otherwise fail only when loaded. A device
product (`retroplug_product(... DEVICE)`, section 7) takes the device entries only. The offline pool keeps its own fixed
subset (`core` and `dsp`), and the script-node pack keeps its own path: it is loaded at run time, not linked.

## 5. Fixed-capacity settings

A node meant for the device keeps every settings field fixed-size:

- `rp::text<N>`: up to `N` bytes of UTF-8 inline. Read and written as a JSON string; a longer value is a settings
  error naming the field.
- `rp::list<T, N>`: up to `N` elements of `T` inline. Read and written as a JSON array; a longer one is refused the same
  way.
- `rp::choice<"a", "b", ...>` replaces `rfl::Literal` in a portable node's settings (`rfl/Literal.hpp` includes
  `<string>` and `<vector>`). It keeps `Literal`'s interface (`value()`, `make<>`, `value_of<>`) and stores one byte;
  its desktop parser delegates to the same `rfl::Literal`, so the JSON, the refusals and the schema are identical.
- `rp::resource_ref<Type>` stores its resource id as a number. The desktop parses the document's `"res:N"` into it and
  writes it back as the same string, so documents do not change. On the wire to the device it is that string too.
- Numbers and bools, as now.
- Enums, with limits. The device reads an enum by its enumerator name, or by a number in a string as reflect-cpp does
  (`std::stoi`'s reading), through a scan of our own (`enums.hpp`, enchantum's technique, in the range -256 to 256).
  The enum needs a fixed underlying type and no `operator|`; its enumerators must lie within the scanned range. A flag
  enum (reflect-cpp reads those as names joined by `|`) and an unscoped enum without a fixed type are refused at
  compile time, and so is an `rfl::config::enum_range<E>` that changes what the scan sees (the desktop holds the scan
  to reflect-cpp's with a `static_assert` for every enum a portable `Settings` holds). A custom `rfl::Reflector<E>` is
  not seen by that check or by the device decode, which reads enumerator names: a portable enum must not have one (a
  rule, not a refusal; no node has one). `enum_param` is held to the same rules on the desktop too, which narrows what
  it accepted before. Each portable enum costs about 55 to 95 ms of compile time per translation unit, for both scans
  on the desktop.

The device build `static_assert`s that a portable type's `Settings` holds only these, so a `std::string` slipping into
one is a compile error with the type's name, not a link failure; so does a pointer, and so does a `heap` or `threads`
type built for the device. A desktop-only node (anything declaring `heap`: the emulators, `rp.dsp.resample`,
`rp.dsp.dpcm`) keeps whatever types it likes.

The core nodes move to them: `join` and `split`'s counts become `rp::list<std::uint32_t, kMaxPorts>` (16, which they
already enforce), and every `layout`, `from` and `to` becomes `rp::text<16>`. Their JSON Schemas are written as they
are today (a string, an array of integers), so no descriptor golden moves and no document changes. `validate` stays a
desktop hook, since the desktop validates every settings object before the device sees it; a node that builds for the
device keeps it, and `adjust_layout`, inside one `#ifndef RP_DEVICE` (section 6). `AddKind`, `BiquadType` and
`DitherAlgorithm` become `rp::choice`. A malformed `"res:N"` is now refused when the settings are parsed
(`settings-invalid`) rather than when the node is created (`create-failed`), with the same text.

## 6. Device mode and msgpack

**One entry header, built two ways.** A node's source includes `graph/node.hpp` and reads its guide. Compiled with
`RP_DEVICE`, the SDK is the portable core only. `node.hpp` is split by what each half is for:

| Header | Holds | Built |
|---|---|---|
| `node_core.hpp` | the port and param wrappers, `Block`, the compile-time reflection of `inputs`, `outputs` and `params`, the hook checks, `ResourceHold` and the resource-field walk, the block-path entries | both |
| `node_device.hpp` | `DeviceTypeCheck`, the in-place instance, the msgpack-only settings read, `query_memory`, `create`, `destroy`, the vtable with null `prepare`, `commit` and `release` | `RP_DEVICE` |
| `node_desc.hpp` | the descriptor, `layout_for`, the JSON parse, `prepare_settings` and `commit_settings`, `validate`, the schema, the heap-settings instance | desktop |
| `fields.hpp`, `enums.hpp`, `settings_types.hpp`, `settings_msgpack.hpp`, `msgpack_reader.hpp`, `PortTypes.hpp`, `ResourceId.hpp` | the field walk, the enum scan, the fixed-capacity types, the msgpack decode and reader, and the constants and names both builds share | both |
| `settings_json.hpp` | the reflect-cpp parsers for `text`, `list`, `choice` and `resource_ref` | desktop |

The portable boundary is a file boundary, which is easy to audit, where one file of interleaved `#if` blocks is not.
The device build carries an `#error` (under libstdc++) when the translation unit has already included `<string>`,
`<vector>`, `<map>`, `<optional>`, `<stdexcept>`, `<exception>`, `<iostream>`, `<functional>`, `<memory>`, `<random>`,
`rfl/json` or yyjson; it sees only what is included before `node.hpp`, and the link trap is the real guard. The field
walk is reflect-cpp's internal compile-time core (`num_fields`, `bind_to_tuple`) with our own name extraction, since
its public `to_view` and `named_tuple_t` include heap headers; `rfl::Rename` and `rfl::Flatten` fields are refused at
compile time, and a graph-test case holds the walk to `rfl::fields<S>()`. Since the decode matches by name and steps
over a key it does not know, a compiler that spelt a field's name otherwise would decode every field as its default:
a `static_assert` in `settings_msgpack.hpp` holds the names of a probe struct, a field of each portable kind, to their
spellings on every compiler that builds the decode, the device's included.

**A node author's rules.** A device-capable node's source is not written exactly as before, by the user's decision: it
wraps its desktop-only hooks, `validate`, `adjust_layout` and the helpers only they use, in one `#ifndef RP_DEVICE`.
`validate` returns `std::optional<std::string>` and `adjust_layout` takes a `graph::Layout`, desktop types the node's
own source names, so the hooks cannot compile in device mode. Three things enforce the guard on every compiler: a
forward declaration of `Layout` in device mode (an `adjust_layout` left outside fails with an incomplete type), a
`DeviceTypeCheck` `static_assert` that no `validate` or `adjust_layout` is visible, and the `<target>-device` object
libraries, which the desktop build compiles for every target of a manifest entry with `device: true` (the `RP_DEVICE`
flag, `-fno-exceptions -fno-rtti` off MSVC, nothing linked), so an unguarded hook fails the desktop build and CI.
Beyond that: settings hold the section 5 types only, a `heap` or `threads` type is not built for the device, and a
source built both ways keeps its own names with linkage under `rp` or in an anonymous namespace. Device mode puts
the SDK's names where the desktop's are (`rp`, `rp::graph`, `rp::detail`): a device image holds one build. The one
binary that holds both, `retroplug-device-mode-test`, compiles its device-mode objects with `rp=rp_devmode`, a token
rename that gives every name under `rp` a second spelling (the kernels the nodes call are compiled in with them), and
renames each package's `extern "C"` entry by a compile definition; `rp_node`, `RP_DEVICE` and string literals are
other tokens. An inline namespace would have moved the device's names with no rename, but MSVC does not extend one
through a later `namespace rp::graph` (CWG 2061), so no device-mode build relies on one.

**A device instance owns its settings in place.** `query_memory` counts the `Settings` in the instance's own size, so
`create` constructs them in the host's memory rather than with `new`: the resource pins, then the settings, then the
node, so the node is destroyed first and the pins last. The pins are a fixed array sized by the number of resource
fields (none for a type without any); there is no layout string and no `try`. The desktop build keeps today's storage
and its pointer swap at a commit. The device's stack in `query_memory` and `create` holds the `Settings` and up to
three 264-byte error buffers.

**`RP_SETTINGS_MSGPACK`** (`rp_settings.encoding` is 1). It is accepted on both sides: the desktop's `create` and
`query_memory` read either encoding for a type whose `Settings` is portable, so the device's decode is exercised on the
desktop. A type with desktop settings (`rp.host.audio-out`, the emulators) and a script node read JSON alone. The reader
is our own, about 530 lines over the compile-time field list, with no allocation and no exception (reflect-cpp's needs
msgpack-c and a heap). It matches fields by name, keeps the default for a missing field and steps over an unknown one,
as `rfl::DefaultIfMissing` does for JSON, so a blob written by a newer desktop still loads its known fields. It mirrors
the JSON reader's rules (an unsigned field takes only a non-negative integer, a float field takes an integer or a
float, a repeated key keeps the last) and its refusal wording, naming the field. Two inputs part, by choice: an
integer above `INT64_MAX` in a signed field, which reflect-cpp's reader wraps (18446744073709551615 reads as -1 in an
int32) and the decode refuses as out of range, and `-0`, which the converter writes as msgpack's 0 for an unsigned
field to take where the JSON reader refuses it as negative. The desktop's converter,
`settingsToMsgpack(json)` in `node-sdk`, writes each number as the JSON spells it (an integer literal in its smallest
form, anything with a fraction or exponent as float64); E3's `exportPlan` uses it. A desktop-only SDK interface,
`rp.sdk.settings-json/1`, writes an instance's settings back as JSON, which every `node.hpp` type answers; the device
check's case generator reads a type's defaults through it.

## 7. The device build

**`rp-device-check`** is a small Pico SDK firmware, `packages/graph-runtime/device-check/`, built with `arm-none-eabi`
against the Pico SDK 2.1.1 (the devcontainer's tag) with no exceptions and no RTTI. It is a DEVICE product like any
other: its list is `core` and `dsp` (`device-check/packages.cmake`, which the image and the case generator both
include, and the generated header records, so they cannot drift), its registration is generated, and only the
device-capable types are compiled, in `RP_DEVICE` mode. The device project is its own CMake build (`RP_DEVICE_BUILD`):
`cmake/packages.cmake` finds the packages relative to itself, skips the desktop's `-device` objects, and builds the
libraries with the SDK's code-generation flags. Its `main`, for each portable type: the type's default settings as
msgpack, `query_memory`, `create` into a static 400 KiB arena, `activate`, one block of 64 frames through `runBlock`,
`deactivate`, `destroy`, each failure a distinct `Step` naming the type; it prints one result line on the UART. The
settings come from `rp-device-check-cases`, a desktop tool that is itself a DEVICE product of the desktop build: it
creates each type with no settings, reads the defaults back through `rp.sdk.settings-json/1`, converts them with
`settingsToMsgpack`, checks the round trip, resolves the layout as a planner would for an unwired node, and writes one
header of constant arrays (with one completion table, `rp.dsp.normalize` needing `"buffer": "res:1"`, which the
check's host serves). CI builds the image and does not run it; the ELF is flashable, and E3 is the first phase to run
the check's successor on the rig, with plans loaded by the box.

**The link trap.** The image links with `--wrap` on 87 symbols and defines none of the wrapped ones, so any use in
what it links is a link error naming the object that pulled it in: the malloc family (plain, newlib's `_r` forms,
aligned and `memalign` variants), `sbrk`, `__cxa_guard_*`, and every `operator new` and `operator delete`, `__cxa_`
throw entry and `std::__throw_*` that the device multilib's own `libstdc++.a` defines (read with `nm` at configure,
which stops if an expected one is missing). The runtime that exception handling and RTTI reference is trapped too, so
an object compiled with either is a link error even where it throws nothing: `__gxx_personality_v0`,
`__cxa_begin_catch`, `__cxa_end_catch`, `__cxa_end_cleanup` (where an ARM cleanup ends), `_Unwind_Resume`,
`__dynamic_cast` and the three class `type_info` vtables; `__cxa_pure_virtual` is not, since an abstract class built
without RTTI references it. A post-link check fails the build if the image defines any `__wrap_` symbol, or refers to
one at all (a use that a link option tolerating undefined symbols let through). A positive control
(`rp-device-trap-probe`, which does `new int(1)` and `std::malloc(16)`) is linked on every run of the script and must
fail naming both. The runtime is the SDK's own `pico_stdlib`, with `pico_malloc` taken out of `pico_runtime`'s link
libraries in place at configure (so a library that links `pico_stdlib` itself loses it too, and the configure stops if
the name to remove is gone): `pico_malloc` defines the wrapped malloc family, so with it in the link a call to malloc
resolves to its wrapper. Everything else this image links does under the full trap: stdio over the UART links, since
the SDK wraps `printf` and `puts` to its own heap-free implementation. The image builds Release by default, so the
Executor's asserts compile out; a Debug image links too.

**CI.** The linux-x64 job installs `gcc-arm-none-eabi` (with newlib and its libstdc++), clones the Pico SDK 2.1.1 with
no submodules and runs `pnpm device:check` (`tools/build-device-check.sh`), which builds the cases tool, the image and
the probe, runs the trap proof and prints the sizes by owner. The new desktop targets join the `/W4 /WX` list in
`packages/native/cmake/warnings.cmake`, the `-device` object libraries among them.

## 8. Tests

- **`retroplug-device-mode-test`**, a new Catch2 binary (the eleventh in `pnpm test:plugin`), compiles the portable
  sources with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` on the desktop, into an object library reached only
  through the C ABI, as a device's host reaches it (Catch2 is one library built with exceptions, so its own
  translation unit keeps them; MSVC keeps `/EHsc` on the objects). Ten cases: a device-mode type carries no
  description and never re-plans; for every portable `core` and `dsp` type over a table of settings, a device-mode
  node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, with
  create, process and destroy allocating nothing (the counting allocator); the desktop reads msgpack and renders the
  table as it does from JSON; the decode keeps a missing field's default and steps over an unknown one, and a type
  with no settings takes any map; it refuses what JSON refuses, naming the field, in the same words (the two inputs
  where the readers part, section 6, pinned); the converter writes each number as the JSON spells it; and four
  `[device-check]` cases: each manifest device entry's types agree between the two builds, the image's cases list
  exactly those, the run passes in the image's arena with no allocation, and it stops at a type that fails and names
  it, at each of its steps (`rp.devtest.fault` fails a create, an activate and a block). A `try_compile` triple at every configure (GCC and Clang) requires a heap type, a pointer in `Settings` and a
  heap header each to fail in device mode with its own reason.
- **The settings types** in `retroplug-graph-test`: `[settings-types]` (`rp::text` and `rp::list` read and write the
  same JSON as the types they replace, a `resource_ref` round-trips `"res:N"`, every bound is refused with its field),
  `[fields]` (the field walk against `rfl::fields<S>()`) and `[enums]` (the enum scan against reflect-cpp's), and the
  dither's `Mt19937` against `std::mt19937`.
- **Registration**: each product's generated list registers exactly the type ids it registered before, against a
  golden list per product (`[packages]` in `retroplug-engine-graph-test`).
- **Unchanged and run whole**: the descriptor goldens and conformance, `buildGraph`'s goldens, strict parity, the kit
  goldens and the render baseline under their require flags, `test:native`, `test:ui`, `test:node`, `test:cli-ts`,
  `check:docs`, `sdl:smoke`, the engine-graph binary in `build-tsan` and `build-asan`, the Reaper suite, and the
  BlipToaster and NESVJ suites from scratch copies. Nothing is re-recorded and no audio moves.

## 9. Landing

Five commits, each building and passing on its own:

1. **The packages.** `packages/node-sdk`, `graph-runtime`, `core` and `dsp` with their manifests; the graph libraries
   (`packages/native/src/graph`) and the kernels (`packages/native/src/dsp`) moved into them; `retroplug_product()`
   and each product's generated `rp_register_packages()`; the registration golden. No behaviour change.
2. **Fixed-capacity settings.** `rp::text`, `rp::list`, the numeric `resource_ref`; the core and DSP nodes' settings
   converted; the settings-type tests.
3. **Device mode and msgpack.** `RP_DEVICE`, the in-place instance, `RP_SETTINGS_MSGPACK` and its decode, the JSON to
   msgpack converter, `retroplug-device-mode-test`.
4. **The device build.** `rp-device-check`, the build-time settings generator, the link trap, the CI step; the
   prototype answers recorded with the image's sizes.
5. **Docs.** This design's as-built section; the main design's Phase E outline rewritten to E1 to E4 with section 2's
   decisions; spec/01, spec/02 and spec/06; spec/07 (E2 to E4 and the rest of the split as what is left); AGENTS.md (the
   new binary, the device build, where the graph libraries and the kernels went); every doc link to a moved path.

## 10. Left beyond E1

- The N8 pack and the single C++ Edio (E2).
- `exportPlan`, the blob's format, the arena executor and the box as a graph host (E3); with it a device default for
  `rp.core.delay` or a planner cap on the Pico (12.6), and a measure of the device's stack in `query_memory` and
  `create`.
- MicroQuickJS on the Pico (E4).
- The rest of the package split (main design section 11): Mesen's per-console compile-out, per-product esbuild
  bundles, menus and the HD player onto extensions and views, LSDj, risa, smsggdj, mGB, BlipToaster and Launchpad as
  packages, the TS halves of `core` and `dsp`.
- Downloadable native packs.
- Bit-identity across targets: a float node on the Cortex-M33 can differ from the desktop (fused multiply-adds, newlib's
  `libm`), so E3's on-device comparisons hold events and integers exactly and audio to a tolerance, unless E3 decides on
  `-ffp-contract=off` everywhere.
- A Windows run of `build.bat` and `test.bat` (12.6) and a macOS or libc++ run of the refused-source checks.

## 11. Prototype questions

Answered in section 12.

1. Does reflect-cpp's compile-time reflection (`to_view`, `named_tuple_t`, the enum names `enum_param` reads) build with
   no exceptions and no RTTI under the devcontainer's `arm-none-eabi` GCC?
2. Does reflect-cpp's msgpack reader build with no heap and no exceptions, or is the reader our own?
3. Does the Pico SDK's runtime link under the trap without its stdio pulling in `malloc`?
4. What do `core` and `dsp` cost in flash and RAM in device mode? Recorded as E3's budget.
5. Does any portable node reach a heap type through a kernel (a `std::vector` in a kernel's state, say), which the
   trap would name, and how is each answered?

## 12. As built

Five commits on `feature/modularize`: the packages, fixed-capacity settings, device mode and msgpack, the device build,
and these docs. Parity bit for bit, the kit goldens, the render baseline, the descriptor goldens, `buildGraph`'s goldens
and both consumer suites were untouched throughout, and nothing was re-recorded. The Reaper suite was not run by
any commit; the acceptance run covers it. Sections 1 to 10 are corrected in
place where the build made them false (the node author's rules, SpscRing's home, enums, the runtime the trap leaves
in). The final counts: `retroplug-device-mode-test` ends at 10 cases, `retroplug-graph-test` at 282, and
`retroplug-engine-graph-test` at 313 (the baseline's 311 and the two `[packages]` cases).

### 12.1 The packages (commit 1)

- **Manifests.** `packages/{node-sdk,graph-runtime,core,dsp}/package.json` carry the `retroplug` field;
  `retroplug.native` is an object or a list, since `dsp` holds two `rp_package`s with different `device` flags.
  `node-sdk` has no entry and no `device` flag: which half of it a build gets is chosen by the compile mode.
  `graph-runtime` is `device: false` (`rp.host.audio-out` keeps a vector; the device host implements its own in E3).
  `packages/native`'s own manifest lists the five not-yet-carved entries (target `retroplug-backend`), so one
  mechanism registers everything.
- **CMake.** `cmake/packages.cmake` holds `retroplug_package()` (reads the manifest with `string(JSON)`, adds
  `packages/<name>/native`), `retroplug_package_library()` (static, C++20, PIC, `/bigobj`), `retroplug_product()` and
  `retroplug_product_table()`, with configure-time FATAL checks (a missing manifest field, a duplicate or
  non-identifier entry, an unknown package, a product declared twice, a `TARGETS` name that is no target). The root
  names `RP_DESKTOP_PACKAGES core dsp graph-runtime native`, whose order reproduces the old loop (`graph-runtime`
  follows `dsp` so that `rp.host` registers after `rp.dsp.resample`). Nine products carry it: the plugin (one product
  with the DPF binaries as its targets, derived from what DPF built, so macOS's `retroplug-export` is included), the
  host, the CLI, the SDL standalone, the Node addon (opt-in), and four test binaries.
- **Link-time registration.** `Engine.cpp` references `rp_register_packages()` and each product's own object defines it;
  GNU ld resolves an archive's undefined reference against a command-line object in any order. An executable that pulls
  the engine in without being a product fails to link; a Linux module would fail only when loaded, so
  `retroplug_check_products` stands in for the missing link error. The test binaries `retroplug-classid-test` and
  `-watcher-test` never pull `Engine.o` in and are exempt.
- **The golden** is `[packages]` in the engine-graph binary (`golden/product-registrations.json`, 26 ids per product):
  it was recorded from a throwaway program at the design commit that printed the old hard-coded list, not from the new
  code, so it could not agree with itself by construction.
- **Moves.** All by `git mv`, no include changed; `cmake/graph.cmake` was deleted, its targets living in the packages'
  CMakeLists. `retroplug-node-sdk-headers` is the include-only target the executor takes, so it needs no library.
  `SpscRing` went to `node-sdk` (section 4). `util/Hash.hpp` went to `dsp`; `retroplug-core` takes only its include
  directory, not a link edge, since it calls nothing in the kernels.

### 12.2 Fixed-capacity settings (commit 2)

- `rp::text<N>` (inline bytes and a length, an implicit `string_view`, a consteval literal constructor so a default that
  does not fit fails to compile) and `rp::list<T, N>` (an inline array, a consteval initializer list; trivially copyable
  elements only; the length is a byte up to 255 and a `uint16` above). Their reflect-cpp parsers delegate to
  `std::string` and `std::vector`, so reads, writes and schemas are the replaced types'; an over-bound read is an error
  naming the field.
- `resource_ref` stores a number. A malformed `"res:007"` is refused at the settings parse (`settings-invalid`) where it
  failed at create (`create-failed`); the mock backend refuses it first, in the same words.
- `text<16>`: the longest layout any table accepts is `discrete(64)`, 12 bytes. The bound is `kMaxLayoutName`.

### 12.3 Device mode and msgpack (commit 3)

- **The split** is section 6's table. The portable boundary being a file boundary was chosen over interleaved `#if`
  blocks; the param descriptor members became traits (`ParamDescOf`) so the wrappers have no `#if` inside.
- **The node author's rules** (section 6) differ from what the design promised ("a node is written exactly as now"),
  by the user's decision (`.superpowers/e-pico/decisions.md`): the one `#ifndef RP_DEVICE` around the desktop hooks,
  `rp::choice` for `rfl::Literal`, and enums with limits. The `<target>-device` object libraries make the
  guard enforced on every compiler the desktop build uses, and enums on the device (`enums.hpp`), with the
  `enumMatchesReflect` `static_assert` for every enum a portable `Settings` holds. What it narrows: `enum_param` over an
  unscoped enum without a fixed type or a flag enum compiled before and is now refused; an `rfl::config::enum_range<E>`
  override is not honoured (the `static_assert` catches it); a custom `rfl::Reflector<E>` is not seen by the device
  decode, which reads names (`rfl::internal::has_read_reflector<E>` would refuse it in one line; no node has one).
- **Dither's `<random>`.** `std::mt19937` is heap-free but `<random>` includes `<string>`, `<vector>` and `<iosfwd>`.
  `rp::dsp::Mt19937` (`Mt19937.hpp`) is the same engine, constexpr, with the standard's 10000th draw checked at compile
  time and a graph-test sweep over 70 seeds; the kit goldens and parity did not move.
- **Constant tables.** `node_type<T>()` is constexpr and `peer_as` compares with `&kVtable<T>`, so a package's
  `kTypes[]` is constant-initialised and a device object has no `__cxa_guard_acquire` (libsupc++ implements it with a
  throw).
- **The converter's numbers** are as the JSON spells them, so a blob refuses what the JSON refuses (`{"u32": 2.0}` fails
  in both).
- **Refusing a source that must not build for the device**: at every configure `try_compile` requires `HeapType`,
  `PointerSettings` and `HeapHeader` (under `test/device-mode/refused/`) each to fail with its own reason, matched on
  the compiler's `error:` lines only. The heap-header check reads libstdc++'s include guards, so it is skipped under
  libc++ (macOS), where the other two still run and the link trap stands in.
- **Stack.** `query_memory` and `create` hold the `Settings` plus up to three 264-byte error buffers.

### 12.4 The device build (commit 4)

- **The pieces** are section 7's. Product-level decisions: the settings generator is a DEVICE product of the desktop
  build; the cases and the image share `device-check/packages.cmake`; reflect-cpp's pin is read from rpcpp's
  `CPMAddPackage` line (0.26.2) so the two builds cannot drift, with a FetchContent fallback; no UF2 (the ELF is the
  artifact, and `pico_add_extra_outputs` would build picotool); `PICO_SDK_PATH` set but not an SDK is an error, the
  fallback to `/workspaces/pico-sdk` applies only when unset; the build directory is `build-device/`.
- **The trap** is 87 symbols, each its own `--wrap` (a comma-joined list is split by gcc into input files). The package
  libraries compile with the SDK's code-generation flags (`-ffunction-sections -fdata-sections -fno-unwind-tables
  -fno-use-cxa-atexit`), so the trap names what the image keeps.
- **A new SDK interface**, `rp.sdk.settings-json/1`, exists because the ABI had no way to get a type's defaults and the
  descriptor schema has none; adding either would have moved goldens. Every `node.hpp` type answers it (one string
  compare on a plan-time `get_interface`).
- **Not run:** the image is never flashed or run, so instance sizes on ARM are unmeasured; the x86-64 sizes are upper
  bounds.

### 12.5 The prototype questions, answered

1. **Reflection builds with no exceptions and no RTTI on `arm-none-eabi` GCC 13.2.1, with the SDK's real flags
   (`-mcpu=cortex-m33 -march=armv8-m.main+fp+dsp -mfloat-abi=softfp`).** But the public headers (`to_view`,
   `named_tuple_t`) include `<iostream>`, `<string>`, `<vector>`, `<stdexcept>` and `<unordered_map>`, so the portable
   core uses reflect-cpp's internal compile-time core (`num_fields`, `bind_to_tuple`, the fake-object name trick: only
   `<tuple>`, `<array>`, `<utility>`, `<cassert>`) with a copy of its 20-line name extraction. The extraction needs a
   deduced return type: a named `std::string_view` return makes GCC append the template arguments to the name.
   `fields.hpp` is `#error` under `REFLECTCPP_USE_CPP26_REFLECTION`, and the `[fields]` case holds it to
   `rfl::fields<S>()` across seven struct shapes.
2. **The reader is our own.** reflect-cpp's msgpack needs msgpack-c (not in the tree), unpacks into a `msgpack_zone`
   (malloc, 2048-byte chunks), reads `<istream>` and reports through `std::string`. `msgpack_reader.hpp` and
   `settings_msgpack.hpp` are about 530 lines with no allocation or exception.
3. **Only `pico_malloc` has to go.** It defines the wrapped malloc family, so a call to malloc resolves to the wrapper
   and the trap fires further down, at newlib's `malloc.o`. Everything else links under the full trap, `pico_stdio`
   (UART) and `pico_printf` included, because the SDK wraps `puts` and `printf` to its own heap-free implementation; a
   scratch build that also linked `pico_stdio_usb` (TinyUSB) passed too. The first build's failure at newlib's
   `findfp.o` came from the default panic's `puts` in an image with no `pico_stdio`, which is newlib's and allocates
   its buffers; it was not a heap inside stdio. So the image keeps stdio and reports on the UART, and
   `pico_minimize_runtime` is for size only.
4. **The sizes** (RP2350 flags, link map by owner, bytes; stdio over the UART and printf in the image):

   | Owner | Flash, Release (-O3) | Flash, MinSizeRel (-Os) | RAM (data / bss, Release) |
   |---|---|---|---|
   | core: `rp.core` nodes | 22,002 | 13,832 | 0 / 0 |
   | dsp: `rp.dsp` nodes (decode included) | 14,027 | 8,615 | 0 / 0 |
   | dsp: kernels | 4,424 | 2,944 | 0 / 0 |
   | graph-runtime: executor | 1,404 | 1,078 | 0 / 0 |
   | the check (cases, run, registration) | 6,326 | 5,100 | 40 / 411,937 (arena 409,600 and a 2,048-byte buffer) |
   | Pico SDK runtime (stdio, printf) | 15,593 | 13,010 | 96 / 373 |
   | toolchain (libc, libm, libgcc) | 1,380 | 1,588 | 1,708 / 432 |
   | **total** | **65,156** | **46,167** | 1,844 / 412,742 |

   Release's sections: `.text` 58,248, `.rodata` 4,852, `.data` 1,848, `.bss` 412,472. `core`, `dsp` and the executor
   are 41,857 bytes of flash at -O3 (26,469 at -Os) and **no static RAM**: their RAM is per instance, from
   `query_memory`.
5. **No portable node reaches a heap type or a throw through a kernel**; the image links `core` and `dsp` under the full
   trap and it names nothing in them. The one heap-bringing header was the dither's `<random>`. The DPCM kernel is
   heap-free and sits in the device kernels archive but nothing references it, so it is not linked; its node holds a
   vector and is desktop-only by its `heap` declaration. The trap's reach is proved: a `new int(7)` in `main` and a
   `new std::vector<float>` in the gain kernel each fail naming the object, and the probe proves it on every run.

### 12.6 What was found

- **`rp.core.delay`'s default needs 384 KB per instance** (`maxSamples` 48,000, stereo; 384,104 bytes on x86-64), about
  74% of the RP2350's 520 KB of RAM, and sizes the check's 400 KiB arena. E3 needs a device default or a planner cap.
- **Windows and MSVC never built E1's device-mode targets.** The `-device` object libraries are in the default build
  under `/W4 /WX`, so a Windows problem breaks `build.bat`'s plain build. The code review found the first build's
  device mode certain to fail there: it put the SDK in an inline namespace `rp::device`, and MSVC (19.44 to latest)
  opens a new `rp::graph` at a later `namespace rp::graph {` rather than extending `rp::device::graph` (CWG 2061;
  `C2039`, `C2872`). Device mode now uses the desktop's namespaces, and the device-mode test renames its objects'
  `rp` instead (section 6). The review's MSVC snippets compiled the rest clean under `/W4 /WX` (`rp::choice::make<>`
  and `rp::text`'s literal constructor as consteval in default member initializers, the template-lambda variable
  templates in `enums.hpp`, `__FUNCSIG__` in a consteval function); not built on MSVC: the copied `/bigobj`,
  `std::strcpy` in `DeviceCheck.cpp`, and `/Drp=rp_devmode` over the MSVC STL's headers. A `build.bat` and `test.bat`
  run is owed. libc++ runs two of the three refused-source checks.
- **Per-enum compile cost** is about 55 to 95 ms per int-underlying enum per translation unit (3 to 5 times
  enchantum's), and the desktop pays both scans. If enums multiply, test validity with `enumNameFromSignature` directly
  or batch values per signature.
- **Catch2 cannot be built exception-free here** (one shared library), hence the object library of section 8.
- **clang 18 rejects alias-template CTAD**, so the parsers spell `rfl::Unexpected<rfl::Error>(...)`.
- **A debug `__assert_func`** in the SDK prints through its own printf and links under the trap; the Executor's asserts
  compile out in Release.
- **Sanitizers:** TSan and ASan runs of the engine-graph and graph-test binaries, and ASan of the device-mode test, are
  clean.
