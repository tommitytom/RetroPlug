# Downloadable packs M1: native packs. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load a pack's native half (a dynamic library exporting `rp_pack_entry`) from a version folder at run time,
into every Engine of the process, with the folder model (`pack.json`, `state.json`, locks, development folders) under
it, and prove the pinned C++ boundary by booting the Mesen package built as a pack bit for bit against its static build.

**Architecture:** A new static library `retroplug-packs` in the shell (`packages/native/src/host/packs/`) holds the
folder model and a `PackTable` that opens each pack's library once and never closes it; `retroplug-backend` links it
and defines the process's table (`processPacks()`), seeded with the product's built-in packages. Every `Engine`
registers the built-ins, the built-in script pack, then its table's packs, and pulls packs loaded later at each
`setGraph` on its own control thread (`Engine::syncPacks`), so no host names a pack. `cmake/packs.cmake` builds a
package's static targets into a MODULE with a generated entry and every other symbol local, staged at
`build/packs/<id>/<version>/`.

**Tech Stack:** C++20, CMake 3.27, reflect-cpp (`rfl::json`) for the JSON, `dlopen` / `LoadLibraryExW`, `flock` /
`LockFileEx`, Catch2 v3, ThreadSanitizer and AddressSanitizer through the repo's `RETROPLUG_SANITIZE` builds.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) is part 1 of the packs road (loading a
pack at run time). It is built in five milestones, each a plan of its own:

- **M1, native packs (this plan):** the pack folder model, the API level and `rp_pack_entry`, the native loader,
  Engine registration (the built-ins, then loaded packs, live adds reaching running Engines), `retroplug_pack()`, a
  fixture pack with its broken variants, and the feasibility of the pinned C++ boundary on the Mesen package.
- **M2, TS halves:** the TS halves of a pack, the live registries the generated ones seed, the one set of checks the
  generator and the loader share, staging and all-or-nothing commit across halves.
- **M3, placeholders:** a project naming a missing pack opens, keeps the missing parts' data and saves it back byte for
  byte, and comes alive when the pack arrives.
- **M4, archives and commands:** `.rppack` (miniz), `retroplug-cli pack install / list / remove`, `--pack <dir>`,
  Settings > Packs.
- **M5, the real packages as packs:** Mesen and BlipToaster loaded as packs in a test product with their suites
  unchanged, and the plugin, Node, concurrency and sanitizer legs.

**Execution model.** Sonnet implements each task from its text alone; Opus reviews each task's diff before the next one
starts. Every step names its exact command and what it prints. The code here was compiled and run in a prototype
worktree (`/workspaces/packs-proto`, branch `proto/packs-m1`, uncommitted) on Linux: an implementer who meets a
difference can diff against it, but this plan's text is the source of truth.

**Cross-platform gates.** Tasks 2 to 10 change every platform's configure, and Task 3 puts `retroplug-packs-test` in
`pnpm test:plugin`, which CI runs on macOS and Windows. Nothing here was compiled there, so two steps (the end of
Task 6 and the end of Task 10) build and run the test binary on a Windows and a macOS machine before anything is
pushed; a failure there is fixed in the task it belongs to, not deferred. The Windows and macOS code paths this plan
already writes for (wide paths, `LoadLibraryExW`, `LockFileEx`, a non-empty exported-symbols list, the throwing
variant at `/EHs` and the loader's handler at `/EHsc`, folders a loaded DLL keeps) are the ones those gates prove.

## Decisions this plan makes

The design leaves these open; each is the plan's choice, and [Design questions](#design-questions-this-plan-found)
lists them for review with the gaps the design leaves.

- **The library lives in the shell, `packages/native/src/host/packs/`, as `retroplug-packs`.** Every desktop host (the
  plugin's formats, the SDL and DPF standalones, the CLI, the headless host, the Node addon) links `retroplug-backend`,
  which links it, so one library reaches all of them with no host edit. It is host infrastructure, not a node package:
  it registers no node type, never builds for a device (the Pico does not change), and needs only `retroplug-graph`
  (for `NodeRegistry`), which the shell already declares. It is a library apart from the backend so a test of the
  folder model needs no product; the one piece that needs the product, `processPacks()` (seeded with
  `rp_register_packages`), is a backend source.
- **Engines pull; the table never pushes.** `PackTable` is append-only and holds each pack immutable once appended. An
  Engine remembers how many of its table's packs it has read and, in its constructor and at the top of every
  `setGraph`, registers the rest (`syncPacks`): the same control thread and the same point between plans at which
  `loadScriptPack` changes the registry. A load from any thread therefore reaches every live Engine at its next
  `setGraph`, a new Engine (a render job, a second plugin instance) sees every load at construction, and nothing needs
  a cross-thread post.
- **The process's table starts empty, and no host loads at launch in M1.** `loadLaunchPacks(table, roots, devDirs)`
  is the API a host calls at start (settle `state.json`, discover, load in dependency order); M1 calls it from tests
  only. A native half alone has nothing the app can place until M2's TS halves, and an automatic scan at the first
  Engine would make every test binary read the user's data directory.
- **The roots take overrides.** `RETROPLUG_PACKS_DIR` and `RETROPLUG_PACKS_CACHE_DIR` replace the data and cache roots,
  as `RETROPLUG_USER_CONFIG_DIR` replaces the config one, so M4's CLI tests can point them at a scratch folder; every
  function that touches the disk also takes a `PackRoots`, so M1's tests hand it a temporary folder.
- **`state.json` is `{"version": 1, "packs": [{"id", "active", "pending"?, "remove"?}]}`.** Its `version` is read on
  its own first, so a newer one is refused as newer (a newer schema need not parse) and never overwritten (the repo's
  config rule). Every access holds `packs/state.lock`: a reader shared, a writer exclusive, so a writer's rename never
  meets a reader with the file open (which Windows refuses) and two read-modify-writes never interleave. A root this
  process cannot write has no lock to take and nothing can write there: it is read, and its installed folders loaded,
  unheld. The waits are unbounded, which the holds' brevity makes safe; a process stopped while it holds the lock (a
  debugger, SIGSTOP) blocks other launches until it runs again, which is documented rather than timed out. Writes go
  to a temporary file named by the pid and a counter, flushed (`rp::fsyncPath`, which moves into `retroplug-packs` and
  gains a wide-path overload) and renamed over the file.
- **The version lock is `pack.lock`, a shared `flock` / `LockFileEx` lock held for the life of the process** in each
  installed version folder whose library a process opened, loaded or refused (one hold per folder, however often a
  refused load is retried), and in no folder it refused before opening anything. The OS drops it
  when the process ends, however it ends, so a crash leaves no stale lock. A settle deletes a folder only while it holds
  that lock exclusively, taken at once or not at all. Development folders take no lock.
- **A launch settles, discovers and takes its version locks in one exclusive hold of `state.lock`**
  (`loadLaunchPacks`), then loads with the lock let go, the table keeping the holds it was handed. Another launch's
  settle takes the same lock, so it deletes a version either before this launch discovers it or not at all.
- **`pack.json` is a template in the package (`packages/<p>/pack.json`) whose `apiLevel` the configure stamps** from the
  `#define RP_API_LEVEL` line of `graph/rp_pack.h`, so a pack's manifest and its entry cannot disagree. The library's
  file is `halves.native` plus `.so`, `.dylib` or `.dll`, and `halves.native` must be `rp-<id>`: two packs' libraries
  never share a file name, which Windows matches an already-loaded module by.
- **`requires.packs` names packs only (decided).** A built-in package is not something a manifest names; a cart pack
  that needs a console names its platform in `requires.platforms` (M2's to check).
- **`provides.nodeTypes` is held both ways (decided):** a registered type it does not declare is refused, and so is a
  declared type the library does not register, since part 5's resolver trusts it.
- **A pack with any half but the native one is refused in M1**, since M1 loads no TS half and a pack never loads in
  part; M2 lifts it.
- **A refused pack's library stays open.** Opening it ran its static constructors, and whatever they registered (an
  `atexit` handler among them) must still find its code at exit.
- **The fixture pack lives in `packages/native/test/packs/fixture/`, not as a workspace package**, so it needs no pnpm
  lockfile change and joins no product list. Its broken variants are built from one source with definitions.
- **The feasibility test's product is a new root list, `packs-host`: the plugin's list without `mesen` and
  `bliptoaster`.** M1 needs no audio backend, MIDI port or Launchpad, so the test binary links none of PortAudio, SDL
  audio or RtMidi; M5 adds the list its product needs. The test binary also links `retroplug-mesen` statically,
  unregistered, and hands it to one Engine through `addNodePackage`, so the pack and the static build render
  interleaved in one process and their output is compared block for block. Whether the two copies share a static is
  not that comparison's to say: the module's dynamic symbols are (one export, no GNU-unique symbol, no RetroPlug
  import), read from its file in a test.

## Commands used throughout

- Configure (after any CMake change that adds a target): `pnpm configure` (that is `cmake -S . -B build`). Never
  `rm -rf build`.
- Build one test binary: `cmake --build build --target retroplug-packs-test -j$(nproc)`. The test binaries and the pack
  modules are `EXCLUDE_FROM_ALL`: build them by name.
- Run: `build/bin/retroplug-packs-test` (all cases) or `build/bin/retroplug-packs-test "[packs-loader]"` (a tag).
- `deps/sameboy` shows as modified after a configure: expected, never stage, reset or stash it. Stage files by name.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/node-sdk/native/src/graph/rp_pack.h` | `RP_API_LEVEL`, `RP_PACK_ENTRY_SYMBOL`, `rp_pack_entry` |
| `cmake/packs.cmake` | the level and platform for CMake, `retroplug_pack_module()`, `retroplug_pack()`, the PIC check |
| `packages/native/src/host/packs/PackDirs.{hpp,cpp}` | the data and cache roots, the platform folder, the pack path |
| `packages/native/src/host/packs/PackManifest.{hpp,cpp}` | `pack.json` parsed and checked |
| `packages/native/src/host/packs/FileLock.{hpp,cpp}` | advisory file locks, the version lock |
| `packages/native/src/host/packs/PackState.{hpp,cpp}` | `state.json`: read, atomic write, locked update, settle |
| `packages/native/src/host/packs/PackCandidate.hpp` | `PackOrigin`, `PackCandidate`, `PackRefusal` |
| `packages/native/src/host/packs/NativeLibrary.{hpp,cpp}` | open a library for good, find a symbol |
| `packages/native/src/host/packs/PackTable.{hpp,cpp}` | the loader: the table, its refusals, dependency order |
| `packages/native/src/host/packs/PackDiscovery.{hpp,cpp}` | which folders a launch loads, and the launch itself |
| `packages/native/src/host/packs/ProcessPacks.{hpp,cpp}` | the process's table (a backend source) |
| `packages/native/test/packs/fixture/CMakeLists.txt` | the fixture pack and its thirteen variants |
| `packages/native/test/packs/fixture/FixtureNodes.cpp` | `rp.packfixture.tone` and `.gain` on the node SDK |
| `packages/native/test/packs/fixture/FixtureSecond.cpp` | the same source again, the pair variant's second package |
| `packages/native/test/packs/fixture/manifests/*.json` | the fourteen `pack.json` templates |
| `packages/native/test/packs/PackTestSupport.hpp` | `TempDir` (with its sweep), `fixture()`, `install()`, `ScopedEnv` |
| `packages/native/test/packs/ElfSymbols.hpp` | a module's dynamic exports and imports, read from its file (Linux) |
| `packages/native/test/packs/Pack{Roots,Manifest,State,Loader,Discovery,Engines}.test.cpp` | the cases, by layer |
| `packages/native/test/packs/MesenPack.test.cpp` | the Mesen package as a pack, its PCM against the static build |
| `packages/native/test/packs/BoundaryRules.test.cpp` | no RTTI cast in the host's code |
| `packages/mesen/pack.json` | the Mesen pack's manifest template |

Modified: `packages/node-sdk/native/src/graph/rp_node.h` (the old `rp_package_entry` declaration retired),
`packages/graph-runtime/native/src/graph/NodeRegistry.{hpp,cpp}` (`canAdd`),
`packages/native/src/util/FileSync.{hpp,cpp}` (a wide-path overload),
`cmake/packages.cmake` (the reserved entry name), `CMakeLists.txt` (includes `cmake/packs.cmake`), `package.json`
(the `packs-host` list, `test:plugin`), `packages/native/CMakeLists.txt` (`retroplug-packs`, the backend's links),
`packages/native/cmake/tests.cmake` (the fixture, `retroplug-packs-test`), `packages/native/cmake/warnings.cmake`
(`/W4 /WX` on the new targets), `packages/native/src/host/engine/Engine.{hpp,cpp}` (pack registration),
`packages/native/test/engine-graph/golden/product-registrations.json` (one row added),
`packages/retroplug/scripts/run-plugin-tests.mjs`, `tools/run-sanitizer.sh`, `packages/mesen/native/CMakeLists.txt`,
and the docs (`spec/02`, `spec/06`, `spec/07`, `spec/13`, `AGENTS.md`, two CMake comments).

---

### Task 1: The API level and the pack entry

The design's entry is `rp_pack_entry(uint32_t* api_level, size_t* count)`. `rp_node.h` still declares the older,
never-defined `rp_package_entry(void)` as "the symbol a downloaded pack exports"; this task retires that declaration
(its `rp_package_entry_fn` typedef stays: the generated registration tables use it) and reserves the new name.

**Files:**
- Create: `packages/node-sdk/native/src/graph/rp_pack.h`
- Modify: `packages/node-sdk/native/src/graph/rp_node.h` (lines 48-49 and 380-384)
- Modify: `cmake/packages.cmake:321-323`

- [ ] **Step 1: Write the header**

Create `packages/node-sdk/native/src/graph/rp_pack.h`:

```c
/* rp_pack.h - what a pack's native library exports, and the API level it is held to.
 *
 * A pack (docs/downloadable-packs-loading-design.md section 2) is a folder a RetroPlug loads at run time; its native
 * half is one dynamic library exporting rp_pack_entry and nothing else. The entry answers the API level the library
 * was built against and the same rp_package list a built-in package's generated table holds (graph/Packages.hpp).
 * The loader reads the level first and uses nothing else from a library built at any other level, so this header's
 * shape is the one thing that may never change: a new field is a new header, not an edit to this one.
 */
#ifndef RP_PACK_H
#define RP_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "graph/rp_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The API level: one integer, bumped by hand in any commit that breaks packs (the C ABI in rp_node.h, SystemBase and
 * the contract headers, the TS contract modules). A pack records the level it was built against, in its pack.json and
 * from its entry, and is refused at any other. cmake/packs.cmake reads this line to stamp a pack's pack.json, and M2's
 * TS halves read it the same way (the generator, packages/retroplug/scripts/lib/packages.mjs, writing it into a
 * generated module), so it stays a plain decimal with a `u` suffix on a line of its own (both readers parse that line)
 * and there is one source for both languages. */
#define RP_API_LEVEL 1u

/* The symbol a pack's library exports (an rp_pack_entry_fn). */
#define RP_PACK_ENTRY_SYMBOL "rp_pack_entry"

/* [control] Writes the level the library was built against to *api_level and the package count to *count, and returns
 * the packages, valid for the life of the process (a pack's library is never unloaded). The loader always passes
 * non-NULL api_level and count; the entry writes both. When it returns NULL or *count is 0 the pack offers nothing and
 * the returned pointer is not read. It must not throw: no exception crosses this entry. */
typedef const rp_package* const* (*rp_pack_entry_fn)(uint32_t* api_level, size_t* count);
RP_EXPORT const rp_package* const* rp_pack_entry(uint32_t* api_level, size_t* count);

#ifdef __cplusplus
}
#endif

#endif /* RP_PACK_H */
```

- [ ] **Step 2: Retire the old declaration in rp_node.h**

In `packages/node-sdk/native/src/graph/rp_node.h`, replace

```c
/* The symbol a downloaded pack exports (an rp_package_entry_fn), and the visibility it needs. */
#define RP_PACKAGE_ENTRY_SYMBOL "rp_package_entry"
#ifndef RP_EXPORT
```

with

```c
/* The visibility a pack's one exported symbol needs (graph/rp_pack.h's rp_pack_entry). */
#ifndef RP_EXPORT
```

and replace

```c
/* A downloaded pack exports this symbol; statically linked packages hand the same struct to the
 * generated registration list. Declared here, inside the header's C linkage, so a C++ pack that defines
 * it cannot forget extern "C". */
typedef const rp_package* (*rp_package_entry_fn)(void);
RP_EXPORT const rp_package* rp_package_entry(void);
```

with

```c
/* A statically linked package's entry, which the generated registration list calls (graph/Packages.hpp). A pack's
 * library exports rp_pack_entry instead (graph/rp_pack.h), which returns a list of these packages. */
typedef const rp_package* (*rp_package_entry_fn)(void);
```

- [ ] **Step 3: Reserve the new name for packs**

In `cmake/packages.cmake` (inside `_retroplug_read_manifest`), replace

```cmake
        # rp_package_entry is a downloaded pack's, never a linked package's.
        if(_entry IN_LIST _taken OR _entry STREQUAL "rp_package_entry")
```

with

```cmake
        # rp_pack_entry is a pack library's export (graph/rp_pack.h), never a linked package's.
        if(_entry IN_LIST _taken OR _entry STREQUAL "rp_pack_entry")
```

- [ ] **Step 4: Verify the header is C, nothing names the old symbol, and the tree builds**

Run:

```bash
gcc -std=c11 -Wall -Wextra -fsyntax-only -x c -I packages/node-sdk/native/src \
    packages/node-sdk/native/src/graph/rp_pack.h
```

Expected: no output, exit 0.

Run: `git grep -n "rp_package_entry\b\|RP_PACKAGE_ENTRY_SYMBOL" -- ':!docs'`
Expected: no output (exit 1): only plan records under `docs/` still mention them.

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: ends with `[100%] Built target retroplug-engine-graph-test`.

- [ ] **Step 5: Commit**

```bash
git add packages/node-sdk/native/src/graph/rp_pack.h packages/node-sdk/native/src/graph/rp_node.h cmake/packages.cmake
git commit -m "node-sdk: the API level and rp_pack_entry, a pack library's one export"
```

---

### Task 2: retroplug_pack_module and the fixture pack

`cmake/packs.cmake` reads the level and the platform for CMake and defines `retroplug_pack_module()`: a MODULE built
from static targets and a generated entry, every symbol local but the entry's, staged at `build/packs/<id>/<version>/`.
The fixture pack and its thirteen variants (one more good one, the dependent, and twelve broken in one way each) are
its first users. Static libraries linked into a module must be
position-independent: every package library already is (`retroplug_package_library` sets it) and so is every vendored
library a package links (the plugin, a shared module, links them all), so the function checks the link closure at
configure and refuses one that is not, naming it.

**Files:**
- Create: `cmake/packs.cmake`
- Create: `packages/native/test/packs/fixture/FixtureNodes.cpp`, `packages/native/test/packs/fixture/FixtureSecond.cpp`
- Create: `packages/native/test/packs/fixture/CMakeLists.txt`
- Create: `packages/native/test/packs/fixture/throws/CMakeLists.txt`, `.../fixture/throws/FixtureThrows.cpp`
- Create: `packages/native/test/packs/fixture/manifests/*.json` (fourteen, by the script in Step 4)
- Modify: `CMakeLists.txt:310` (include the new file)
- Modify: `packages/native/cmake/tests.cmake` (add the fixture's directory)

- [ ] **Step 1: Write cmake/packs.cmake**

Create `cmake/packs.cmake` (Task 10 appends `retroplug_pack()` to it):

```cmake
# Packs: a package's native half built to load at run time (docs/downloadable-packs-loading-design.md section 2), beside
# the static build every product links. The root CMakeLists.txt includes it after cmake/packages.cmake and before the
# first retroplug_package(), so a package's own native/CMakeLists.txt can call retroplug_pack() once its targets exist.
#
# A pack module is a MODULE library: the package's static targets linked in privately with every static dependency
# they carry (its own copies of node-sdk, system, reflect-cpp, a vendored core), plus a generated entry translation unit
# defining rp_pack_entry (graph/rp_pack.h). Every symbol but the entry is local to the module: a version script on
# Linux, an exported-symbols list on macOS, and on Windows a DLL exports only what is dllexport'd, which is the entry
# alone. So two packs' copies of a library, or a pack's and the host's, never bind to each other; and Linux links it
# with --no-undefined, so a module that imports a symbol from RetroPlug (rather than reaching the host through rp_host
# interfaces and vtables) fails to link instead of failing to load.
#
# Static libraries linked into a module must be position-independent. Every package library is
# (retroplug_package_library), and so is every vendored library a package links today, because the plugin, itself a
# shared module, links them all; _retroplug_pack_check_pic refuses at configure any static library in a module's link
# closure that does not say so, naming it, rather than leaving a relocation error to the linker.
#
# The module is staged where a development pack is loaded from (a version folder's layout):
#
#   build/packs/<id>/<version>/pack.json                      the package's manifest template, apiLevel stamped
#   build/packs/<id>/<version>/native/<platform>/<stem><ext>  the module (halves.native names the stem)

# The API level, read from the header a pack's entry compiles against, so the level a pack.json is stamped with and the
# level its entry reports cannot disagree.
set(_rp_pack_header "${CMAKE_CURRENT_LIST_DIR}/../packages/node-sdk/native/src/graph/rp_pack.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rp_pack_header}")
file(STRINGS "${_rp_pack_header}" _rp_pack_level_line REGEX "^#define RP_API_LEVEL [0-9]+u$")
string(REGEX REPLACE "^#define RP_API_LEVEL ([0-9]+)u$" "\\1" RP_API_LEVEL "${_rp_pack_level_line}")
if(NOT RP_API_LEVEL MATCHES "^[0-9]+$")
    message(FATAL_ERROR "cmake/packs.cmake: ${_rp_pack_header} has no `#define RP_API_LEVEL <n>u` line")
endif()

# The platform folder this build's modules go in, as host/packs/PackDirs.cpp's currentPlatform() names it (the packs
# test holds the two to each other). Empty on a platform no pack is built for.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(RP_PACK_PLATFORM "windows-x64")
elseif(APPLE)
    set(RP_PACK_PLATFORM "macos-universal")
    if(NOT "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES OR NOT "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
        message(WARNING "cmake/packs.cmake: macOS pack modules go in a folder named macos-universal, but "
                        "CMAKE_OSX_ARCHITECTURES (${CMAKE_OSX_ARCHITECTURES}) is not both arm64 and x86_64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(RP_PACK_PLATFORM "linux-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(RP_PACK_PLATFORM "linux-arm64")
else()
    set(RP_PACK_PLATFORM "")
endif()
set(RP_PACKS_DIR "${CMAKE_BINARY_DIR}/packs")

# _retroplug_pack_check_pic(<module> <target>...): every static library the targets link, directly or through
# INTERFACE_LINK_LIBRARIES, is position-independent. An imported or interface target is not judged (it has no objects
# of ours), nor is a plain library name or path. retroplug_pack_module defers the call to the end of the configure, so
# a library defined after the module (or given its PIC property late) is judged as the build will see it.
function(_retroplug_pack_check_pic module)
    set(_queue ${ARGN})
    set(_seen "")
    while(_queue)
        list(POP_FRONT _queue _item)
        # $<LINK_ONLY:x>, $<BUILD_INTERFACE:x> and their kin name x; a configuration expression names nothing here.
        string(REGEX REPLACE "^\\$<(LINK_ONLY|BUILD_INTERFACE|BUILD_LOCAL_INTERFACE):(.*)>$" "\\2" _item "${_item}")
        if(_item MATCHES "^\\$<" OR NOT TARGET "${_item}" OR _item IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_item}")
        get_target_property(_aliased "${_item}" ALIASED_TARGET)
        if(_aliased)
            list(APPEND _queue "${_aliased}")
            continue()
        endif()
        get_target_property(_imported "${_item}" IMPORTED)
        if(_imported)
            continue()
        endif()
        get_target_property(_type "${_item}" TYPE)
        if(_type STREQUAL "STATIC_LIBRARY" OR _type STREQUAL "OBJECT_LIBRARY")
            get_target_property(_pic "${_item}" POSITION_INDEPENDENT_CODE)
            if(NOT _pic)
                message(FATAL_ERROR "retroplug_pack_module: ${module} links the static or object library "
                                    "${_item}, which is not position-independent: set its POSITION_INDEPENDENT_CODE "
                                    "ON (a pack module is a shared object, and a static or object library linked "
                                    "into one must be built -fPIC)")
            endif()
        endif()
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_links "${_item}" LINK_LIBRARIES)
            if(_links)
                list(APPEND _queue ${_links})
            endif()
        endif()
        get_target_property(_iface "${_item}" INTERFACE_LINK_LIBRARIES)
        if(_iface)
            list(APPEND _queue ${_iface})
        endif()
    endwhile()
endfunction()

# retroplug_pack_module(<target> MANIFEST <pack.json template> [LIBS <target>...] [SOURCES <file>...]
#                       [DEFINES <name=value>...] [ENTRIES <C function>...] [API_LEVEL <n>] [ENTRY_API_LEVEL <n>]
#                       [NO_ENTRY] [THROWING_ENTRY] [NO_LIBRARY])
#
# Stages build/packs/<id>/<version>/pack.json from MANIFEST (its `id`, `version` and `halves.native` read from it, its
# `apiLevel` set to API_LEVEL, RP_API_LEVEL by default), and defines <target>, the MODULE library built from SOURCES and
# LIBS with an entry returning ENTRIES' rp_packages in order and reporting ENTRY_API_LEVEL (API_LEVEL by default).
# EXCLUDE_FROM_ALL: what loads a pack depends on its target. The fixture pack's broken variants use the rest:
# NO_ENTRY builds a module whose one export is rp_pack_entry_withheld (an exported-symbols list is never empty, which
# macOS's linker would refuse); THROWING_ENTRY one whose entry first calls rp_pack_entry_prelude(), a C++ function the
# variant's SOURCES define (and throw from: a throw written in the extern "C", dllexport'ed entry itself is MSVC's
# C4297); and NO_LIBRARY stages the manifest alone, defining no target.
function(retroplug_pack_module target)
    cmake_parse_arguments(M "NO_ENTRY;THROWING_ENTRY;NO_LIBRARY" "MANIFEST;API_LEVEL;ENTRY_API_LEVEL"
                          "LIBS;SOURCES;DEFINES;ENTRIES" ${ARGN})
    if(M_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_pack_module(${target}): unrecognised argument(s): ${M_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT M_MANIFEST OR NOT EXISTS "${M_MANIFEST}")
        message(FATAL_ERROR "retroplug_pack_module(${target}): MANIFEST names no file (${M_MANIFEST})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${M_MANIFEST}")
    file(READ "${M_MANIFEST}" _json)
    foreach(_member id version)
        string(JSON _${_member} ERROR_VARIABLE _err GET "${_json}" ${_member})
        if(_err OR NOT _${_member} MATCHES "^[a-z0-9][a-z0-9.-]*$")
            message(FATAL_ERROR "retroplug_pack_module(${target}): ${M_MANIFEST} has no plain `${_member}`")
        endif()
    endforeach()
    if(NOT DEFINED M_API_LEVEL)
        set(M_API_LEVEL ${RP_API_LEVEL})
    endif()
    if(NOT DEFINED M_ENTRY_API_LEVEL)
        set(M_ENTRY_API_LEVEL ${M_API_LEVEL})
    endif()
    string(JSON _json SET "${_json}" apiLevel ${M_API_LEVEL})
    set(_dir "${RP_PACKS_DIR}/${_id}/${_version}")
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${_id}/${_version}" IN_LIST _staged)
        message(FATAL_ERROR "retroplug_pack_module(${target}): ${_id}/${_version} is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_id}/${_version}")
    # Written at configure, and copied only when its content changed, so a pack's folder exists before anything builds
    # and a reconfigure leaves an unchanged manifest's timestamp alone.
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json" "${_json}\n")
    file(COPY_FILE "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.pack.json" "${_dir}/pack.json" ONLY_IF_DIFFERENT)
    if(M_NO_LIBRARY)
        return()
    endif()
    string(JSON _stem ERROR_VARIABLE _err GET "${_json}" halves native)
    if(_err OR NOT _stem MATCHES "^[a-z][a-z0-9-]*$")
        message(FATAL_ERROR "retroplug_pack_module(${target}): ${M_MANIFEST} has no `halves.native` library stem")
    endif()
    if(NOT RP_PACK_PLATFORM)
        message(FATAL_ERROR "retroplug_pack_module(${target}): this platform (${CMAKE_SYSTEM_NAME} "
                            "${CMAKE_SYSTEM_PROCESSOR}) has no pack platform folder")
    endif()
    if(NOT M_ENTRIES AND NOT M_NO_ENTRY)
        message(FATAL_ERROR "retroplug_pack_module(${target}): ENTRIES names no package entry")
    endif()

    # The entry, generated as a product's registration is: the packages in the order ENTRIES names them.
    set(_decls "")
    set(_calls "")
    list(LENGTH M_ENTRIES _count)
    foreach(_e IN LISTS M_ENTRIES)
        string(APPEND _decls "extern \"C\" const rp_package* ${_e}(void);\n")
        string(APPEND _calls "        ${_e}(),\n")
    endforeach()
    set(_name "rp_pack_entry")
    set(_linkage "")
    if(M_NO_ENTRY)
        # The same body under another name, exported in the entry's place: a module that links and has no entry.
        set(_name "rp_pack_entry_withheld")
        set(_linkage "RP_EXPORT ")
    endif()
    set(_throw "")
    set(_prelude "")
    if(M_THROWING_ENTRY)
        # Defined in another translation unit, so the compiler cannot see that it always throws: the rest of the body
        # stays reachable (MSVC's C4702 is an error under /WX).
        set(_prelude "void rp_pack_entry_prelude();\n")
        set(_throw "    rp_pack_entry_prelude();\n")
    endif()
    # A zero-length array is ill-formed C++ (MSVC C2466), so a module with no entries (NO_ENTRY alone) returns none.
    if(_count EQUAL 0)
        set(_body "    *count = 0;\n    return nullptr;\n")
    else()
        set(_body "    static const rp_package* const packages[] = {\n${_calls}    };\n")
        string(APPEND _body "    *count = ${_count};\n    return packages;\n")
    endif()
    set(_entry "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}-entry.cpp")
    file(GENERATE OUTPUT "${_entry}" CONTENT
"// Generated by retroplug_pack_module(${target}) in cmake/packs.cmake from ${M_MANIFEST}. Do not edit.

#include \"graph/rp_pack.h\"

${_decls}${_prelude}
extern \"C\" ${_linkage}const rp_package* const* ${_name}(uint32_t* api_level, size_t* count) {
${_throw}    *api_level = ${M_ENTRY_API_LEVEL}u;
${_body}}
")
    add_library(${target} MODULE EXCLUDE_FROM_ALL "${_entry}" ${M_SOURCES})
    target_link_libraries(${target} PRIVATE retroplug-node-sdk-headers ${M_LIBS})
    if(M_DEFINES)
        target_compile_definitions(${target} PRIVATE ${M_DEFINES})
    endif()
    if(WIN32)
        set(_suffix ".dll")
    elseif(APPLE)
        set(_suffix ".dylib")
    else()
        set(_suffix ".so")
    endif()
    # $<1:...> keeps a multi-config generator from appending a per-configuration folder: the loader looks in
    # native/<platform>/ and nowhere else.
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        PREFIX ""
        OUTPUT_NAME "${_stem}"
        SUFFIX "${_suffix}"
        LIBRARY_OUTPUT_DIRECTORY "$<1:${_dir}/native/${RP_PACK_PLATFORM}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${_dir}/native/${RP_PACK_PLATFORM}>"
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        RP_PACK_DIR "${_dir}")
    if(APPLE)
        set(_exports "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.exports")
        file(CONFIGURE OUTPUT "${_exports}" CONTENT "_${_name}\n")
        target_link_options(${target} PRIVATE "LINKER:-exported_symbols_list,${_exports}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_exports}")
    elseif(NOT WIN32)
        set(_script "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.map")
        file(CONFIGURE OUTPUT "${_script}" CONTENT "{ global: ${_name}; local: *; };\n")
        target_link_options(${target} PRIVATE "LINKER:--version-script=${_script}" "LINKER:--no-undefined")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_script}")
    endif()
    # EVAL expands the variables now: a deferred call's arguments would be read at the end of the configure, when this
    # function's variables are gone.
    cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] "
                             "CALL _retroplug_pack_check_pic ${target} ${M_LIBS})")
    set_property(GLOBAL APPEND PROPERTY RP_PACK_TARGETS ${target})
endfunction()
```

- [ ] **Step 2: Include it from the root**

In `CMakeLists.txt`, directly after the line `include(cmake/packages.cmake)` (there is one), insert:

```cmake
# A package's native half built as a pack too (retroplug_pack), staged under build/packs/.
include(cmake/packs.cmake)
```

- [ ] **Step 3: Write the fixture's node source**

Create `packages/native/test/packs/fixture/FixtureNodes.cpp`:

```cpp
// The fixture pack's node types (packages/native/test/packs/fixture/CMakeLists.txt): a constant tone and a gain, built
// on the node SDK as any C++ package's are. The broken variants compile this same file with other definitions, so
// each differs from the good pack in exactly the way its manifest or its CMake says.
//
//   RP_PACKFIXTURE_PREFIX      the type ids' prefix ("rp.packfixture": rp.packfixture.tone, rp.packfixture.gain)
//   RP_PACKFIXTURE_PACKAGE     the rp_package's id ("rp.packfixture")
//   RP_PACKFIXTURE_TONE_ID     the tone's id in place of PREFIX.tone (the duplicate variant takes a built-in's)
//   RP_PACKFIXTURE_EXTRA_TYPE  defined: a third type, PREFIX.extra, which the undeclared variant's manifest leaves out
//   RP_PACKFIXTURE_ENTRY       the C entry's name (rp_packfixture_package): FixtureSecond.cpp compiles this file again,
//                              under another prefix and entry, as the pair variant's second package
//
// The types are in an unnamed namespace, so two translation units of this file in one module are two sets of types.

#include <cstdint>
#include <iterator>

#include "graph/node.hpp"

#ifndef RP_PACKFIXTURE_PREFIX
#define RP_PACKFIXTURE_PREFIX "rp.packfixture"
#endif
#ifndef RP_PACKFIXTURE_PACKAGE
#define RP_PACKFIXTURE_PACKAGE "rp.packfixture"
#endif
#ifndef RP_PACKFIXTURE_TONE_ID
#define RP_PACKFIXTURE_TONE_ID RP_PACKFIXTURE_PREFIX ".tone"
#endif
#ifndef RP_PACKFIXTURE_ENTRY
#define RP_PACKFIXTURE_ENTRY rp_packfixture_package
#endif

namespace rp::packfixture {

// Outside the unnamed namespace: a settings struct is named for its schema (graph/node.hpp), and the two translation
// units of the pair variant define it identically.
struct ToneSettings {
    float level = 0.25f;  // every sample of both channels
};

namespace {

// A constant on both channels: what a test reads back to know the pack's code ran.
struct Tone {
    static constexpr const char* id = RP_PACKFIXTURE_TONE_ID;
    using Settings = ToneSettings;
    struct { rp::audio_out<rp::stereo> out; } outputs;

    float level = 0.25f;
    void on_settings(const Settings& s) noexcept { level = s.level; }
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = level;
    }
};

// Its input scaled by its param.
struct Gain {
    static constexpr const char* id = RP_PACKFIXTURE_PREFIX ".gain";
    struct { rp::audio_in<rp::stereo> in; } inputs;
    struct { rp::audio_out<rp::stereo> out; } outputs;
    struct { rp::float_param<"Gain", rp::range{0, 4, 1}> gain; } params;

    void process(const rp::Block& b) noexcept {
        const float g = static_cast<float>(params.gain.latest());
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = inputs.in[c][i] * g;
    }
};

#ifdef RP_PACKFIXTURE_EXTRA_TYPE
struct Extra {
    static constexpr const char* id = RP_PACKFIXTURE_PREFIX ".extra";
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};
#endif

const rp_package* package() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Tone>(),
        rp::node_type<Gain>(),
#ifdef RP_PACKFIXTURE_EXTRA_TYPE
        rp::node_type<Extra>(),
#endif
    };
    static const rp_package kPackage{RP_ABI_VERSION, "{\"id\":\"" RP_PACKFIXTURE_PACKAGE "\",\"version\":\"1.0.0\"}",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace

}  // namespace rp::packfixture

extern "C" const rp_package* RP_PACKFIXTURE_ENTRY(void) { return rp::packfixture::package(); }
```

Create `packages/native/test/packs/fixture/FixtureSecond.cpp` (the pair variant's second package):

```cpp
// The pair variant's second package (CMakeLists.txt's packfixture-pair): FixtureNodes.cpp compiled again in the same
// module under its own prefix, package id and entry, so the pack's library returns two packages. Its prefix is the
// good pack's, rp.packfixture, whose types this test binary also links statically: an Engine given that copy holds the
// second package's types and must keep the whole pair out, the first package with it.
#undef RP_PACKFIXTURE_PREFIX
#undef RP_PACKFIXTURE_PACKAGE
#undef RP_PACKFIXTURE_ENTRY
#define RP_PACKFIXTURE_PREFIX "rp.packfixture"
#define RP_PACKFIXTURE_PACKAGE "rp.packfixture-pair-second"
#define RP_PACKFIXTURE_ENTRY rp_packfixture_second_package
#include "FixtureNodes.cpp"
```

- [ ] **Step 4: Write the fourteen manifest templates**

Run this from the repository root; it writes each manifest as `json.dumps(indent=2)` formats it:

```bash
mkdir -p packages/native/test/packs/fixture/manifests
python3 - <<'EOF'
import json

VARIANTS = {
    # id: (node types it declares, packs it requires, halves beside the native one)
    "packfixture": (["rp.packfixture.tone", "rp.packfixture.gain"], [], {}),
    "packfixture-dependent": (["rp.packfixture-dependent.tone", "rp.packfixture-dependent.gain"], ["packfixture"], {}),
    "packfixture-api-newer": (["rp.packfixture-api-newer.tone", "rp.packfixture-api-newer.gain"], [], {}),
    "packfixture-api-older": (["rp.packfixture-api-older.tone", "rp.packfixture-api-older.gain"], [], {}),
    "packfixture-api-mismatch": (["rp.packfixture-api-mismatch.tone", "rp.packfixture-api-mismatch.gain"], [], {}),
    "packfixture-undeclared": (["rp.packfixture-undeclared.tone", "rp.packfixture-undeclared.gain"], [], {}),
    "packfixture-overdeclared": (["rp.packfixture-overdeclared.tone", "rp.packfixture-overdeclared.gain",
                                  "rp.packfixture-overdeclared.missing"], [], {}),
    "packfixture-duplicate": (["rp.core.add", "rp.packfixture-duplicate.gain"], [], {}),
    "packfixture-clone": (["rp.packfixture.tone", "rp.packfixture.gain"], [], {}),
    "packfixture-noplatform": (["rp.packfixture-noplatform.tone", "rp.packfixture-noplatform.gain"], [], {}),
    "packfixture-noentry": (["rp.packfixture-noentry.tone", "rp.packfixture-noentry.gain"], [], {}),
    "packfixture-throws": (["rp.packfixture-throws.tone", "rp.packfixture-throws.gain"], [], {}),
    "packfixture-pair": (["rp.packfixture-pair.tone", "rp.packfixture-pair.gain", "rp.packfixture.tone",
                          "rp.packfixture.gain"], [], {}),
    "packfixture-tshalf": (["rp.packfixture-tshalf.tone", "rp.packfixture-tshalf.gain"], [], {"app": "js/app.js"}),
}
for pack_id, (types, needs, halves) in VARIANTS.items():
    manifest = {
        "id": pack_id,
        "name": f"Pack fixture ({pack_id})",
        "version": "1.0.0",
        "license": "MIT",
        "source": "packages/native/test/packs/fixture",
        "requires": {"packs": needs, "platforms": []},
        "provides": {"platforms": [], "nodeTypes": types, "roles": []},
        "halves": {"native": f"rp-{pack_id}", **halves},
    }
    with open(f"packages/native/test/packs/fixture/manifests/{pack_id}.json", "w") as f:
        f.write(json.dumps(manifest, indent=2) + "\n")
EOF
```

Expected: fourteen files in `packages/native/test/packs/fixture/manifests/`. None carries `apiLevel`: the configure
stamps it.

- [ ] **Step 5: Write the fixture's CMakeLists**

Create `packages/native/test/packs/fixture/CMakeLists.txt`:

```cmake
# The fixture pack (docs/downloadable-packs-m1-plan.md): a pack built in this tree, from FixtureNodes.cpp, and its
# deliberately broken variants, each built from the same source and differing from the good pack in one way. Each is
# staged under build/packs/<id>/1.0.0/ (cmake/packs.cmake), and retroplug-packs-test loads them from there. No product
# lists any of them: a pack is loaded, never linked.
#
#   packfixture               the good pack: rp.packfixture.tone and .gain, over a static library as a package's are
#   packfixture-dependent     requires.packs ["packfixture"]: loads after it, refused without it
#   packfixture-api-newer     built for the next API level: refused, "update RetroPlug"
#   packfixture-api-older     built for the previous one: refused, "update the pack"
#   packfixture-api-mismatch  its pack.json says this level, its library reports the next: refused by the entry
#   packfixture-undeclared    registers a third type its provides.nodeTypes leaves out
#   packfixture-overdeclared  declares a third type in provides.nodeTypes that its library does not register
#   packfixture-throws        a library whose entry throws (throws/: its own directory, for MSVC's /EHs)
#   packfixture-pair          two packages from one library (FixtureSecond.cpp), the second taking the good pack's types
#   packfixture-tshalf        a manifest with a TS half beside its native one, which M1 does not load
#   packfixture-duplicate     registers rp.core.add, a built-in's type
#   packfixture-clone         registers the good pack's two types under another package id
#   packfixture-noplatform    a manifest with no library for any platform
#   packfixture-noentry       a library that exports no rp_pack_entry

set(_rp_fixture_dir ${CMAKE_CURRENT_SOURCE_DIR})
set(_rp_fixture_source ${_rp_fixture_dir}/FixtureNodes.cpp)

# The good pack's types as a static library, as a package's native target is: its module links it, and
# retroplug-packs-test links it too, to hold two copies of one package in one process (the module's stays hidden).
retroplug_package_library(retroplug-packfixture-nodes ${_rp_fixture_source})
target_link_libraries(retroplug-packfixture-nodes PUBLIC retroplug-node-sdk)

retroplug_pack_module(retroplug-packfixture-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture.json
    LIBS     retroplug-packfixture-nodes
    ENTRIES  rp_packfixture_package)

# A variant: the fixture source compiled straight into its module, its types (PREFIX) and its package (PACKAGE) named
# after the variant unless it says otherwise. PAIR adds FixtureSecond.cpp's package after the first; SOURCES adds files
# of the variant's own.
function(_rp_fixture_variant id)
    cmake_parse_arguments(V "PAIR" "PREFIX;PACKAGE" "DEFINES;ARGS;SOURCES" ${ARGN})
    if(NOT V_PREFIX)
        set(V_PREFIX "rp.${id}")
    endif()
    if(NOT V_PACKAGE)
        set(V_PACKAGE "rp.${id}")
    endif()
    set(_sources ${_rp_fixture_source} ${V_SOURCES})
    set(_entries rp_packfixture_package)
    if(V_PAIR)
        list(APPEND _sources ${_rp_fixture_dir}/FixtureSecond.cpp)
        list(APPEND _entries rp_packfixture_second_package)
    endif()
    retroplug_pack_module(retroplug-${id}-pack
        MANIFEST ${_rp_fixture_dir}/manifests/${id}.json
        SOURCES  ${_sources}
        LIBS     retroplug-node-sdk
        DEFINES  RP_PACKFIXTURE_PREFIX="${V_PREFIX}" RP_PACKFIXTURE_PACKAGE="${V_PACKAGE}" ${V_DEFINES}
        ENTRIES  ${_entries}
        ${V_ARGS})
    set_property(GLOBAL APPEND PROPERTY RP_PACKFIXTURE_TARGETS retroplug-${id}-pack)
endfunction()

math(EXPR _rp_level_newer "${RP_API_LEVEL} + 1")
math(EXPR _rp_level_older "${RP_API_LEVEL} - 1")
set_property(GLOBAL PROPERTY RP_PACKFIXTURE_TARGETS retroplug-packfixture-pack)
_rp_fixture_variant(packfixture-dependent)
_rp_fixture_variant(packfixture-api-newer ARGS API_LEVEL ${_rp_level_newer})
_rp_fixture_variant(packfixture-api-older ARGS API_LEVEL ${_rp_level_older})
_rp_fixture_variant(packfixture-api-mismatch ARGS ENTRY_API_LEVEL ${_rp_level_newer})
_rp_fixture_variant(packfixture-undeclared DEFINES RP_PACKFIXTURE_EXTRA_TYPE)
_rp_fixture_variant(packfixture-duplicate DEFINES RP_PACKFIXTURE_TONE_ID="rp.core.add")
_rp_fixture_variant(packfixture-clone PREFIX rp.packfixture)
_rp_fixture_variant(packfixture-noentry ARGS NO_ENTRY)
_rp_fixture_variant(packfixture-overdeclared)
add_subdirectory(throws)
_rp_fixture_variant(packfixture-pair PAIR)
# The manifests alone: retroplug_pack_module defines no target for them.
retroplug_pack_module(retroplug-packfixture-noplatform-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture-noplatform.json
    NO_LIBRARY)
retroplug_pack_module(retroplug-packfixture-tshalf-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture-tshalf.json
    NO_LIBRARY)
```

The throwing variant lives in a directory of its own, so MSVC compiles it with `/EHs` (the entry is then not assumed
never to throw, and the exception unwinds through it to the loader); its throw is in a C++ function the generated
`extern "C"` entry calls, since a throw written in an `extern "C"` `dllexport` function is MSVC's C4297, an error under
`/WX`. Create `packages/native/test/packs/fixture/throws/CMakeLists.txt`:

```cmake
# The throwing variant, packfixture-throws, in a directory of its own (the parent's _rp_fixture_variant does the rest).
# Its generated extern "C" entry calls FixtureThrows.cpp's prelude, which throws. MSVC compiles this directory with
# /EHs rather than the tree's /EHsc, so the entry is not assumed never to throw and the exception unwinds through it
# to the loader's handler: a per-source /EHs would be a second /EH option on one command line, D9025, which cannot be
# suppressed, and a directory's CMAKE_CXX_FLAGS is its own.
if(MSVC)
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    # The variant's point is an entry that may throw: without /EHs it would quietly stop being one.
    if(NOT CMAKE_CXX_FLAGS MATCHES "/EHs([ ]|$)")
        message(FATAL_ERROR "packfixture-throws: CMAKE_CXX_FLAGS has no /EHs (${CMAKE_CXX_FLAGS})")
    endif()
endif()
_rp_fixture_variant(packfixture-throws SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/FixtureThrows.cpp ARGS THROWING_ENTRY)
```

Create `packages/native/test/packs/fixture/throws/FixtureThrows.cpp`:

```cpp
// The throwing variant's prelude (throws/CMakeLists.txt): the C++ function its generated extern "C" entry calls first
// (cmake/packs.cmake's THROWING_ENTRY). The throw is the fixture's whole point: the loader must catch it on its side of
// the C entry and refuse the pack. It is thrown here, with C++ linkage, because a throw written in an extern "C"
// dllexport'ed function is MSVC's C4297.

#include <stdexcept>

void rp_pack_entry_prelude();

void rp_pack_entry_prelude() { throw std::runtime_error("this pack's entry throws"); }
```

- [ ] **Step 6: Add the fixture to the tests**

In `packages/native/cmake/tests.cmake`, after the two `retroplug_providers_header(...)` calls that follow
`add_dependencies(retroplug-engine-graph-test retroplug-script-test-bundle)`, that is after the lines

```cmake
                           "packages/native/cmake/tests.cmake (a header naming the launchpad provider)"
                           launchpad)
```

insert

```cmake

# The fixture pack and its broken variants (test/packs/fixture): modules nothing links, staged under build/packs/.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test/packs/fixture ${CMAKE_BINARY_DIR}/packs-fixture EXCLUDE_FROM_ALL)
```

- [ ] **Step 7: Configure and build the modules**

Run: `pnpm configure`
Expected: ends with `-- Build files have been written to: .../build`, and `ls build/packs` lists the fourteen
fixture ids.

Run:

```bash
cmake --build build -j$(nproc) --target retroplug-packfixture-pack retroplug-packfixture-dependent-pack \
    retroplug-packfixture-api-newer-pack retroplug-packfixture-api-older-pack retroplug-packfixture-api-mismatch-pack \
    retroplug-packfixture-undeclared-pack retroplug-packfixture-duplicate-pack retroplug-packfixture-clone-pack \
    retroplug-packfixture-noentry-pack retroplug-packfixture-overdeclared-pack retroplug-packfixture-throws-pack \
    retroplug-packfixture-pair-pack
```

Expected: ends with `Built target retroplug-packfixture-pair-pack`, no warnings.

- [ ] **Step 8: Verify the staging and the exports (Linux)**

Run: `nm -D --defined-only build/packs/packfixture/1.0.0/native/linux-x64/rp-packfixture.so`
Expected: exactly one line, ending ` T rp_pack_entry`.

Run: `nm -D --defined-only build/packs/packfixture-noentry/1.0.0/native/linux-x64/rp-packfixture-noentry.so`
Expected: exactly one line, ending ` T rp_pack_entry_withheld` (an export list is never empty: macOS's linker refuses
an empty one). Task 6 reads both files' symbols in a test; this is the first look.

Run: `ls build/packs/packfixture-noplatform/1.0.0 && grep apiLevel build/packs/packfixture-api-newer/1.0.0/pack.json`
Expected: `pack.json` alone, then `  "apiLevel" : 2,` (one past `RP_API_LEVEL`).

- [ ] **Step 9: Commit**

```bash
git add cmake/packs.cmake CMakeLists.txt packages/native/cmake/tests.cmake \
    packages/native/test/packs/fixture/CMakeLists.txt packages/native/test/packs/fixture/FixtureNodes.cpp \
    packages/native/test/packs/fixture/FixtureSecond.cpp packages/native/test/packs/fixture/throws \
    packages/native/test/packs/fixture/manifests
git commit -m "cmake: retroplug_pack_module builds a pack module, and the fixture pack with its broken variants"
```

---

### Task 3: The packs-host product, retroplug-packs and its test binary

The test binary is a product (it builds Engines from Task 8 on), and its product must be a root list. `packs-host` is
the plugin's list without `mesen` and `bliptoaster`: Task 10 needs an Engine whose product has no `rp.mesen.*`, and M1
needs no audio backend, MIDI port or Launchpad, so the binary links no PortAudio, SDL audio or RtMidi. A new product
adds one row to the registration golden; the dump re-record rewrites the whole file and drops the opt-in
`retroplug-node` row, so the row is inserted by script instead, and no existing row moves.

**Files:**
- Modify: `package.json` (the `packs-host` list; `test:plugin`)
- Modify: `packages/native/CMakeLists.txt` (`retroplug-packs`; the backend links it)
- Create: `packages/native/src/host/packs/PackDirs.hpp`, `packages/native/src/host/packs/PackDirs.cpp`
- Create: `packages/native/test/packs/PackTestSupport.hpp`, `packages/native/test/packs/PackRoots.test.cpp`
- Modify: `packages/native/cmake/tests.cmake` (the test binary)
- Modify: `packages/native/test/engine-graph/golden/product-registrations.json` (one row)
- Modify: `packages/retroplug/scripts/run-plugin-tests.mjs`, `packages/native/cmake/warnings.cmake`

- [ ] **Step 1: Add the product list**

In the root `package.json`, replace

```json
      "plugin": ["core", "system", "sameboy", "dsp", "mgb", "lsdj", "graph-runtime", "n8-wire", "n8", "mesen",
                 "smsggdj", "risa", "host-io", "native", "bliptoaster"]
```

with

```json
      "plugin": ["core", "system", "sameboy", "dsp", "mgb", "lsdj", "graph-runtime", "n8-wire", "n8", "mesen",
                 "smsggdj", "risa", "host-io", "native", "bliptoaster"],
      "packs-host": ["core", "system", "sameboy", "dsp", "mgb", "lsdj", "graph-runtime", "n8-wire", "n8", "smsggdj",
                     "risa", "host-io", "native"]
```

Run: `node packages/retroplug/scripts/lib/product-lists.mjs package.json | tail -1`
Expected: `packs-host=core,system,sameboy,dsp,mgb,lsdj,graph-runtime,n8-wire,n8,smsggdj,risa,host-io,native`

- [ ] **Step 2: Write the test support header**

Create `packages/native/test/packs/PackTestSupport.hpp`:

```cpp
#pragma once

// What retroplug-packs-test's files share: a scratch folder standing in for the user's pack roots, the staged fixture
// packs (build/packs/<id>/1.0.0/, test/packs/fixture/CMakeLists.txt), and an environment variable set for one case.

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "host/packs/PackDirs.hpp"
#include "util/ProcessId.hpp"

namespace rp::host::packs::test {

// A fresh folder under the system's temporary directory, removed with everything in it when the case ends. Never the
// user's own data or cache directory: every case that touches a root touches one of these. Windows cannot delete a
// library a case loaded from one (a pack is never unloaded), so a folder may outlive its process there; the first
// TempDir of each run sweeps the ones whose process is gone.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> serial{0};
        static const bool swept = sweepAbandoned();
        (void)swept;
        const std::string name = std::string(kPrefix) + std::to_string(rp::currentProcessId()) + "-" +
                                 std::to_string(serial.fetch_add(1));
        path_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    PackRoots roots() const { return {path_ / "data", path_ / "cache"}; }

private:
    static constexpr const char* kPrefix = "retroplug-packs-test-";

    // Every retroplug-packs-test-<pid>-<n> folder whose process is no longer alive, removed.
    static bool sweepAbandoned() {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(kPrefix, 0) != 0) continue;
            const std::string rest = name.substr(std::string(kPrefix).size());
            long long pid = 0;
            try {
                pid = std::stoll(rest.substr(0, rest.find('-')));
            } catch (...) {
                continue;
            }
            if (pid == rp::currentProcessId() || rp::processAlive(pid)) continue;
            std::error_code gone;
            std::filesystem::remove_all(entry.path(), gone);
        }
        return true;
    }

    std::filesystem::path path_;
};

// A fixture pack's staged version folder.
inline std::filesystem::path fixture(const std::string& id) {
    return std::filesystem::path(RP_PACKS_BUILD_DIR) / id / "1.0.0";
}

// `id`'s staged folder copied into `root` as an installed pack (<root>/<id>/1.0.0/), its path answered.
inline std::filesystem::path install(const std::filesystem::path& root, const std::string& id,
                                     const std::string& version = "1.0.0") {
    const std::filesystem::path to = root / id / version;
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy(fixture(id), to, std::filesystem::copy_options::recursive);
    return to;
}

// An environment variable set (or unset, for nullopt) until the case ends, then put back as it was.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::optional<std::string> value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        set(value);
    }
    ~ScopedEnv() { set(old_); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void set(const std::optional<std::string>& value) {
#if defined(_WIN32)
        _putenv_s(name_, value ? value->c_str() : "");
#else
        if (value) setenv(name_, value->c_str(), 1);
        else unsetenv(name_);
#endif
    }
    const char*                name_;
    std::optional<std::string> old_;
};

}  // namespace rp::host::packs::test
```

- [ ] **Step 3: Write the failing test**

Create `packages/native/test/packs/PackRoots.test.cpp`:

```cpp
// Where packs live (docs/downloadable-packs-loading-design.md section 1): each platform's data and cache roots and
// their overrides, the platform folder a pack's libraries sit in, and the development pack path. Nothing here reads
// or writes a folder: the roots are computed from the environment, which each case sets for itself.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "PackTestSupport.hpp"
#include "graph/rp_pack.h"
#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;
using namespace rp::host::packs;
using rp::host::packs::test::ScopedEnv;

TEST_CASE("the pack roots follow the platform's convention, and the overrides win", "[packs-folders]") {
    {
        const ScopedEnv data("RETROPLUG_PACKS_DIR", "/scratch/data");
        const ScopedEnv cache("RETROPLUG_PACKS_CACHE_DIR", "/scratch/cache");
        const PackRoots roots = defaultPackRoots();
        CHECK(roots.data == fs::path("/scratch/data"));
        CHECK(roots.cache == fs::path("/scratch/cache"));
    }
    const ScopedEnv data("RETROPLUG_PACKS_DIR", std::nullopt);
    const ScopedEnv cache("RETROPLUG_PACKS_CACHE_DIR", std::nullopt);
#if defined(_WIN32)
    const ScopedEnv local("LOCALAPPDATA", "C:\\Users\\me\\AppData\\Local");
    const PackRoots roots = defaultPackRoots();
    CHECK(roots.data == fs::path("C:\\Users\\me\\AppData\\Local") / "RetroPlug" / "packs");
    CHECK(roots.cache == fs::path("C:\\Users\\me\\AppData\\Local") / "RetroPlug" / "cache" / "packs");
#elif defined(__APPLE__)
    const ScopedEnv home("HOME", "/Users/me");
    const PackRoots roots = defaultPackRoots();
    CHECK(roots.data == fs::path("/Users/me/Library/Application Support/RetroPlug/packs"));
    CHECK(roots.cache == fs::path("/Users/me/Library/Caches/RetroPlug/packs"));
#else
    const ScopedEnv home("HOME", "/home/me");
    {
        const ScopedEnv xdgData("XDG_DATA_HOME", std::nullopt);
        const ScopedEnv xdgCache("XDG_CACHE_HOME", std::nullopt);
        const PackRoots roots = defaultPackRoots();
        CHECK(roots.data == fs::path("/home/me/.local/share/retroplug/packs"));
        CHECK(roots.cache == fs::path("/home/me/.cache/retroplug/packs"));
    }
    {
        // A relative base directory is invalid under the XDG spec and ignored.
        const ScopedEnv xdgData("XDG_DATA_HOME", "relative/data");
        const ScopedEnv xdgCache("XDG_CACHE_HOME", "relative/cache");
        const PackRoots roots = defaultPackRoots();
        CHECK(roots.data == fs::path("/home/me/.local/share/retroplug/packs"));
        CHECK(roots.cache == fs::path("/home/me/.cache/retroplug/packs"));
    }
    const ScopedEnv xdgData("XDG_DATA_HOME", "/xdg/data");
    const ScopedEnv xdgCache("XDG_CACHE_HOME", "/xdg/cache");
    const PackRoots roots = defaultPackRoots();
    CHECK(roots.data == fs::path("/xdg/data/retroplug/packs"));
    CHECK(roots.cache == fs::path("/xdg/cache/retroplug/packs"));
#endif
}

TEST_CASE("the platform folder and the API level are the build's", "[packs-folders]") {
    // cmake/packs.cmake stages a module under the folder it names and stamps the level it reads from rp_pack.h; the
    // loader looks where currentPlatform() says and compares with RP_API_LEVEL. The two sides must agree.
    CHECK(std::string(currentPlatform()) == RP_PACK_PLATFORM_CMAKE);
    CHECK(RP_API_LEVEL == RP_API_LEVEL_CMAKE);
    CHECK(libraryFileName("rp-demo").rfind("rp-demo.", 0) == 0);
}

TEST_CASE("RETROPLUG_PACK_PATH splits as PATH does", "[packs-folders]") {
#if defined(_WIN32)
    const auto dirs = splitPackPath("C:\\a;;C:\\b c;");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == fs::path("C:\\a"));
    CHECK(dirs[1] == fs::path("C:\\b c"));
#else
    const auto dirs = splitPackPath("/a::/b c:");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == fs::path("/a"));
    CHECK(dirs[1] == fs::path("/b c"));
#endif
    // The environment's value goes through the same splitter, wide on Windows.
#if defined(_WIN32)
    const ScopedEnv fromEnv("RETROPLUG_PACK_PATH", "C:\\a;;C:\\b c;");
    const fs::path first = "C:\\a", second = "C:\\b c";
#else
    const ScopedEnv fromEnv("RETROPLUG_PACK_PATH", "/a::/b c:");
    const fs::path first = "/a", second = "/b c";
#endif
    const auto fromVar = packPathFromEnv();
    REQUIRE(fromVar.size() == 2);
    CHECK(fromVar[0] == first);
    CHECK(fromVar[1] == second);
    const ScopedEnv unset("RETROPLUG_PACK_PATH", std::nullopt);
    CHECK(packPathFromEnv().empty());
}
```

- [ ] **Step 4: Declare the test binary**

In `packages/native/cmake/tests.cmake`, directly after the `add_subdirectory(... test/packs/fixture ...)` line Task 2
added, insert:

```cmake

# The packs (docs/downloadable-packs-loading-design.md sections 1 and 2, docs/downloadable-packs-m1-plan.md): the folder
# model on a temporary root, the loader's every refusal against the fixture pack's variants (test/packs/fixture), a pack
# reaching a running Engine, two Engines sharing one load, a live load while an audio thread renders (its
# ThreadSanitizer run is tools/run-sanitizer.sh thread's), and the Mesen package built as a pack and booted beside its
# static build. Its product is the packs-host list, the desktop's without mesen and bliptoaster, so rp.mesen.* reaches
# its engines only from the pack; it links the static retroplug-mesen too, which no engine of its registers until a
# case hands it over (Engine::addNodePackage), so the two builds render side by side in one process.
retroplug_add_test_binary(
    NAME     retroplug-packs-test
    SOURCES  test/packs/PackRoots.test.cpp         # the data and cache roots, the platform folder, the pack path
    LIBS     retroplug-backend                     # Engine, the process's pack table, the folder model
    PACKAGES ${RP_PRODUCT_PACKAGES_packs-host}
    DEFS     RP_PACKS_BUILD_DIR="${RP_PACKS_DIR}"
             RP_PACK_PLATFORM_CMAKE="${RP_PACK_PLATFORM}"
             RP_API_LEVEL_CMAKE=${RP_API_LEVEL}u)
# The packs it loads are modules nothing links, built before it.
get_property(_rp_packfixture_targets GLOBAL PROPERTY RP_PACKFIXTURE_TARGETS)
add_dependencies(retroplug-packs-test ${_rp_packfixture_targets})
```

Later tasks add sources, libraries and definitions to this call; the comment already describes the finished binary.

- [ ] **Step 5: Run the build to see it fail**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL compiling `PackRoots.test.cpp` (through `PackTestSupport.hpp`) with
`fatal error: host/packs/PackDirs.hpp: No such file or directory`.

- [ ] **Step 6: Write PackDirs**

Create `packages/native/src/host/packs/PackDirs.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Where packs live (docs/downloadable-packs-loading-design.md section 1): the installed packs and their state.json in
// the user's data directory, the bytecode compiled from their bundles in the user's cache directory, and the
// development pack folders RETROPLUG_PACK_PATH names. Every function that touches the disk takes a PackRoots, so a
// test hands it a temporary directory and never reaches the user's own folders.
namespace rp::host::packs {

struct PackRoots {
    std::filesystem::path data;   // <data>/state.json and <data>/<id>/<version>/
    std::filesystem::path cache;  // <cache>/<id>/<version>/, rebuilt whenever it is missing
};

// The user's roots on this platform: RETROPLUG_PACKS_DIR and RETROPLUG_PACKS_CACHE_DIR when set (the CLI's tests point
// them at a scratch folder, as RETROPLUG_USER_CONFIG_DIR does the config), else
//   Linux    $XDG_DATA_HOME/retroplug/packs (~/.local/share/...), $XDG_CACHE_HOME/retroplug/packs (~/.cache/...)
//   macOS    ~/Library/Application Support/RetroPlug/packs, ~/Library/Caches/RetroPlug/packs
//   Windows  %LOCALAPPDATA%\RetroPlug\packs, %LOCALAPPDATA%\RetroPlug\cache\packs
// A root it cannot find (no HOME, no LOCALAPPDATA) is empty, and a caller loads no installed pack.
PackRoots defaultPackRoots();

// The platform folder a pack's native libraries are under (native/<platform>/): linux-x64, linux-arm64,
// macos-universal or windows-x64. Empty on a platform no pack is built for, where no pack with a native half loads.
std::string_view currentPlatform();

// The file a pack's native half named `name` (pack.json's halves.native) is, inside native/<platform>/: rp-mesen.so,
// rp-mesen.dylib, rp-mesen.dll.
std::string libraryFileName(std::string_view name);

// The folders a RETROPLUG_PACK_PATH value names, in order: separated by ';' on Windows and ':' elsewhere, as PATH is,
// with empty entries dropped.
std::vector<std::filesystem::path> splitPackPath(std::string_view value);
#if defined(_WIN32)
// The same split of the wide form packPathFromEnv reads, so a profile path outside the ANSI code page survives.
std::vector<std::filesystem::path> splitPackPath(std::wstring_view value);
#endif

// splitPackPath(RETROPLUG_PACK_PATH), or nothing when it is unset.
std::vector<std::filesystem::path> packPathFromEnv();

// `p` as UTF-8 for a message or a comparison with an id. Never throws: on Windows path::string() converts through the
// ANSI code page and throws for a name outside it, which a refusal's message must not do.
std::string displayPath(const std::filesystem::path& p);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/PackDirs.cpp`:

```cpp
#include "host/packs/PackDirs.hpp"

#include <cstdlib>

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// An environment variable as a path, empty when it is unset or empty. Windows reads the wide form, so a profile path
// outside the ANSI code page survives.
fs::path envPath(const char* name) {
#if defined(_WIN32)
    const std::wstring wide(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wide.c_str());
    if (value == nullptr || *value == L'\0') return {};
    return fs::path(value);
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return {};
    return fs::path(value);
#endif
}

// `value` split at `separator`, empty entries dropped: one loop for the narrow and the wide path list.
template <typename Char>
std::vector<fs::path> splitAt(std::basic_string_view<Char> value, Char separator) {
    std::vector<fs::path> out;
    std::size_t at = 0;
    while (at <= value.size()) {
        std::size_t end = value.find(separator, at);
        if (end == std::basic_string_view<Char>::npos) end = value.size();
        if (end > at) out.emplace_back(std::basic_string<Char>(value.substr(at, end - at)));
        at = end + 1;
    }
    return out;
}

// An XDG base directory, empty when it is unset or relative: the XDG spec says a relative one is invalid and ignored.
[[maybe_unused]] fs::path xdgPath(const char* name) {
    fs::path p = envPath(name);
    return p.is_absolute() ? p : fs::path();
}

}  // namespace

PackRoots defaultPackRoots() {
    PackRoots roots;
    roots.data = envPath("RETROPLUG_PACKS_DIR");
    roots.cache = envPath("RETROPLUG_PACKS_CACHE_DIR");
#if defined(_WIN32)
    // The local profile, not the roaming %APPDATA%: a domain copies the roaming one between machines at each logon, and
    // a pack's native libraries are large and machine-specific.
    const fs::path local = envPath("LOCALAPPDATA");
    if (roots.data.empty() && !local.empty()) roots.data = local / "RetroPlug" / "packs";
    if (roots.cache.empty() && !local.empty()) roots.cache = local / "RetroPlug" / "cache" / "packs";
#elif defined(__APPLE__)
    const fs::path home = envPath("HOME");
    if (roots.data.empty() && !home.empty())
        roots.data = home / "Library" / "Application Support" / "RetroPlug" / "packs";
    if (roots.cache.empty() && !home.empty()) roots.cache = home / "Library" / "Caches" / "RetroPlug" / "packs";
#else
    const fs::path home = envPath("HOME");
    if (roots.data.empty()) {
        const fs::path xdg = xdgPath("XDG_DATA_HOME");
        if (!xdg.empty()) roots.data = xdg / "retroplug" / "packs";
        else if (!home.empty()) roots.data = home / ".local" / "share" / "retroplug" / "packs";
    }
    if (roots.cache.empty()) {
        const fs::path xdg = xdgPath("XDG_CACHE_HOME");
        if (!xdg.empty()) roots.cache = xdg / "retroplug" / "packs";
        else if (!home.empty()) roots.cache = home / ".cache" / "retroplug" / "packs";
    }
#endif
    return roots;
}

std::string_view currentPlatform() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "windows-x64";
#elif defined(__APPLE__)
    return "macos-universal";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return {};
#endif
}

std::string libraryFileName(std::string_view name) {
#if defined(_WIN32)
    return std::string(name) + ".dll";
#elif defined(__APPLE__)
    return std::string(name) + ".dylib";
#else
    return std::string(name) + ".so";
#endif
}

std::vector<fs::path> splitPackPath(std::string_view value) {
#if defined(_WIN32)
    return splitAt<char>(value, ';');
#else
    return splitAt<char>(value, ':');
#endif
}

#if defined(_WIN32)
std::vector<fs::path> splitPackPath(std::wstring_view value) { return splitAt<wchar_t>(value, L';'); }
#endif

std::string displayPath(const fs::path& p) {
#if defined(_WIN32)
    try {
        const std::u8string utf8 = p.u8string();
        return std::string(utf8.begin(), utf8.end());
    } catch (...) {
        return "<a path that is not valid UTF-16>";
    }
#else
    return p.string();
#endif
}

std::vector<fs::path> packPathFromEnv() {
    const fs::path value = envPath("RETROPLUG_PACK_PATH");
    if (value.empty()) return {};
#if defined(_WIN32)
    return splitPackPath(std::wstring_view(value.native()));
#else
    return splitPackPath(value.string());
#endif
}

}  // namespace rp::host::packs
```

- [ ] **Step 7: Add the library and link it from the backend**

In `packages/native/CMakeLists.txt`, directly before the comment `# retroplug-backend: the Backend as a static library`,
insert:

```cmake
# retroplug-packs: the pack folder model and the native loader (docs/downloadable-packs-loading-design.md sections 1 and
# 2): the per-platform data and cache roots, pack.json, state.json, the version locks, the development pack path, and
# the table that opens each pack's library once. Every desktop host links it through retroplug-backend, whose Engine
# registers what the table loads; it needs no product (the process's table, seeded with the product's packages, is
# the backend's: src/host/packs/ProcessPacks.cpp), so a test of the folder model links it alone.
add_library(retroplug-packs STATIC
    src/host/packs/PackDirs.cpp)
target_include_directories(retroplug-packs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-packs PUBLIC retroplug-graph PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(retroplug-packs PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)  # linked into the shared plugin module, through the backend

```

and in `target_link_libraries(retroplug-backend PUBLIC ...)`, replace

```cmake
    retroplug-graph     # the graph executor, planner and GraphHost the engine runs on (packages/graph-runtime)
```

with

```cmake
    retroplug-graph     # the graph executor, planner and GraphHost the engine runs on (packages/graph-runtime)
    retroplug-packs     # the loaded packs every engine registers after the product's own (src/host/packs)
```

- [ ] **Step 8: Run the tests**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 3 test cases)`.

- [ ] **Step 9: Add the product's golden row**

Run:

```bash
cmake --build build --target retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-engine-graph-test "[packages]"
```

Expected: FAIL at `REQUIRE( it != golden.end() )` with `product retroplug-packs-test`.

Insert the row (the packs-host list's registration, in its order), keeping every other row as it is:

```bash
python3 - <<'EOF'
import json
p = 'packages/native/test/engine-graph/golden/product-registrations.json'
rows = json.load(open(p))
rows['retroplug-packs-test'] = [
    'rp.core.add', 'rp.core.merge', 'rp.core.join', 'rp.core.split', 'rp.core.downmix', 'rp.core.relabel',
    'rp.core.delay', 'rp.core.constant', 'rp.core.buffer-player', 'rp.core.buffer-recorder', 'rp.core.midi-filter',
    'rp.sameboy.gb', 'rp.dsp.gain', 'rp.dsp.biquad', 'rp.dsp.dither', 'rp.dsp.normalize', 'rp.dsp.dpcm',
    'rp.dsp.resample', 'rp.host.audio-out', 'rp.n8.nes', 'rp.host.midi-in', 'rp.host.midi-out',
]
open(p, 'w').write(json.dumps(dict(sorted(rows.items())), indent=2) + '\n')
EOF
```

Run: `git diff --numstat packages/native/test/engine-graph/golden/product-registrations.json`
Expected: `24	0	packages/native/test/engine-graph/golden/product-registrations.json` (additions only).

Run: `build/bin/retroplug-engine-graph-test "[packages]"`
Expected: `All tests passed (... assertions in 2 test cases)`.

- [ ] **Step 10: Run it in test:plugin, and hold the new targets to /W4 /WX**

In `package.json`'s `test:plugin` script, replace `retroplug-device-mode-test retroplug-n8-test` with
`retroplug-device-mode-test retroplug-packs-test retroplug-n8-test`.

In `packages/retroplug/scripts/run-plugin-tests.mjs`, replace

```js
  "retroplug-engine-graph-test", "retroplug-device-mode-test", "retroplug-n8-test",
```

with

```js
  "retroplug-engine-graph-test", "retroplug-device-mode-test", "retroplug-packs-test", "retroplug-n8-test",
```

In `packages/native/cmake/warnings.cmake`, replace

```cmake
        # The MicroQuickJS script host, its device-mode build and its runner (packages/graph-runtime).
        retroplug-mqjs-host retroplug-mqjs-host-device retroplug-mqjs-run)
```

with

```cmake
        # The MicroQuickJS script host, its device-mode build and its runner (packages/graph-runtime).
        retroplug-mqjs-host retroplug-mqjs-host-device retroplug-mqjs-run
        # The pack folder model and loader (src/host/packs), its test and the fixture pack's static library; the pack
        # modules themselves are added below, as cmake/packs.cmake records them.
        retroplug-packs retroplug-packs-test retroplug-packfixture-nodes)
    # And every pack module (cmake/packs.cmake's retroplug_pack_module): the fixture's variants and a package's pack.
    get_property(_rp_pack_modules GLOBAL PROPERTY RP_PACK_TARGETS)
    list(APPEND _rp_werror_targets ${_rp_pack_modules})
```

- [ ] **Step 11: Check the TS side takes the new list**

Run: `pnpm typecheck`
Expected: exit 0, its last lines naming `typecheck: the packs-host product's tree (...)` and `Done`.

Run: `node packages/retroplug/scripts/run-plugin-tests.mjs packs-test`
Expected: `# 1 plugin test binary(ies) passed`.

- [ ] **Step 12: Commit**

```bash
git add package.json packages/native/CMakeLists.txt packages/native/cmake/tests.cmake \
    packages/native/cmake/warnings.cmake packages/retroplug/scripts/run-plugin-tests.mjs \
    packages/native/test/engine-graph/golden/product-registrations.json \
    packages/native/src/host/packs/PackDirs.hpp packages/native/src/host/packs/PackDirs.cpp \
    packages/native/test/packs/PackTestSupport.hpp packages/native/test/packs/PackRoots.test.cpp
git commit -m "packs: the packs-host product, retroplug-packs with the pack roots, and retroplug-packs-test"
```

---

### Task 4: pack.json

**Files:**
- Create: `packages/native/src/host/packs/PackManifest.hpp`, `packages/native/src/host/packs/PackManifest.cpp`
- Create: `packages/native/test/packs/PackManifest.test.cpp`
- Modify: `packages/native/CMakeLists.txt` (`retroplug-packs` sources), `packages/native/cmake/tests.cmake` (sources)

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/packs/PackManifest.test.cpp`:

```cpp
// pack.json (docs/downloadable-packs-loading-design.md section 1): a manifest parses whole, the staged fixture's
// included, and each malformed member is refused with a reason that names it.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "PackTestSupport.hpp"
#include "graph/rp_pack.h"
#include "host/packs/PackManifest.hpp"

using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;

namespace {

// A manifest's text with one member's value replaced (or the member dropped, for an empty value).
std::string manifestWith(const std::string& member, const std::string& value) {
    std::string out = "{";
    const std::vector<std::pair<std::string, std::string>> base = {
        {"id", R"("demo")"},
        {"name", R"("Demo")"},
        {"version", R"("1.2.0")"},
        {"apiLevel", std::to_string(RP_API_LEVEL)},
        {"license", R"("MIT")"},
        {"source", R"("https://example.invalid/demo")"},
        {"requires", R"({"packs":[],"platforms":[]})"},
        {"provides", R"({"platforms":[],"nodeTypes":["rp.demo.tone"],"roles":[]})"},
        {"halves", R"({"native":"rp-demo"})"},
    };
    bool first = true;
    for (const auto& [key, v] : base) {
        const std::string& text = key == member ? value : v;
        if (text.empty()) continue;
        out += (first ? "\"" : ",\"") + key + "\":" + text;
        first = false;
    }
    return out + "}";
}

}  // namespace

TEST_CASE("pack.json parses whole, and each malformed member or file is refused by name", "[packs-folders]") {
    std::string why;
    const auto good = parsePackManifest(manifestWith("", ""), &why);
    INFO(why);
    REQUIRE(good);
    CHECK(good->id == "demo");
    CHECK(good->version == "1.2.0");
    CHECK(good->apiLevel == RP_API_LEVEL);
    CHECK(good->nativeHalf() == "rp-demo");
    CHECK(good->providesNodeTypes == std::vector<std::string>{"rp.demo.tone"});

    // The staged fixture's manifest, stamped by cmake/packs.cmake, is one too.
    const auto staged = readPackManifest(rp::host::packs::test::fixture("packfixture"), &why);
    INFO(why);
    REQUIRE(staged);
    CHECK(staged->apiLevel == RP_API_LEVEL);
    CHECK(staged->nativeHalf() == "rp-packfixture");

    const std::vector<std::pair<std::string, std::string>> refused = {
        {manifestWith("id", ""), "id is required"},
        {manifestWith("id", R"("Demo")"), "is not a pack id"},
        {manifestWith("version", R"("1.2")"), "is not MAJOR.MINOR.PATCH"},
        {manifestWith("version", R"("01.2.0")"), "is not MAJOR.MINOR.PATCH"},
        {manifestWith("apiLevel", ""), "apiLevel is required"},
        {manifestWith("apiLevel", "-1"), "apiLevel is -1, outside 0..4294967295"},
        {manifestWith("apiLevel", "4294967296"), "apiLevel is 4294967296, outside 0..4294967295"},
        {manifestWith("apiLevel", R"("4")"), "not a manifest"},
        {manifestWith("name", ""), "name is required"},
        {manifestWith("source", ""), "source is required"},
        {manifestWith("name", R"("a\u0000b")"), "name holds a control character"},
        {manifestWith("license", R"("a\u001bb")"), "license holds a control character"},
        {manifestWith("source", "\"" + std::string(257, 'x') + "\""), "source is longer than 256 characters"},
        {manifestWith("requires", R"({"packs":["ok"],"platforms":["a b"]})"), "requires.platforms holds"},
        {manifestWith("requires", R"({"pack":["other"]})"), "requires holds \"pack\""},
        {manifestWith("provides", R"({"nodeTypes":["a"],"nodeType":["b"]})"), "provides holds \"nodeType\""},
        {manifestWith("provides", R"({"nodeTypes":["a"],"platforms":["a\"b"]})"), "provides.platforms holds"},
        {manifestWith("provides", R"({"nodeTypes":["a"],"roles":[""]})"), "provides.roles holds"},
        {manifestWith("provides", R"({"nodeTypes":"a"})"), "not a manifest"},
        {manifestWith("id", R"("zzz","id":"demo")"), "the member \"id\" appears twice"},
        {manifestWith("requires", R"({"packs":[],"packs":["x"]})"), "the member \"packs\" appears twice"},
        {manifestWith("name", R"("x","\u006eame":"y")"), "the member \"name\" appears twice"},
        {manifestWith("license", ""), "license is required"},
        {manifestWith("requires", R"({"packs":["demo"]})"), "names the pack itself"},
        {manifestWith("requires", R"({"packs":["Not An Id"]})"), "which is not a pack id"},
        {manifestWith("provides", R"({"nodeTypes":["a","a"]})"), "names \"a\" twice"},
        {manifestWith("provides", R"({"nodeTypes":[]})"), "provides.nodeTypes is empty"},
        {manifestWith("halves", R"({"native":"rp-demo","wasm":"x.wasm"})"), "halves.wasm is no half"},
        {manifestWith("halves", R"({"native":"../rp-demo"})"), "named after its id: rp-demo"},
        {manifestWith("halves", R"({"native":"rp-other"})"), "named after its id: rp-demo"},
        {manifestWith("halves", R"({"native":"rp-demo","app":"../app.js"})"), "not a path inside the pack"},
        {manifestWith("halves", R"({})"), "names no half"},
        {"[1, 2]", "not a manifest"},
    };
    for (const auto& [text, reason] : refused) {
        INFO(text);
        why.clear();
        CHECK_FALSE(parsePackManifest(text, &why));
        CHECK_THAT(why, ContainsSubstring("pack.json: ") && ContainsSubstring(reason));
    }

    // Nesting is refused before any parse recurses, wherever it sits and however deep: a 100k-level array under an
    // unknown member of requires, or at the top level, must come back as a refusal, not a stack overflow.
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    const std::vector<std::string> deepTexts = {manifestWith("requires", R"({"zzz":)" + deep + "}"),
                                                manifestWith("name", deep), "{\"x\":" + deep + "}", deep};
    for (const std::string& text : deepTexts) {
        why.clear();
        CHECK_FALSE(parsePackManifest(text, &why));
        CHECK_THAT(why, ContainsSubstring("pack.json: nested deeper than 32 levels"));
    }

    // Deep nesting does not switch the duplicate check off: past the limit it is refused whole, within it the repeat
    // after the nesting is still found.
    for (const auto& [levels, reason] : std::vector<std::pair<int, std::string>>{
             {70, "nested deeper than 32 levels"}, {20, "the member \"id\" appears twice"}}) {
        why.clear();
        CHECK_FALSE(parsePackManifest(R"({"x":)" + std::string(levels, '[') + std::string(levels, ']') +
                                          R"(,"id":"zzz","id":"demo"})", &why));
        CHECK_THAT(why, ContainsSubstring(reason));
    }

    // A 1 MiB object of distinct keys is refused or parsed, without a quadratic key check.
    {
        std::string wide = "{";
        for (int i = 0; wide.size() < 1000000; ++i) wide += "\"k" + std::to_string(i) + "\":1,";
        wide += "\"id\":\"demo\"}";
        why.clear();
        CHECK_FALSE(parsePackManifest(wide, &why));
    }

    // A cause from the parser is echoed longer than an input value, and neither is cut inside a UTF-8 sequence.
    why.clear();
    CHECK_FALSE(parsePackManifest(manifestWith("version", "\"" + std::string(63, 'a') + "\xc3\xa9\xc3\xa9\""), &why));
    CHECK_THAT(why, ContainsSubstring(std::string(63, 'a') + "..."));

    // Hostile input is echoed escaped and bounded, never raw.
    why.clear();
    CHECK_FALSE(parsePackManifest(manifestWith("version", "\"1\\u001b[31m" + std::string(200, 'z') + "\""), &why));
    CHECK(why.find('\x1b') == std::string::npos);
    CHECK(why.size() < 200);
    CHECK_THAT(why, ContainsSubstring("\\x1b"));

    // And it is read only as a bounded regular file.
    rp::host::packs::test::TempDir dir;

    // A directory named pack.json is unreadable, not an empty manifest.
    std::filesystem::create_directory(dir.path() / "pack.json");
    CHECK_FALSE(readPackManifest(dir.path(), &why));
    CHECK_THAT(why, ContainsSubstring("cannot read"));

    std::filesystem::remove(dir.path() / "pack.json");
    {
        std::ofstream out(dir.path() / "pack.json", std::ios::binary);
        out << manifestWith("", "") << std::string(1024 * 1024 + 1, ' ');
    }
    why.clear();
    CHECK_FALSE(readPackManifest(dir.path(), &why));
    CHECK_THAT(why, ContainsSubstring("too large"));
}
```

In `packages/native/cmake/tests.cmake`, in `retroplug-packs-test`'s `SOURCES`, after the `PackRoots.test.cpp` line add:

```cmake
             test/packs/PackManifest.test.cpp      # pack.json, and each malformed member's refusal
```

- [ ] **Step 2: Run the build to see it fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL with `fatal error: host/packs/PackManifest.hpp: No such file or directory`.

- [ ] **Step 3: Write PackManifest**

Create `packages/native/src/host/packs/PackManifest.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A pack's manifest, pack.json (docs/downloadable-packs-loading-design.md section 1), parsed and checked: every member
// is held to its shape here, so a manifest a later milestone reads is one this parser already accepted whole. What M1's
// loader (PackTable) enforces beyond the shape is the native half and provides.nodeTypes; it refuses a pack naming any
// other half, since M1 cannot load a TS half and a pack never loads in part. provides.platforms and .roles, and
// requires.platforms, are M2's to enforce.
namespace rp::host::packs {

struct PackManifest {
    std::string   id;        // a plain id: lowercase letters, digits and '-', starting with a letter
    std::string   name;      // what Settings > Packs shows
    std::string   version;   // MAJOR.MINOR.PATCH, the name of its folder under packs/<id>/
    std::uint32_t apiLevel = 0;
    std::string   license;   // an SPDX expression, shown beside the pack
    std::string   source;    // where its source lives
    std::vector<std::string> requiresPacks;      // pack ids loaded before it
    std::vector<std::string> requiresPlatforms;  // platform ids some loaded pack provides (checked by M2)
    std::vector<std::string> providesPlatforms;
    std::vector<std::string> providesNodeTypes;  // every node type its native half may register
    std::vector<std::string> providesRoles;
    // Its halves by kind: "native" names the library stem, which is always "rp-<id>" (native/<platform>/rp-<id>.so:
    // bound to the id, so two packs' libraries never share a file name, which Windows matches a loaded module by), the
    // others a JS file inside the version folder. A kind no RetroPlug knows is refused here.
    std::map<std::string, std::string> halves;

    // The native half's library stem, or empty for a pack with no native half.
    std::string nativeHalf() const;
};

// The kinds of half this host knows, in the order the loader loads them.
const std::vector<std::string>& knownHalves();

// Whether `id` is a plain pack id: 1 to 64 characters, [a-z][a-z0-9-]*.
bool isPackId(std::string_view id);

// Whether `version` is MAJOR.MINOR.PATCH in decimal with no leading zero: it names a folder, so nothing else may.
bool isPackVersion(std::string_view version);

// `json` as a manifest, or nothing with the reason in *why (each reason starts "pack.json: ").
std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why);

// <versionDir>/pack.json read and parsed; nothing, with the reason, when it cannot be read or does not parse.
std::optional<PackManifest> readPackManifest(const std::filesystem::path& versionDir, std::string* why);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/PackManifest.cpp`:

```cpp
#include "host/packs/PackManifest.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <unordered_set>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// The manifest as JSON spells it. Optional members are checked below, so a missing one is reported by name rather
// than as reflect-cpp's generic message. Unknown top-level members are ignored: a member added at the same API level
// is additive by definition, and an older host of that level still loads the pack whole.
//
// `requires` and `provides` are closed: a member they do not know (a misspelt "pack" for "packs") would otherwise parse
// as an empty list and drop a dependency without a word, so `extra` catches it and the parse refuses it by name.
struct RawRequires {
    std::optional<std::vector<std::string>> packs;
    std::optional<std::vector<std::string>> platforms;
    rfl::ExtraFields<rfl::Generic>          extra;
};
struct RawProvides {
    std::optional<std::vector<std::string>> platforms;
    std::optional<std::vector<std::string>> nodeTypes;
    std::optional<std::vector<std::string>> roles;
    rfl::ExtraFields<rfl::Generic>          extra;
};
struct RawManifest {
    std::optional<std::string>                                id;
    std::optional<std::string>                                name;
    std::optional<std::string>                                version;
    std::optional<std::int64_t>                               apiLevel;
    std::optional<std::string>                                license;
    std::optional<std::string>                                source;
    rfl::Rename<"requires", std::optional<RawRequires>>       requirements;
    std::optional<RawProvides>                                provides;
    std::optional<std::map<std::string, std::string>>         halves;
};

// The largest pack.json read: a manifest is a few hundred bytes, and the file comes from a folder anyone can write.
constexpr std::uintmax_t kMaxManifestBytes = 1024 * 1024;
// The longest free-text member (name, license, source) accepted.
constexpr std::size_t kMaxTextChars = 256;

bool refuse(std::string* why, std::string reason) {
    if (why) *why = "pack.json: " + std::move(reason);
    return false;
}

// The deepest nesting of arrays and objects a manifest may have. The deepest real member is two levels down; the
// limit exists because reflect-cpp's generic parser recurses once per level, and a deep document would overflow the
// stack of a host that reads it at launch.
constexpr int kMaxNesting = 32;

// `text` as a message may echo it: non-printable bytes escaped as \xNN and cut to `cap` bytes (never inside a UTF-8
// sequence), so a hostile manifest cannot put control codes or a megabyte into a log line. A value from the manifest
// is held to 64 bytes; the text of a library's own error, which names the cause at its end, to 256.
std::string echoed(std::string_view text, std::size_t cap = 64) {
    bool cut = false;
    if (text.size() > cap) {
        std::size_t at = cap;
        while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) --at;
        text = text.substr(0, at);
        cut = true;
    }
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f || c == '\\' || c == '"') {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
        } else {
            out += ch;
        }
    }
    if (cut) out += "...";
    return out;
}

// What DuplicateKeyScan found. Anything else (valid JSON with neither, or text it cannot follow, which the parse then
// reports) is Nothing.
struct ScanResult {
    enum class Kind { Nothing, Duplicate, TooDeep } kind = Kind::Nothing;
    std::string key;  // the repeated key, for Duplicate
};

// Scans `json` for the two things refused before it is parsed: nesting past kMaxNesting levels, and a key an object
// repeats at any depth. yyjson (under reflect-cpp) keeps the first of a repeated key and JS's JSON.parse the last, so
// a manifest the two readers would see differently is refused rather than parsed either way. The scan recurses at
// most kMaxNesting levels itself, and stops at the first finding.
class DuplicateKeyScan {
public:
    explicit DuplicateKeyScan(std::string_view s) : s_(s) {}

    ScanResult find() {
        value(0);
        return result_;
    }

private:

    void skipSpace() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\n' || s_[at_] == '\r')) ++at_;
    }

    // A string at at_ (on its opening quote), its escapes decoded to bytes; \u escapes become UTF-8.
    bool string(std::string& out) {
        if (at_ >= s_.size() || s_[at_] != '"') return false;
        ++at_;
        while (at_ < s_.size() && s_[at_] != '"') {
            char c = s_[at_++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= s_.size()) return false;
            c = s_[at_++];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (at_ + 4 > s_.size()) return false;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s_[at_++];
                        const int digit = h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0';
                        cp = cp * 16 + static_cast<unsigned>(digit);
                    }
                    // A surrogate half is kept as its own three bytes: two keys differ unless they spell it alike.
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xc0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3f));
                    } else {
                        out += static_cast<char>(0xe0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (cp & 0x3f));
                    }
                    break;
                }
                default: out += c; break;  // " \\ /
            }
        }
        if (at_ >= s_.size()) return false;
        ++at_;
        return true;
    }

    // One value at at_; false once the text stops being JSON this scan can follow, or a duplicate is found.
    bool value(int depth) {
        skipSpace();
        if (at_ >= s_.size()) return false;
        const char c = s_[at_];
        if (c == '{' || c == '[') {
            if (depth + 1 > kMaxNesting) {
                result_.kind = ScanResult::Kind::TooDeep;
                return false;
            }
            return c == '{' ? object(depth + 1) : array(depth + 1);
        }
        if (c == '"') {
            std::string ignored;
            return string(ignored);
        }
        while (at_ < s_.size() && s_[at_] != ',' && s_[at_] != '}' && s_[at_] != ']') ++at_;
        return true;
    }

    bool array(int depth) {
        ++at_;
        skipSpace();
        if (at_ < s_.size() && s_[at_] == ']') return ++at_, true;
        for (;;) {
            if (!value(depth)) return false;
            skipSpace();
            if (at_ >= s_.size()) return false;
            if (s_[at_] == ']') return ++at_, true;
            if (s_[at_++] != ',') return false;
        }
    }

    bool object(int depth) {
        ++at_;
        std::unordered_set<std::string> keys;
        skipSpace();
        if (at_ < s_.size() && s_[at_] == '}') return ++at_, true;
        for (;;) {
            skipSpace();
            std::string key;
            if (!string(key)) return false;
            if (!keys.insert(key).second) {
                result_ = {ScanResult::Kind::Duplicate, std::move(key)};
                return false;
            }
            skipSpace();
            if (at_ >= s_.size() || s_[at_++] != ':') return false;
            if (!value(depth)) return false;
            skipSpace();
            if (at_ >= s_.size()) return false;
            if (s_[at_] == '}') return ++at_, true;
            if (s_[at_++] != ',') return false;
        }
    }

    std::string_view           s_;
    std::size_t                at_ = 0;
    ScanResult                 result_;
};

}  // namespace

bool isPackVersion(std::string_view v) {
    int parts = 0;
    std::size_t at = 0;
    while (at <= v.size()) {
        std::size_t end = v.find('.', at);
        if (end == std::string_view::npos) end = v.size();
        const std::string_view part = v.substr(at, end - at);
        if (part.empty() || part.size() > 9) return false;
        if (part.size() > 1 && part[0] == '0') return false;
        for (const char c : part)
            if (c < '0' || c > '9') return false;
        ++parts;
        at = end + 1;
    }
    return parts == 3;
}

namespace {

// A half's path: relative, inside the version folder, '/'-separated, no empty, "." or ".." segment.
bool isInsidePath(std::string_view p) {
    if (p.empty() || p.front() == '/') return false;
    if (p.find('\\') != std::string_view::npos || p.find(':') != std::string_view::npos) return false;
    std::size_t at = 0;
    while (at <= p.size()) {
        std::size_t end = p.find('/', at);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view seg = p.substr(at, end - at);
        if (seg.empty() || seg == "." || seg == "..") return false;
        at = end + 1;
    }
    return true;
}

// A node type, platform or role id as provides spells it: non-empty, printable ASCII with no space or quote.
bool isPlainName(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    for (const char c : s)
        if (c <= ' ' || c > '~' || c == '"' || c == '\\') return false;
    return true;
}

// A free-text member: printable characters only (no control code, NUL included) and a bounded length.
bool checkText(const std::string& text, const char* member, std::string* why) {
    std::size_t chars = 0;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f) return refuse(why, std::string(member) + " holds a control character");
        if ((c & 0xc0) != 0x80) ++chars;  // count code points, not UTF-8 continuation bytes
    }
    if (chars > kMaxTextChars)
        return refuse(why, std::string(member) + " is longer than " + std::to_string(kMaxTextChars) + " characters");
    return true;
}

bool checkList(const std::optional<std::vector<std::string>>& list, const char* member, bool packIds,
               std::vector<std::string>& out, std::string* why) {
    if (!list) return true;
    for (const auto& item : *list) {
        if (packIds ? !isPackId(item) : !isPlainName(item))
            return refuse(why, std::string(member) + " holds \"" + echoed(item) + "\", which is not " +
                                   (packIds ? "a pack id" : "an id"));
        for (const auto& seen : out)
            if (seen == item) return refuse(why, std::string(member) + " names \"" + echoed(item) + "\" twice");
        out.push_back(item);
    }
    return true;
}

}  // namespace

const std::vector<std::string>& knownHalves() {
    static const std::vector<std::string> kHalves = {"native", "app", "consoles", "protocol", "ui", "cli", "script"};
    return kHalves;
}

std::string PackManifest::nativeHalf() const {
    const auto it = halves.find("native");
    return it == halves.end() ? std::string() : it->second;
}

bool isPackId(std::string_view id) {
    if (id.empty() || id.size() > 64 || id[0] < 'a' || id[0] > 'z') return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why) {
    const ScanResult scan = DuplicateKeyScan(json).find();
    if (scan.kind == ScanResult::Kind::TooDeep) {
        refuse(why, "nested deeper than " + std::to_string(kMaxNesting) + " levels");
        return std::nullopt;
    }
    if (scan.kind == ScanResult::Kind::Duplicate) {
        refuse(why, "the member \"" + echoed(scan.key) + "\" appears twice in one object");
        return std::nullopt;
    }
    auto raw = rfl::json::read<RawManifest>(std::string(json));
    if (!raw) {
        refuse(why, std::string("not a manifest: ") + echoed(raw.error().what(), 256));
        return std::nullopt;
    }
    PackManifest m;
    const auto required = [&](const std::optional<std::string>& value, const char* member, std::string& out) {
        if (!value || value->empty()) return refuse(why, std::string(member) + " is required (a non-empty string)");
        out = *value;
        return true;
    };
    if (!required(raw->id, "id", m.id) || !required(raw->name, "name", m.name) ||
        !required(raw->version, "version", m.version) || !required(raw->license, "license", m.license) ||
        !required(raw->source, "source", m.source))
        return std::nullopt;
    if (!checkText(m.name, "name", why) || !checkText(m.license, "license", why) ||
        !checkText(m.source, "source", why))
        return std::nullopt;
    if (!isPackId(m.id)) {
        refuse(why, "id \"" + echoed(m.id) +
                        "\" is not a pack id (lowercase letters, digits and '-', starting with a letter)");
        return std::nullopt;
    }
    if (!isPackVersion(m.version)) {
        refuse(why, "version \"" + echoed(m.version) + "\" is not MAJOR.MINOR.PATCH");
        return std::nullopt;
    }
    if (!raw->apiLevel) {
        refuse(why, "apiLevel is required (a whole number)");
        return std::nullopt;
    }
    if (*raw->apiLevel < 0 || *raw->apiLevel > 0xffffffffll) {
        refuse(why, "apiLevel is " + std::to_string(*raw->apiLevel) + ", outside 0..4294967295");
        return std::nullopt;
    }
    m.apiLevel = static_cast<std::uint32_t>(*raw->apiLevel);
    if (const auto& r = raw->requirements.value()) {
        if (const auto& extra = r->extra; !extra.empty()) {
            refuse(why, "requires holds \"" + echoed(extra.begin()->first) + "\", which is no member of it");
            return std::nullopt;
        }
        if (!checkList(r->packs, "requires.packs", true, m.requiresPacks, why) ||
            !checkList(r->platforms, "requires.platforms", false, m.requiresPlatforms, why))
            return std::nullopt;
    }
    for (const auto& dep : m.requiresPacks)
        if (dep == m.id) {
            refuse(why, "requires.packs names the pack itself");
            return std::nullopt;
        }
    if (const auto& p = raw->provides) {
        if (const auto& extra = p->extra; !extra.empty()) {
            refuse(why, "provides holds \"" + echoed(extra.begin()->first) + "\", which is no member of it");
            return std::nullopt;
        }
        if (!checkList(p->platforms, "provides.platforms", false, m.providesPlatforms, why) ||
            !checkList(p->nodeTypes, "provides.nodeTypes", false, m.providesNodeTypes, why) ||
            !checkList(p->roles, "provides.roles", false, m.providesRoles, why))
            return std::nullopt;
    }
    if (raw->halves) {
        for (const auto& [kind, value] : *raw->halves) {
            bool known = false;
            for (const auto& k : knownHalves()) known = known || k == kind;
            if (!known) {
                refuse(why, "halves." + echoed(kind) + " is no half this RetroPlug loads");
                return std::nullopt;
            }
            if (kind == "native" && value != "rp-" + m.id) {
                refuse(why, "halves.native is \"" + echoed(value) +
                                "\", and a pack's library is named after its id: rp-" +
                                m.id);
                return std::nullopt;
            }
            if (kind != "native" && !isInsidePath(value)) {
                refuse(why, "halves." + echoed(kind) + " is \"" + echoed(value) +
                                "\", which is not a path inside the pack");
                return std::nullopt;
            }
        }
        m.halves = *raw->halves;
    }
    if (m.halves.empty()) {
        refuse(why, "halves names no half: the pack would load nothing");
        return std::nullopt;
    }
    if (!m.nativeHalf().empty() && m.providesNodeTypes.empty()) {
        refuse(why, "halves.native is set but provides.nodeTypes is empty: a native half registers node types");
        return std::nullopt;
    }
    return m;
}

std::optional<PackManifest> readPackManifest(const fs::path& versionDir, std::string* why) {
    const fs::path file = versionDir / "pack.json";
    std::error_code ec;
    const auto size = fs::is_regular_file(file, ec) ? fs::file_size(file, ec) : 0;
    if (ec || !fs::is_regular_file(file, ec)) {
        if (why) *why = "pack.json: cannot read " + displayPath(file);
        return std::nullopt;
    }
    if (size > kMaxManifestBytes) {
        if (why) *why = "pack.json: too large (over " + std::to_string(kMaxManifestBytes) + " bytes)";
        return std::nullopt;
    }
    // One byte past the limit is read, so a file that grew since the size was taken is still refused, not truncated.
    std::ifstream in(file, std::ios::binary);
    std::string text(static_cast<std::size_t>(kMaxManifestBytes) + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.bad() || (in.fail() && !in.eof())) {
        if (why) *why = "pack.json: cannot read " + displayPath(file);
        return std::nullopt;
    }
    text.resize(static_cast<std::size_t>(in.gcount()));
    if (text.size() > kMaxManifestBytes) {
        if (why) *why = "pack.json: too large (over " + std::to_string(kMaxManifestBytes) + " bytes)";
        return std::nullopt;
    }
    return parsePackManifest(text, why);
}

}  // namespace rp::host::packs
```

In `packages/native/CMakeLists.txt`, replace

```cmake
add_library(retroplug-packs STATIC
    src/host/packs/PackDirs.cpp)
```

with

```cmake
add_library(retroplug-packs STATIC
    src/host/packs/PackDirs.cpp
    src/host/packs/PackManifest.cpp)
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 4 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/packs/PackManifest.hpp packages/native/src/host/packs/PackManifest.cpp \
    packages/native/test/packs/PackManifest.test.cpp packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "packs: pack.json parsed and checked, each malformed member refused by name"
```

---

### Task 5: state.json and the version locks

`rp::fsyncPath` (`src/util/FileSync.cpp`) moves from the backend's sources into `retroplug-packs`, which needs it for
the atomic write, and gains an overload taking a path, which Windows opens by its wide name; the backend links
`retroplug-packs` publicly, so `HostRpcService` still reaches the string one.

**Files:**
- Create: `packages/native/src/host/packs/FileLock.hpp`, `packages/native/src/host/packs/FileLock.cpp`
- Create: `packages/native/src/host/packs/PackState.hpp`, `packages/native/src/host/packs/PackState.cpp`
- Create: `packages/native/test/packs/PackState.test.cpp`
- Modify: `packages/native/src/util/FileSync.hpp`, `packages/native/src/util/FileSync.cpp`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/packs/PackState.test.cpp`:

```cpp
// state.json and the version locks (docs/downloadable-packs-loading-design.md section 1): the state round-trips and is
// replaced atomically, a newer one is left alone, concurrent writers each land, a version folder is in use while a
// process holds it, and a launch settles pending updates and removals. Every case runs on a TempDir.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-folders]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "PackTestSupport.hpp"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackState.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::TempDir;

namespace {

void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The file's identity, which a rewrite (a temporary renamed over it) changes and an untouched file keeps. A timestamp
// would not do: it can match across two writes inside the file system's granularity.
#if defined(_WIN32)
std::string fileIdentity(const fs::path& file) {
    HANDLE h = CreateFileW(file.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    FILE_ID_INFO id{};
    const bool ok = GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof id) != 0;
    CloseHandle(h);
    return ok ? std::string(reinterpret_cast<const char*>(&id), sizeof id) : std::string();
}
#else
std::string fileIdentity(const fs::path& file) {
    struct stat st {};
    return ::stat(file.c_str(), &st) == 0 ? std::to_string(st.st_ino) : std::string();
}
#endif

}  // namespace

TEST_CASE("state.json round-trips, is replaced atomically, and a newer one is left alone", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::string why;
    // No file is an empty state.
    auto empty = readPackState(root, &why);
    REQUIRE(empty);
    CHECK(empty->packs.empty());

    PackState state;
    state.packs.push_back({"mesen", "1.2.0", "1.3.0", false});
    state.packs.push_back({"risa", "2.3.0", "", true});
    REQUIRE(writePackState(root, state, &why));
    const auto back = readPackState(root, &why);
    INFO(why);
    REQUIRE(back);
    REQUIRE(back->packs.size() == 2);
    CHECK(back->find("mesen")->pending == "1.3.0");
    CHECK(back->find("risa")->remove);
    // Nothing but state.json and the lock every access holds is left beside it: the temporary file was renamed over it.
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(root)) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"state.json", "state.lock"});

    // A newer RetroPlug's file is refused and never overwritten.
    const std::string newer = R"({"version": 99, "packs": []})";
    writeText(root / "state.json", newer);
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("newer RetroPlug"));
    CHECK_FALSE(updatePackState(root, [](PackState&) { return true; }, &why));
    CHECK(readText(root / "state.json") == newer);
    // Read for its version first: a newer schema that does not parse as this one is still newer, not malformed.
    writeText(root / "state.json", R"({"version": 2, "packs": "a newer shape"})");
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("newer RetroPlug"));

    // And a malformed entry is refused, naming it.
    writeText(root / "state.json", R"({"version": 1, "packs": [{"id": "Bad Id", "active": "1.0.0"}]})");
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("is not a pack id"));
}

TEST_CASE("updatePackState serialises writers: every change lands", "[packs-folders]") {
    // Each thread takes the exclusive lock on its own open of state.lock, as a second process would, so none of the
    // read-modify-writes overwrites another's. Catch's assertions are not thread safe, so each thread records its
    // outcomes and the checks run here.
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    constexpr int kThreads = 8;
    constexpr int kEach = 25;
    std::vector<std::string> failures(kThreads);
    std::vector<std::thread> writers;
    for (int i = 0; i < kThreads; ++i)
        writers.emplace_back([&root, &failures, i] {
            for (int j = 0; j < kEach; ++j) {
                std::string why;
                const std::string id = "pack-" + std::to_string(i) + "-" + std::to_string(j);
                const bool ok = updatePackState(
                    root,
                    [&id](PackState& s) {
                        s.packs.push_back({id, "1.0.0", "", false});
                        return true;
                    },
                    &why);
                if (!ok || !why.empty()) failures[static_cast<std::size_t>(i)] += id + ": " + why + "; ";
            }
        });
    for (auto& t : writers) t.join();
    for (const auto& f : failures) CHECK(f.empty());
    std::string why;
    const auto state = readPackState(root, &why);
    REQUIRE(state);
    CHECK(state->packs.size() == static_cast<std::size_t>(kThreads * kEach));
}

TEST_CASE("a version folder is in use while a process holds it", "[packs-folders]") {
    TempDir tmp;
    const fs::path version = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    CHECK_FALSE(versionInUse(version));  // no lock file yet: nobody has loaded it
    std::string why;
    {
        auto held = holdVersion(version, &why);
        INFO(why);
        REQUIRE(held);
        CHECK(versionInUse(version));
        auto second = holdVersion(version, &why);  // a second process loading the same version shares it
        CHECK(second);
    }
    CHECK_FALSE(versionInUse(version));
}

TEST_CASE("a launch settles state.json: pending versions switch, removals go, unheld old folders are deleted",
          "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path oldA = rp::host::packs::test::install(root, "packfixture", "1.0.0");
    const fs::path newA = rp::host::packs::test::install(root, "packfixture", "1.1.0");
    const fs::path oldB = rp::host::packs::test::install(root, "packfixture-dependent", "1.0.0");
    const fs::path heldC = rp::host::packs::test::install(root, "packfixture-clone", "1.0.0");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "1.1.0", false});
    state.packs.push_back({"packfixture-dependent", "1.0.0", "", true});
    state.packs.push_back({"packfixture-clone", "1.0.0", "", true});
    REQUIRE(writePackState(root, state, &why));
    // Another process still runs the clone's version.
    auto held = holdVersion(heldC, &why);
    REQUIRE(held);

    REQUIRE(settlePackState(root, &why));
    const auto settled = readPackState(root, &why);
    REQUIRE(settled);
    REQUIRE(settled->packs.size() == 1);
    CHECK(settled->packs[0].id == "packfixture");
    CHECK(settled->packs[0].active == "1.1.0");
    CHECK(settled->packs[0].pending.empty());
    CHECK_FALSE(fs::exists(oldA));
    CHECK(fs::exists(newA));
    CHECK_FALSE(fs::exists(oldB.parent_path()));  // the removed pack's folder, emptied, goes too
    CHECK(fs::exists(heldC));                    // held: left for a later launch

    held.reset();
    REQUIRE(settlePackState(root, &why));
    CHECK_FALSE(fs::exists(heldC));

    // A promotion alone (no removal beside it) is written too: the changed test compares against the state as read.
    const fs::path newer = rp::host::packs::test::install(root, "packfixture", "1.2.0");
    REQUIRE(updatePackState(root, [](PackState& s) { s.find("packfixture")->pending = "1.2.0"; return true; }, &why));
    REQUIRE(settlePackState(root, &why));
    const auto promoted = readPackState(root, &why);
    REQUIRE(promoted);
    CHECK(promoted->find("packfixture")->active == "1.2.0");
    CHECK(promoted->find("packfixture")->pending.empty());
    CHECK(fs::exists(newer));
}

TEST_CASE("a lock on a file that was replaced while it was taken is refused", "[packs-folders]") {
    // The settle deletes a version folder while it holds the exclusive lock; a hold that opened the lock file before
    // that must not "hold" a file that is no longer the folder's.
    TempDir tmp;
    const fs::path version = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    const fs::path lockFile = version / kVersionLockFile;
    std::string why;
    FileLock::Failure failure = FileLock::Failure::None;
    auto held = FileLock::acquire(
        lockFile, FileLock::Mode::Shared, true, &why, &failure, [&] {
            std::error_code ec;
            fs::remove(lockFile, ec);
            std::ofstream(lockFile) << "";
        });
    CHECK_FALSE(held);
    CHECK(failure == FileLock::Failure::Replaced);
    CHECK_THAT(why, ContainsSubstring("being deleted"));
    // The next hold, on the file now there, is ordinary.
    CHECK(holdVersion(version, &why));
}

TEST_CASE("a launch with no state.json sweeps nothing, and a settle that changes nothing writes nothing",
          "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path version = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(settlePackState(root, &why));
    CHECK(fs::exists(version));
    CHECK_FALSE(fs::exists(root / "state.json"));  // nothing recorded, nothing written

    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    const auto before = fileIdentity(root / "state.json");
    // Leftovers a crash made: a writer's temporary, and a deletion's trash.
    writeText(root / "state.json.tmp-1-0", "x");
    writeText(version.parent_path() / ".0.9.0.deleting-1-0" / "pack.json", "{}");
    REQUIRE(settlePackState(root, &why));
    CHECK(fileIdentity(root / "state.json") == before);  // a rewrite renames a new file over it: a new inode
    CHECK(fs::exists(version));
    CHECK_FALSE(fs::exists(root / "state.json.tmp-1-0"));
    CHECK_FALSE(fs::exists(version.parent_path() / ".0.9.0.deleting-1-0"));
}

TEST_CASE("state.json version 0 is malformed", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    writeText(root / "state.json", R"({"version": 0, "packs": []})");
    std::string why;
    CHECK_FALSE(readPackState(root, &why));
    CHECK_THAT(why, ContainsSubstring("is not a version"));
}

#if !defined(_WIN32)
TEST_CASE("updatePackState serialises writers across processes", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    constexpr int kChildren = 4;
    constexpr int kEach = 50;
    // fork in a process with other threads running copies only the calling one, and a lock or allocator another held is
    // held forever in the child: this case relies on no other thread existing here (the sanitizer legs run it too).
    std::vector<pid_t> children;
    for (int c = 0; c < kChildren; ++c) {
        const pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            // The child only calls what it needs and leaves with _exit, never touching the parent's Catch state.
            int bad = 0;
            for (int j = 0; j < kEach; ++j) {
                std::string why;
                const std::string id = "child-" + std::to_string(c) + "-" + std::to_string(j);
                const bool ok = updatePackState(
                    root,
                    [&id](PackState& s) {
                        s.packs.push_back({id, "1.0.0", "", false});
                        return true;
                    },
                    &why);
                if (!ok) ++bad;
            }
            _exit(bad == 0 ? 0 : 1);
        }
        children.push_back(pid);
    }
    for (const pid_t pid : children) {
        int status = 0;
        REQUIRE(waitpid(pid, &status, 0) == pid);
        CHECK((WIFEXITED(status) && WEXITSTATUS(status) == 0));
    }
    std::string why;
    const auto state = readPackState(root, &why);
    INFO(why);
    REQUIRE(state);
    CHECK(state->packs.size() == static_cast<std::size_t>(kChildren * kEach));
}

TEST_CASE("a process killed while it holds a version leaves no lock behind", "[packs-folders]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path version = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{}, &why));
    int ready[2];
    REQUIRE(pipe(ready) == 0);
    // fork carries the same single-threaded assumption as the case above.
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    // Whatever fails below, the child must not outlive the case, or it would hold the lock and a pipe for good.
    struct Reap {
        pid_t pid;
        bool  done = false;  // set once the case has reaped the child itself, so a recycled pid is never signalled
        ~Reap() {
            if (done) return;
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
        }
    } reap{pid};
    if (pid == 0) {
        auto held = holdVersion(version, nullptr);
        const char byte = held ? 1 : 0;
        (void)!write(ready[1], &byte, 1);
        for (;;) pause();
    }
    char byte = 0;
    REQUIRE(read(ready[0], &byte, 1) == 1);
    REQUIRE(byte == 1);
    CHECK(versionInUse(version));
    REQUIRE(settlePackState(root, &why));
    CHECK(fs::exists(version));  // held by the child, so the settle leaves it
    kill(pid, SIGKILL);
    int status = 0;
    const pid_t waited = waitpid(pid, &status, 0);
    reap.done = waited == pid;
    REQUIRE(waited == pid);
    close(ready[0]);
    close(ready[1]);
    CHECK_FALSE(versionInUse(version));
    REQUIRE(settlePackState(root, &why));
    CHECK_FALSE(fs::exists(version));
}
#endif

#if !defined(_WIN32)
TEST_CASE("a data root this process cannot write is read without a lock", "[packs-folders]") {
    // A read-only profile or a shared install: no state.lock can be made there, and nothing can write state.json
    // there either, so the read goes ahead unheld rather than failing.
    if (geteuid() == 0) SKIP("root writes through any permission, so a read-only folder cannot be made");
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    fs::remove(root / kStateLockFile);
    fs::permissions(root, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);
    const auto state = readPackState(root, &why);
    fs::permissions(root, fs::perms::owner_write, fs::perm_options::add);  // so the TempDir can go
    INFO(why);
    REQUIRE(state);
    CHECK(state->find("packfixture") != nullptr);
    CHECK_FALSE(fs::exists(root / kStateLockFile));
}

TEST_CASE("a settle keeps a pending version it cannot look for, and promotes it once it can", "[packs-folders]") {
    if (geteuid() == 0) SKIP("root reads through any permission, so an unreadable folder cannot be made");
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    rp::host::packs::test::install(root, "packfixture", "1.0.0");
    rp::host::packs::test::install(root, "packfixture", "1.1.0");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "1.1.0", false}}}, &why));
    // The pack's folder cannot be searched: that the manifest is not there is not known, so the pending version is not
    // forgotten (the listing failure is reported, as it is for any folder the settle cannot read).
    const fs::path packDir = root / "packfixture";
    fs::permissions(packDir, fs::perms::owner_all, fs::perm_options::remove);
    const bool unreadable = settlePackState(root, &why);
    fs::permissions(packDir, fs::perms::owner_all, fs::perm_options::add);
    CHECK_FALSE(unreadable);
    auto state = readPackState(root, &why);
    REQUIRE(state);
    CHECK(state->find("packfixture")->active == "1.0.0");
    CHECK(state->find("packfixture")->pending == "1.1.0");
    REQUIRE(settlePackState(root, &why));
    state = readPackState(root, &why);
    REQUIRE(state);
    CHECK(state->find("packfixture")->active == "1.1.0");
    CHECK(state->find("packfixture")->pending.empty());
}
#endif
```

In `retroplug-packs-test`'s `SOURCES`, after the `PackManifest.test.cpp` line add:

```cmake
             test/packs/PackState.test.cpp         # state.json, its writers, the version locks, a launch's settle
```

- [ ] **Step 2: Run the build to see it fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL with `fatal error: host/packs/FileLock.hpp: No such file or directory`.

- [ ] **Step 3: Write FileLock**

Create `packages/native/src/host/packs/FileLock.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

// An advisory lock on a file, held by this process until the FileLock goes (or the process ends, however it ends: the
// OS drops it, so a crash leaves no stale lock behind). flock on POSIX, LockFileEx on Windows. Both lock per open
// file, not per process, so two FileLocks on one file in one process conflict as two processes' would, which is what
// lets one test process stand in for two. Two limits of the OS: flock on NFS is emulated with POSIX byte-range locks,
// which belong to the process (so two FileLocks in one process do not conflict there) and which some servers do not
// grant at all; and a child made by fork without exec shares its parent's open file descriptions, so it shares the
// parent's locks and keeps them after the parent's FileLock goes. The pack folders are local files and a host never
// forks without exec, so neither is handled.
namespace rp::host::packs {

class FileLock {
public:
    enum class Mode { Shared, Exclusive };
    // Why acquire gave nothing, for the caller that treats one cause apart.
    enum class Failure { None, Denied, Replaced, Other };

    // The lock on `file` (created when missing), or nothing with the reason in *why and the cause in *failure. `wait`
    // blocks until the lock is free; otherwise a lock held elsewhere in a conflicting mode answers nothing at once.
    // The file is opened read-only (a lock needs no write access, so a read-only folder's existing lock file can still
    // be taken). Once locked, the file at `file` must still be the one held: a lock on a file that was unlinked, or
    // replaced by another, while this call waited protects nothing, so it is released and refused (Failure::Replaced).
    // `afterOpen`, when given, runs between the open and the lock: a test's way of making that happen.
    static std::optional<FileLock> acquire(const std::filesystem::path& file, Mode mode, bool wait, std::string* why,
                                           Failure* failure = nullptr,
                                           const std::function<void()>& afterOpen = {});

    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock();

    Mode mode() const { return mode_; }

private:
    FileLock() = default;
    void release() noexcept;
    Mode mode_ = Mode::Shared;
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// The lock file a process holds in each installed version folder it loaded.
inline constexpr const char* kVersionLockFile = "pack.lock";

// This process's hold on `versionDir`: a shared lock on its lock file, kept for the life of the process by the pack
// table, since a loaded pack is never unloaded. A folder a settle is deleting is emptied under its exclusive lock, so a
// hold that races the deletion fails to lock; one that comes after it holds an empty folder, whose manifest read
// refuses it (on POSIX the folder is also renamed away, so a hold that opened the old lock file is refused as
// "replaced"). What is NOT guaranteed on Windows is that the lock file is never recreated in the emptied folder
// before the settle removes it: callers hold under state.lock where the folder must not vanish (loadLaunchPacks),
// and a hold taken without it is only as good as the manifest read that follows. May fail transiently while
// versionInUse (below) has its brief exclusive lock.
// *failure says why a hold failed: Denied (a folder this process cannot write, which no settle can delete either, so
// the caller may load it unheld), Replaced (being deleted), or Other (contended: worth a brief retry).
std::optional<FileLock> holdVersion(const std::filesystem::path& versionDir, std::string* why,
                                    FileLock::Failure* failure = nullptr);

// holdVersion, tried again briefly while the failure is only contention (versionInUse's instant of an exclusive lock,
// a settle's hold) or, on Windows, an access error that may be a scanner's brief handle on the file (Denied): the one
// bounded loop the table and a launch's discovery share. A folder being deleted (Replaced) is refused at once, and so
// is a Denied one elsewhere, where it means a folder this process cannot write and waiting changes nothing.
std::optional<FileLock> holdVersionBriefly(const std::filesystem::path& versionDir, std::string* why,
                                           FileLock::Failure* failure);

// Whether any process (this one included) holds `versionDir`: true when an exclusive lock cannot be taken at once. A
// folder with no lock file is held by nobody. The probe takes the exclusive lock for an instant and lets it go, so a
// holdVersion that lands in that instant fails once; callers retry or treat it as in use.
bool versionInUse(const std::filesystem::path& versionDir);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/FileLock.cpp`:

```cpp
#include "host/packs/FileLock.hpp"

#include <chrono>
#include <cstring>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

void fail(std::string* why, FileLock::Failure* failure, FileLock::Failure kind, std::string reason) {
    if (why) *why = std::move(reason);
    if (failure) *failure = kind;
}

#if defined(_WIN32)
bool sameFile(HANDLE held, const fs::path& file) {
    // A fresh open of the path, to compare what it names now with what is held.
    HANDLE now = CreateFileW(file.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (now == INVALID_HANDLE_VALUE) return false;
    // The 128-bit id: the 64-bit index of GetFileInformationByHandle is not unique on every file system (ReFS).
    FILE_ID_INFO a{}, b{};
    const bool same = GetFileInformationByHandleEx(held, FileIdInfo, &a, sizeof a) &&
                      GetFileInformationByHandleEx(now, FileIdInfo, &b, sizeof b) &&
                      a.VolumeSerialNumber == b.VolumeSerialNumber &&
                      std::memcmp(&a.FileId, &b.FileId, sizeof a.FileId) == 0;
    CloseHandle(now);
    return same;
}
#else
bool sameFile(int held, const fs::path& file) {
    struct stat a {}, b {};
    return ::fstat(held, &a) == 0 && ::stat(file.c_str(), &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
#endif

}  // namespace

std::optional<FileLock> FileLock::acquire(const fs::path& file, Mode mode, bool wait, std::string* why,
                                          Failure* failure, const std::function<void()>& afterOpen) {
    if (failure) *failure = Failure::None;
    FileLock lock;
    lock.mode_ = mode;
#if defined(_WIN32)
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        fail(why, failure,
             error == ERROR_ACCESS_DENIED || error == ERROR_WRITE_PROTECT ? Failure::Denied : Failure::Other,
             "cannot open " + displayPath(file) + ": error " + std::to_string(error));
        return std::nullopt;
    }
    lock.handle_ = h;  // released by the destructor on every return below
    if (afterOpen) afterOpen();
    DWORD flags = mode == Mode::Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    if (!wait) flags |= LOCKFILE_FAIL_IMMEDIATELY;
    OVERLAPPED at{};
    if (!LockFileEx(h, flags, 0, 1, 0, &at)) {
        fail(why, failure, Failure::Other,
             "cannot lock " + displayPath(file) + ": error " + std::to_string(GetLastError()));
        return std::nullopt;
    }
    if (!sameFile(h, file)) {
        fail(why, failure, Failure::Replaced,
             displayPath(file) + " was replaced while it was locked: the folder is being deleted");
        return std::nullopt;
    }
#else
    const int fd = ::open(file.c_str(), O_RDONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        const int error = errno;
        fail(why, failure,
             error == EACCES || error == EROFS || error == EPERM ? Failure::Denied : Failure::Other,
             "cannot open " + displayPath(file) + ": " + std::strerror(error));
        return std::nullopt;
    }
    lock.fd_ = fd;  // released by the destructor on every return below
    if (afterOpen) afterOpen();
    int op = mode == Mode::Exclusive ? LOCK_EX : LOCK_SH;
    if (!wait) op |= LOCK_NB;
    int rc = 0;
    do rc = ::flock(fd, op);
    while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        fail(why, failure, Failure::Other, "cannot lock " + displayPath(file) + ": " + std::strerror(errno));
        return std::nullopt;
    }
    if (!sameFile(fd, file)) {
        fail(why, failure, Failure::Replaced,
             displayPath(file) + " was replaced while it was locked: the folder is being deleted");
        return std::nullopt;
    }
#endif
    return lock;
}

FileLock::FileLock(FileLock&& other) noexcept {
#if defined(_WIN32)
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    mode_ = other.mode_;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
        mode_ = other.mode_;
#if defined(_WIN32)
        handle_ = other.handle_;
        other.handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
    }
    return *this;
}

FileLock::~FileLock() { release(); }

void FileLock::release() noexcept {
#if defined(_WIN32)
    if (handle_ != nullptr) {
        OVERLAPPED at{};
        UnlockFileEx(static_cast<HANDLE>(handle_), 0, 1, 0, &at);
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

std::optional<FileLock> holdVersion(const fs::path& versionDir, std::string* why, FileLock::Failure* failure) {
    return FileLock::acquire(versionDir / kVersionLockFile, FileLock::Mode::Shared, false, why, failure);
}

std::optional<FileLock> holdVersionBriefly(const fs::path& versionDir, std::string* why, FileLock::Failure* failure) {
    constexpr int kAttempts = 5;
    FileLock::Failure cause = FileLock::Failure::None;
    std::optional<FileLock> taken;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        taken = holdVersion(versionDir, why, &cause);
        if (taken || cause == FileLock::Failure::Replaced) break;
#if !defined(_WIN32)
        if (cause == FileLock::Failure::Denied) break;
#endif
        if (attempt + 1 < kAttempts) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (failure != nullptr) *failure = taken ? FileLock::Failure::None : cause;
    return taken;
}

bool versionInUse(const fs::path& versionDir) {
    std::error_code ec;
    if (!fs::exists(versionDir / kVersionLockFile, ec)) return false;
    return !FileLock::acquire(versionDir / kVersionLockFile, FileLock::Mode::Exclusive, false, nullptr).has_value();
}

}  // namespace rp::host::packs
```

- [ ] **Step 4: Give fsyncPath a path overload**

Replace `packages/native/src/util/FileSync.hpp` with:

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace rp {

// Force `path`'s already-written contents out of the OS page cache to the storage device, so a
// crash after this returns cannot resurrect a shorter, older, or zero-length version of the file.
//
// This is the half of "atomic write" that the rename alone does not give you. fs::rename is atomic
// with respect to the DIRECTORY - a reader sees the old file or the new one, never a mix - but it
// says nothing about whether the new file's BYTES reached the disk. Without this, a crash between
// the write and the flush leaves the rename durable and its contents not: the file is present,
// correctly named, and empty. Syncing the temp BEFORE the rename is what closes that window.
//
// Deliberately NOT a full durability barrier. This call does not sync the parent directory (so a
// crash may lose the rename itself and leave the previous file in place - which is a fine outcome,
// and the one the contract promises; the state.json writer, whose rename must outlast a crash, syncs
// the directory itself with fsyncDirectory), and macOS gets plain fsync rather than F_FULLFSYNC (which drains the
// drive's own write cache). Both are omitted on purpose: the SRAM auto-save mirror runs this every
// couple of seconds, and a per-tick drive barrier is not worth paying for a guarantee one step
// stronger than "never torn".
//
// Out of line because the Windows implementation needs <windows.h>, and the same rule applies here
// as to processAlive: one TU pays for that include instead of every caller. Returns false if the
// file cannot be opened or the flush fails.
bool fsyncPath(const std::string& path);
// The same for a path, which Windows opens by its wide name (CreateFileW), so a file under a profile folder outside
// the ANSI code page is flushed too.
bool fsyncPath(const std::filesystem::path& path);
// Spelled out because a string literal converts to both of the above equally well, which is ambiguous.
inline bool fsyncPath(const char* path) { return fsyncPath(std::string(path)); }
// Flush a directory's entries (a rename into it) to the device. POSIX only: Windows does not open directories for
// this, and its callers rename with MOVEFILE_WRITE_THROUGH instead, so there it answers true.
bool fsyncDirectory(const std::filesystem::path& dir);

} // namespace rp
```

Replace `packages/native/src/util/FileSync.cpp` with:

```cpp
#include "util/FileSync.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rp {

bool fsyncPath(const std::string& path) {
#if defined(_WIN32)
    // GENERIC_WRITE, not READ: FlushFileBuffers documents a write handle as its requirement.
    const HANDLE h = ::CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = ::FlushFileBuffers(h) != 0;
    ::CloseHandle(h);
    return ok;
#else
    return fsyncPath(std::filesystem::path(path));
#endif
}

bool fsyncPath(const std::filesystem::path& path) {
#if defined(_WIN32)
    const HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = ::FlushFileBuffers(h) != 0;
    ::CloseHandle(h);
    return ok;
#else
    // O_WRONLY rather than O_RDONLY: fsync on a read-only descriptor is allowed on Linux but is not
    // portable (POSIX leaves it unspecified, and some systems answer EBADF). The file always exists
    // by the time we get here - we just wrote it - so no O_CREAT.
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) return false;
    int rc;
    do {
        rc = ::fsync(fd);
    } while (rc != 0 && errno == EINTR);
    ::close(fd);
    return rc == 0;
#endif
}

bool fsyncDirectory(const std::filesystem::path& dir) {
#if defined(_WIN32)
    (void)dir;
    return true;
#else
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    int rc;
    do {
        rc = ::fsync(fd);
    } while (rc != 0 && errno == EINTR);
    ::close(fd);
    return rc == 0;
#endif
}

} // namespace rp
```

- [ ] **Step 5: Write PackState**

Create `packages/native/src/host/packs/PackState.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "host/packs/FileLock.hpp"

// packs/state.json (docs/downloadable-packs-loading-design.md section 1): the installed packs, the version of each
// that loads, and what waits for the next launch. Versioned raw JSON like every config root: a newer stamp is refused
// and never overwritten (AGENTS.md, Config migrations), and it keeps only its latest schema. Members this version does
// not know are not kept when it rewrites the file, so ANY change to the file's shape bumps kPackStateVersion (and adds
// a migration), an additive one included.
//
// The install contract (M4's installer): a version folder is moved into place under packs/<id>/<version>/ and recorded
// (the updatePackState overload that takes the hold) within ONE exclusive hold of state.lock (lockPackState). A
// settle deletes every folder no entry names, so a folder moved in outside that hold, or recorded after it, can be
// deleted before it is recorded. An install in progress is unpacked into a '.'-prefixed folder, which a settle leaves
// alone. A settle that could not finish deleting a version (a racing hold) can leave an emptied packs/<id>/<version>/
// holding only pack.lock: it blocks a reinstall of that version until a settle sweeps it, so the installer settles (or
// clears it) before it moves a folder into place.
//
// The functions that lock state.lock themselves (every overload without a `held` parameter) must not be called while
// this process already holds it, and not from inside a `change` callback: flock locks per open file description, so a
// second open in the same process conflicts with the first and the call waits for itself forever. A caller that holds
// the lock passes it to the `held` overloads, which check it is exclusive (readPackState accepts either mode).
//
// Every access holds <dataRoot>/state.lock: a reader shared, a writer exclusive. So a writer's rename never meets a
// reader with the file open (which Windows refuses), two writers' read-modify-writes never interleave, and a launch
// that settles, reads and takes its version locks in one exclusive hold (loadLaunchPacks) can never meet another
// launch's settle deleting the version it is about to load. The waits are unbounded, which the holds' brevity makes
// safe (a read, a write, a settle): a process that ends, however it ends, drops its lock, but one stopped while it
// holds it (a debugger's breakpoint, SIGSTOP) blocks every launch's start until it runs again.
namespace rp::host::packs {

inline constexpr std::uint32_t kPackStateVersion = 1;
inline constexpr const char*   kStateLockFile = "state.lock";

struct InstalledPack {
    std::string id;
    std::string active;   // the version folder that loads: packs/<id>/<active>/
    std::string pending;  // a version unpacked beside it, switched to at the next launch; empty for none
    bool        remove = false;  // removed at the next launch
};

struct PackState {
    std::vector<InstalledPack> packs;

    InstalledPack*       find(const std::string& id);
    const InstalledPack* find(const std::string& id) const;
};

// A hold of <dataRoot>/state.lock in `mode`, waiting for it; creates <dataRoot>. Nothing, with the reason, when the
// folder or the lock file cannot be made.
std::optional<FileLock> lockPackState(const std::filesystem::path& dataRoot, FileLock::Mode mode, std::string* why);

// <dataRoot>/state.json under a shared hold, or an empty state when there is none; without a hold when state.lock
// cannot be made (a root this process cannot write, which nothing can write state.json in either). Nothing, with the
// reason in *why, for a file whose version is newer than kPackStateVersion (read before the rest, since a newer
// schema need not parse as this one), that does not parse, or that holds an entry that is not well formed: such a file
// is left as it is. The unlocked read happens only when the lock file cannot be made for want of permission; any other
// failure to lock is a refusal.
std::optional<PackState> readPackState(const std::filesystem::path& dataRoot, std::string* why);

// `state` written to <dataRoot>/state.json under an exclusive hold, atomically: to a temporary file beside it, flushed
// to the disk, then renamed over it, so a reader sees the old file or the new one and never a torn one.
bool writePackState(const std::filesystem::path& dataRoot, const PackState& state, std::string* why);

// A read-modify-write under one exclusive hold, so two processes installing at once each see the other's change rather
// than one overwriting it. `change` returns false to write nothing.
bool updatePackState(const std::filesystem::path& dataRoot, const std::function<bool(PackState&)>& change,
                     std::string* why);

// What a launch does before it loads, under one exclusive hold: each pending version becomes active, each pending
// removal drops its entry, and every version folder no entry names any more is deleted, unless a process holds it.
// A folder is deleted while this call holds its pack.lock exclusively: it is renamed to a '.'-prefixed trash name in
// the same pack folder (so a process that opened the old lock file finds the path gone and is refused), then removed;
// trash a crash left behind, and state.json temporaries, are swept by the next settle. A state.json that does not exist
// is settled to nothing and sweeps nothing: an absent file must not read as "no pack is installed" and delete them
// all. state.json is rewritten only when the settle changed it. A directory listing that fails is reported through
// *why after the rest has been done (the state is already settled). A folder inside the data root that state.json does
// not name (an installed-looking one a user copied there, or a development folder that happens to live there) is the
// settle's to delete, like any other. A pending version whose manifest cannot be looked for (an unreadable folder, as
// against a gone one) stays pending.
bool settlePackState(const std::filesystem::path& dataRoot, std::string* why);

// updatePackState for a caller that already holds state.lock exclusively (`held`): the folder move and the record in
// one hold, which the plain overload, taking its own lock, cannot do (two locks on one file conflict, in one process
// too).
bool updatePackState(const std::filesystem::path& dataRoot, const FileLock& held,
                     const std::function<bool(PackState&)>& change, std::string* why);

// The read and the settle for a caller that already holds state.lock exclusively (`held`, from lockPackState): a
// launch, which settles, reads and takes its version locks in that one hold.
std::optional<PackState> readPackState(const std::filesystem::path& dataRoot, const FileLock& held, std::string* why);
bool settlePackState(const std::filesystem::path& dataRoot, const FileLock& held, std::string* why);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/PackState.cpp`:

```cpp
#include "host/packs/PackState.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <thread>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "host/packs/PackDirs.hpp"
#include "host/packs/PackManifest.hpp"
#include "util/FileSync.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#define RP_GETPID _getpid
#else
#include <unistd.h>
#define RP_GETPID getpid
#endif

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

struct RawVersion {
    std::uint32_t version = 0;
};
struct RawEntry {
    std::string                id;
    std::string                active;
    std::optional<std::string> pending;
    std::optional<bool>        remove;
};
struct RawState {
    std::uint32_t         version = kPackStateVersion;
    std::vector<RawEntry> packs;
};

// A folder being deleted is renamed to `.<version>` + this + `<pid>-<n>`; a crash can leave one for the next settle.
constexpr const char* kTrashMarker = ".deleting-";

bool refuse(std::string* why, std::string reason) {
    if (why) *why = std::move(reason);
    return false;
}

// The file read and checked; the caller holds state.lock.
std::optional<PackState> readHeld(const fs::path& dataRoot, std::string* why) {
    const fs::path file = dataRoot / "state.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) return PackState{};
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        refuse(why, "state.json: cannot read " + displayPath(file));
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // The version first, on its own: a newer RetroPlug's schema need not parse as this one's, and its file must be
    // refused as newer rather than as malformed.
    const auto stamp = rfl::json::read<RawVersion>(text);
    if (!stamp) {
        refuse(why, std::string("state.json: no version: ") + stamp.error().what());
        return std::nullopt;
    }
    if (stamp->version < 1) {
        refuse(why, "state.json: version " + std::to_string(stamp->version) + " is not a version");
        return std::nullopt;
    }
    if (stamp->version > kPackStateVersion) {
        refuse(why, "state.json: written by a newer RetroPlug (version " + std::to_string(stamp->version) +
                        ", this one reads " + std::to_string(kPackStateVersion) + "); left as it is");
        return std::nullopt;
    }
    auto raw = rfl::json::read<RawState>(text);
    if (!raw) {
        refuse(why, std::string("state.json: ") + raw.error().what());
        return std::nullopt;
    }
    PackState state;
    for (const auto& e : raw->packs) {
        if (!isPackId(e.id)) {
            refuse(why, "state.json: \"" + e.id + "\" is not a pack id");
            return std::nullopt;
        }
        if (!isPackVersion(e.active) || (e.pending && !isPackVersion(*e.pending))) {
            refuse(why, "state.json: pack " + e.id + " names a version that is not MAJOR.MINOR.PATCH");
            return std::nullopt;
        }
        if (state.find(e.id)) {
            refuse(why, "state.json: pack " + e.id + " is listed twice");
            return std::nullopt;
        }
        state.packs.push_back({e.id, e.active, e.pending.value_or(""), e.remove.value_or(false)});
    }
    return state;
}

// `from` renamed over `to`. Windows refuses while another process has `to` open for an instant (a reader's antivirus
// scan, an indexer), so it retries briefly, and writes through so the rename is on the device when it returns.
bool replaceFile(const fs::path& from, const fs::path& to, std::error_code* ec) {
#if defined(_WIN32)
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD error = GetLastError();
        if (attempt >= 9 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)) {
            *ec = std::error_code(static_cast<int>(error), std::system_category());
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#else
    fs::rename(from, to, *ec);
    return !*ec;
#endif
}

// The file replaced; the caller holds state.lock exclusively.
bool writeHeld(const fs::path& dataRoot, const PackState& state, std::string* why) {
    RawState raw;
    for (const auto& p : state.packs) {
        RawEntry e{p.id, p.active, std::nullopt, std::nullopt};
        if (!p.pending.empty()) e.pending = p.pending;
        if (p.remove) e.remove = true;
        raw.packs.push_back(std::move(e));
    }
    // A name of this process's and this call's own, so two writers (two processes, or two threads of one) never share
    // a temporary file.
    static std::atomic<std::uint32_t> serial{0};
    const fs::path temp = dataRoot / ("state.json.tmp-" + std::to_string(RP_GETPID()) + "-" +
                                      std::to_string(serial.fetch_add(1)));
    std::error_code ec;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return refuse(why, "state.json: cannot write " + displayPath(temp));
        out << rfl::json::write(raw, rfl::json::pretty) << "\n";
        if (!out.flush()) return refuse(why, "state.json: cannot write " + displayPath(temp));
    }
    if (!rp::fsyncPath(temp)) {
        fs::remove(temp, ec);
        return refuse(why, "state.json: cannot flush " + displayPath(temp));
    }
    if (!replaceFile(temp, dataRoot / "state.json", &ec)) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return refuse(why, "state.json: cannot replace it: " + ec.message());
    }
    // The rename is only durable once its directory is flushed. Best effort: a file system that cannot sync a directory
    // still has the new file, and the contract allows a crash to keep the old one.
    rp::fsyncDirectory(dataRoot);
    return true;
}

// `dir`, a version folder no entry names any more, deleted unless a process holds it. The exclusive pack.lock is taken
// at once or not at all, and every entry but pack.lock is removed while it is held (the same on every platform, and
// independent of how the OS treats a delete of an open file): a process that tries to hold the folder meanwhile fails
// to lock, and one that holds it afterwards holds an empty folder whose manifest read refuses it. Then the lock file
// and the folder go. On POSIX the folder is first renamed to a trash name beside it while the lock is held, so a
// process that opened the old lock file finds the path gone (FileLock refuses it); if that rename fails the empty
// folder is left for a later settle. Windows cannot rename a folder with a file open in it, so there pack.lock and the
// folder are removed once the lock is let go, and a failure (a racer holding it) leaves them for a later settle.
void deleteUnheld(const fs::path& dir) {
    auto lock = FileLock::acquire(dir / kVersionLockFile, FileLock::Mode::Exclusive, false, nullptr);
    if (!lock) return;  // held: left for a later launch
    std::error_code ec;
    // The manifest first, so one is present only in a complete folder and a walk cut short leaves one the manifest
    // read refuses.
    fs::remove(dir / "pack.json", ec);
    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (displayPath(it->path().filename()) != kVersionLockFile) {
            std::error_code removeEc;
            fs::remove_all(it->path(), removeEc);
        }
#if defined(_WIN32)
    lock.reset();
    fs::remove(dir / kVersionLockFile, ec);
    fs::remove(dir, ec);  // not remove_all: a folder something was put back in is left alone
#else
    static std::atomic<std::uint32_t> serial{0};
    const fs::path trash =
        dir.parent_path() / ("." + displayPath(dir.filename()) + kTrashMarker + std::to_string(RP_GETPID()) + "-" +
                             std::to_string(serial.fetch_add(1)));
    fs::rename(dir, trash, ec);
    const bool renamed = !ec;
    lock.reset();
    if (renamed) fs::remove_all(trash, ec);
#endif
}

bool startsWith(const std::string& text, std::string_view prefix) { return text.rfind(prefix, 0) == 0; }

bool sameState(const std::vector<InstalledPack>& a, const std::vector<InstalledPack>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].active != b[i].active || a[i].pending != b[i].pending ||
            a[i].remove != b[i].remove)
            return false;
    return true;
}

}  // namespace

InstalledPack* PackState::find(const std::string& id) {
    for (auto& p : packs)
        if (p.id == id) return &p;
    return nullptr;
}

const InstalledPack* PackState::find(const std::string& id) const {
    for (const auto& p : packs)
        if (p.id == id) return &p;
    return nullptr;
}

std::optional<FileLock> lockPackState(const fs::path& dataRoot, FileLock::Mode mode, std::string* why) {
    std::error_code ec;
    fs::create_directories(dataRoot, ec);
    if (ec) {
        refuse(why, "state.json: cannot create " + displayPath(dataRoot) + ": " + ec.message());
        return std::nullopt;
    }
    return FileLock::acquire(dataRoot / kStateLockFile, mode, true, why);
}

std::optional<PackState> readPackState(const fs::path& dataRoot, std::string* why) {
    std::error_code ec;
    if (!fs::exists(dataRoot, ec)) return PackState{};
    // A root this process cannot write (a read-only profile, a shared install) has no state.lock to take, and nothing
    // can write state.json there either, so the read goes ahead without one.
    FileLock::Failure failure = FileLock::Failure::None;
    std::string lockWhy;
    auto hold = FileLock::acquire(dataRoot / kStateLockFile, FileLock::Mode::Shared, true, &lockWhy, &failure);
    if (!hold && failure != FileLock::Failure::Denied) {
        refuse(why, "state.json: " + lockWhy);
        return std::nullopt;
    }
    return readHeld(dataRoot, why);
}

std::optional<PackState> readPackState(const fs::path& dataRoot, const FileLock&, std::string* why) {
    return readHeld(dataRoot, why);
}

bool writePackState(const fs::path& dataRoot, const PackState& state, std::string* why) {
    auto hold = lockPackState(dataRoot, FileLock::Mode::Exclusive, why);
    if (!hold) return false;
    return writeHeld(dataRoot, state, why);
}

bool updatePackState(const fs::path& dataRoot, const std::function<bool(PackState&)>& change, std::string* why) {
    auto hold = lockPackState(dataRoot, FileLock::Mode::Exclusive, why);
    if (!hold) return false;
    return updatePackState(dataRoot, *hold, change, why);
}

bool updatePackState(const fs::path& dataRoot, const FileLock& held, const std::function<bool(PackState&)>& change,
                     std::string* why) {
    if (held.mode() != FileLock::Mode::Exclusive) return refuse(why, "state.json: state.lock is not held exclusively");
    auto state = readHeld(dataRoot, why);
    if (!state) return false;
    if (!change(*state)) return true;
    return writeHeld(dataRoot, *state, why);
}

bool settlePackState(const fs::path& dataRoot, std::string* why) {
    auto hold = lockPackState(dataRoot, FileLock::Mode::Exclusive, why);
    if (!hold) return false;
    return settlePackState(dataRoot, *hold, why);
}

bool settlePackState(const fs::path& dataRoot, const FileLock& held, std::string* why) {
    if (held.mode() != FileLock::Mode::Exclusive) return refuse(why, "state.json: state.lock is not held exclusively");
    std::error_code ec;
    // An absent state.json reads as an empty state, which would make every installed folder "unnamed" and delete it:
    // with nothing recorded there is nothing to settle, and nothing is swept.
    if (!fs::exists(dataRoot / "state.json", ec)) return true;
    auto state = readHeld(dataRoot, why);
    if (!state) return false;
    // This call holds the exclusive lock every writer takes, so a temporary file here is a crashed writer's.
    std::string listing;
    fs::directory_iterator root(dataRoot, ec);
    for (; !ec && root != fs::directory_iterator(); root.increment(ec)) {
        std::error_code entryEc;
        if (startsWith(displayPath(root->path().filename()), "state.json.tmp-") && root->is_regular_file(entryEc))
            fs::remove(root->path(), entryEc);
    }
    if (ec) listing = "state.json: cannot list " + displayPath(dataRoot) + ": " + ec.message();

    const std::vector<InstalledPack> before = state->packs;  // the promotion below edits state->packs in place
    std::vector<InstalledPack> kept;
    for (auto& p : state->packs) {
        if (p.remove) continue;
        if (!p.pending.empty()) {
            // A pending version whose folder is gone (a half-finished install cleaned away) is forgotten, and the
            // active one keeps loading.
            // A manifest that cannot be looked for (an unreadable folder) is not a gone one: the pending version stays
            // for a launch that can read it.
            std::error_code lookEc;
            const bool present = fs::exists(dataRoot / p.id / p.pending / "pack.json", lookEc);
            if (present) p.active = p.pending;
            if (present || !lookEc) p.pending.clear();
        }
        kept.push_back(p);
    }
    const bool changed = !sameState(before, kept);
    state->packs = std::move(kept);
    if (changed && !writeHeld(dataRoot, *state, why)) return false;
    // Every version folder no entry names any more goes, unless a process holds it. A name that is no pack id or no
    // version (an install in progress starts with '.') is left alone, except the trash a deletion left behind.
    ec.clear();
    fs::directory_iterator ids(dataRoot, ec);
    for (; !ec && ids != fs::directory_iterator(); ids.increment(ec)) {
        std::error_code entryEc;
        const std::string id = displayPath(ids->path().filename());
        if (!ids->is_directory(entryEc) || !isPackId(id)) continue;
        const InstalledPack* entry = state->find(id);
        std::error_code inner;
        fs::directory_iterator versions(ids->path(), inner);
        for (; !inner && versions != fs::directory_iterator(); versions.increment(inner)) {
            const std::string version = displayPath(versions->path().filename());
            if (!versions->is_directory(entryEc)) continue;
            if (startsWith(version, ".") && version.find(kTrashMarker) != std::string::npos) {
                fs::remove_all(versions->path(), entryEc);
                continue;
            }
            if (!isPackVersion(version)) continue;
            if (entry && (entry->active == version || entry->pending == version)) continue;
            deleteUnheld(versions->path());
        }
        if (inner && listing.empty())
            listing = "state.json: cannot list " + displayPath(ids->path()) + ": " + inner.message();
        std::error_code empty;
        if (fs::is_empty(ids->path(), empty) && !empty) fs::remove(ids->path(), empty);
    }
    if (ec && listing.empty()) listing = "state.json: cannot list " + displayPath(dataRoot) + ": " + ec.message();
    if (!listing.empty()) return refuse(why, listing);
    return true;
}

}  // namespace rp::host::packs
```

- [ ] **Step 6: Add them to the library, with FileSync.cpp moved in**

In `packages/native/CMakeLists.txt`, replace

```cmake
add_library(retroplug-packs STATIC
    src/host/packs/PackDirs.cpp
    src/host/packs/PackManifest.cpp)
```

with

```cmake
add_library(retroplug-packs STATIC
    src/host/packs/PackDirs.cpp
    src/host/packs/PackManifest.cpp
    src/host/packs/PackState.cpp
    src/host/packs/FileLock.cpp
    # fsyncPath needs <windows.h> (FlushFileBuffers), so it stays out of line rather than in every includer. Here
    # because state.json's write needs it; the backend's HostRpcService reaches it through this library.
    src/util/FileSync.cpp)
```

and in `add_library(retroplug-backend STATIC ...)` delete these two lines:

```cmake
    # fsyncPath needs <windows.h> (FlushFileBuffers), so it stays out of line rather than in every includer.
    src/util/FileSync.cpp
```

- [ ] **Step 7: Run the tests**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 15 test cases)` (11 on Windows, which has no read-only folder case, no
unreadable-pending case and no fork cases).

Run: `cmake --build build --target retroplug-host -j$(nproc)`
Expected: `[100%] Built target retroplug-host` (the backend still links `fsyncPath`).

- [ ] **Step 8: Commit**

```bash
git add packages/native/src/host/packs/FileLock.hpp packages/native/src/host/packs/FileLock.cpp \
    packages/native/src/host/packs/PackState.hpp packages/native/src/host/packs/PackState.cpp \
    packages/native/test/packs/PackState.test.cpp packages/native/src/util/FileSync.hpp \
    packages/native/src/util/FileSync.cpp \
    packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "packs: state.json under its lock, written atomically and settled at a launch, and the version locks"
```

---

### Task 6: The native loader

**Files:**
- Create: `packages/native/src/host/packs/PackCandidate.hpp`
- Create: `packages/native/src/host/packs/NativeLibrary.hpp`, `packages/native/src/host/packs/NativeLibrary.cpp`
- Create: `packages/native/src/host/packs/PackTable.hpp`, `packages/native/src/host/packs/PackTable.cpp`
- Create: `packages/native/test/packs/PackLoader.test.cpp`, `packages/native/test/packs/ElfSymbols.hpp`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`,
  `packages/native/src/host/packs/PackManifest.{hpp,cpp}` (a parse given the host's level judges a manifest of another
  level on its level alone), `packages/node-sdk/native/src/graph/rp_pack.h` (one comment)

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/packs/ElfSymbols.hpp` (a module's dynamic symbols, which the loader's Linux case reads):

```cpp
#pragma once

// A pack module's dynamic symbols, read from its ELF file (Linux only): what it exports, and what it imports, so a
// case can hold a module to exporting rp_pack_entry alone and importing nothing of RetroPlug's, the two properties
// cmake/packs.cmake's version script and --no-undefined give it. Reads the section headers for SHT_DYNSYM and the
// string table it links, which every shared object the toolchain makes has.

#if defined(__linux__)

#include <elf.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace rp::host::packs::test {

struct DynamicSymbols {
    // Defined, global, weak or GNU-unique, with any visibility but hidden or internal (protected counts: it is in the
    // dynamic table for others to bind to). A GNU-unique symbol (an inline function's static,
    // a template's static member) is merged process-wide by glibc even under RTLD_LOCAL: the one way two copies'
    // statics really become one, so it counts as an export.
    std::vector<std::string> exported;
    std::vector<std::string> imported;  // undefined, global (a weak undefined one is optional and left out)
};

inline std::optional<DynamicSymbols> readDynamicSymbols(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(Elf64_Ehdr) || std::memcmp(bytes.data(), ELFMAG, SELFMAG) != 0 ||
        bytes[EI_CLASS] != ELFCLASS64)
        return std::nullopt;
    Elf64_Ehdr eh;
    std::memcpy(&eh, bytes.data(), sizeof eh);
    if (eh.e_shentsize < sizeof(Elf64_Shdr)) return std::nullopt;
    const auto section = [&](std::size_t i) {
        Elf64_Shdr sh;
        std::memcpy(&sh, bytes.data() + eh.e_shoff + i * eh.e_shentsize, sizeof sh);
        return sh;
    };
    if (eh.e_shoff + std::size_t{eh.e_shnum} * eh.e_shentsize > bytes.size()) return std::nullopt;
    for (std::size_t i = 0; i < eh.e_shnum; ++i) {
        const Elf64_Shdr dynsym = section(i);
        if (dynsym.sh_type != SHT_DYNSYM || dynsym.sh_entsize != sizeof(Elf64_Sym)) continue;
        if (dynsym.sh_link >= eh.e_shnum) return std::nullopt;
        const Elf64_Shdr strtab = section(dynsym.sh_link);
        if (dynsym.sh_offset + dynsym.sh_size > bytes.size() || strtab.sh_offset + strtab.sh_size > bytes.size())
            return std::nullopt;
        DynamicSymbols out;
        for (std::size_t k = 1; k < dynsym.sh_size / sizeof(Elf64_Sym); ++k) {  // entry 0 is the null symbol
            Elf64_Sym sym;
            std::memcpy(&sym, bytes.data() + dynsym.sh_offset + k * sizeof sym, sizeof sym);
            if (sym.st_name >= strtab.sh_size) return std::nullopt;
            const std::string name(bytes.data() + strtab.sh_offset + sym.st_name);
            const unsigned bind = ELF64_ST_BIND(sym.st_info);
            if (sym.st_shndx == SHN_UNDEF) {
                if (bind == STB_GLOBAL) out.imported.push_back(name);
            } else if ((bind == STB_GLOBAL || bind == STB_WEAK || bind == STB_GNU_UNIQUE) &&
                       ELF64_ST_VISIBILITY(sym.st_other) != STV_HIDDEN &&
                       ELF64_ST_VISIBILITY(sym.st_other) != STV_INTERNAL) {
                out.exported.push_back(name);
            }
        }
        return out;
    }
    return std::nullopt;
}

// Whether an imported name is RetroPlug's: a C symbol of ours (rp_...), or anything mangled that names namespace rp
// (its functions, vtables, typeinfo and guards all carry the nested name N2rp).
inline bool isRetroPlugSymbol(const std::string& name) {
    return name.rfind("rp_", 0) == 0 || (name.rfind("_Z", 0) == 0 && name.find("N2rp") != std::string::npos);
}

}  // namespace rp::host::packs::test

#endif  // __linux__
```

Create `packages/native/test/packs/PackLoader.test.cpp`:

```cpp
// The native loader (docs/downloadable-packs-loading-design.md section 2): the fixture pack loads from its staged
// folder, packs load in dependency order, and each broken variant (test/packs/fixture/CMakeLists.txt) is refused with
// its own reason and registers nothing. Each case loads into a PackTable of its own, seeded as the process's is with
// this binary's built-in packages, so no load outlives its case.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-loader]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <dlfcn.h>
#endif

#include "ElfSymbols.hpp"
#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "graph/rp_pack.h"
#include "host/packs/FileLock.hpp"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackTable.hpp"

// The fixture's types as this binary links them (retroplug-packfixture-nodes), beside the module's own copy.
extern "C" const rp_package* rp_packfixture_package(void);

using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::fixture;
using rp::host::packs::test::TempDir;

namespace {

std::vector<std::string> typesOf(const LoadedPack& pack) {
    std::vector<std::string> ids;
    for (const rp_package* p : pack.packages)
        for (std::uint32_t k = 0; k < p->node_type_count; ++k) ids.emplace_back(p->node_types[k]->id);
    return ids;
}

// `id` refused with a reason holding `reason`, recorded, and nothing added to the table.
void checkRefused(PackTable& table, const std::string& id, const std::string& reason) {
    INFO("pack " << id);
    const std::size_t before = table.size();
    const PackLoad load = table.load(fixture(id));
    INFO(load.error);
    CHECK_FALSE(load);
    CHECK_THAT(load.error, ContainsSubstring(reason));
    CHECK(table.size() == before);
    CHECK(table.find(id) == nullptr);
    REQUIRE_FALSE(table.refusals().empty());
    CHECK(table.refusals().back().id == id);
    CHECK(table.refusals().back().reason == load.error);
}

// A pack folder holding nothing but a pack.json, for the cases that never reach a library.
std::filesystem::path writePack(const TempDir& tmp, const std::string& id, const std::string& body) {
    const auto dir = tmp.path() / id;
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "pack.json") << body;
    return dir;
}

std::string manifestJson(const std::string& id, std::uint32_t level, const std::string& needs,
                         const std::string& halves) {
    return "{\"id\": \"" + id + "\", \"name\": \"n\", \"version\": \"1.0.0\", \"apiLevel\": " +
           std::to_string(level) + ", \"license\": \"MIT\", \"source\": \"s\", \"requires\": {\"packs\": " +
           needs + "}, \"provides\": {\"nodeTypes\": [\"rp." + id + ".t\"]}, \"halves\": {" + halves + "}}";
}

#if defined(__linux__)
// Whether `file` is mapped in this process: RTLD_NOLOAD answers a handle only for a library already open.
bool libraryIsOpen(const std::filesystem::path& file) {
    void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle != nullptr) dlclose(handle);
    return handle != nullptr;
}
#endif

}  // namespace

TEST_CASE("the fixture pack loads from its staged folder, once", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(fixture("packfixture"));
    INFO(load.error);
    REQUIRE(load);
    CHECK(load.pack->manifest.id == "packfixture");
    CHECK(load.pack->origin == PackOrigin::Development);
    CHECK(load.pack->library != nullptr);
    CHECK(typesOf(*load.pack) == std::vector<std::string>{"rp.packfixture.tone", "rp.packfixture.gain"});
    CHECK(load.pack->packageIds == std::vector<std::string>{"rp.packfixture"});
    CHECK(table.size() == 1);
    CHECK(table.find("packfixture") == load.pack);

    // The same folder again is the same load; the library is opened once.
    const PackLoad again = table.load(fixture("packfixture"));
    CHECK(again.pack == load.pack);
    CHECK(table.size() == 1);

    // Its library exports the entry and nothing else: the package entry it links is its own, and hidden.
    CHECK(librarySymbol(load.pack->library, RP_PACK_ENTRY_SYMBOL) != nullptr);
    CHECK(librarySymbol(load.pack->library, "rp_packfixture_package") == nullptr);
    // And so its package is not this binary's static copy of the same source.
    CHECK(load.pack->packages[0] != rp_packfixture_package());
}

#if defined(__linux__)
TEST_CASE("a pack module exports its entry alone and imports nothing of RetroPlug's", "[packs-loader]") {
    // What the version script and --no-undefined promise (cmake/packs.cmake), read from the files themselves.
    const std::string so = "native/" + std::string(currentPlatform()) + "/";
    const auto good =
        rp::host::packs::test::readDynamicSymbols((fixture("packfixture") / so / "rp-packfixture.so").string());
    REQUIRE(good);
    CHECK(good->exported == std::vector<std::string>{"rp_pack_entry"});
    for (const auto& name : good->imported) {
        INFO(name);
        CHECK_FALSE(rp::host::packs::test::isRetroPlugSymbol(name));
    }
    const auto withheld = rp::host::packs::test::readDynamicSymbols(
        (fixture("packfixture-noentry") / so / "rp-packfixture-noentry.so").string());
    REQUIRE(withheld);
    CHECK(withheld->exported == std::vector<std::string>{"rp_pack_entry_withheld"});
}
#endif

TEST_CASE("packs load in dependency order, whatever order they are given in", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    const auto loads = table.loadAll({{fixture("packfixture-dependent")}, {fixture("packfixture")}});
    REQUIRE(loads.size() == 2);
    REQUIRE(loads[0]);
    REQUIRE(loads[1]);
    CHECK(loads[0].pack->manifest.id == "packfixture");
    CHECK(loads[1].pack->manifest.id == "packfixture-dependent");
    const auto order = table.loadedFrom(0);
    REQUIRE(order.size() == 2);
    CHECK(order[0]->manifest.id == "packfixture");
    CHECK(table.loadedFrom(1).size() == 1);
}

TEST_CASE("a pack whose requirement is not loaded is refused, naming it", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-dependent", "requires pack packfixture, which is not loaded");
    // Through loadAll too: it waits for a pack that never comes, then is refused.
    const auto loads = table.loadAll({{fixture("packfixture-dependent")}});
    REQUIRE(loads.size() == 1);
    CHECK_FALSE(loads[0]);
    CHECK(table.size() == 0);
}

TEST_CASE("a pack built for another API level is refused, saying which side is out of date", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-api-newer",
                 "is built for API level " + std::to_string(RP_API_LEVEL + 1) + ", and this RetroPlug is level " +
                     std::to_string(RP_API_LEVEL) + ": update RetroPlug");
    checkRefused(table, "packfixture-api-older",
                 "is built for API level " + std::to_string(RP_API_LEVEL - 1) + ", and this RetroPlug is level " +
                     std::to_string(RP_API_LEVEL) + ": update the pack");
    // A manifest of another level is judged on its level alone: whatever else it changed (a half this host has no word
    // for, a member of another shape) must not hide the advice.
    {
        TempDir tmp;
        const auto dir = writePack(tmp, "levelpack",
                                   manifestJson("levelpack", RP_API_LEVEL + 1, "{}",
                                                "\"native\": \"rp-levelpack\", \"web\": \"js/w.js\""));
        const PackLoad load = table.load(dir);
        CHECK_FALSE(load);
        CHECK_THAT(load.error, ContainsSubstring("is built for API level " + std::to_string(RP_API_LEVEL + 1) +
                                                 ", and this RetroPlug is level " + std::to_string(RP_API_LEVEL) +
                                                 ": update RetroPlug"));
        // At this level the same manifest is refused for what it is.
        const auto same = writePack(tmp, "levelpack2",
                                    manifestJson("levelpack2", RP_API_LEVEL, "[]",
                                                 "\"native\": \"rp-levelpack2\", \"web\": \"js/w.js\""));
        CHECK_THAT(table.load(same).error, ContainsSubstring("halves.web is no half this RetroPlug loads"));
    }
    // A manifest that says this level over a library that reports another: the entry's answer is read first.
    checkRefused(table, "packfixture-api-mismatch",
                 "'s library reports API level " + std::to_string(RP_API_LEVEL + 1) + ", and this RetroPlug is level " +
                     std::to_string(RP_API_LEVEL) + ": update RetroPlug");
}

TEST_CASE("a pack whose library is missing or exports no entry is refused", "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-noplatform",
                 std::string("has no library for ") + std::string(currentPlatform()) + " (native/" +
                     std::string(currentPlatform()) + "/rp-packfixture-noplatform.");
    checkRefused(table, "packfixture-noentry", "'s library exports no rp_pack_entry");
    // An entry that throws is refused, the exception caught on this side of the C entry.
    checkRefused(table, "packfixture-throws", "'s rp_pack_entry threw");
    // A TS half beside the native one: M1 loads none, and a pack never loads in part.
    checkRefused(table, "packfixture-tshalf",
                 "has a TS half, halves.app (js/app.js), which this RetroPlug does not load yet");
}

TEST_CASE("a pack that registers what it does not declare, or what is registered already, is refused",
          "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    checkRefused(table, "packfixture-undeclared",
                 "registers node type rp.packfixture-undeclared.extra, which its pack.json's provides.nodeTypes does "
                 "not declare");
    checkRefused(table, "packfixture-duplicate",
                 "registers node type rp.core.add, which package rp.core has registered already");
    // provides stays truthful the other way too: a declared type the library never registers.
    checkRefused(table, "packfixture-overdeclared",
                 "'s pack.json declares node type rp.packfixture-overdeclared.missing, which its library does not "
                 "register");
    // Against another pack: the clone registers the good pack's types under another package id.
    REQUIRE(table.load(fixture("packfixture")));
    checkRefused(table, "packfixture-clone",
                 "registers node type rp.packfixture.tone, which package rp.packfixture has registered already");
    CHECK(table.size() == 1);
}

TEST_CASE("an installed pack holds its version folder while it is loaded", "[packs-loader]") {
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    {
        PackTable table(rp::graph::productPackages());
        const PackLoad load = table.load(PackCandidate{dir, PackOrigin::Installed, false, nullptr});
        INFO(load.error);
        REQUIRE(load);
        CHECK(load.pack->lock != nullptr);
        CHECK(versionInUse(dir));
    }
    CHECK_FALSE(versionInUse(dir));

    // A refused installed pack holds its folder too: its library may be open, and a pack's library is never closed.
    const auto refusedDir = rp::host::packs::test::install(tmp.roots().data, "packfixture-noentry");
    {
        PackTable table(rp::graph::productPackages());
        CHECK_FALSE(table.load(PackCandidate{refusedDir, PackOrigin::Installed, false, nullptr}));
        CHECK(versionInUse(refusedDir));
        // Retried, it is refused again and its folder still held (one hold per folder, kept by the table).
        CHECK_FALSE(table.load(PackCandidate{refusedDir, PackOrigin::Installed, false, nullptr}));
        CHECK(versionInUse(refusedDir));
    }
    CHECK_FALSE(versionInUse(refusedDir));
    // One refused before its library opens keeps no hold: nothing of its folder is mapped.
    const auto levelDir = rp::host::packs::test::install(tmp.roots().data, "packfixture-api-newer");
    {
        PackTable table(rp::graph::productPackages());
        CHECK_FALSE(table.load(PackCandidate{levelDir, PackOrigin::Installed, false, nullptr}));
        CHECK_FALSE(versionInUse(levelDir));
#if defined(__linux__)
        CHECK_FALSE(libraryIsOpen(levelDir / "native" / std::string(currentPlatform()) /
                                  "rp-packfixture-api-newer.so"));
#endif
    }

    // An installed pack must sit in packs/<id>/<version>/: anywhere else, state.json does not describe it.
    const auto stray = rp::host::packs::test::install(tmp.roots().data, "packfixture", "9.9.9");
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(PackCandidate{stray, PackOrigin::Installed, false, nullptr});
    CHECK_FALSE(load);
    CHECK_THAT(load.error, ContainsSubstring("which is not packs/packfixture/1.0.0"));
}

TEST_CASE("a library of two packages loads both, and a type its second takes from a loaded pack refuses it whole",
          "[packs-loader]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad pair = table.load(fixture("packfixture-pair"));
    INFO(pair.error);
    REQUIRE(pair);
    CHECK(pair.pack->packageIds == std::vector<std::string>{"rp.packfixture-pair", "rp.packfixture-pair-second"});
    CHECK(typesOf(*pair.pack) == std::vector<std::string>{"rp.packfixture-pair.tone", "rp.packfixture-pair.gain",
                                                           "rp.packfixture.tone", "rp.packfixture.gain"});
    // Its second package holds the good pack's types now, so the good pack is refused, and nothing of it registers.
    checkRefused(table, "packfixture",
                 "registers node type rp.packfixture.tone, which package rp.packfixture-pair-second has registered "
                 "already");
}

TEST_CASE("a second folder of a loaded pack's id is refused until the next launch", "[packs-loader]") {
    TempDir tmp;
    const auto copy = rp::host::packs::test::install(tmp.path(), "packfixture");
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(fixture("packfixture")));
    const PackLoad second = table.load(copy);
    CHECK_FALSE(second);
    CHECK_THAT(second.error, ContainsSubstring("is loaded already from"));
    CHECK(table.size() == 1);
}

TEST_CASE("an installed folder named with a trailing separator or a dot loads as the folder it is", "[packs-loader]") {
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(PackCandidate{std::filesystem::path(dir.string() + "/"), PackOrigin::Installed,
                                                   false, nullptr});
    INFO(load.error);
    REQUIRE(load);
    CHECK(load.pack->dir == dir);
    // The same folder spelled another way is the same load.
    CHECK(table.load(PackCandidate{dir / ".", PackOrigin::Installed, false, nullptr}).pack == load.pack);
    CHECK(table.size() == 1);
}

TEST_CASE("an installed folder first loaded as a development one is still held", "[packs-loader]") {
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    {
        PackTable table(rp::graph::productPackages());
        const PackLoad dev = table.load(PackCandidate{dir, PackOrigin::Development, false, nullptr});
        REQUIRE(dev);
        CHECK_FALSE(versionInUse(dir));
        // Its installed load answers the same pack, and the hold it took stays with the table.
        const PackLoad installed = table.load(PackCandidate{dir, PackOrigin::Installed, false, nullptr});
        CHECK(installed.pack == dev.pack);
        CHECK(versionInUse(dir));
    }
    CHECK_FALSE(versionInUse(dir));
}

TEST_CASE("a pack refused after its library opened keeps its id: no second library of it opens", "[packs-loader]") {
    TempDir tmp;
    const auto first = rp::host::packs::test::install(tmp.roots().data, "packfixture-noentry");
    PackTable table(rp::graph::productPackages());
    CHECK_FALSE(table.load(PackCandidate{first, PackOrigin::Installed, false, nullptr}));
    const PackLoad second = table.load(fixture("packfixture-noentry"));
    CHECK_FALSE(second);
    CHECK_THAT(second.error, ContainsSubstring("'s library was opened already from"));
#if defined(__linux__)
    // The refused copy's library is open, and the refusal says so (the staged copy is opened by other cases of this
    // binary, so its not being open is not something a case here can show).
    CHECK(libraryIsOpen(first / "native" / std::string(currentPlatform()) / "rp-packfixture-noentry.so"));
#endif
}

TEST_CASE("packs that require each other are refused as a cycle, one waiting on them as waiting",
          "[packs-loader]") {
    TempDir tmp;
    const auto a = writePack(tmp, "cyc-a",
                              manifestJson("cyc-a", RP_API_LEVEL, "[\"cyc-b\"]", "\"native\": \"rp-cyc-a\""));
    const auto b = writePack(tmp, "cyc-b",
                              manifestJson("cyc-b", RP_API_LEVEL, "[\"cyc-a\"]", "\"native\": \"rp-cyc-b\""));
    const auto c = writePack(tmp, "cyc-c",
                              manifestJson("cyc-c", RP_API_LEVEL, "[\"cyc-a\"]", "\"native\": \"rp-cyc-c\""));
    PackTable table(rp::graph::productPackages());
    const auto loads = table.loadAll({{a}, {b}, {c}});
    REQUIRE(loads.size() == 3);
    CHECK_THAT(loads[0].error, ContainsSubstring("requires cycle"));
    CHECK_THAT(loads[1].error, ContainsSubstring("requires cycle"));
    CHECK_THAT(loads[2].error, ContainsSubstring("requires pack cyc-a, which is not loaded"));
    CHECK(table.size() == 0);
}
```

In `retroplug-packs-test`'s `SOURCES`, after the `PackState.test.cpp` line add:

```cmake
             test/packs/PackLoader.test.cpp        # the table: loads, dependency order, every refusal
```

and in its `LIBS`, after the `retroplug-backend` line add:

```cmake
             retroplug-packfixture-nodes           # the fixture's types, statically: a second copy beside the pack's
```

- [ ] **Step 2: Run the build to see it fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL with `fatal error: host/packs/NativeLibrary.hpp: No such file or directory`.

- [ ] **Step 3: Write the candidate types**

Create `packages/native/src/host/packs/PackCandidate.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "host/packs/FileLock.hpp"

// What a launch hands the loader (host/packs/PackDiscovery.hpp) and what the loader answers when it refuses
// (host/packs/PackTable.hpp): a version folder and where it came from, and a refusal with its reason.
namespace rp::host::packs {

enum class PackOrigin { Installed, Development };

// A pack the loader may load: its version folder and where it was found. A candidate found by a launch carries the
// hold on its folder the launch took while it held state.lock (loadLaunchPacks), so no other launch's settle can delete
// the folder between its discovery and its load: an installed one, or a development one that lies inside the data
// root. The loader takes a hold itself for an installed candidate without one, and none for a development one.
struct PackCandidate {
    std::filesystem::path     dir;
    PackOrigin                origin = PackOrigin::Development;
    bool                      overridesInstalled = false;  // a development pack standing in for an installed one
    std::shared_ptr<FileLock> lock;                        // the hold discovery took, when it took one
};

struct PackRefusal {
    std::string           id;  // the manifest's id, or the folder's name when the manifest did not parse
    std::filesystem::path dir;
    std::string           reason;
};

}  // namespace rp::host::packs
```

- [ ] **Step 4: Write NativeLibrary**

Create `packages/native/src/host/packs/NativeLibrary.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>

// A pack's native library, opened for the life of the process. There is no close: a pack's code is never unloaded
// (docs/downloadable-packs-loading-design.md section 2), and a refused pack's library stays mapped too, since opening
// it already ran its static constructors and whatever they registered (an atexit handler among them) must still find
// its code at exit.
namespace rp::host::packs {

// The library at `file` (made absolute here), or null with the loader's reason in *why. Its symbols stay local to it
// (RTLD_LOCAL): a pack's copies of what it links are its own, and nothing loaded later binds to them. Opening a library
// runs its static constructors before anything of it can be checked, the entry's API level included: what keeps an
// honest pack of another level from being opened at all is the manifest's level, which the loader compares first.
void* openLibrary(const std::filesystem::path& file, std::string* why);

// The symbol `name` in `library`, or null.
void* librarySymbol(void* library, const char* name);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/NativeLibrary.cpp`:

```cpp
#include "host/packs/NativeLibrary.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "host/packs/PackDirs.hpp"

namespace rp::host::packs {

void* openLibrary(const std::filesystem::path& file, std::string* why) {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::absolute(file, ec);
    if (ec) {
        if (why) *why = "cannot load " + displayPath(file) + ": " + ec.message();
        return nullptr;
    }
#if defined(_WIN32)
    // The DLL's own folder is searched for the DLLs it depends on, then the application's folder, System32 and any
    // folder added with AddDllDirectory: never PATH or the current directory.
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module == nullptr && why) {
        const DWORD error = GetLastError();  // before anything else can change it
        *why = "cannot load " + displayPath(path) + ": error " + std::to_string(error);
    }
    return reinterpret_cast<void*>(module);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr && why) {
        const char* error = dlerror();
        *why = "cannot load " + displayPath(path) + ": " + (error ? error : "unknown error");
    }
    return handle;
#endif
}

void* librarySymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

}  // namespace rp::host::packs
```

- [ ] **Step 5: Write PackTable**

Create `packages/native/src/host/packs/PackTable.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "graph/rp_node.h"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackCandidate.hpp"
#include "host/packs/PackManifest.hpp"

// The packs a process has loaded (docs/downloadable-packs-loading-design.md section 2): each pack's library opened
// once and never closed, its manifest, and the rp_packages its entry returned. Every Engine in the process reads one
// table (host/packs/ProcessPacks.hpp), so a plugin's instances, its render jobs and the standalone share each load. An
// Engine takes what the table holds when it is built, then each pack loaded since at its next setGraph
// (Engine::syncPacks), on its own control thread: the table never reaches into an Engine.
//
// The table is append-only and its packs are immutable once appended, so a pointer it hands out is valid for the
// table's life. Loads are serialised by one mutex, which loadedFrom takes too: any thread may load while Engines read.
namespace rp::host::packs {

struct LoadedPack {
    PackManifest                   manifest;
    std::filesystem::path          dir;
    PackOrigin                     origin = PackOrigin::Development;
    bool                           overridesInstalled = false;
    std::vector<const rp_package*> packages;  // what rp_pack_entry returned, in its order; empty with no native half
    std::vector<std::string>       packageIds;  // each package's manifest id, index for index
    void*                          library = nullptr;  // never closed
    std::shared_ptr<FileLock>      lock;  // the hold on its version folder: installed, or development in the data root
};

// A load's answer: the pack, or null with the reason.
struct PackLoad {
    const LoadedPack* pack = nullptr;
    std::string       error;
    explicit operator bool() const { return pack != nullptr; }
};

class PackTable {
public:
    // `builtIns` are the packages every Engine of this process registers before any pack (the product's
    // rp_register_packages): a pack may not take one of their node types or package ids. They must outlive the table,
    // as a product's do. requires.packs names packs only, never a built-in package.
    explicit PackTable(std::span<const rp_package* const> builtIns = {});
    PackTable(const PackTable&) = delete;
    PackTable& operator=(const PackTable&) = delete;

    // One pack, all or nothing: refused, it is recorded in refusals() and adds nothing to the table. A pack whose id
    // is loaded already answers that load when it is the same folder, and is refused when it is another (a second
    // version takes effect at the next launch: native code is never unloaded).
    PackLoad load(const PackCandidate& candidate);
    PackLoad load(const std::filesystem::path& dir) { return load(PackCandidate{dir}); }
    // Several packs in dependency order: each loads once every pack its requires.packs names has, whatever order the
    // candidates come in; one whose requirement never loads is refused naming it. Answers in the order they loaded,
    // the refused ones last. `refused`, when given, receives the refusals this call recorded, in the order recorded:
    // the table's own refusals() also hold other threads' loads, which a caller of one call must not claim.
    std::vector<PackLoad> loadAll(const std::vector<PackCandidate>& candidates,
                                  std::vector<PackRefusal>* refused = nullptr);

    std::size_t                    size() const;
    const LoadedPack*              find(std::string_view id) const;
    // The packs loaded at or after position `first`, in load order (dependency order): what an Engine that has
    // registered `first` of them has yet to register.
    std::vector<const LoadedPack*> loadedFrom(std::size_t first) const;
    std::vector<PackRefusal>       refusals() const;

private:
    PackLoad loadLocked(const PackCandidate& offered);
    PackLoad refuse(const PackCandidate& candidate, std::string id, std::string reason,
                    std::shared_ptr<FileLock> hold);

    std::vector<const rp_package*>           builtIns_;
    mutable std::mutex                       mutex_;
    std::vector<std::unique_ptr<LoadedPack>> packs_;
    std::vector<PackRefusal>                 refusals_;
    // The holds on installed folders a LoadedPack does not carry, one per folder: a pack refused after its library
    // opened (the library stays open, never closed, so its folder is held for the life of the process as a loaded
    // one's is), and an installed folder whose pack this table loaded earlier as a development one.
    std::map<std::filesystem::path, std::shared_ptr<FileLock>> folderHolds_;
    // The folder each id's library was first opened from, loaded or refused: a second library of one id would be a
    // second rp-<id> in the process, which Windows matches an already-loaded module by, so it never opens.
    std::map<std::string, std::filesystem::path> openedFrom_;
};

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/PackTable.cpp`:

```cpp
#include "host/packs/PackTable.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <system_error>
#include <thread>

#include <rfl.hpp>
#include <rfl/from_generic.hpp>

#include "graph/CanonicalJson.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/rp_pack.h"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackDirs.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// An rp_package's id, from its manifest JSON; empty when it has none (the registry refuses such a package anyway).
std::string packageIdOf(const rp_package* package) {
    struct Id {
        std::string id;
    };
    if (package == nullptr || package->manifest_json == nullptr) return {};
    // The registry's own parser, so a repeated key reads here as it reads there.
    const auto parsed = rp::graph::parseJson(package->manifest_json).and_then([](const rfl::Generic& g) {
        return rfl::from_generic<Id>(g);
    });
    return parsed ? parsed->id : std::string();
}

// Which side is out of date, for a level that is not this host's.
std::string levelAdvice(std::uint32_t level) {
    return level > RP_API_LEVEL ? "update RetroPlug" : "update the pack";
}

fs::path canonicalOr(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

// The entry, called inside a handler. The entry must not throw (it is C), and one that does is refused rather than let
// unwind into the host. The call goes through a pointer whose type is declared outside any extern "C", which keeps the
// handler on compilers that tie /EHsc-style assumptions to language linkage; MSVC does not put linkage in a function's
// type, so there the handler rests on the call being through a pointer, which it cannot assume never throws. The
// throwing fixture variant holds it on Linux in the packs test, and the Windows gate (the plan's Task 6) on MSVC.
using EntryCall = const rp_package* const* (*)(std::uint32_t*, std::size_t*);

bool callEntry(EntryCall entry, std::uint32_t& level, std::size_t& count, const rp_package* const*& list) noexcept {
    try {
        list = entry(&level, &count);
        return true;
    } catch (...) {
        return false;
    }
}

// The scratch registry's check of one package, which calls into the pack's code (each node type's descriptor), inside
// the same kind of handler as the entry's: an exception from the pack's C code is refused, not unwound into the host.
// `thrown` is set when one was caught, and `at` names the node type whose descriptor threw when it can be told. (No
// fixture variant throws from a descriptor, so this is held by reading, not by a case.)
bool trialAdd(rp::graph::NodeRegistry& trial, const rp_package* package, std::string* why, bool& thrown,
              std::string& at) noexcept {
    try {
        return trial.addPackage(package, why);
    } catch (...) {
        thrown = true;
        for (std::uint32_t k = 0; package->node_types && k < package->node_type_count; ++k) {
            const rp_node_type* type = package->node_types[k];
            if (type == nullptr || type->descriptor == nullptr || type->id == nullptr) continue;
            try {
                (void)type->descriptor(type);
            } catch (...) {
                at = type->id;
                break;
            }
        }
        return false;
    }
}

// `dir` as the table keys it: lexically normal and with no trailing separator, so "packs/a/1.0.0/" and
// "packs/a/1.0.0" are one folder to the placement check and to the refusal's name.
fs::path normalisedDir(const fs::path& dir) {
    fs::path p = dir.lexically_normal();
    if (!p.has_filename() && p.has_parent_path()) p = p.parent_path();
    return p;
}

}  // namespace

PackTable::PackTable(std::span<const rp_package* const> builtIns) : builtIns_(builtIns.begin(), builtIns.end()) {}

std::size_t PackTable::size() const {
    std::lock_guard<std::mutex> hold(mutex_);
    return packs_.size();
}

const LoadedPack* PackTable::find(std::string_view id) const {
    std::lock_guard<std::mutex> hold(mutex_);
    for (const auto& p : packs_)
        if (p->manifest.id == id) return p.get();
    return nullptr;
}

std::vector<const LoadedPack*> PackTable::loadedFrom(std::size_t first) const {
    std::lock_guard<std::mutex> hold(mutex_);
    std::vector<const LoadedPack*> out;
    for (std::size_t i = first; i < packs_.size(); ++i) out.push_back(packs_[i].get());
    return out;
}

std::vector<PackRefusal> PackTable::refusals() const {
    std::lock_guard<std::mutex> hold(mutex_);
    return refusals_;
}

PackLoad PackTable::load(const PackCandidate& candidate) {
    std::lock_guard<std::mutex> hold(mutex_);
    return loadLocked(candidate);
}

std::vector<PackLoad> PackTable::loadAll(const std::vector<PackCandidate>& candidates,
                                         std::vector<PackRefusal>* refused) {
    std::lock_guard<std::mutex> hold(mutex_);
    const std::size_t refusedBefore = refusals_.size();
    std::vector<PackLoad> out;
    // Each candidate's requirements, read once; one whose manifest does not parse is loaded straight away, which
    // refuses it with the parse's reason.
    struct Pending {
        PackCandidate            candidate;
        std::string              id;
        std::vector<std::string> needs;
    };
    std::vector<Pending> pending;
    for (const auto& c : candidates) {
        std::string why;
        const auto manifest = readPackManifest(c.dir, &why, RP_API_LEVEL);
        if (!manifest) {
            out.push_back(loadLocked(c));
            continue;
        }
        pending.push_back({c, manifest->id, manifest->requiresPacks});
    }
    const auto loaded = [&](const std::string& dep) {
        for (const auto& p : packs_)
            if (p->manifest.id == dep) return true;
        return false;
    };
    // Kahn's order without a graph: each pass loads every pending pack whose requirements are all loaded, in the order
    // given, until a pass loads none. What is left waits on a pack that never loaded (absent, refused, or a cycle), and
    // its own load refuses it naming the requirement.
    for (bool progress = true; progress && !pending.empty();) {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            bool ready = true;
            for (const auto& dep : it->needs) ready = ready && loaded(dep);
            if (!ready) {
                ++it;
                continue;
            }
            out.push_back(loadLocked(it->candidate));
            it = pending.erase(it);
            progress = true;
        }
    }
    // A leftover that can reach itself through the other leftovers' requirements is in a cycle, and says so; any other
    // waits on a pack that is absent or refused, which its own load names.
    const auto inCycle = [&](const Pending& start) {
        std::vector<std::string> seen;
        std::vector<std::string> todo = start.needs;
        while (!todo.empty()) {
            const std::string at = todo.back();
            todo.pop_back();
            if (at == start.id) return true;
            if (std::find(seen.begin(), seen.end(), at) != seen.end()) continue;
            seen.push_back(at);
            for (const auto& q : pending)
                if (q.id == at) todo.insert(todo.end(), q.needs.begin(), q.needs.end());
        }
        return false;
    };
    for (const auto& p : pending) {
        if (inCycle(p))
            out.push_back(refuse(p.candidate, p.id, "pack " + p.id + " is part of a requires cycle: it waits, through "
                                                    "requires.packs, on itself", nullptr));
        else
            out.push_back(loadLocked(p.candidate));
    }
    if (refused != nullptr)
        refused->assign(refusals_.begin() + static_cast<std::ptrdiff_t>(refusedBefore), refusals_.end());
    return out;
}

PackLoad PackTable::refuse(const PackCandidate& candidate, std::string id, std::string reason,
                           std::shared_ptr<FileLock> hold) {
    if (id.empty()) id = displayPath(candidate.dir.filename());
    // One hold per folder, however often a load of it is retried and refused.
    if (hold) folderHolds_.emplace(canonicalOr(candidate.dir), std::move(hold));
    refusals_.push_back({id, candidate.dir, reason});
    return PackLoad{nullptr, std::move(reason)};
}

PackLoad PackTable::loadLocked(const PackCandidate& offered) {
    PackCandidate candidate = offered;
    candidate.dir = normalisedDir(offered.dir);
    std::string why;
    // An installed folder is held before anything of it is read, and kept whatever the answer: a launch's settle must
    // never delete a folder this process has opened a library from, loaded or refused.
    std::shared_ptr<FileLock> hold = candidate.lock;
    if (candidate.origin == PackOrigin::Installed && !hold) {
        FileLock::Failure failure = FileLock::Failure::None;
        std::optional<FileLock> taken = holdVersionBriefly(candidate.dir, &why, &failure);
        if (taken) {
            hold = std::make_shared<FileLock>(std::move(*taken));
        } else if (failure != FileLock::Failure::Denied) {
            return refuse(candidate, {}, "cannot hold its version folder: " + why, nullptr);
        }
        // Denied: a folder this process cannot write, which no settle can delete either, so it loads unheld.
    }
    // A refusal keeps the folder's hold only once the pack's library is open: until then nothing of the folder is
    // mapped, and the hold goes with the refusal.
    bool opened = false;
    const auto refuseHere = [&](std::string id, std::string reason) {
        return refuse(candidate, std::move(id), std::move(reason), opened ? hold : nullptr);
    };

    auto manifest = readPackManifest(candidate.dir, &why, RP_API_LEVEL);
    if (!manifest) return refuseHere({}, why);
    const std::string& id = manifest->id;
    const std::string pack = "pack " + id;

    // The manifest's API level before anything else of it is used (a manifest of another level read no further).
    if (manifest->apiLevel != RP_API_LEVEL)
        return refuseHere(id, pack + " is built for API level " + std::to_string(manifest->apiLevel) +
                                  ", and this RetroPlug is level " + std::to_string(RP_API_LEVEL) + ": " +
                                  levelAdvice(manifest->apiLevel));

    // An installed pack is packs/<id>/<version>/: anything else is a folder state.json does not describe.
    if (candidate.origin == PackOrigin::Installed &&
        (candidate.dir.filename() != manifest->version || candidate.dir.parent_path().filename() != id))
        return refuseHere(id, pack + " " + manifest->version + " is installed in " + displayPath(candidate.dir) +
                                  ", which is not packs/" + id + "/" + manifest->version);

    // Only the native half loads in M1, and a pack loads whole or not at all.
    for (const auto& [kind, file] : manifest->halves)
        if (kind != "native")
            return refuseHere(id, pack + " has a TS half, halves." + kind + " (" + file +
                                      "), which this RetroPlug does not load yet; a pack loads whole or not at all");

    for (const auto& p : packs_) {
        if (p->manifest.id != id) continue;
        if (canonicalOr(p->dir) == canonicalOr(candidate.dir)) {
            // An installed load of a folder first loaded as a development one: the library is open from it, so the
            // hold this call took is kept rather than dropped with the call. (A development load of an installed
            // folder, the other order, takes no hold: it is the caller's to ask for the folder as installed.)
            if (hold && !p->lock) folderHolds_.emplace(canonicalOr(candidate.dir), hold);
            return PackLoad{p.get(), {}};
        }
        return refuseHere(id, pack + " is loaded already from " + displayPath(p->dir) +
                                  "; another copy takes effect at the next launch");
    }

    // A library of this id opened before and refused: a second copy of it never opens in this process.
    if (const auto it = openedFrom_.find(id); it != openedFrom_.end() && it->second != canonicalOr(candidate.dir))
        return refuseHere(id, pack + "'s library was opened already from " + displayPath(it->second) +
                                  " and refused; another copy takes effect at the next launch");

    // requires.packs names packs, and only packs: a built-in package is not a pack a manifest may name.
    for (const auto& dep : manifest->requiresPacks) {
        bool found = false;
        for (const auto& p : packs_) found = found || p->manifest.id == dep;
        if (!found) return refuseHere(id, pack + " requires pack " + dep + ", which is not loaded");
    }

    auto loaded = std::make_unique<LoadedPack>();
    loaded->dir = candidate.dir;
    loaded->origin = candidate.origin;
    loaded->overridesInstalled = candidate.overridesInstalled;

    std::set<std::string> registered;  // every node type the library registers
    const std::string stem = manifest->nativeHalf();
    if (!stem.empty()) {
        const std::string_view platform = currentPlatform();
        if (platform.empty()) return refuseHere(id, pack + " has a native half, and this platform has none");
        const fs::path rel = fs::path("native") / std::string(platform) / libraryFileName(stem);
        std::error_code ec;
        const fs::path file = fs::absolute(candidate.dir / rel, ec);
        if (ec || !fs::is_regular_file(file, ec))
            return refuseHere(id, pack + " has no library for " + std::string(platform) + " (" + rel.generic_string() +
                                      ")");
        void* library = openLibrary(file, &why);
        if (library == nullptr) return refuseHere(id, pack + ": " + why);
        loaded->library = library;
        opened = true;
        openedFrom_.emplace(id, canonicalOr(candidate.dir));
        auto entry = reinterpret_cast<EntryCall>(librarySymbol(library, RP_PACK_ENTRY_SYMBOL));
        if (entry == nullptr) return refuseHere(id, pack + "'s library exports no " RP_PACK_ENTRY_SYMBOL);
        std::uint32_t level = 0;
        std::size_t count = 0;
        const rp_package* const* list = nullptr;
        if (!callEntry(entry, level, count, list)) return refuseHere(id, pack + "'s " RP_PACK_ENTRY_SYMBOL " threw");
        if (level != RP_API_LEVEL)
            return refuseHere(id, pack + "'s library reports API level " + std::to_string(level) +
                                      ", and this RetroPlug is level " + std::to_string(RP_API_LEVEL) + ": " +
                                      levelAdvice(level));
        if (list == nullptr || count == 0) return refuseHere(id, pack + "'s library registers no package");
        loaded->packages.assign(list, list + count);

        // Who holds each type id now: the built-ins, then every pack loaded before this one.
        std::map<std::string, std::string, std::less<>> holder;
        const auto holdTypes = [&](const rp_package* p) {
            if (p == nullptr) return;
            for (std::uint32_t k = 0; k < p->node_type_count; ++k)
                if (p->node_types && p->node_types[k] && p->node_types[k]->id)
                    holder.emplace(p->node_types[k]->id, packageIdOf(p));
        };
        for (const rp_package* b : builtIns_) holdTypes(b);
        for (const auto& p : packs_)
            for (const rp_package* q : p->packages) holdTypes(q);
        for (const rp_package* p : loaded->packages) {
            if (p == nullptr) return refuseHere(id, pack + "'s library returned a null package");
            if (p->abi_version != RP_ABI_VERSION) continue;  // the registry's trial below names the ABI mismatch
            for (std::uint32_t k = 0; p->node_types && k < p->node_type_count; ++k) {
                const rp_node_type* type = p->node_types[k];
                if (type == nullptr || type->id == nullptr) continue;  // the trial names it
                const std::string typeId = type->id;
                registered.insert(typeId);
                bool declared = false;
                for (const auto& d : manifest->providesNodeTypes) declared = declared || d == typeId;
                if (!declared)
                    return refuseHere(id, pack + " registers node type " + typeId +
                                              ", which its pack.json's provides.nodeTypes does not declare");
                if (const auto it = holder.find(typeId); it != holder.end())
                    return refuseHere(id, pack + " registers node type " + typeId + ", which package " + it->second +
                                              " has registered already");
            }
        }
        // Everything else the registry checks (descriptors, layouts, package ids, the ABI version, a clash between the
        // pack's own packages), on a scratch registry holding what every Engine will hold before it, so an Engine never
        // meets a refusal the table let through.
        rp::graph::NodeRegistry trial;
        for (const rp_package* b : builtIns_) trial.addPackage(b);
        for (const auto& p : packs_)
            for (const rp_package* q : p->packages) trial.addPackage(q);
        for (const rp_package* p : loaded->packages) {
            bool thrown = false;
            std::string at;
            if (!trialAdd(trial, p, &why, thrown, at)) {
                if (thrown) {
                    const std::string what = at.empty() ? "types were checked" : "type " + at + " was described";
                    return refuseHere(id, pack + " threw while its node " + what);
                }
                return refuseHere(id, pack + ": " + why);
            }
            loaded->packageIds.push_back(packageIdOf(p));
        }
    }
    // provides stays truthful: part 5 resolves a project's missing type to the pack that declares it, so a declared
    // type the library does not register is refused as an undeclared one is.
    for (const auto& d : manifest->providesNodeTypes)
        if (registered.count(d) == 0)
            return refuseHere(id, pack + "'s pack.json declares node type " + d +
                                      ", which its library does not register");

    loaded->lock = std::move(hold);
    loaded->manifest = std::move(*manifest);
    packs_.push_back(std::move(loaded));
    return PackLoad{packs_.back().get(), {}};
}

}  // namespace rp::host::packs
```

In `packages/native/CMakeLists.txt`, in `add_library(retroplug-packs STATIC ...)`, after the
`src/host/packs/FileLock.cpp` line add:

```cmake
    src/host/packs/NativeLibrary.cpp
    src/host/packs/PackTable.cpp
```

- [ ] **Step 6: Hold the manifest's level first**

A manifest of another API level may have changed any other member (a half this host has no word for, a member of
another shape), and it must be refused for its level, "update RetroPlug", not for whatever else it changed. Task 4's
parser gains an optional host level: after its size, nesting and duplicate-key gates, a manifest whose `apiLevel`
differs from it is answered with `levelMismatch` set and only `id` and `apiLevel` filled, and `PackTable` (above) passes
`RP_API_LEVEL` and refuses such a manifest before it reads anything else. Apply these hunks to Task 4's files, and the
`rp_pack.h` one to Task 1's header (a comment only: opening a library runs its static constructors whatever its level,
so the manifest's level is the gate that keeps an honest pack of another level from being opened):

`packages/native/src/host/packs/PackManifest.cpp`:

```diff
@@ -33,6 +33,11 @@ struct RawProvides {
     std::optional<std::vector<std::string>> roles;
     rfl::ExtraFields<rfl::Generic>          extra;
 };
+// Just the members the level gate reads, so a manifest of another level is judged before the rest of it is.
+struct LevelPeek {
+    std::optional<std::string>  id;
+    std::optional<std::int64_t> apiLevel;
+};
 struct RawManifest {
     std::optional<std::string>                                id;
     std::optional<std::string>                                name;
@@ -310,7 +315,7 @@ bool isPackId(std::string_view id) {
     return true;
 }
 
-std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why) {
+std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why, std::uint32_t hostLevel) {
     const ScanResult scan = DuplicateKeyScan(json).find();
     if (scan.kind == ScanResult::Kind::TooDeep) {
         refuse(why, "nested deeper than " + std::to_string(kMaxNesting) + " levels");
@@ -320,6 +325,17 @@ std::optional<PackManifest> parsePackManifest(std::string_view json, std::string
         refuse(why, "the member \"" + echoed(scan.key) + "\" appears twice in one object");
         return std::nullopt;
     }
+    if (hostLevel != 0) {
+        const auto peek = rfl::json::read<LevelPeek>(std::string(json));
+        if (peek && peek->id && isPackId(*peek->id) && peek->apiLevel && *peek->apiLevel >= 0 &&
+            *peek->apiLevel <= 0xffffffffll && *peek->apiLevel != static_cast<std::int64_t>(hostLevel)) {
+            PackManifest m;
+            m.id = *peek->id;
+            m.apiLevel = static_cast<std::uint32_t>(*peek->apiLevel);
+            m.levelMismatch = true;
+            return m;
+        }
+    }
     auto raw = rfl::json::read<RawManifest>(std::string(json));
     if (!raw) {
         refuse(why, std::string("not a manifest: ") + echoed(raw.error().what(), 256));
@@ -413,7 +429,7 @@ std::optional<PackManifest> parsePackManifest(std::string_view json, std::string
     return m;
 }
 
-std::optional<PackManifest> readPackManifest(const fs::path& versionDir, std::string* why) {
+std::optional<PackManifest> readPackManifest(const fs::path& versionDir, std::string* why, std::uint32_t hostLevel) {
     const fs::path file = versionDir / "pack.json";
     std::error_code ec;
     const auto size = fs::is_regular_file(file, ec) ? fs::file_size(file, ec) : 0;
@@ -438,7 +454,7 @@ std::optional<PackManifest> readPackManifest(const fs::path& versionDir, std::st
         if (why) *why = "pack.json: too large (over " + std::to_string(kMaxManifestBytes) + " bytes)";
         return std::nullopt;
     }
-    return parsePackManifest(text, why);
+    return parsePackManifest(text, why, hostLevel);
 }
 
 }  // namespace rp::host::packs
```

`packages/native/src/host/packs/PackManifest.hpp`:

```diff
@@ -31,6 +31,9 @@ struct PackManifest {
     // bound to the id, so two packs' libraries never share a file name, which Windows matches a loaded module by), the
     // others a JS file inside the version folder. A kind no RetroPlug knows is refused here.
     std::map<std::string, std::string> halves;
+    // Set only by a parse given a host level (below) that this manifest's apiLevel differs from: only id and apiLevel
+    // are read then, since a manifest of another level may spell every other member in ways this host does not know.
+    bool levelMismatch = false;
 
     // The native half's library stem, or empty for a pack with no native half.
     std::string nativeHalf() const;
@@ -45,10 +48,14 @@ bool isPackId(std::string_view id);
 // Whether `version` is MAJOR.MINOR.PATCH in decimal with no leading zero: it names a folder, so nothing else may.
 bool isPackVersion(std::string_view version);
 
-// `json` as a manifest, or nothing with the reason in *why (each reason starts "pack.json: ").
-std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why);
+// `json` as a manifest, or nothing with the reason in *why (each reason starts "pack.json: "). With a non-zero
+// `hostLevel`, a manifest whose apiLevel differs is answered as soon as its size, nesting and duplicate keys have been
+// checked (those gates always run first, for safety), with levelMismatch set and only id and apiLevel filled: a pack
+// of another level is refused for its level, whatever else it has changed since this host's.
+std::optional<PackManifest> parsePackManifest(std::string_view json, std::string* why, std::uint32_t hostLevel = 0);
 
 // <versionDir>/pack.json read and parsed; nothing, with the reason, when it cannot be read or does not parse.
-std::optional<PackManifest> readPackManifest(const std::filesystem::path& versionDir, std::string* why);
+std::optional<PackManifest> readPackManifest(const std::filesystem::path& versionDir, std::string* why,
+                                             std::uint32_t hostLevel = 0);
 
 }  // namespace rp::host::packs
```

`packages/node-sdk/native/src/graph/rp_pack.h`:

```diff
@@ -3,8 +3,10 @@
  * A pack (docs/downloadable-packs-loading-design.md section 2) is a folder a RetroPlug loads at run time; its native
  * half is one dynamic library exporting rp_pack_entry and nothing else. The entry answers the API level the library
  * was built against and the same rp_package list a built-in package's generated table holds (graph/Packages.hpp).
- * The loader reads the level first and uses nothing else from a library built at any other level, so this header's
- * shape is the one thing that may never change: a new field is a new header, not an edit to this one.
+ * The loader holds the manifest's level against the host's before it opens the library, and the entry's answer before
+ * it uses anything else of it, so this header's shape is the one thing that may never change: a new field is a new
+ * header, not an edit to this one. (Opening a library runs its static constructors whatever its level; the manifest
+ * gate is what keeps an honest pack of another level from being opened at all.)
  */
 #ifndef RP_PACK_H
 #define RP_PACK_H
```

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`.
Expected: it builds; `PackManifest.test.cpp`'s cases still pass (they pass no host level).

- [ ] **Step 7: Run the tests**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 29 test cases)` on Linux (28 on macOS, without the dynamic-symbol case;
24 on Windows, without the read-only folder case too).

- [ ] **Step 8: Commit**

```bash
git add packages/native/src/host/packs/PackCandidate.hpp packages/native/src/host/packs/NativeLibrary.hpp \
    packages/native/src/host/packs/NativeLibrary.cpp packages/native/src/host/packs/PackTable.hpp \
    packages/native/src/host/packs/PackTable.cpp packages/native/src/host/packs/PackManifest.hpp \
    packages/native/src/host/packs/PackManifest.cpp packages/node-sdk/native/src/graph/rp_pack.h \
    packages/native/test/packs/PackLoader.test.cpp packages/native/test/packs/ElfSymbols.hpp \
    packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "packs: the native loader, one load per library, every refusal registering nothing"
```

- [ ] **Step 9: Cross-platform gate, before anything is pushed**

On a Windows machine (the CI runner's toolchain, MSVC 14.5x, in the MSVC x64 environment `build.bat` enters) and on a
macOS one, from this commit:

```
build.bat                                       (Windows)    ./build.sh   (macOS)
cmake --build build --target retroplug-packs-test -j
build\bin\retroplug-packs-test.exe             (Windows)    build/bin/retroplug-packs-test   (macOS)
```

Expected: `All tests passed (... assertions in 28 test cases)` on macOS and `24` on Windows. What this proves there:
the module's export list (an exported-symbols list on macOS, `dllexport` on Windows) and the withheld variant's;
`LoadLibraryExW` and the `.dylib` path; on MSVC, that the throwing variant compiles under `/W4 /WX` (its throw in a C++
helper, its directory at `/EHs`) and that the loader's handler, compiled at the tree's `/EHsc`, catches it and refuses
the pack (Linux is the only place the plan has run that case); `LockFileEx` and wide paths. A failure is fixed in
this task's files before Task 7. On Windows a `TempDir` that held a loaded DLL is left behind and swept by the next
run's first `TempDir` (its process gone), which a second run shows.

---

### Task 7: Discovery and the launch

**Files:**
- Create: `packages/native/src/host/packs/PackDiscovery.hpp`, `packages/native/src/host/packs/PackDiscovery.cpp`
- Create: `packages/native/test/packs/PackDiscovery.test.cpp`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/packs/PackDiscovery.test.cpp`:

```cpp
// Which packs a launch loads (docs/downloadable-packs-loading-design.md section 1): each installed pack's active
// version, the development folders after them, a development pack standing in for an installed one of its id, and a
// launch that settles state.json and loads all of them in dependency order. Every case runs on a TempDir.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-discovery]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "host/packs/FileLock.hpp"
#include "host/packs/PackDiscovery.hpp"
#include "host/packs/PackState.hpp"
#include "host/packs/PackTable.hpp"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;
using namespace rp::host::packs;
using rp::host::packs::test::fixture;
using rp::host::packs::test::TempDir;

TEST_CASE("a launch loads each installed pack's active version, and a development pack overrides it",
          "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const fs::path installed = rp::host::packs::test::install(root, "packfixture");
    rp::host::packs::test::install(root, "packfixture-dependent");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "", false});
    state.packs.push_back({"packfixture-dependent", "1.0.0", "", false});
    REQUIRE(writePackState(root, state, &why));

    const auto plain = discoverPacks(tmp.roots(), {});
    REQUIRE(plain.candidates.size() == 2);
    CHECK(plain.candidates[0].dir == installed);
    CHECK(plain.candidates[0].origin == PackOrigin::Installed);
    CHECK(plain.refused.empty());

    // The staged build folder of the same id stands in for the installed one, in its place.
    const fs::path dev = rp::host::packs::test::fixture("packfixture");
    const auto overridden = discoverPacks(tmp.roots(), {dev});
    REQUIRE(overridden.candidates.size() == 2);
    CHECK(overridden.candidates[0].dir == dev);
    CHECK(overridden.candidates[0].origin == PackOrigin::Development);
    CHECK(overridden.candidates[0].overridesInstalled);
    CHECK(overridden.refused.empty());

    // The same folder reached twice (a spelling of it, as RETROPLUG_PACK_PATH and an explicit --pack may give it) is
    // one candidate and no refusal.
    const auto same = discoverPacks(tmp.roots(), {dev, fs::path(dev.string() + "/"), dev / "." / ""});
    CHECK(same.candidates.size() == 2);
    CHECK(same.refused.empty());

    // Two different folders of one id: the first loads, the second is refused.
    const fs::path other = tmp.path() / "other-copy";
    fs::copy(dev, other, fs::copy_options::recursive);
    const auto twice = discoverPacks(tmp.roots(), {dev, other});
    CHECK(twice.candidates.size() == 2);
    REQUIRE(twice.refused.size() == 1);
    CHECK(twice.refused[0].dir == other);
    CHECK_THAT(twice.refused[0].reason, ContainsSubstring("is given twice"));

    // A development folder with no readable manifest is refused, and nothing else changes.
    const auto broken = discoverPacks(tmp.roots(), {tmp.path() / "nowhere"});
    CHECK(broken.candidates.size() == 2);
    REQUIRE(broken.refused.size() == 1);
    CHECK_THAT(broken.refused[0].reason, ContainsSubstring("cannot read"));
    // A trailing separator does not leave the refusal with an empty name.
    const auto slashed = discoverPacks(tmp.roots(), {(tmp.path() / "nowhere").string() + "/"});
    REQUIRE(slashed.refused.size() == 1);
    CHECK(slashed.refused[0].id == "nowhere");
}

TEST_CASE("a folder a host passes beats the same id from RETROPLUG_PACK_PATH", "[packs-discovery]") {
    TempDir tmp;
    const fs::path fromEnv = tmp.path() / "from-env";
    const fs::path explicitDir = tmp.path() / "explicit";
    fs::copy(fixture("packfixture"), fromEnv, fs::copy_options::recursive);
    fs::copy(fixture("packfixture"), explicitDir, fs::copy_options::recursive);
    const rp::host::packs::test::ScopedEnv path("RETROPLUG_PACK_PATH", fromEnv.string());
    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {explicitDir});
    REQUIRE(table.find("packfixture") != nullptr);
    CHECK(table.find("packfixture")->dir == explicitDir);
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].dir == fromEnv);
    CHECK_THAT(refused[0].reason, ContainsSubstring("is given twice"));
}

TEST_CASE("a launch settles state.json, then loads the installed and the development packs in dependency order",
          "[packs-discovery]") {
    // The installed fixture and, from RETROPLUG_PACK_PATH, the pack that needs it, given first: the dependent one
    // loads second all the same.
    TempDir tmp;
    const auto installed = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    std::string why;
    PackState state;
    state.packs.push_back({"packfixture", "1.0.0", "", false});
    REQUIRE(writePackState(tmp.roots().data, state, &why));
    const rp::host::packs::test::ScopedEnv path("RETROPLUG_PACK_PATH", fixture("packfixture-dependent").string());

    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {fixture("packfixture-noentry")});
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].id == "packfixture-noentry");
    const auto order = table.loadedFrom(0);
    REQUIRE(order.size() == 2);
    CHECK(order[0]->manifest.id == "packfixture");
    CHECK(order[0]->origin == PackOrigin::Installed);
    CHECK(order[0]->dir == installed);
    CHECK(order[1]->manifest.id == "packfixture-dependent");
    CHECK(order[1]->origin == PackOrigin::Development);
}

TEST_CASE("a launch holds the installed versions it loads, so another launch's settle keeps them",
          "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const auto loaded = rp::host::packs::test::install(root, "packfixture", "1.0.0");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    PackTable table(rp::graph::productPackages());
    REQUIRE(loadLaunchPacks(table, tmp.roots(), {}).empty());
    REQUIRE(table.find("packfixture") != nullptr);
    CHECK(table.find("packfixture")->lock != nullptr);  // the hold discovery took, handed to the table
    CHECK(versionInUse(loaded));

    // An update installed while this process runs, then another launch's settle: 1.1.0 becomes active, and 1.0.0,
    // which this process has loaded, stays on disk until no process holds it.
    const auto next = rp::host::packs::test::install(root, "packfixture", "1.1.0");
    const auto update = [](PackState& s) {
        s.find("packfixture")->pending = "1.1.0";
        return true;
    };
    REQUIRE(updatePackState(root, update, &why));
    REQUIRE(settlePackState(root, &why));
    CHECK(readPackState(root, &why)->find("packfixture")->active == "1.1.0");
    CHECK(fs::exists(loaded / "pack.json"));
    CHECK(fs::exists(next / "pack.json"));
}

TEST_CASE("a launch that waits on another's settle discovers what the settle left, never what it deleted",
          "[packs-discovery]") {
    // The interleaving a launch can meet, driven in order: another launch holds state.lock (it is settling), and while
    // it holds it, this launch starts and must wait; the other then deletes the version this launch would have found
    // and drops it from state.json, and lets go. This launch discovers only then, so it loads nothing and refuses
    // nothing, rather than finding a folder the settle is deleting.
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const auto doomed = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));

    auto other = lockPackState(root, FileLock::Mode::Exclusive, &why);
    REQUIRE(other);
    PackTable table(rp::graph::productPackages());
    std::vector<PackRefusal> refused;
    std::atomic<bool> done{false};
    std::thread launch([&] {
        refused = loadLaunchPacks(table, tmp.roots(), {});
        done = true;
    });
    // While the other launch holds the lock, this one cannot have read state.json, whatever the scheduling.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(done.load());
    fs::remove_all(doomed);
    {
        // The other launch's write, under the hold it already has (writePackState would take the lock again).
        std::ofstream(root / "state.json", std::ios::binary) << R"({"version": 1, "packs": []})" << "\n";
    }
    other.reset();
    launch.join();
    CHECK(refused.empty());
    CHECK(table.size() == 0);
}

TEST_CASE("a development folder inside the installed root is held like an installed one", "[packs-discovery]") {
    // A folder offered as a development pack that is in fact packs/<id>/<version>/ of the data root: the launch holds
    // it under state.lock, or another launch's settle could delete a folder this process has a library open from.
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    {
        // Another launch holds state.lock: this one waits, so it holds nothing yet, and takes the version hold only
        // once it has the lock (the table never takes a hold for a development candidate, so the hold is discovery's).
        std::string why;
        auto other = lockPackState(tmp.roots().data, FileLock::Mode::Exclusive, &why);
        REQUIRE(other);
        PackTable table(rp::graph::productPackages());
        std::vector<PackRefusal> refused;
        std::thread launch([&] { refused = loadLaunchPacks(table, tmp.roots(), {dir}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(versionInUse(dir));
        other.reset();
        launch.join();
        CHECK(refused.empty());
        const LoadedPack* pack = table.find("packfixture");
        REQUIRE(pack != nullptr);
        CHECK(pack->origin == PackOrigin::Development);
        CHECK(pack->lock != nullptr);
        CHECK(versionInUse(dir));
    }
    CHECK_FALSE(versionInUse(dir));
    // A development folder outside the root takes no hold: nothing settles it.
    PackTable outside(rp::graph::productPackages());
    REQUIRE(loadLaunchPacks(outside, tmp.roots(), {fixture("packfixture")}).empty());
    CHECK(outside.find("packfixture")->lock == nullptr);
}

TEST_CASE("which development folders count as inside the data root", "[packs-discovery]") {
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    const auto heldBy = [&](const fs::path& dev) {
        PackTable table(rp::graph::productPackages());
        REQUIRE(loadLaunchPacks(table, tmp.roots(), {dev}).empty());
        REQUIRE(table.find("packfixture") != nullptr);
        return table.find("packfixture")->lock != nullptr;
    };
    // A trailing separator and a "." step are the same folder.
    CHECK(heldBy(fs::path(dir.string() + "/")));
    CHECK(heldBy(dir / "." / ""));
    // A sibling whose name only starts with the data root's ("data2") is not inside it.
    const fs::path sibling = tmp.path() / "data2";
    const auto elsewhere = rp::host::packs::test::install(sibling, "packfixture");
    CHECK_FALSE(heldBy(elsewhere));
#if !defined(_WIN32)
    // A symlink outside the root that resolves into it is inside it.
    const fs::path link = tmp.path() / "link-to-installed";
    fs::create_symlink(dir, link);
    CHECK(heldBy(link));
#endif
}

TEST_CASE("a development folder inside the data root that cannot be held is refused, not loaded unheld",
          "[packs-discovery]") {
    TempDir tmp;
    const auto dir = rp::host::packs::test::install(tmp.roots().data, "packfixture");
    // Another process holds the folder exclusively (a settle deleting it), so a shared hold fails however often it is
    // tried.
    std::string why;
    auto exclusive = FileLock::acquire(dir / kVersionLockFile, FileLock::Mode::Exclusive, false, &why);
    REQUIRE(exclusive);
    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {dir});
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].id == "packfixture");
    CHECK_THAT(refused[0].reason, ContainsSubstring("cannot hold its version folder"));
    CHECK(table.size() == 0);
}

TEST_CASE("a launch's own settle deletes a development folder in the data root that state.json does not name",
          "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    rp::host::packs::test::install(root, "packfixture-clone");
    const auto stray = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture-clone", "1.0.0", "", false}}}, &why));
    {
        PackTable table(rp::graph::productPackages());
        const auto refused = loadLaunchPacks(table, tmp.roots(), {stray});
        REQUIRE(refused.size() == 1);
        CHECK_THAT(refused[0].reason, ContainsSubstring("cannot read"));
        CHECK_FALSE(fs::exists(stray / "pack.json"));
        CHECK(table.find("packfixture") == nullptr);
        CHECK(table.find("packfixture-clone") != nullptr);
    }
    // Named by state.json (the clone's entry swapped for it: the two register the same node types), the same folder
    // is kept and loaded, held as an installed one is.
    const auto named = rp::host::packs::test::install(root, "packfixture");
    const auto name = [](PackState& s) {
        s.packs = {{"packfixture", "1.0.0", "", false}};
        return true;
    };
    REQUIRE(updatePackState(root, name, &why));
    PackTable table(rp::graph::productPackages());
    REQUIRE(loadLaunchPacks(table, tmp.roots(), {named}).empty());
    REQUIRE(table.find("packfixture") != nullptr);
    CHECK(table.find("packfixture")->origin == PackOrigin::Development);
    CHECK(table.find("packfixture")->lock != nullptr);
}

#if !defined(_WIN32)
TEST_CASE("a launch from a data root it cannot write loads its installed packs unheld, without settling",
          "[packs-discovery]") {
    if (geteuid() == 0) SKIP("root writes through any permission, so a read-only folder cannot be made");
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    const auto installed = rp::host::packs::test::install(root, "packfixture");
    std::string why;
    REQUIRE(writePackState(root, PackState{{{"packfixture", "1.0.0", "", false}}}, &why));
    fs::remove(root / kStateLockFile);
    const auto readOnly = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    for (const fs::path& dir : {root, installed}) fs::permissions(dir, readOnly, fs::perm_options::remove);
    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {});
    for (const fs::path& dir : {root, installed}) fs::permissions(dir, fs::perms::owner_write, fs::perm_options::add);
    CHECK(refused.empty());
    REQUIRE(table.find("packfixture") != nullptr);
    CHECK(table.find("packfixture")->lock == nullptr);
}
#endif

TEST_CASE("a launch reports a state.json that will not read once", "[packs-discovery]") {
    TempDir tmp;
    const fs::path root = tmp.roots().data;
    fs::create_directories(root);
    std::ofstream(root / "state.json", std::ios::binary) << "not json";
    PackTable table(rp::graph::productPackages());
    const auto refused = loadLaunchPacks(table, tmp.roots(), {});
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].id == "state.json");
    CHECK(table.size() == 0);
}
```

In `retroplug-packs-test`'s `SOURCES`, after the `PackLoader.test.cpp` line add:

```cmake
             test/packs/PackDiscovery.test.cpp     # which folders a launch loads, and the launch itself
```

- [ ] **Step 2: Run the build to see it fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL with `fatal error: host/packs/PackDiscovery.hpp: No such file or directory`.

- [ ] **Step 3: Write PackDiscovery**

Create `packages/native/src/host/packs/PackDiscovery.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "host/packs/PackCandidate.hpp"
#include "host/packs/PackDirs.hpp"

// Which packs a launch loads (docs/downloadable-packs-loading-design.md section 1): each installed pack's active
// version, from state.json, and every development pack folder, the ones a host passes (the CLI's --pack and Settings'
// Development packs, M4) and then RETROPLUG_PACK_PATH's, so an explicit choice beats the environment. A development
// pack overrides an installed one with the same id. A folder inside the data root is the launch's settle's to delete
// unless state.json names it (PackState.hpp), development or not.
namespace rp::host::packs {

class PackTable;

struct PackDiscovery {
    std::vector<PackCandidate> candidates;  // installed in state.json's order, then development in the order given
    std::vector<std::string>   ids;         // each candidate's pack id, index for index
    std::vector<PackRefusal>   refused;     // a folder that could not be a candidate, with the reason
};

// The candidates under `roots` and in `devDirs` (in order; a later folder of an id already given is refused, and the
// same folder given twice, by whatever spelling, is one candidate and no refusal). A state.json that does not read
// (corrupt, or a newer RetroPlug's) loads no installed pack and is reported once.
PackDiscovery discoverPacks(const PackRoots& roots, const std::vector<std::filesystem::path>& devDirs);

// What a host's launch does. Under one exclusive hold of state.lock: settle state.json (pending updates and removals),
// read it, discover, and take the hold on each installed candidate's version folder and on each development one whose
// canonical path lies inside the data root (a folder a settle could delete as readily; one that cannot be held there is
// refused, not loaded unheld); then, with state.lock let go, load in dependency order into `table`, which keeps those
// holds. So another launch's settle, which takes state.lock too, deletes a version folder either before this launch
// discovers it or not at all. The development folders are `extraDevDirs` followed by RETROPLUG_PACK_PATH's. Returns
// every refusal, discovery's and the loads'.
std::vector<PackRefusal> loadLaunchPacks(PackTable& table, const PackRoots& roots,
                                         const std::vector<std::filesystem::path>& extraDevDirs);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/PackDiscovery.cpp`:

```cpp
#include "host/packs/PackDiscovery.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "graph/rp_pack.h"
#include "host/packs/PackManifest.hpp"
#include "host/packs/PackState.hpp"
#include "host/packs/PackTable.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

// Whether `dir` lies under `root`, both canonical: a development folder that is really an installed version folder.
bool isUnder(const fs::path& dir, const fs::path& root) {
    std::error_code ec;
    const fs::path d = fs::weakly_canonical(dir, ec);
    if (ec) return false;
    const fs::path r = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const fs::path rel = d.lexically_relative(r);
    return !rel.empty() && *rel.begin() != ".." && *rel.begin() != ".";
}

// `dir` as PackTable keys it (lexically normal, no trailing separator), so a spelling of a folder never reads as
// another one and a refusal never carries an empty name.
fs::path normalised(const fs::path& dir) {
    fs::path p = dir.lexically_normal();
    if (!p.has_filename() && p.has_parent_path()) p = p.parent_path();
    return p;
}

// The candidates of `state` (empty when it did not read, `why` then saying why, or null when that was reported
// already) and of `devDirs`.
PackDiscovery discoverFrom(const PackRoots& roots, const std::optional<PackState>& state, const std::string* why,
                           const std::vector<fs::path>& devDirs) {
    PackDiscovery out;
    if (state) {
        for (const auto& p : state->packs) {
            if (p.remove) continue;
            out.candidates.push_back({roots.data / p.id / p.active, PackOrigin::Installed, false, nullptr});
            out.ids.push_back(p.id);
        }
    } else if (!roots.data.empty() && why != nullptr) {
        out.refused.push_back({"state.json", roots.data / "state.json", *why});
    }
    std::vector<fs::path> given;  // each development folder's canonical path, to drop the same one given twice
    for (const auto& offered : devDirs) {
        const fs::path dir = normalised(offered);
        std::error_code canonEc;
        const fs::path real = fs::weakly_canonical(dir, canonEc);
        if (std::find(given.begin(), given.end(), canonEc ? dir : real) != given.end()) continue;
        given.push_back(canonEc ? dir : real);
        std::string why;
        const auto manifest = readPackManifest(dir, &why, RP_API_LEVEL);  // a level mismatch still names its id
        if (!manifest) {
            out.refused.push_back({displayPath(dir.filename()), dir, why});
            continue;
        }
        bool placed = false;
        for (std::size_t i = 0; i < out.candidates.size(); ++i) {
            if (out.ids[i] != manifest->id) continue;
            placed = true;
            if (out.candidates[i].origin == PackOrigin::Installed) {
                out.candidates[i] = {dir, PackOrigin::Development, true, nullptr};
            } else {
                out.refused.push_back({manifest->id, dir,
                                       "development pack " + manifest->id + " is given twice: " +
                                           displayPath(out.candidates[i].dir) + " loads, " + displayPath(dir) +
                                           " does not"});
            }
            break;
        }
        if (!placed) {
            out.candidates.push_back({dir, PackOrigin::Development, false, nullptr});
            out.ids.push_back(manifest->id);
        }
    }
    return out;
}

}  // namespace

PackDiscovery discoverPacks(const PackRoots& roots, const std::vector<fs::path>& devDirs) {
    std::string why;
    std::optional<PackState> state;
    if (!roots.data.empty()) state = readPackState(roots.data, &why);
    return discoverFrom(roots, state, &why, devDirs);
}

std::vector<PackRefusal> loadLaunchPacks(PackTable& table, const PackRoots& roots,
                                         const std::vector<fs::path>& extraDevDirs) {
    std::vector<PackRefusal> refused;
    // The explicit folders first: an id given twice loads the first, so a host's --pack beats the environment.
    std::vector<fs::path> devDirs = extraDevDirs;
    const std::vector<fs::path> fromEnv = packPathFromEnv();
    devDirs.insert(devDirs.end(), fromEnv.begin(), fromEnv.end());
    PackDiscovery found;
    {
        // One exclusive hold for the settle, the read, the discovery and the version holds, so another launch's settle
        // (which takes it too) cannot delete a folder this one has found and not yet held. No data root, or none on
        // disk yet, has no installed pack and nothing to settle.
        std::string why;
        std::error_code ec;
        std::optional<FileLock> hold;
        std::optional<PackState> state;
        bool reported = false;  // a state.json that will not read is reported once, by the settle or by the read
        if (!roots.data.empty() && fs::exists(roots.data, ec)) {
            hold = lockPackState(roots.data, FileLock::Mode::Exclusive, &why);
            if (hold) {
                if (!settlePackState(roots.data, *hold, &why)) {
                    refused.push_back({"state.json", roots.data / "state.json", why});
                    reported = true;
                }
                state = readPackState(roots.data, *hold, &why);
            } else {
                // A root this process cannot write: nothing to settle, and the read goes ahead without a hold.
                state = readPackState(roots.data, &why);
            }
        } else {
            state = PackState{};
        }
        found = discoverFrom(roots, state, reported ? nullptr : &why, devDirs);
        for (std::size_t i = 0; i < found.candidates.size();) {
            PackCandidate& c = found.candidates[i];
            // An installed folder is held; so is a development one that lies inside the data root, which a settle can
            // delete as readily (the candidate keeps its Development origin: the loader adopts a hold either way).
            if (c.origin != PackOrigin::Installed && !(!roots.data.empty() && isUnder(c.dir, roots.data))) {
                ++i;
                continue;
            }
            std::string held;
            FileLock::Failure failure = FileLock::Failure::None;
            if (auto lock = holdVersionBriefly(c.dir, &held, &failure)) {
                c.lock = std::make_shared<FileLock>(std::move(*lock));
                ++i;
            } else if (c.origin == PackOrigin::Development && failure != FileLock::Failure::Denied) {
                // Inside the managed root a development folder must be held, not loaded unheld. (An installed one
                // that is not held here is held, or refused, by the table's own bounded retry.)
                found.refused.push_back({found.ids[i], c.dir, "cannot hold its version folder: " + held});
                found.candidates.erase(found.candidates.begin() + static_cast<std::ptrdiff_t>(i));
                found.ids.erase(found.ids.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
    refused.insert(refused.end(), found.refused.begin(), found.refused.end());
    std::vector<PackRefusal> loadRefused;
    table.loadAll(found.candidates, &loadRefused);
    refused.insert(refused.end(), loadRefused.begin(), loadRefused.end());
    return refused;
}

}  // namespace rp::host::packs
```

In `add_library(retroplug-packs STATIC ...)`, after the `src/host/packs/PackTable.cpp` line add:

```cmake
    src/host/packs/PackDiscovery.cpp
```

- [ ] **Step 4: Build the held development folder**

The `isUnder` helper in `PackDiscovery.cpp`, the hold loop in `loadLaunchPacks` (a development folder whose canonical
path is under the canonical data root is held with `holdVersionBriefly` inside the `state.lock` section, `c.lock` set,
its origin left `Development`; one that cannot be held is refused; one outside the root takes no hold) and their test
cases are built with the rest of the task.

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: it builds.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 40 test cases)` on Linux (39 on macOS, 34 on Windows).

- [ ] **Step 6: Commit**

```bash
git add packages/native/src/host/packs/PackDiscovery.hpp packages/native/src/host/packs/PackDiscovery.cpp \
    packages/native/test/packs/PackDiscovery.test.cpp packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "packs: which folders a launch loads, a development pack overriding an installed one"
```

---

### Task 8: Engines register the loaded packs

The process's table is a backend source because it seeds itself with the product's packages. `Engine` keeps its
existing two constructors (they read the process's table) and gains two that take a table, which the tests use so no
load outlives its case.

**Files:**
- Create: `packages/native/src/host/packs/ProcessPacks.hpp`, `packages/native/src/host/packs/ProcessPacks.cpp`
- Modify: `packages/graph-runtime/native/src/graph/NodeRegistry.hpp`, `.../NodeRegistry.cpp` (`canAdd`)
- Modify: `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/engine/Engine.cpp`
- Create: `packages/native/test/packs/PackEngines.test.cpp`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/packs/PackEngines.test.cpp`:

```cpp
// Loaded packs in Engines (docs/downloadable-packs-loading-design.md section 2): an Engine registers the built-ins,
// then every pack its table has loaded, in load order; a pack loaded while an Engine exists joins it at its next
// setGraph, on its control thread; two Engines share one load; an Engine that holds a type already refuses the whole
// pack and only there; and a live load while an audio thread renders (tools/run-sanitizer.sh thread runs this binary).
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-engines]"`.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "GraphDocs.hpp"
#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "host/engine/Engine.hpp"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProcessPacks.hpp"

// The fixture's types as this binary links them (retroplug-packfixture-nodes), beside the module's own copy.
extern "C" const rp_package* rp_packfixture_package(void);

using rp::host::packs::PackTable;
using rp::host::packs::test::fixture;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 256;

// The fixture's tone at `level` on the host's stereo pair.
Engine::GraphRequest toneDocument(float level) {
    const std::string doc = R"({"nodes":[{"id":"tone","type":"rp.packfixture.tone","settings":{"level":)" +
                            std::to_string(level) +
                            R"(}},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],)"
                            R"("wires":[{"from":"tone.out","to":"out.out.1-2"}]})";
    return rp::host::test::graphRequest(doc, {});
}

// One block's first left sample.
float firstSample(Engine& e) {
    std::vector<float> l(kFrames, -1.0f), r(kFrames, -1.0f);
    e.processBlock(kFrames, l.data(), r.data());
    return l[0];
}

std::string refusal(const Engine::GraphResult& r) {
    return r.errors.empty() ? std::string() : r.errors[0].code + ": " + r.errors[0].message;
}

}  // namespace

TEST_CASE("an Engine registers its table's packs after the built-ins, and a document places their types",
          "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(fixture("packfixture")));
    PackTable none(rp::graph::productPackages());
    Engine    bare(kRate, none);
    Engine    e(kRate, table);
    // Two more types than an engine with no pack, and the built-ins as they are.
    CHECK(e.nodeTypeCount() == bare.nodeTypeCount() + 2);
    CHECK(e.packsRegistered() == 1);
    const auto placed = e.setGraph(toneDocument(0.5f));
    INFO(refusal(placed));
    REQUIRE(placed.ok);
    CHECK(firstSample(e) == 0.5f);
}

TEST_CASE("a pack loaded while an Engine runs joins it at its next setGraph", "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    Engine e(kRate, table);
    const std::size_t before = e.nodeTypeCount();
    const auto unknown = e.setGraph(toneDocument(0.5f));
    CHECK_FALSE(unknown.ok);
    REQUIRE(table.load(fixture("packfixture")));
    CHECK(e.nodeTypeCount() == before);  // nothing reaches into the engine: it reads the table when it plans
    const auto placed = e.setGraph(toneDocument(0.5f));
    INFO(refusal(placed));
    REQUIRE(placed.ok);
    CHECK(e.nodeTypeCount() == before + 2);
    CHECK(firstSample(e) == 0.5f);
}

TEST_CASE("two Engines share one load, and an Engine built after it sees it at once", "[packs-engines]") {
    PackTable table(rp::graph::productPackages());
    Engine first(kRate, table);
    Engine second(kRate, table);
    REQUIRE(table.load(fixture("packfixture")));
    REQUIRE(first.setGraph(toneDocument(0.25f)).ok);
    REQUIRE(second.setGraph(toneDocument(0.75f)).ok);
    CHECK(firstSample(first) == 0.25f);
    CHECK(firstSample(second) == 0.75f);
    Engine third(kRate, table);  // a render job's engine, or a second plugin instance
    CHECK(third.packsRegistered() == 1);
    REQUIRE(third.setGraph(toneDocument(0.125f)).ok);
    CHECK(firstSample(third) == 0.125f);
    CHECK(table.size() == 1);  // one library, opened once, for all three
}

TEST_CASE("an Engine that already holds a pack's type refuses the whole pack, and only that Engine does",
          "[packs-engines]") {
    // This binary links the fixture's source statically too: an engine given that copy first (addNodePackage, which
    // the table cannot see) holds rp.packfixture.tone, so the loaded pack is refused there and nowhere else.
    PackTable table(rp::graph::productPackages());
    Engine holder(kRate, table);
    REQUIRE(holder.addNodePackage(rp_packfixture_package()));
    Engine other(kRate, table);
    REQUIRE(table.load(fixture("packfixture")));
    const std::size_t held = holder.nodeTypeCount();
    REQUIRE(holder.setGraph(toneDocument(0.5f)).ok);  // the static copy's tone
    CHECK(holder.nodeTypeCount() == held);
    CHECK(holder.refusedPacks() == std::vector<std::string>{"packfixture"});
    CHECK(holder.packsRegistered() == 0);
    REQUIRE(other.setGraph(toneDocument(0.5f)).ok);    // the pack's
    CHECK(other.refusedPacks().empty());
    CHECK(firstSample(holder) == 0.5f);
    CHECK(firstSample(other) == 0.5f);
}

TEST_CASE("a pack of two packages whose second clashes in an Engine registers neither there", "[packs-engines]") {
    // The pair's second package takes rp.packfixture.tone and .gain, which the holder has from this binary's static
    // copy: the first package, which clashes with nothing, must stay out with it. The other engine takes all four.
    PackTable table(rp::graph::productPackages());
    Engine holder(kRate, table);
    REQUIRE(holder.addNodePackage(rp_packfixture_package()));
    Engine other(kRate, table);
    const std::size_t held = holder.nodeTypeCount(), plain = other.nodeTypeCount();
    REQUIRE(table.load(fixture("packfixture-pair")));
    const std::string pairTone = R"({"nodes":[{"id":"tone","type":"rp.packfixture-pair.tone"},)"
                                 R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],)"
                                 R"("wires":[{"from":"tone.out","to":"out.out.1-2"}]})";
    CHECK_FALSE(holder.setGraph(rp::host::test::graphRequest(pairTone, {})).ok);
    CHECK(holder.nodeTypeCount() == held);
    CHECK(holder.refusedPacks() == std::vector<std::string>{"packfixture-pair"});
    REQUIRE(other.setGraph(rp::host::test::graphRequest(pairTone, {})).ok);
    CHECK(other.nodeTypeCount() == plain + 4);
    CHECK(firstSample(other) == 0.25f);  // the tone's default level
}

TEST_CASE("an Engine built with no table of its own reads the process's", "[packs-engines]") {
    // The one case that loads into the process's table, which outlives it: every later default-built engine in this
    // binary sees the fixture, and no other case builds one.
    rp::host::packs::PackTable& process = rp::host::packs::processPacks();
    CHECK(&process == &rp::host::packs::processPacks());
    REQUIRE(process.load(fixture("packfixture")));
    Engine e(kRate);
    CHECK(e.packsRegistered() == 1);
    REQUIRE(e.setGraph(toneDocument(0.5f)).ok);
    CHECK(firstSample(e) == 0.5f);
}

TEST_CASE("a pack loads on another thread while an audio thread renders, and the next plan runs its node",
          "[packs-engines][threads]") {
    // Three threads, as a plugin has them: the audio thread running blocks, a loader (an install from the UI) and the
    // control thread that plans. The table's lock orders the load against the control thread's read, and the
    // registry changes on the control thread between plans, which the audio thread never reads.
    PackTable table(rp::graph::productPackages());
    Engine e(kRate, table);
    e.setAudioThreadOwns(true);
    std::atomic<bool>  running{true};
    std::atomic<int>   toned{0};
    std::thread        audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.processBlock(kFrames, l.data(), r.data());
            if (l[0] == 0.5f) toned.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    std::thread loader([&] { table.load(fixture("packfixture")); });
    bool        placed = false;
    std::string why;
    const auto  patience = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!placed && std::chrono::steady_clock::now() < patience) {
        const auto r = e.setGraph(toneDocument(0.5f));
        placed = r.ok;
        if (!placed) {
            why = refusal(r);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        e.reclaim();
    }
    loader.join();
    while (placed && toned.load() < 10 && std::chrono::steady_clock::now() < patience)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    INFO(why);
    REQUIRE(placed);
    CHECK(toned.load() >= 10);
}
```

In `retroplug-packs-test`'s `SOURCES`, after the `PackDiscovery.test.cpp` line add:

```cmake
             test/packs/PackEngines.test.cpp       # a pack in a running Engine, two Engines, a live load under audio
```

after its `PACKAGES` line add:

```cmake
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test/engine-graph  # GraphDocs.hpp
```

and replace its `DEFS` with (GraphDocs.hpp's `romFor` compiles every console's fixture path):

```cmake
    DEFS     RP_PACKS_BUILD_DIR="${RP_PACKS_DIR}"
             RP_PACK_PLATFORM_CMAKE="${RP_PACK_PLATFORM}"
             RP_API_LEVEL_CMAKE=${RP_API_LEVEL}u
             RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/packages/mgb/rom/mGB.gb"
             RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms")
```

- [ ] **Step 2: Run the build to see it fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL with `fatal error: host/packs/ProcessPacks.hpp: No such file or directory`.

- [ ] **Step 3: Write the process's table**

Create `packages/native/src/host/packs/ProcessPacks.hpp`:

```cpp
#pragma once

#include "host/packs/PackTable.hpp"

namespace rp::host::packs {

// The process's one pack table, seeded with the product's built-in packages (rp_register_packages): every Engine
// built with no table of its own reads it, so every plugin instance, render job and standalone engine in the process
// shares each load. Made at the first call, which any thread may make. It starts empty: a host's launch fills it
// (loadLaunchPacks) and a live install adds to it (M4), and nothing here scans a folder by itself, so a binary that
// never asks reads no user directory.
PackTable& processPacks();

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/ProcessPacks.cpp`:

```cpp
#include "host/packs/ProcessPacks.hpp"

#include "graph/Packages.hpp"

namespace rp::host::packs {

PackTable& processPacks() {
    // A function-local static: built once, under the guard the language gives it, by whichever thread asks first.
    static PackTable table(rp::graph::productPackages());
    return table;
}

}  // namespace rp::host::packs
```

In `packages/native/CMakeLists.txt`, in `add_library(retroplug-backend STATIC ...)`, replace

```cmake
    src/kit/SampleCache.cpp
```

with

```cmake
    src/kit/SampleCache.cpp
    src/host/packs/ProcessPacks.cpp         # the process's pack table, seeded with the product's packages
```

- [ ] **Step 4: Let the registry check without adding**

An Engine stages a pack: every package checked against its registry before the first is added, so a pack of several
packages is all or nothing there too. In `packages/graph-runtime/native/src/graph/NodeRegistry.hpp`, replace

```cpp
    bool addPackage(const rp_package* package, std::string* error = nullptr);
```

with

```cpp
    bool addPackage(const rp_package* package, std::string* error = nullptr);
    // Whether addPackage would register `package` now, with its reason when not, changing nothing: how a host stages
    // a pack of several packages (Engine::syncPacks), every one of them checked before the first is added.
    bool canAdd(const rp_package* package, std::string* error = nullptr) const;
```

and in `NodeRegistry.cpp`, directly before `const rp_package* NodeRegistry::replacePackage(`, insert:

```cpp
bool NodeRegistry::canAdd(const rp_package* package, std::string* error) const {
    std::string pkg;
    std::vector<Entry> added;
    return check(package, {}, pkg, added, error);
}

```

- [ ] **Step 5: Declare the Engine's side**

In `packages/native/src/host/engine/Engine.hpp`, replace

```cpp
namespace rp::host { class OfflineJobs; struct SystemEntry; struct SystemTable; }
```

with

```cpp
namespace rp::host { class OfflineJobs; struct SystemEntry; struct SystemTable; }
namespace rp::host::packs { class PackTable; }
```

replace

```cpp
    explicit Engine(double sampleRate = 44100.0);
    // With the script nodes of `scriptBundle` (its chunks, run in order) in place of the embedded bundle's: a test's
    // toy nodes.
    Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle);
```

with

```cpp
    // Every constructor registers the product's built-in packages, then the built-in script pack, then every pack
    // `packs` has loaded, in its (dependency) order; the two that take no table read the process's
    // (packs::processPacks()), which is how every host's engines see one set of loads.
    explicit Engine(double sampleRate = 44100.0);
    // With the script nodes of `scriptBundle` (its chunks, run in order) in place of the embedded bundle's: a test's
    // toy nodes.
    Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle);
    // With the packs of `packs`, which must outlive the engine, in place of the process's: a test's table of its own.
    Engine(double sampleRate, rp::host::packs::PackTable& packs);
    Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle,
           rp::host::packs::PackTable& packs);
```

replace

```cpp
    // The node types a document can name: the registry's, native and script.
    std::size_t nodeTypeCount() const;
```

with

```cpp
    // The node types a document can name: the registry's, native and script.
    std::size_t nodeTypeCount() const;
    // The loaded packs this engine has registered, and the ids of those it refused (a type or package id another of
    // its packages holds, which only a pack the table could not judge reaches: one taking a script node's type id, or
    // one a test added with addNodePackage). Control thread.
    std::size_t              packsRegistered() const { return packsSeen_ - packsRefused_.size(); }
    std::vector<std::string> refusedPacks() const { return packsRefused_; }
```

and replace

```cpp
    // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
    // liveCores_) as they go.
    std::unique_ptr<Graph> graph_;
```

with

```cpp
    // The pack table this engine registers from, and how far into it it has read (syncPacks): the packs before
    // packsSeen_ are registered or, named in packsRefused_, refused here.
    rp::host::packs::PackTable* packs_ = nullptr;
    std::size_t                 packsSeen_ = 0;
    std::vector<std::string>    packsRefused_;
    // Each pack the table loaded since the last call, registered whole or not at all: from the constructor, and from
    // each setGraph before it plans, on the control thread, as loadScriptPack changes the registry.
    void syncPacks();

    // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
    // liveCores_) as they go.
    std::unique_ptr<Graph> graph_;
```

- [ ] **Step 6: Register the packs in Engine.cpp**

In `packages/native/src/host/engine/Engine.cpp`, replace

```cpp
#include "host/graph/SystemTable.hpp"
```

with

```cpp
#include "host/graph/SystemTable.hpp"
#include "host/packs/ProcessPacks.hpp"  // the process's loaded packs, which every engine registers after its own
```

replace

```cpp
Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle)
    : sampleRate_(sampleRate), pendingMidi_(rp::host::kHostMidiInMessages, rp::host::kHostMidiInBytes) {
```

with

```cpp
Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle)
    : Engine(sampleRate, scriptBundle, rp::host::packs::processPacks()) {}

Engine::Engine(double sampleRate, rp::host::packs::PackTable& packs) : Engine(sampleRate, embeddedScripts(), packs) {}

Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> scriptBundle,
               rp::host::packs::PackTable& packs)
    : sampleRate_(sampleRate), pendingMidi_(rp::host::kHostMidiInMessages, rp::host::kHostMidiInBytes),
      packs_(&packs) {
```

replace

```cpp
    if (!graph_->registry.addPackage(graph_->script.package(), &why))
        std::fprintf(stderr, "[engine] the script node types were refused (%s); none is registered\n", why.c_str());
```

with

```cpp
    if (!graph_->registry.addPackage(graph_->script.package(), &why))
        std::fprintf(stderr, "[engine] the script node types were refused (%s); none is registered\n", why.c_str());
    // Then the loaded packs, after every built-in, so a built-in's order and ids are what they were with none loaded.
    syncPacks();
```

replace (in `Engine::publishGraph`)

```cpp
    graph_->host->reclaim();
    graph_->script.reclaim();
    auto doc = rp::graph::parseGraphDocument(request.document);
```

with

```cpp
    graph_->host->reclaim();
    graph_->script.reclaim();
    // A pack loaded since the last plan joins the registry before this one is made, on this thread, which is the only
    // one that reads the registry: its node types exist from this setGraph on.
    syncPacks();
    auto doc = rp::graph::parseGraphDocument(request.document);
```

and replace

```cpp
void Engine::setScriptCallDeadline(std::chrono::milliseconds limit) { graph_->script.setCallDeadline(limit); }
```

with

```cpp
void Engine::setScriptCallDeadline(std::chrono::milliseconds limit) { graph_->script.setCallDeadline(limit); }

void Engine::syncPacks() {
    if (packs_ == nullptr) return;
    rp::graph::NodeRegistry& registry = graph_->registry;
    for (const rp::host::packs::LoadedPack* pack : packs_->loadedFrom(packsSeen_)) {
        ++packsSeen_;
        // All or nothing here too, staged: every package is checked against this engine's registry before the first is
        // added. The table judged the pack against the built-ins and the packs before it, its packages one after
        // another on one registry, so what is left to clash with is this engine's own (its script node types, a test's
        // addNodePackage), which canAdd sees, and a clash anywhere keeps the whole pack out.
        std::string why;
        bool fits = true;
        for (const rp_package* p : pack->packages) fits = fits && registry.canAdd(p, &why);
        if (!fits) {
            std::fprintf(stderr, "[engine] pack %s was refused here: %s\n", pack->manifest.id.c_str(), why.c_str());
            packsRefused_.push_back(pack->manifest.id);
            continue;
        }
        // Each add then succeeds: canAdd proved each package against this registry, and the only thing an earlier add
        // could change for a later one is a clash between the pack's own packages, which the table's trial refused.
        for (const rp_package* p : pack->packages) {
            [[maybe_unused]] const bool added = registry.addPackage(p, &why);
            assert(added && "a package canAdd passed and the table's trial registered after its siblings");
        }
    }
}
```

- [ ] **Step 7: Run the tests**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 47 test cases)` on Linux (46 on macOS, 41 on Windows), and two stderr
lines from the
cases that refuse a pack in one Engine:
`[engine] pack packfixture was refused here: package "rp.packfixture" is already registered` and
`[engine] pack packfixture-pair was refused here: node type "rp.packfixture.tone" is already registered`.

- [ ] **Step 8: Hold the built-ins' registration where it was**

Run:

```bash
cmake --build build --target retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-engine-graph-test "[packages]"
```

Expected: `All tests passed (... assertions in 2 test cases)`: with the process's table empty, every engine's registry
is its product's row plus the script pack's types, as before.

- [ ] **Step 9: Every host path through the changed Engine**

Run: `pnpm test:plugin`
Expected: `# 17 plugin test binary(ies) passed`.

Run: `pnpm test:native`
Expected: every file passes but, in the container this plan was prototyped in, `app-cores`' GBA case, which fails there
with the pre-M1 host binary too (it is not this plan's: report it, do not fix it here).

- [ ] **Step 10: Commit**

```bash
git add packages/native/src/host/packs/ProcessPacks.hpp packages/native/src/host/packs/ProcessPacks.cpp \
    packages/graph-runtime/native/src/graph/NodeRegistry.hpp packages/graph-runtime/native/src/graph/NodeRegistry.cpp \
    packages/native/src/host/engine/Engine.hpp packages/native/src/host/engine/Engine.cpp \
    packages/native/test/packs/PackEngines.test.cpp packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "engine: register the loaded packs after the built-ins, and each later load at the next setGraph"
```

---

### Task 9: The sanitizer legs

The `[threads]` case of Task 8 loads a pack on one thread while the control thread plans and an audio thread renders;
the rest load and refuse libraries, hold and release folders, and (from Task 10) run two copies of Mesen in one
process. `tools/run-sanitizer.sh` builds and runs `retroplug-packs-test` in both its legs, ThreadSanitizer in
`build-tsan/` and AddressSanitizer in `build-asan/`, after the slugs; the pack modules are instrumented with the rest,
since `RETROPLUG_SANITIZE`'s flags are global.

**Files:**
- Modify: `tools/run-sanitizer.sh`

- [ ] **Step 1: Add the packs to both legs**

In `tools/run-sanitizer.sh`, replace

```bash
fi

[ "$rc" -eq 0 ] && echo "==> host passed under $san sanitizer"
```

with

```bash
fi

# The packs, in both modes: a pack loaded on one thread while another plans and a third renders (thread), and the
# loader's libraries, held folders and the Mesen pack's two copies in one process (address). The pack modules (the
# fixture's and the Mesen package's) are instrumented with the rest, since the sanitizer flags are global.
echo "==> building retroplug-packs-test (instrumented)"
cmake --build "$builddir" -j"$(nproc)" --target retroplug-packs-test
echo "==> running the packs under $san sanitizer"
if ! "$repo/$builddir/bin/retroplug-packs-test"; then
    echo "!! retroplug-packs-test failed under $san sanitizer" >&2
    rc=1
fi

[ "$rc" -eq 0 ] && echo "==> host passed under $san sanitizer"
```

- [ ] **Step 2: Run the packs under ThreadSanitizer**

The full legs are `tools/run-sanitizer.sh thread` and `tools/run-sanitizer.sh address` (each builds the instrumented
`retroplug-host` first, the long part). The packs' own part, by hand:

```bash
cmake -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DRETROPLUG_SANITIZE=thread \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build-tsan --target retroplug-packs-test -j$(nproc)
TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" \
    build-tsan/bin/retroplug-packs-test
```

Expected: `All tests passed (... assertions in 47 test cases)` and no `WARNING: ThreadSanitizer` line. Confirm the
modules are instrumented:

```bash
nm -D --undefined-only build-tsan/packs/packfixture/1.0.0/native/linux-x64/rp-packfixture.so | grep -c __tsan
```

prints a number above 0.

- [ ] **Step 3: Run them under AddressSanitizer**

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DRETROPLUG_SANITIZE=address \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build-asan --target retroplug-packs-test -j$(nproc)
ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-packs-test
```

Expected: `All tests passed (... assertions in 47 test cases)`, exit 0, no `ERROR: AddressSanitizer` or
`ERROR: LeakSanitizer` line.

- [ ] **Step 4: Commit**

```bash
git add tools/run-sanitizer.sh
git commit -m "tools: both sanitizer legs run the packs"
```

---

### Task 10: The pinned C++ boundary, tried on the Mesen package

> Note (review): `retroplug-sdl` links `-static-libstdc++`; this task's result covers the hosts that link the shared
> runtime only (Design questions 14).

The feasibility task. `retroplug_pack(mesen)` builds the Mesen package's static target, `retroplug-mesen`, into a module
with its own static copies of `retroplug-system`, the node SDK, `retroplug-n8-wire`, reflect-cpp and `deps/mesen`, all
of them local to it. The test loads it into an Engine of the `packs-host` product (no Mesen) and boots BlipToaster, the
NES fixture the parity suite's NES groups boot (`resources/roms/bliptoaster.nes`, tracked; native tests reach fixtures
through a `DEFS` path, as `GraphDocs.hpp` does), beside an Engine given the static build through `addNodePackage`.

What the cases prove, and only that: the module exports its entry alone and imports nothing of RetroPlug's (read from
its file on Linux); its types describe themselves as the static build's do; host MIDI drives the pack's core (exactly
silent before the first note, sounding in every block after it), its PCM equals the static build's byte for byte,
block for block, the two run interleaved, and the work RAM each publishes through `SystemBase`'s snapshot virtuals is
equal. Not proved: the SMS and GBA (they stage files in the pid-keyed scratch paths both copies share in this process,
see Step 6), and anything on Windows, where the case is a counted `SKIP` until the allocator decision (Design
questions, 1). A second case scans every built-in package's native source but Mesen's for an RTTI cast.

**Files:**
- Modify: `cmake/packs.cmake` (append `retroplug_pack()`)
- Create: `packages/mesen/pack.json`
- Modify: `packages/mesen/native/CMakeLists.txt` (one call at the end)
- Create: `packages/native/test/packs/MesenPack.test.cpp`, `packages/native/test/packs/BoundaryRules.test.cpp`
- Modify: `packages/native/cmake/tests.cmake`

- [ ] **Step 1: Write the failing tests**

Create `packages/native/test/packs/MesenPack.test.cpp`:

```cpp
// The pinned C++ boundary, tried on a real package (docs/downloadable-packs-loading-design.md section 2): the Mesen
// package's native target built as a pack (retroplug_pack(mesen), packages/mesen/native/CMakeLists.txt), carrying its
// own static copies of retroplug-system, node-sdk, n8-wire, reflect-cpp and deps/mesen, loaded into an Engine whose
// product (packs-host) lists no mesen, beside an Engine given this binary's static build of the same package.
//
// What it proves, and no more: the module exports its entry alone and imports nothing of RetroPlug's; its node types
// describe themselves as the static build's do; and a BlipToaster NES built through the pack's build function, driven
// by host MIDI through rp.host.midi-in, renders the static build's PCM byte for byte, block for block, the two run
// interleaved, silent until the first note and sounding after it, and publishes the same work RAM through SystemBase's
// snapshot virtuals. That exercises the engine calling a pack's SystemBase through its vtable (the block, the snapshot
// publish, the deleting destructor in Engine::destroySystem) and a build request's ownership crossing into the pack.
//
// What it does not: the two copies share this process's pid-keyed Mesen scratch paths (MesenGlobalInit.cpp's
// /tmp/retroplug-mesen/<pid>, which each copy's first init clears, and NesEverdriveFifo.hpp's retroplug-sd-<pid>/<n>,
// whose slot counter each copy keeps for itself, so both NES systems get slot 0). A NES BlipToaster boot writes
// neither, which is why the case is the NES alone; the SMS and GBA stage files there and are left to M5, where Mesen is
// a pack and no static copy sits beside it. And it does not run on Windows: the boundary moves heap ownership both
// ways, which the static CRT (/MT) makes two allocators there, so a pass would prove nothing until the allocator
// decision is made (docs/downloadable-packs-m1-plan.md, Design questions).
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-mesen]"`.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ElfSymbols.hpp"
#include "GraphDocs.hpp"
#include "graph/Descriptor.hpp"
#include "graph/Packages.hpp"
#include "host/engine/Engine.hpp"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackDirs.hpp"
#include "host/packs/PackTable.hpp"

// The Mesen package's static build, which this binary links and no engine of its registers by itself.
extern "C" const rp_package* rp_mesen_package(void);

using rp::host::packs::PackTable;
using rp::host::test::DocExtras;
using rp::host::test::DocSystem;
using rp::host::test::docNode;
using rp::host::test::docWire;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 1024;

std::filesystem::path mesenPack() { return std::filesystem::path(RP_PACKS_BUILD_DIR) / "mesen" / "0.1.0"; }

// A lone BlipToaster NES through rp.host.midi-in, on the host's stereo pair, its ROM put in `e`.
Engine::GraphRequest nesDocument(Engine& e) {
    rp::host::test::Roms   roms;
    std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
    rp::host::test::withRoms(e, systems, roms);
    const DocExtras extras{{docNode("midi-in", "rp.host.midi-in")}, {docWire("midi-in.midi", "sys-1.midi")}};
    return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems, extras), {1});
}

// One block, interleaved, with the notes due in it staged first. BlipToaster ignores MIDI for its first second or so
// (parity/scenes.ts BOOT_MS), so the notes start at 1.5 s: a chord, a change at 2.5 s, and its release.
std::vector<float> block(Engine& e, std::uint64_t at) {
    const auto due = [at](double seconds) {
        const auto from = static_cast<std::uint64_t>(seconds * kRate);
        return at <= from && from < at + kFrames ? static_cast<std::uint32_t>(from - at) : UINT32_MAX;
    };
    if (const auto f = due(1.5); f != UINT32_MAX) {
        e.stageMidi(f, std::vector<std::uint8_t>{0x90, 60, 100});
        e.stageMidi(f, std::vector<std::uint8_t>{0x91, 64, 100});
        e.stageMidi(f, std::vector<std::uint8_t>{0x92, 48, 100});
    }
    if (const auto f = due(2.5); f != UINT32_MAX) e.stageMidi(f, std::vector<std::uint8_t>{0x90, 67, 100});
    if (const auto f = due(3.0); f != UINT32_MAX) e.stageMidi(f, std::vector<std::uint8_t>{0x80, 67, 0});
    std::vector<float> l(kFrames), r(kFrames), out;
    float*             outs[2] = {l.data(), r.data()};
    e.processBlock(kFrames, outs, 2);
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        out.push_back(l[i]);
        out.push_back(r[i]);
    }
    return out;
}

}  // namespace

TEST_CASE("the Mesen package loads as a pack, its types described as its static build describes them",
          "[packs-mesen]") {
    PackTable  table(rp::graph::productPackages());
    const auto load = table.load(mesenPack());
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->packages.size() == 1);
    const rp_package* pack = load.pack->packages[0];
    const rp_package* builtIn = rp_mesen_package();
    CHECK(pack != builtIn);  // the module's own copy, not this binary's
    REQUIRE(pack->node_type_count == builtIn->node_type_count);
    for (std::uint32_t k = 0; k < pack->node_type_count; ++k) {
        INFO(builtIn->node_types[k]->id);
        CHECK(std::string(pack->node_types[k]->id) == builtIn->node_types[k]->id);
        CHECK(std::string(pack->node_types[k]->descriptor(pack->node_types[k])) ==
              builtIn->node_types[k]->descriptor(builtIn->node_types[k]));
    }
    // Everything the module links stays its own: the static build's entry is not among its exports.
    CHECK(rp::host::packs::librarySymbol(load.pack->library, "rp_mesen_package") == nullptr);
#if defined(__linux__)
    // And the file says so: one export, and no import of RetroPlug's (cmake/packs.cmake's --no-undefined made every
    // import one of the system libraries').
    const auto symbols = rp::host::packs::test::readDynamicSymbols(
        (mesenPack() / "native" / std::string(rp::host::packs::currentPlatform()) / "rp-mesen.so").string());
    REQUIRE(symbols);
    CHECK(symbols->exported == std::vector<std::string>{"rp_pack_entry"});
    for (const auto& name : symbols->imported) {
        INFO(name);
        CHECK_FALSE(rp::host::packs::test::isRetroPlugSymbol(name));
    }
#endif
}

TEST_CASE("a NES booted through the Mesen pack renders the static build's PCM and work RAM, bit for bit",
          "[packs-mesen]") {
#if defined(_WIN32)
    // The whole body is the #else: code after SKIP would be unreachable, which MSVC's C4702 makes an error under /WX.
    SKIP("the boundary moves heap ownership both ways, and /MT gives each module its own allocator: held until the "
         "allocator decision (docs/downloadable-packs-m1-plan.md, Design questions 1)");
#else
    PackTable none(rp::graph::productPackages());
    Engine    builtIn(kRate, none);
    REQUIRE(builtIn.addNodePackage(rp_mesen_package()));
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(mesenPack()));
    Engine fromPack(kRate, table);
    REQUIRE(fromPack.packsRegistered() == 1);

    const auto placedBuiltIn = builtIn.setGraph(nesDocument(builtIn));
    const auto placedPack = fromPack.setGraph(nesDocument(fromPack));
    REQUIRE(placedBuiltIn.ok);
    REQUIRE(placedPack.ok);
    REQUIRE(fromPack.findSystem(1) != nullptr);

    // Interleaved, block by block: each block of the pack's core against the same block of the static build's. The
    // NES is exactly silent until the first note (at 1.5 s) and sounds in every block from the one that carries it,
    // so what agrees is the notes the MIDI drove, not two idle cores.
    const auto firstNote = static_cast<std::uint64_t>(1.5 * kRate);
    std::size_t firstDiff = SIZE_MAX, loudBeforeNote = 0, quietAfterNote = 0;
    for (std::uint64_t at = 0; at < static_cast<std::uint64_t>(3.5 * kRate); at += kFrames) {
        const auto a = block(builtIn, at);
        const auto b = block(fromPack, at);
        REQUIRE(a.size() == b.size());
        if (firstDiff == SIZE_MAX && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) != 0)
            firstDiff = at;
        double energy = 0;
        for (const float s : a) energy += static_cast<double>(s) * s;
        if (at + kFrames <= firstNote && energy != 0.0) ++loudBeforeNote;
        if (at + kFrames > firstNote && energy < 1.0) ++quietAfterNote;
    }
    INFO("first differing block at frame " << firstDiff);
    CHECK(firstDiff == SIZE_MAX);
    CHECK(loudBeforeNote == 0);
    CHECK(quietAfterNote == 0);
    // The same work RAM, read through the snapshot each core publishes (SystemBase's virtuals, called by the engine).
    const auto ramBuiltIn = builtIn.readRam(1);
    const auto ramPack = fromPack.readRam(1);
    REQUIRE(ramBuiltIn);
    REQUIRE(ramPack);
    CHECK_FALSE(ramBuiltIn->empty());
    CHECK(*ramBuiltIn == *ramPack);
    CHECK(builtIn.faultCount() == 0);
    CHECK(fromPack.faultCount() == 0);
#endif
}
```

Create `packages/native/test/packs/BoundaryRules.test.cpp`:

```cpp
// The crossing rules of the pinned C++ boundary (docs/downloadable-packs-loading-design.md section 2) that a test can
// hold: no RTTI cast in the code that handles a pack's objects. A pack's types (a console's SystemBase subclass, its
// capability interfaces) carry their own typeinfo, local to its module, so a dynamic_cast, dynamic_pointer_cast or
// typeid on one of them depends on how the platform compares typeinfo across modules; the host calls them through
// virtual functions and branches on null interface pointers instead. Any built-in package may hold a pack's object
// (the engine, the system contract, the graph runtime, a host, a cart package beside a console pack), so the scan is
// every package's native source and the shell's hosts, but for Mesen's, which becomes a pack and may cast its own
// objects inside itself. Tests and vendored dependencies are not scanned: a test may cast the cores it built.
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-boundary]"`.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The folders scanned: every packages/<name>/native/src but Mesen's, and the shell's hosts.
std::vector<fs::path> scannedRoots(const fs::path& repo) {
    std::vector<fs::path> roots;
    for (const auto& package : fs::directory_iterator(repo / "packages")) {
        const fs::path src = package.path() / "native" / "src";
        if (package.path().filename() != "mesen" && fs::is_directory(src)) roots.push_back(src);
    }
    for (const char* host : {"plugin", "cli", "sdl", "node"})
        if (fs::is_directory(repo / "packages" / "native" / host)) roots.push_back(repo / "packages" / "native" / host);
    return roots;
}

}  // namespace

TEST_CASE("the host's code casts no object by RTTI", "[packs-boundary]") {
    const fs::path repo(RP_REPO_ROOT);
    const std::vector<fs::path> roots = scannedRoots(repo);
    REQUIRE(roots.size() > 10);
    std::vector<std::string> found;
    std::size_t files = 0;
    for (const fs::path& root : roots) {
        for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
            const std::string name = it->path().filename().string();
            if (it->is_directory() && (name == "node_modules" || name == "test")) {
                it.disable_recursion_pending();
                continue;
            }
            const std::string ext = it->path().extension().string();
            if (!it->is_regular_file() || (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".c" &&
                                           ext != ".mm"))
                continue;
            ++files;
            std::ifstream in(it->path(), std::ios::binary);
            std::string line;
            for (int n = 1; std::getline(in, line); ++n) {
                // What a line says outside its comment: the code, not a note about why there is no cast.
                const std::string code = line.substr(0, line.find("//"));
                if (code.find("dynamic_cast") != std::string::npos ||
                    code.find("dynamic_pointer_cast") != std::string::npos ||
                    code.find("typeid(") != std::string::npos)
                    found.push_back(it->path().string() + ":" + std::to_string(n) + ": " + line);
            }
        }
    }
    CHECK(files > 150);  // the scan read the trees, not an empty folder
    for (const auto& f : found) UNSCOPED_INFO(f);
    CHECK(found.empty());
}
```

In `retroplug-packs-test`'s `SOURCES`, put `Deterministic.cpp` first (every core in the binary powers on in a fixed
state; the pack's copy of `util/Determinism.hpp` reads the same environment variable) and add the two files last, so
the list reads:

```cmake
    SOURCES  test/engine-graph/Deterministic.cpp  # every core powers on in a fixed state, the pack's copies too
             test/packs/PackRoots.test.cpp         # the data and cache roots, the platform folder, the pack path
             test/packs/PackManifest.test.cpp      # pack.json, and each malformed member's refusal
             test/packs/PackState.test.cpp         # state.json, its writers, the version locks, a launch's settle
             test/packs/PackLoader.test.cpp        # the table: loads, dependency order, every refusal
             test/packs/PackDiscovery.test.cpp     # which folders a launch loads, and the launch itself
             test/packs/PackEngines.test.cpp       # a pack in a running Engine, two Engines, a live load under audio
             test/packs/MesenPack.test.cpp         # the Mesen package as a pack, its PCM held to the static build's
             test/packs/BoundaryRules.test.cpp     # the host's code casts nothing by RTTI
```

In its `LIBS`, after the `retroplug-packfixture-nodes` line add:

```cmake
             retroplug-mesen                       # the static build MesenPack.test.cpp holds the pack to
```

In its `DEFS`, after the `RP_PACKS_BUILD_DIR` line add:

```cmake
             RP_REPO_ROOT="${CMAKE_SOURCE_DIR}"     # the trees BoundaryRules.test.cpp scans
```

and replace `add_dependencies(retroplug-packs-test ${_rp_packfixture_targets})` with
`add_dependencies(retroplug-packs-test ${_rp_packfixture_targets} retroplug-mesen-pack)`.

- [ ] **Step 2: Run the configure to see it fail**

Run: `pnpm configure`
Expected: FAIL: `The dependency target "retroplug-mesen-pack" of target "retroplug-packs-test" does not exist.`

- [ ] **Step 3: Append retroplug_pack() to cmake/packs.cmake**

Append to `cmake/packs.cmake`:

```cmake

# retroplug_pack(<package> [MANIFEST <file>]): the package's native entries built as a pack, retroplug-<package>-pack,
# from its manifest's desktop targets and entries (the same ones a product links), its pack.json template MANIFEST
# (packages/<package>/pack.json by default). Called from the package's own native/CMakeLists.txt, after its targets,
# so the module is the package's own target and links nothing its manifest does not declare. Part 1 loads a pack's
# native node types alone, so a package with a provider, an audio backend, a script pack or an embed is refused here
# rather than built into a pack that would silently lose them.
function(retroplug_pack package)
    cmake_parse_arguments(P "" "MANIFEST" "" ${ARGN})
    if(P_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "retroplug_pack(${package}): unrecognised argument(s): ${P_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT P_MANIFEST)
        set(P_MANIFEST "${RP_PACKAGES_DIR}/${package}/pack.json")
    endif()
    get_property(_entries GLOBAL PROPERTY RP_PACKAGE_${package}_ENTRIES)
    get_property(_targets GLOBAL PROPERTY RP_PACKAGE_${package}_TARGETS)
    foreach(_part PROVIDERS AUDIO_BACKEND_TARGETS SCRIPT_ENTRY EMBED_IDS)
        get_property(_has GLOBAL PROPERTY RP_PACKAGE_${package}_${_part})
        if(_has)
            message(FATAL_ERROR "retroplug_pack(${package}): the package has ${_part} (${_has}), which a pack does not "
                                "load yet (docs/downloadable-packs-loading-design.md): build it into a product")
        endif()
    endforeach()
    if(NOT _entries)
        message(FATAL_ERROR "retroplug_pack(${package}): the package's manifest names no native entry, so its pack "
                            "would register nothing")
    endif()
    retroplug_pack_module(retroplug-${package}-pack MANIFEST "${P_MANIFEST}" LIBS ${_targets} ENTRIES ${_entries})
endfunction()
```

- [ ] **Step 4: The Mesen pack's manifest and target**

Create `packages/mesen/pack.json` (its `license` is the vendored Mesen's GPL v3; its LICENSE file grants no "or
later", so `GPL-3.0-only` until the owner says otherwise):

```json
{
  "id": "mesen",
  "name": "Mesen",
  "version": "0.1.0",
  "license": "GPL-3.0-only",
  "source": "https://github.com/tommitytom/RetroPlug",
  "requires": {
    "packs": [],
    "platforms": []
  },
  "provides": {
    "platforms": ["nes", "sms", "gg", "gba"],
    "nodeTypes": ["rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"],
    "roles": ["mesen"]
  },
  "halves": {
    "native": "rp-mesen"
  }
}
```

Append to `packages/mesen/native/CMakeLists.txt`:

```cmake

# --- retroplug-mesen-pack: the same entry built as a pack (cmake/packs.cmake), staged at build/packs/mesen/0.1.0/ from
# ../pack.json. Built by name, or by what loads it: retroplug-packs-test boots a NES through it and holds its PCM to
# this package's static build. Only the native half: the TS halves join a pack in M2.
retroplug_pack(mesen)
```

- [ ] **Step 5: Build and run**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed (... assertions in 50 test cases)` on Linux (49 on macOS; on Windows 44, the PCM case
among them reported skipped in Catch2's own summary). The Mesen cases print each boot's iNES lines twice (one per
build) and pass.

The first Mesen case reads the same from the file; `nm` is the look by hand:

Run: `nm -D --defined-only build/packs/mesen/0.1.0/native/linux-x64/rp-mesen.so`
Expected: exactly one line, ending ` T rp_pack_entry`.

Run:

```bash
nm -D --undefined-only build/packs/mesen/0.1.0/native/linux-x64/rp-mesen.so | grep -v "@GLIBC\|@CXXABI\|@GLIBCXX\|@GCC"
```

Expected: only the weak `__gmon_start__`, `_ITM_deregisterTMCloneTable` and `_ITM_registerTMCloneTable`: the module
imports nothing from RetroPlug.

- [ ] **Step 6: How a boundary failure shows, and the fallback**

Nothing to run: this is what the reviewer checks the step's results against, and what a later failure means.

- **A symbol the module did not keep to itself** (an export beside the entry, a GNU-unique static glibc would merge
  with the host's copy, an import of a RetroPlug symbol): the first Mesen case's dynamic-symbol read names it on
  Linux, and `--no-undefined` fails the link of a module that imports one. The interleaved PCM case is no detector
  of a shared static: it shows the two copies' output agrees, and would differ only if a merged static changed what
  a core computes.
- **RTTI across the boundary:** `[packs-boundary]` lists the line. The fix is the host's: call through a virtual or a
  null-checked capability, as `SystemBase` does today.
- **A static the host sets and the pack reads** (none today: determinism is the environment, read by each copy): the
  PCM differs from the first block. The fix is to pass it through `rp_host` or the build request.
- **Fallback, if the C++ boundary cannot be held on a platform:** the pack keeps its systems behind the C ABI (a
  console node owning its core, with the capabilities the engine reads reached through `get_interface` rather than
  `SystemBase`), which is the large refactor the design avoided; or that platform's packs build into the product as
  today, with run-time loading for the C-only packs alone. Report which, with the failing case's output, before M5.
- **Scratch paths the two copies share:** each copy of Mesen keys its scratch by the pid alone
  (`MesenGlobalInit.cpp` clears `/tmp/retroplug-mesen/<pid>` at its first init, and `NesEverdriveFifo.hpp` counts its
  SD-card slot per copy, so both copies' first NES gets `retroplug-sd-<pid>/0`). A BlipToaster NES writes neither,
  which is why the case is the NES alone; a console that stages a file there (the SMS's ROM, the GBA's BIOS) would
  see one copy clear the other's. That is a property of two copies in one process, which M5's test product (Mesen as
  a pack and nothing beside it) does not have; it is not a boundary failure.
- **Ownership across the boundary, on every platform:** heap ownership crosses it today (the build request's vectors
  are moved into the pack and freed there, the pack writes the host's `std::string* why`, and `Engine::destroySystem`
  deletes the pack's core through `SystemBase`'s virtual deleting destructor). That is sound only while host and pack
  share one allocator: true on Linux and macOS (one libstdc++ or libc++, one malloc, and no pack may replace the global
  `operator new`), not under Windows' static CRT. Until Design questions (1) is decided the PCM case `SKIP`s on Windows:
  a Release run there would usually pass, since the static CRT has allocated from the process heap since VS2012, and
  that pass would prove nothing. The skip is counted in Catch2's summary only: `run-plugin-tests.mjs` reads the exit
  code, and `skip-baseline.json` ratchets the TS suites' skips, not a Catch2 binary's.

- [ ] **Step 7: Commit**

```bash
git add cmake/packs.cmake packages/mesen/pack.json packages/mesen/native/CMakeLists.txt \
    packages/native/test/packs/MesenPack.test.cpp packages/native/test/packs/BoundaryRules.test.cpp \
    packages/native/cmake/tests.cmake
git commit -m "packs: the Mesen package built as a pack renders its static build's PCM, bit for bit"
```

- [ ] **Step 8: Cross-platform gate, before anything is pushed**

As at the end of Task 6, on a Windows and a macOS machine, from this commit: build `retroplug-packs-test` and run it.
Expected: on macOS `All tests passed (... assertions in 49 test cases)` (the Mesen module a `.dylib` built for the
architectures the build names: CI's `build.yml` passes `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`); on Windows every
case passes and the Mesen PCM case is reported skipped, with `rp-mesen.dll` built and its descriptors equal to the
static build's. Then `pnpm test:plugin` on both (`test.bat plugin` on Windows). The user pushes only after both pass.

---

### Task 11: The docs

How it works goes in the spec doc that owns it (spec/02 for the host, spec/06 for the build and tests), AGENTS.md gets
the one rule a commit has to remember (bump the API level), the test binary counts move from sixteen to seventeen, and
spec/07's packs entry stops saying every pack is built in. No spec/07 item is finished by M1, so none is deleted.

**Files:**
- Modify: `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/13-subsystems.md`,
  `AGENTS.md`, `cmake/test-binary.cmake`, `packages/native/cmake/tests.cmake`

- [ ] **Step 1: Apply the edits**

Run from the repository root (each edit must match exactly once, or the script stops naming it):

```bash
python3 - <<'PYEOF'
import pathlib

SPEC02_ANCHOR = "packs-a-packages-native-half-loaded-at-run-time"


def edit(path, old, new):
    p = pathlib.Path(path)
    s = p.read_text()
    if s.count(old) != 1:
        raise SystemExit(f"{path}: expected one match for {old[:60]!r}, found {s.count(old)}")
    p.write_text(s.replace(old, new))


# spec/02: the packs, after the packages section and before the kit reproducibility one.
SPEC02 = """### Packs: a package's native half loaded at run time

A pack ([downloadable-packs-loading-design.md](../docs/downloadable-packs-loading-design.md) sections 1 and 2) is a
version folder: `pack.json`, and a native library per platform under `native/<platform>/` (`linux-x64`,
`linux-arm64`, `macos-universal`, `windows-x64`). The library exports one C function,
[`rp_pack_entry`](../packages/node-sdk/native/src/graph/rp_pack.h), which answers the API level it was built against
and the same `rp_package` list a product's generated table holds. `RP_API_LEVEL` is one integer, bumped by hand in any
commit that breaks packs (the C ABI, `SystemBase` and the contract headers, the TS contract modules); a pack at any
other level is refused, saying which side is out of date, before anything else of it is used.

`retroplug-packs` ([src/host/packs](../packages/native/src/host/packs)) is the folder model and the loader, linked by
every desktop host through `retroplug-backend`. `PackDirs` names the roots (the user's data directory for the packs
and `state.json`, the cache directory for the bytecode M2 compiles; `RETROPLUG_PACKS_DIR` and
`RETROPLUG_PACKS_CACHE_DIR` override them, and every function takes the roots, so a test hands it a temporary
folder). `PackManifest` parses and checks `pack.json` (a library is always `rp-<id>`); `PackState` reads `state.json`
under a shared hold of `state.lock` and writes it under an exclusive one, through a temporary file and a rename, and
settles pending updates and removals at a launch; `FileLock` is the shared lock a process holds in each installed
version folder it opened, loaded or refused, which a settle must take exclusively, and keep while it deletes, before it
deletes the folder; `PackDiscovery` finds the installed packs and the development folders (the ones a host passes, then
`RETROPLUG_PACK_PATH`'s, so an explicit choice wins), a development pack overriding an installed one of its id, and
`loadLaunchPacks` settles, discovers and takes its version locks in one exclusive hold of `state.lock`, so another
launch's settle never deletes a folder between this one's discovery and its load.

`PackTable` opens each pack's library once and never closes it (a refused one too: opening it ran its static
constructors). A pack loads all or nothing: refused for its API level, a half other than the native one (M1 loads no TS
half), a missing library or entry, an entry that throws (caught on the host's side of the entry: run on Linux, and on
MSVC by the plan's Windows gate), a node type its `provides.nodeTypes` does not declare or a declared one it does not
register, one a built-in package or an earlier pack holds, a `requires.packs` (packs only) not yet loaded, or anything
the `NodeRegistry` refuses on a scratch registry holding what every Engine holds before it. `processPacks()` is the
process's table, seeded with the product's packages and empty until a host loads into it (no host does at launch yet: a
native half alone has nothing the app places until M2's TS halves).
Every `Engine` registers its table's packs after the product's packages and the built-in script pack, in load order,
which is dependency order; a pack loaded later joins each Engine at its next `setGraph`, on its control thread, the
thread `loadScriptPack` changes the registry on (`Engine::syncPacks`), every package of the pack checked
(`NodeRegistry::canAdd`) before the first is added. An Engine that already holds one of a pack's ids (a script
node's, or a test's `addNodePackage`) refuses that pack whole, there alone (`refusedPacks()`). The offline pool keeps
its fixed `core` and `dsp` subset and registers no pack.

`retroplug_pack(<package>)` ([cmake/packs.cmake](../cmake/packs.cmake)), called from a package's own CMakeLists,
builds its desktop entries as a MODULE (`retroplug-<package>-pack`, built by name) from its static targets and a
generated entry, staged at `build/packs/<id>/<version>/`. The module keeps every symbol but the entry to itself (a
version script on Linux, an exported-symbols list on macOS, `dllexport` on Windows) and Linux links it
`--no-undefined`, so a pack imports nothing from RetroPlug and its copies of node-sdk, system, reflect-cpp and its core
never bind to the host's (`[packs-loader]` and `[packs-mesen]` read the modules' dynamic symbols on Linux). The
boundary between a pack's systems and the engine is C++ (`SystemBase`), pinned by the toolchain and the API level, and
heap ownership crosses it (a build request's vectors, a refusal's string, a core the engine deletes through its virtual
destructor), so it needs one allocator shared by host and pack, which Windows' static CRT does not give: that
decision is open ([downloadable-packs-m1-plan.md](../docs/downloadable-packs-m1-plan.md), Design questions). The host
casts nothing by RTTI (`[packs-boundary]` scans every package's native source but Mesen's), and `[packs-mesen]`
boots a NES through the Mesen package built as a pack beside its static build and holds the two to the same PCM and
work RAM, bit for bit, on Linux and macOS.

### Kit reproducibility
"""
edit("spec/02-native-host.md", "### Kit reproducibility\n", SPEC02)

# spec/06: retroplug_pack beside the package rules.
ADDING = "**Adding a package.** In the order a configure demands it:"
SPEC06_PACK = f"""\
**A package's native half also builds as a pack.** `retroplug_pack(<package>)` (cmake/packs.cmake), called from the
package's own `native/CMakeLists.txt` after its targets, makes `retroplug-<package>-pack`: a MODULE, built by name or by
what loads it, linking the package's static targets privately with a generated `rp_pack_entry`, its symbols local but
the entry's, staged with the package's `pack.json` (its `apiLevel` stamped from `graph/rp_pack.h`) at
`build/packs/<id>/<version>/`. Every static library in a module's link closure must be position-independent, which
the configure checks. A package with a provider, an audio backend, a script pack or an embed is refused: part 1 loads
native node types alone ([spec/02](02-native-host.md#{SPEC02_ANCHOR})).

{ADDING}"""
edit("spec/06-build-test.md", ADDING, SPEC06_PACK)

# spec/06: the test binary in the targets table, and the counts.
edit("spec/06-build-test.md",
     "| executable | Fourteen of the sixteen Catch2 binaries `pnpm test:plugin` builds and runs (the other two are "
     "the N8 pack's, the next row).",
     "| executable | Fourteen of the seventeen Catch2 binaries `pnpm test:plugin` builds and runs (the N8 pack's two "
     "are the next row, the packs' one the row after it).")
N8_ROW = "| `retroplug-n8-test` / `-serial-test` / `-n8-hwtest` | executable |"
PACKS_ROW = (
    f"| `retroplug-packs-test` | executable | The packs ([spec/02](02-native-host.md#{SPEC02_ANCHOR})): the folder "
    "model on a temporary root (`[packs-folders]`), the loader's refusals against the fixture pack's variants "
    "(`packages/native/test/packs/fixture`, `[packs-loader]`), which folders a launch loads (`[packs-discovery]`), "
    "packs in running Engines and a live load under an audio thread (`[packs-engines]`), the Mesen package as a pack "
    "(`[packs-mesen]`) and the RTTI rule (`[packs-boundary]`). Its product is the `packs-host` list, the desktop's "
    "without `mesen` and `bliptoaster`. | no |\n")
edit("spec/06-build-test.md", N8_ROW, PACKS_ROW + N8_ROW)
edit("spec/06-build-test.md", "over sixteen Catch2 binaries |", "over seventeen Catch2 binaries |")
edit("spec/06-build-test.md",
     "The C++ unit tier's sixteen binaries are the [`pnpm scripts`](#pnpm-scripts) table's `test:plugin` row, which "
     "says what\neach case holds;",
     "The C++ unit tier's seventeen binaries are the [`pnpm scripts`](#pnpm-scripts) table's `test:plugin` row, which "
     "says\nwhat each case holds;")
edit("spec/06-build-test.md", "| `test:plugin` | sixteen Catch2 binaries (",
     "| `test:plugin` | seventeen Catch2 binaries (")
edit("spec/06-build-test.md", "`-device-mode-test`, `-audio-host-test`, `-n8-test`, `-serial-test`) |",
     "`-device-mode-test`, `-audio-host-test`, `-packs-test`, `-n8-test`, `-serial-test`) |")
edit("spec/06-build-test.md",
     "The thread run also builds\nand runs `retroplug-launchpad-test`, `retroplug-midi-test` and "
     "`retroplug-audio-host-test` under ThreadSanitizer after\nthe native slugs. ",
     "The thread run also builds\nand runs `retroplug-launchpad-test`, `retroplug-midi-test` and "
     "`retroplug-audio-host-test` under ThreadSanitizer after\nthe native slugs, and both runs build and run "
     "`retroplug-packs-test` (a pack loaded on one thread while another\nplans and a third renders, and the loader's "
     "libraries and held folders).\n")
edit("spec/13-subsystems.md", "(one of the sixteen `pnpm test:plugin` binaries)",
     "(one of the seventeen `pnpm test:plugin` binaries)")

# spec/07: a pack's native half now loads; what is left of the road is the design's later parts.
SPEC07_OLD = (
    "Every pack is built in. The authoring loop, the pack format and downloads, a pack's UI half and unloading a pack "
    "(the\n`Engine` has `loadScriptPack` and no unload) are what downloadable packs need (the C1 design's section 8).")
SPEC07_NEW = f"""A pack's native half loads at run time
([spec/02](02-native-host.md#{SPEC02_ANCHOR}), part 1's M1 of
[downloadable-packs-loading-design.md](../docs/downloadable-packs-loading-design.md)), but no host loads one at
launch, and a pack's TypeScript halves, placeholders, archives and the CLI's pack commands are that design's M2 to M5.
The authoring loop, downloads and unloading a pack (the `Engine` has `loadScriptPack` and no unload) are what
downloadable packs still need (the C1 design's section 8)."""
edit("spec/07-remaining-work.md", SPEC07_OLD, SPEC07_NEW)

# The test binary counts in the CMake comments.
edit("cmake/test-binary.cmake",
     "# There are seventeen of them (sixteen run by `pnpm test:plugin`, whose N8 pair",
     "# There are eighteen of them (seventeen run by `pnpm test:plugin`, whose N8 pair")
edit("packages/native/cmake/tests.cmake",
     "# Sixteen Catch2 binaries, all EXCLUDE_FROM_ALL and built BY NAME (nothing reads BUILD_TESTING), which\n"
     "# `pnpm test:plugin` builds and runs: nine here",
     "# Seventeen Catch2 binaries, all EXCLUDE_FROM_ALL and built BY NAME (nothing reads BUILD_TESTING), which\n"
     "# `pnpm test:plugin` builds and runs: ten here")

# AGENTS.md: the API level is a rule a commit has to remember.
AGENTS_RULE = f"""\
- **A commit that breaks packs bumps `RP_API_LEVEL`** (graph/rp_pack.h): the C ABI, `SystemBase` and the contract
  headers, the TS contract modules ([spec/02](spec/02-native-host.md#{SPEC02_ANCHOR})).
- **Config migrations**:"""
edit("AGENTS.md", "- **Config migrations**:", AGENTS_RULE)
PYEOF
```

- [ ] **Step 2: Check the links**

Run: `pnpm check:docs`
Expected: exit 0 (warnings in older plan records are allowed; no `ERROR` line).

Run: `wc -l AGENTS.md`
Expected: under 200.

- [ ] **Step 3: Commit**

```bash
git add spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md spec/13-subsystems.md AGENTS.md \
    cmake/test-binary.cmake packages/native/cmake/tests.cmake
git commit -m "docs: packs M1 in spec/02 and spec/06, the API level rule in AGENTS.md"
```

---

### Task 12: Verify the whole tree

- [ ] **Step 1: The full build** (a UI change needs the bare build, and `packs-host`'s control-plane bundle is in ALL)

Run: `cmake --build build -j$(nproc)`
Expected: exit 0.

- [ ] **Step 2: The C++ unit tier**

Run: `pnpm test:plugin`
Expected: `# 17 plugin test binary(ies) passed`.

- [ ] **Step 3: The native tier, which drives the changed Engine through every host path**

Run: `pnpm test:native`
Expected: every file passes. (In the dev container the plan was prototyped in, `app-cores`' GBA case failed with the
pre-M1 host binary as well: it is not this plan's, and it is reported, not fixed here.)

- [ ] **Step 4: The checks**

Run: `pnpm typecheck && pnpm check:boundaries && pnpm check:docs`
Expected: exit 0; `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

- [ ] **Step 5: Nothing stray**

Run: `git status --short`
Expected: only `deps/sameboy` (` m deps/sameboy`, expected after a configure; never staged).

---

## Design questions this plan found

**Decided while the plan was reviewed** (applied above): `requires.packs` names packs only; a type declared in
`provides.nodeTypes` and not registered is refused, as an undeclared registration is; the Mesen pack's licence stays
`GPL-3.0-only` pending the owner's confirmation.

**Open:**

0. **For M2:** `isInsidePath` (PackManifest.cpp) does not refuse Windows reserved device names (`NUL`, `COM1`) or a
   trailing dot or space in a segment; M1 refuses every non-native half, so M2 adds them when it loads a TS half.

1. **The allocator must be shared, or no ownership may cross the boundary, on every platform. Decide before M5.**
   Heap ownership crosses the pinned C++ boundary today in three places: `SystemBuildFn` takes the
   `SystemBuildRequest&&` whose `rom`, `sram` and `savestate` vectors were allocated on the other side and frees them;
   a pack writes the host's `std::string* why`; and `Engine::destroySystem` runs `delete core` in the host, which is
   right only because `SystemBase`'s virtual deleting destructor runs the pack's. Ten `SystemBase` virtuals also return
   `std::vector` or `std::string` by value (`channelLayout`, `drainCoreBytes`, `saveSramBytes`, `saveStateBytes`,
   `getCpuRegisters` among them), each allocated in the pack and freed in the host. On Linux and macOS one libstdc++ or
   libc++ and one malloc serve both sides, so this holds as long as no pack replaces the global `operator new`. On
   Windows the tree pins the static CRT (`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded`, root `CMakeLists.txt`), so each
   DLL has its own; a Release cross-module free usually passes silently there (the static CRT has allocated from the
   process heap since VS2012) and fails under `/MTd`, so no Windows test run detects it. The options:
   - **`/MD` across the tree.** One CRT, so every crossing above is sound unchanged. Cost: txiki, mbedtls and
     libwebsockets force `MultiThreaded` for their own targets inside `deps/dpf.js` (a change there and the
     three-commit submodule pointer bump spec/06 describes), vcpkg's `x64-windows` triplet for SDL2 and libffi in
     place of the static one, and the VC++ runtime shipped with every Windows build (the redistributable, or the CRT
     DLLs app-local beside the plugin, since a DAW loads plugins from folders it does not own), with the plugin then
     depending on whichever runtime a host process already loaded.
   - **No ownership crossing the boundary.** `SystemBuildFn` takes spans and copies inside the pack, `why` becomes a
     fixed buffer (as `rp_error` is), a core is destroyed through a deleter the pack's build function returns beside it,
     and the ten virtuals fill caller-provided buffers or return views the pack owns. Cost: the `SystemBase` contract
     and every console's implementation (SameBoy's too), an API level bump, and a rule the SDK has to keep for every
     future virtual. Either way, `[packs-mesen]` stays skipped on Windows until it is done.
2. **`requires.platforms` is parsed and not checked in M1**: platforms are registered by the TS halves (M2). A cart
   pack naming a console it needs (risa's `nes`) depends on it.
3. **Static constructors run before the API level is read.** `dlopen` and `LoadLibraryExW` run a library's
   initialisers before the loader can call `rp_pack_entry`, so "uses nothing else from a pack at any other level" holds
   for the entry's answer, not for its static initialisation. The SDK (part 3) should keep a pack's static
   initialisation trivial, and the design should say a refused library stays mapped (M1 keeps it open, since its
   initialisers may have registered `atexit` handlers, and keeps its installed folder held).
4. **No host loads packs at launch yet.** M1 provides `loadLaunchPacks` and calls it from tests; which milestone wires
   it into the CLI's `main`, `PluginDSP`, the SDL standalone and the Node addon (M2, with the TS halves that make a
   pack placeable, or M4 with `--pack` and Settings) is open.
5. **The offline pool registers no pack.** Its registry is the fixed `core` and `dsp` subset, filled once and read
   lock-free by its workers, so a pack's DSP node cannot run in an offline document (a kit compile).
6. **"One load per process" is one load per module.** Two plugin formats in one DAW process (a VST3 and a CLAP of
   RetroPlug) are two modules, each with its own `retroplug-backend` and so its own `processPacks()` table: each opens
   the pack (the OS hands back the same mapping) and registers it in its own engines. Harmless, but the design's
   sentence is stronger than a static backend can give.
7. **A script node type clash is per Engine.** The table judges a pack against the built-in packages and earlier
   packs; the built-in script pack's types are registered per Engine at run time, so a pack taking one of those ids is
   refused in each Engine (all or nothing there) while the table holds it as loaded. Settings > Packs (M4) would show
   it loaded; the table could learn the script types to refuse it once.
8. **Two copies of a console in one process share its pid-keyed scratch paths** (Task 10, Step 6). Only the
   feasibility test has two copies; but a pack and a later version of it never load together (native code is never
   unloaded), so the question is only whether part 2 keeps any static copy of Mesen beside the pack (the web build bakes
   it in, a separate binary).
9. **The roots' overrides and `state.json`'s shape** (`RETROPLUG_PACKS_DIR`, `RETROPLUG_PACKS_CACHE_DIR`; the
   version stamp, `pending`, `remove`; `state.lock` on every access) are this plan's, not the design's.
10. **`macos-universal`:** CI's `build.yml` and `release.yml` configure macOS with
    `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, and a pack module inherits it, so the CI and release modules are
    universal and match the folder's name; a local single-architecture build stages its module under the same name,
    which only that machine loads.
11. **The web build** (feature/web, unmerged) links `retroplug-backend`, which now links `retroplug-packs` and its
    `dlopen` path. The design says the web has no loader, so that branch's merge must leave `NativeLibrary.cpp` and
    `PackTable`'s library step out of the web build, or stub them.
12. **The `packs-host` list costs every full build a control-plane bundle** (the cp bundle target is in ALL for each
    list a product uses) and `pnpm typecheck` one more product tree.

13. **Every load rebuilds a scratch registry from everything loaded before it** (`PackTable::loadLocked`'s trial), which
    is fine at M1's handful of packs and quadratic in the pack count. Before M5 the table keeps one committed registry
    and answers a candidate with `NodeRegistry::canAdd` against it, so a load costs only its own packages.
14. **`retroplug-sdl` links `-static-libstdc++`** (`packages/native/CMakeLists.txt`, near the SDL target): a pack
    `dlopen`ed there would run a second C++ runtime across the pinned C++ boundary (Task 10), each with its own
    allocator state and `std::string` / `std::vector` types the two sides exchange. Task 10's boundary result does not
    cover that host; M5 either links the shared runtime there or keeps packs out of that product.
15. **Opening a library runs its static constructors before its level is checked** (`NativeLibrary.hpp`): what keeps an
    honest pack of another API level from being opened is the manifest's `apiLevel`, which the loader compares first,
    before the rest of the manifest is read. A dishonest manifest over a library of another level is caught by the entry
    only after its constructors ran; that is inside the trust a pack already has (it is native code).

16. **A development folder inside the installed root** (a `--pack` or `RETROPLUG_PACK_PATH` entry that is really
    `packs/<id>/<version>/`) would load unheld, so a settle could delete a folder with a library open from it. Resolved
    in Task 7: `loadLaunchPacks` holds any candidate whose canonical path lies under the canonical data root, inside
    the `state.lock` section (one that cannot be held there is refused, never loaded unheld), and the pack keeps its
    Development origin.

## Self-review

**Spec coverage** (the brief's eight items, and the review's findings):

1. Folder model: roots with overrides and the per-platform defaults (Task 3); `pack.json`, the library stem bound to
   the id, non-native halves refused (Tasks 4 and 6); `state.json` with its version read first, `state.lock` on every
   access, atomic write, settle holding each folder's lock while it deletes (Task 5); the per-process lock in each
   opened version folder, refused ones included (Tasks 5 and 6); `RETROPLUG_PACK_PATH` and a host's list, development
   overriding installed, and a launch's settle, discovery and holds in one hold of `state.lock`, with the interleaving
   driven in order (Task 7); every case on a `TempDir`, swept on Windows (Tasks 3 to 7).
2. The API level `RP_API_LEVEL` and `rp_pack_entry` (Task 1); its TS mirror is M2's, generated from `rp_pack.h` as
   `cmake/packs.cmake` reads it (stated in its header comment, Task 1).
3. The loader: one table, never closed, platform folders, the level first, every refusal with its message and nothing
   registered, the throwing entry and the over-declared manifest among them, packs only in `requires.packs` (Task 6),
   dependency order (Tasks 6 and 7).
4. Engine registration: built-ins first with the golden unmoved (Task 8, Step 8), packs in dependency order, each
   staged with `canAdd` so a pack of two packages is all or nothing per Engine, a live load reaching running Engines at
   their next `setGraph`, new Engines seeing every load, the process's table reaching every host's Engines with no
   host naming a pack, and `test:plugin` and `test:native` at the task's end (Task 8).
5. CMake: `retroplug_pack_module` and `retroplug_pack`, MODULE, hidden but the entry (a non-empty export list on every
   platform), static deps private, the staging, the PIC check and its reasoning (Tasks 2 and 10).
6. The fixture pack and thirteen variants: wrong level both ways and a lying entry, a throwing entry, provides
   mismatched both ways, duplicates against a built-in and against a pack, two packages in one library, a TS half, no
   platform library, no entry, an unmet dependency (Task 2, exercised in Tasks 6 and 8).
7. Tests: the folder model, each refusal, a live add, two Engines, a TSAN- and ASAN-clean run including a live add under
   audio, both legs of `tools/run-sanitizer.sh` (Task 9); Windows and macOS gates after Tasks 6 and 10.
8. Feasibility on Mesen: a pack module with its own copies, its exports and imports read in a test, loaded into a
   product without Mesen, BlipToaster's PCM shown MIDI-driven and held to the static build's byte for byte with its
   work RAM, claims limited to what it proves, detection and fallback, Windows skipped until the allocator decision
   (Task 10).

**Placeholder scan:** every code step carries its code; the commands are the repo's (`pnpm configure`, `test:plugin`,
`test:native`, `typecheck`, `check:boundaries`, `check:docs`, `tools/run-sanitizer.sh`) or exact `cmake` and binary
invocations; no step defers a decision; the one open decision (Design questions, 1) is gated, not assumed.

**Type consistency:** `PackRoots`, `displayPath`, `PackManifest`, `PackState` / `InstalledPack`, `lockPackState`,
`FileLock`, `PackCandidate` (with its `lock`) / `PackOrigin` / `PackRefusal`, `PackTable` / `LoadedPack` / `PackLoad`,
`PackDiscovery`, `loadLaunchPacks`, `processPacks`, `NodeRegistry::canAdd`, `Engine::packsRegistered` /
`refusedPacks` / `syncPacks` are each defined once (Tasks 3 to 8) and used with those names and signatures in every
later task and test. The CMake names (`RP_API_LEVEL`, `RP_PACK_PLATFORM`, `RP_PACKS_DIR`, `RP_PACK_TARGETS`,
`RP_PACKFIXTURE_TARGETS`, `retroplug-<id>-pack`) match between `cmake/packs.cmake`, the fixture, `tests.cmake` and
`warnings.cmake`.
