# Downloadable packs M5a: no heap ownership crosses the pack boundary. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A console pack and the host never free each other's heap memory. On Windows' static C runtime (`/MT`, each
module its own heap) a pack's systems are then as sound as they are on Linux and macOS, and Mesen's pack runs there.

**Architecture:**
- **A result crosses through a sink.** Each virtual a pack implements that returned a container (`std::vector`,
  `std::string`, a struct holding one) now takes a sink the caller made over its own container (`system/Sink.hpp`).
  The callee hands the sink what it has, by const reference or by pointer and size, and the sink copies it into the
  caller's heap. An inline, non-virtual helper with the old name and signature keeps every call site as it was.
- **A parameter crosses as a view.** The build request carries spans and a string view of what the node's resources
  and settings hold, and a build function copies what its core keeps. A refusal's reason is the C `rp_error` buffer.
- **The one object that crosses is a core.** The pack builds it and the host deletes it, through `SystemBase`'s
  virtual destructor, whose deleting form is compiled in the pack: that is the pack's deleter.
- **A test proves it, and a test keeps it.** A copy of the Mesen pack with a heap of its own (its C++ runtime linked
  statically on Linux, `/MT` on Windows) and a tagging allocator on both sides fails on any block freed by the side
  that did not allocate it. A scan of the contract headers refuses a public virtual returning an owning type.

**Tech Stack:** C++20, M1's `retroplug-packs` and `cmake/packs.cmake`, the Mesen and SameBoy packages, Catch2 v3, the
`test:plugin` and `test:native` suites.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones. These
are done, each with its plan beside this one: M1 native packs, M2a live registries, M2b shared contract modules, M2c a
pack's TS halves, M2d the device script half and the render worker, M2e the protocol half, M2f the contribution kinds.

M5 (the real packages as packs) is split in four, decided with the user 2026-11-25, and built before M3 and M4:
- **M5a (this plan):** no heap ownership crosses the pinned C++ boundary.
- **M5b:** Mesen as a whole pack (native plus its TS halves), with packs-host builds of `retroplug-host`, the UI
  harness and the CLI that load it, and Mesen's suites run unchanged against it.
- **M5c:** BlipToaster as a whole pack.
- **M5d:** the host legs (two plugin instances, the Node addon, concurrency, a sanitizer leg).

**The decision (the user, 2026-11-25).** The M1 plan's design question 1 left open how the boundary survives Windows'
static CRT: `/MD` across the tree, or no ownership crossing. The user chose **no ownership crossing**, keeping `/MT`,
because packs written in other languages (Rust) may come later. `/MD` helps no foreign-language pack, and spans, sinks,
a C error buffer and a pack-side deleter are what a later thin C system ABI (an `rp_system` vtable) would forward to.
That C ABI is **not** part of M5a.

**What exists (read before starting).**
- `packages/system/native/src/system/SystemBase.hpp`: the class every console implements. Five public virtuals return
  an owning type: `channelLayout`, `drainCoreBytes`, `saveSramBytes`, `saveStateBytes`, `getCpuRegisters`.
- `packages/system/native/src/system/SystemCapabilities.hpp`: `RomCapability::title()` returns `std::string`.
- `packages/system/native/src/system/DebugTarget.hpp`: `IDebugTarget`, Mesen NES's debugger. Eight virtuals return an
  owning type (`readProfile`, `disassemble`, `readTrace`, `getCallStack`, `getExpansionAudioState`, `getPpuState`,
  `drainBreakHits`, `drainEvents`), and three take host-owned `const std::string&` / `const std::vector&` inputs.
- `packages/system/native/src/system/SystemBuild.hpp` and `HostSystems.hpp`: the build path. A system node (compiled in
  the pack) fills a `SystemBuildRequest` of vectors and a string, passes it **by value** through
  `HostSystems::build` (the host, `rp.host.systems/2`) to the pack's own `SystemBuildFn`, which takes it by `&&` and
  writes refusals into the host's `std::string* why`. Whatever is not moved into the core (every host refusal before
  the build, a build function refusing early, `romName` for every console but SMS) is freed by the side that did not
  allocate it.
- `packages/system/native/src/system/MemoryAccessor.hpp`: carries an optional `std::vector<MemoryPatch>*` that a host
  `write()` would push into. Nothing sets it.
- The host's callers: `host/rpc/DebugRpcService.cpp`, `host/rpc/EngineRpcService.cpp` (`channelLayout`),
  `host/engine/SnapshotRegistry.cpp` (`saveStateBytes`, `saveSramBytes`), `host/engine/Engine.cpp` (`buildSystem`,
  `destroySystem`, the ROM title job).
- The implementations: `SameBoySystem` (packages/sameboy), `MesenNesSystem`, `MesenGbaSystem`, `MesenSmsSystem`,
  `MesenNesDebugSession` (packages/mesen), the build functions `rp::sameboy::buildCore` and
  `rp::mesen::build{Nes,Gba,Sms}Core`, and the test doubles in `packages/native/test/engine-graph/` (`NodeRig.hpp`,
  `EngineGraph.test.cpp`'s fixture console, `SystemNode.test.cpp`'s direct builds).
- `packages/native/test/packs/MesenPack.test.cpp` (`[packs-mesen]`): the Mesen pack against its static build. Its PCM
  case SKIPs on Windows, waiting for this decision.
- `packages/native/test/packs/BoundaryRules.test.cpp` (`[packs-boundary]`): the RTTI scan.
- `cmake/packs.cmake`: `retroplug_pack_module`, which builds a pack module exporting `rp_pack_entry` alone.

**A probe that shaped this plan (2026-11-25, Ubuntu gcc 13.3).** A shared object linked `-static-libstdc++
-static-libgcc` with a version script hiding everything but its entry, which replaces `operator new`, allocates through
its own `operator new` for everything its static libstdc++ does (`std::to_string`, a `std::vector<std::string>`): the
host's replaced `operator new` saw **zero** allocations during the call. So on Linux such a module is a faithful model of
a Windows `/MT` DLL: a heap of its own. `ldd` lists only libc.

**Execution model.**
- Sonnet implements each task from its text alone, and Opus reviews each task's diff before the next one starts.
- The code here was not compiled as written. Where a step meets the tree differently (a helper's exact name, an
  include, a field), the implementer follows the tree, keeps the step's intent, and reports the deviation.
- Every task leaves `pnpm test:plugin` green. The two new checks hold today's boundary as it is until the conversion
  is done: the heap scenario checks its crossings are more than zero, and the contract scan that it finds faults, each
  with a comment naming the task that flips it; Task 5 flips both to zero. (Not `[!shouldfail]`, which passes a case on
  ANY failure and would hide a broken load or REQUIRE while Tasks 3 to 5 rewrite the boundary: the Task 1 review.)
- Task 7 ends with a CI gate on Windows and macOS, which needs the user's go-ahead to push.

## Decisions this plan makes

- **The rule, as one sentence for the SDK:** no heap block crosses the boundary. A container crosses only by const
  reference or view, read during the call; a result is handed to a sink the caller made; nothing crosses by value, by
  `&&`, or as a non-const reference or pointer the other side fills. The one object that crosses is a core, deleted
  through its virtual destructor.
- **Sinks are C-shaped.** `rp::ByteSink` is `{void* ctx; void (*write)(ctx, data, size)}` and `rp::Sink<T>` is
  `{void* ctx; void (*put)(ctx, const T&)}`. The helpers that make them (`appendBytesTo`, `appendTo`, `assignTo`) are
  inline, so a sink's function is compiled in the module that made it, and its copy allocates in that module's heap.
  A record handed to a `Sink<T>` may hold strings (a `ProfiledFunction`): it is lent for the call, the receiver copies
  it with its own code. (A C ABI would need view records there; that is design question 3.)
- **Every old call site keeps working.** Each converted `SystemBase` virtual gets a new name ending in `Into` and takes
  a sink; the old name becomes an inline non-virtual helper on `SystemBase` returning the caller's vector. So
  `SnapshotRegistry`, `EngineRpcService`, `DebugRpcService::drainCoreBytes`/`getCpuRegisters` and every test calling
  `core->saveStateBytes()` or `core->channelLayout()` compile unchanged. A core keeps its old body under a private name
  (`stateBytes()`, `sramBytes()`, ...) and its `...Into` override wraps it, so a core's own internal callers (Mesen's
  `captureStateSnapshot`, which does `dst = saveStateBytes();`) call the private one and pay nothing new.
- **`IDebugTarget` gets no helpers**: its only caller is `DebugRpcService`, which builds its vectors with `appendTo`.
  Its inputs become views: `loadLabels(std::string_view)`, `symbolAddress(std::string_view)`,
  `setBreakpoints(std::span<const BreakpointSpec>)`.
- **`RomCapability::title()` returns `std::string_view`** into the ROM the core keeps (valid for the core's life, as the
  capability object is).
- **The build request is views.** `SystemBuildRequest::rom`, `sram`, `savestate` become `std::span<const std::uint8_t>`
  and `romName` a `std::string_view`, all pointing at what the node's resources pin and its settings hold, alive for the
  call. `SystemBuildFn` becomes `std::unique_ptr<SystemBase> (*)(SystemId, const SystemBuildRequest&, double,
  rp_error*)`: a build function copies what its core keeps (the ROM copy moves from the node's `bytesOf` into the build
  function, so a build still copies the ROM once). `HostSystems::build` takes `const SystemBuildRequest*` and the
  interface id becomes **`rp.host.systems/3`**. `SystemBuild.hpp` gains `refuseBuild(rp_error*, std::string_view)`.
- **A core's deleter is its vtable.** `std::unique_ptr<SystemBase>` still carries the core out of a build function and
  `Engine::destroySystem` still runs `delete core`: both call the virtual deleting destructor, which the compiler emits
  with the class's vtable, in the pack, so the pack's `operator delete` frees it (Itanium's D0, MSVC's scalar deleting
  destructor). The heap test holds this. No separate deleter is added.
- **`MemoryAccessor` drops its patch vector** (`patches_`, the constructor's fifth argument and `MemoryType.hpp`'s
  `MemoryPatch`): nothing sets it, and a host `write()` pushing into a pack's vector is a crossing.
- **The host's calls into `SystemBase`'s non-virtual snapshot members stay** (`readStateSnapshot`,
  `stateSnapshotCapacity`, `stateRegions`, `rearmStateSnapshot`, `runUntilPc`, compiled into the host from the static
  `retroplug-system`): none allocates, so no ownership crosses, but they read the pack's object layout (design
  question 2).
- **The API level becomes 2.** `SystemBase`, the capability and debug interfaces, the build function and the
  `rp.host.systems` interface all change shape.
- **The heap test** is a binary of its own, `retroplug-packs-heap-test`, on the packs-host product with no static
  Mesen, because it replaces the global `operator new` (which `retroplug-packs-test` must not, and which would fight a
  sanitizer's allocator; nothing builds it under one). It loads `mesen-heap`, a second build of the Mesen package as a
  pack (`build/packs/mesen-heap/0.1.0`, stem `rp-mesen-heap`) whose module adds a tagging `operator new`/`delete` and
  exports four test hooks beside its entry (`retroplug_pack_module` gains `EXPORTS`). On Linux with gcc the module links
  `-static-libstdc++ -static-libgcc`; on Windows `/MT` already gives it a heap of its own; on macOS (libc++ is a system
  dylib whose out-of-line members allocate through the process's `operator new`) the module is not built and the cases
  SKIP.
- **The tagging allocator.** Every block carries a 16-byte header before it (the raw `malloc` pointer and a tag naming
  the side that allocated it). A delete that finds the other side's tag counts a violation and **leaks** the block
  (freeing it in the wrong C runtime is the very bug), so a run reports every violation instead of dying at the first.
  The pack counts its own; the host reads them through the pack's exported hooks.

## File structure

| File | Change |
|---|---|
| `packages/system/native/src/system/Sink.hpp` | **New.** `ByteSink`, `Sink<T>`, `appendBytesTo`, `appendTo`, `assignTo` |
| `packages/system/native/src/system/SystemBase.hpp` | Five `...Into` virtuals and their helpers |
| `packages/system/native/src/system/SystemCapabilities.{hpp,cpp}` | `title()` is a `std::string_view` |
| `packages/system/native/src/system/DebugTarget.hpp` | Sinks and views |
| `packages/system/native/src/system/SystemBuild.hpp` | The request is views; `SystemBuildFn` takes `const&` and `rp_error*`; `refuseBuild` |
| `packages/system/native/src/system/HostSystems.hpp` | `build` takes `const SystemBuildRequest*`; `/3` |
| `packages/system/native/src/system/SystemNode.hpp` | `build()` fills views |
| `packages/system/native/src/system/MemoryAccessor.hpp`, `MemoryType.hpp` | The patch vector and `MemoryPatch` go |
| `packages/sameboy/native/src/system/sameboy/SameBoySystem.{hpp,cpp}`, `SameBoyBackend.{hpp,cpp}` | Overrides; build |
| `packages/mesen/native/src/system/mesen/Mesen{Nes,Gba,Sms}System.{hpp,cpp}`, `MesenNesDebugSession.{hpp,cpp}`, `MesenBackend.{hpp,cpp}` | Overrides; build |
| `packages/native/src/host/engine/Engine.{hpp,cpp}` | `buildSystem` takes `const&`; the systems lambda; the title view |
| `packages/native/src/host/rpc/DebugRpcService.cpp` | Builds vectors through sinks |
| `packages/native/test/engine-graph/{NodeRig.hpp,EngineGraph.test.cpp,SystemNode.test.cpp}` | The build path's doubles and direct builds |
| `packages/native/test/packs/heap/{HeapTag.hpp,HeapOperators.inc,HostHeap.cpp,PackHeap.cpp,PackHeap.test.cpp,mesen-heap.pack.json}` | **New.** The heap test |
| `packages/native/test/packs/BoundaryRules.test.cpp` | The contract-header scan |
| `packages/native/test/packs/MesenPack.test.cpp` | The PCM case runs on Windows |
| `cmake/packs.cmake` | `retroplug_pack_module(... EXPORTS ...)` |
| `packages/native/cmake/tests.cmake` | `retroplug-mesen-heap-pack`, `retroplug-packs-heap-test` |
| `packages/native/test/engine-graph/golden/product-registrations.json` | A row for the heap test's product |
| `packages/node-sdk/native/src/graph/rp_pack.h` | `RP_API_LEVEL 2u` |
| `package.json`, `packages/retroplug/scripts/run-plugin-tests.mjs` | The heap test joins `test:plugin` |
| docs, spec/02, spec/06, spec/07, AGENTS.md | The rule, the binary, the open questions |

---

### Task 1: The heap test, failing on today's boundary

**Files:**
- Create: `packages/native/test/packs/heap/HeapTag.hpp`
- Create: `packages/native/test/packs/heap/HeapOperators.inc`
- Create: `packages/native/test/packs/heap/HostHeap.cpp`
- Create: `packages/native/test/packs/heap/PackHeap.cpp`
- Create: `packages/native/test/packs/heap/PackHeap.test.cpp`
- Create: `packages/native/test/packs/heap/mesen-heap.pack.json`
- Modify: `cmake/packs.cmake` (`retroplug_pack_module`)
- Modify: `packages/native/cmake/tests.cmake` (after `retroplug-packs-test`)
- Modify: `packages/native/test/engine-graph/golden/product-registrations.json`

- [ ] **Step 1: The tag and the allocator forms**

`packages/native/test/packs/heap/HeapTag.hpp`:

```cpp
#pragma once

// The heap test's tagging allocator (docs/downloadable-packs-m5a-plan.md). Both sides of the boundary replace the global
// operator new and delete in their own module: the test binary (the host, HostHeap.cpp) and the mesen-heap pack
// (PackHeap.cpp). Every block carries a header naming the side that allocated it, so a delete that finds the other
// side's tag is a block that crossed the boundary: it is counted and LEAKED, never freed, since freeing it in the
// other module's C runtime is the bug being caught (on Windows' /MT, each module's malloc is its own).
//
// No allocation happens in here: a delete runs inside every container's destructor, and an allocation there would recurse.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace rp::test::heap {

inline constexpr std::uint64_t kHostTag = 0x484F53542D484541ull;  // "HOST-HEA"
inline constexpr std::uint64_t kPackTag = 0x5041434B2D484541ull;  // "PACK-HEA"

// Just before every block: the pointer malloc returned (the block is aligned past it) and the allocating side's tag.
struct Header {
    void*         raw;
    std::uint64_t tag;
};
static_assert(sizeof(Header) == 16, "the header keeps a block 16-byte aligned");

// A block of `size` bytes aligned to `align` (at least 16), tagged `tag`; null when malloc has none.
inline void* allocateTagged(std::size_t size, std::size_t align, std::uint64_t tag) noexcept {
    if (align < sizeof(Header)) align = sizeof(Header);
    void* raw = std::malloc(size + align + sizeof(Header));
    if (raw == nullptr) return nullptr;
    std::uintptr_t at = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
    at = (at + align - 1) & ~static_cast<std::uintptr_t>(align - 1);
    Header* header = reinterpret_cast<Header*>(at) - 1;
    header->raw = raw;
    header->tag = tag;
    return reinterpret_cast<void*>(at);
}

// The tag of a block allocateTagged made (on either side).
inline std::uint64_t tagOf(const void* block) noexcept { return (static_cast<const Header*>(block) - 1)->tag; }

// Free a block this side allocated.
inline void freeTagged(void* block) noexcept { std::free((static_cast<Header*>(block) - 1)->raw); }

// The host's counts (HostHeap.cpp): its own allocations, and the pack's blocks it was asked to delete.
std::uint64_t hostBlocks();
std::uint32_t hostViolations();

}  // namespace rp::test::heap
```

`packages/native/test/packs/heap/HeapOperators.inc` (included by exactly one translation unit per module, after it
defines `heapAllocate` and `heapRelease` in an anonymous namespace):

```cpp
// Every replaceable form of the global operator new and delete, each through the including file's heapAllocate and
// heapRelease (HeapTag.hpp). Included once per module: HostHeap.cpp in the test binary, PackHeap.cpp in the pack.

void* operator new(std::size_t n) {
    if (void* p = heapAllocate(n, alignof(std::max_align_t))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (void* p = heapAllocate(n, alignof(std::max_align_t))) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t n, std::align_val_t a) {
    if (void* p = heapAllocate(n, static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
    if (void* p = heapAllocate(n, static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return heapAllocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return heapAllocate(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return heapAllocate(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return heapAllocate(n, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { heapRelease(p); }
void operator delete[](void* p) noexcept { heapRelease(p); }
void operator delete(void* p, std::size_t) noexcept { heapRelease(p); }
void operator delete[](void* p, std::size_t) noexcept { heapRelease(p); }
void operator delete(void* p, std::align_val_t) noexcept { heapRelease(p); }
void operator delete[](void* p, std::align_val_t) noexcept { heapRelease(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { heapRelease(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { heapRelease(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { heapRelease(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { heapRelease(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { heapRelease(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { heapRelease(p); }
```

- [ ] **Step 2: The host's side**

`packages/native/test/packs/heap/HostHeap.cpp`:

```cpp
// The test binary's half of the heap test's allocator (HeapTag.hpp): every block this binary allocates is tagged the
// host's, and a delete of the pack's block is counted, not freed.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "HeapTag.hpp"

namespace {

std::atomic<std::uint64_t> gBlocks{0};
std::atomic<std::uint32_t> gViolations{0};

// A breakpoint target: a debugger stopped here sees the stack that freed another side's block.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void crossed() noexcept {
    gViolations.fetch_add(1, std::memory_order_relaxed);
}

void* heapAllocate(std::size_t size, std::size_t align) noexcept {
    gBlocks.fetch_add(1, std::memory_order_relaxed);
    return rp::test::heap::allocateTagged(size, align, rp::test::heap::kHostTag);
}

void heapRelease(void* block) noexcept {
    if (block == nullptr) return;
    if (rp::test::heap::tagOf(block) != rp::test::heap::kHostTag) {
        crossed();  // the pack's block: counted and leaked
        return;
    }
    rp::test::heap::freeTagged(block);
}

}  // namespace

namespace rp::test::heap {
std::uint64_t hostBlocks() { return gBlocks.load(std::memory_order_relaxed); }
std::uint32_t hostViolations() { return gViolations.load(std::memory_order_relaxed); }
}  // namespace rp::test::heap

#include "HeapOperators.inc"
```

- [ ] **Step 3: The pack's side**

`packages/native/test/packs/heap/PackHeap.cpp` (compiled into the `mesen-heap` module only):

```cpp
// The mesen-heap pack's half of the heap test's allocator (HeapTag.hpp), and the hooks the test reads it through. The
// module is the Mesen package built as a pack again, with a heap of its own: its C++ runtime linked statically on Linux,
// Windows' static CRT. The hooks are exported beside rp_pack_entry (retroplug_pack_module's EXPORTS) and called by the
// test alone; no host looks for them.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "HeapTag.hpp"
#include "graph/rp_node.h"  // RP_EXPORT

namespace {

std::atomic<std::uint64_t> gBlocks{0};
std::atomic<std::uint32_t> gViolations{0};

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void crossed() noexcept {
    gViolations.fetch_add(1, std::memory_order_relaxed);
}

void* heapAllocate(std::size_t size, std::size_t align) noexcept {
    gBlocks.fetch_add(1, std::memory_order_relaxed);
    return rp::test::heap::allocateTagged(size, align, rp::test::heap::kPackTag);
}

void heapRelease(void* block) noexcept {
    if (block == nullptr) return;
    if (rp::test::heap::tagOf(block) != rp::test::heap::kPackTag) {
        crossed();  // the host's block: counted and leaked
        return;
    }
    rp::test::heap::freeTagged(block);
}

}  // namespace

#include "HeapOperators.inc"

// How many of the host's blocks this module was asked to delete.
extern "C" RP_EXPORT std::uint32_t rp_heap_violations(void) { return gViolations.load(std::memory_order_relaxed); }
// How many blocks this module's operator new has made: a count that moves proves the module allocates through it.
extern "C" RP_EXPORT std::uint64_t rp_heap_blocks(void) { return gBlocks.load(std::memory_order_relaxed); }
// A block of this module's, for the test's negative control (the host deleting it must count a violation).
extern "C" RP_EXPORT void* rp_heap_allocate(std::size_t size) { return ::operator new(size, std::nothrow); }
// Delete `block` in this module, for the test's other negative control (a host block must count here).
extern "C" RP_EXPORT void rp_heap_free(void* block) { ::operator delete(block); }
```

`packages/native/test/packs/heap/mesen-heap.pack.json` (the Mesen pack's manifest under its own id; the library stem
is `rp-<id>`):

```json
{
  "id": "mesen-heap",
  "name": "Mesen (heap test)",
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
    "native": "rp-mesen-heap"
  }
}
```

- [ ] **Step 4: `retroplug_pack_module` exports test hooks**

In `cmake/packs.cmake`, `retroplug_pack_module`:
- Add `EXPORTS` to the multi-value keywords: `"LIBS;SOURCES;DEFINES;ENTRIES;EXPORTS"`.
- Document it in the function's header comment: `[EXPORTS <C function>...]` "exported beside the entry: a test
  variant's hooks, which the SOURCES define `extern "C" RP_EXPORT` (on Windows a DLL exports what is dllexport'd, so
  the list matters only on Linux and macOS)".
- Build the export lists from the entry's name and the extras. Replace the macOS block's file content and the Linux
  version script:

```cmake
    set(_exported "${_name}" ${M_EXPORTS})
    if(APPLE)
        set(_exports "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.exports")
        list(TRANSFORM _exported PREPEND "_" OUTPUT_VARIABLE _apple)
        list(JOIN _apple "\n" _apple)
        file(CONFIGURE OUTPUT "${_exports}" CONTENT "${_apple}\n")
        target_link_options(${target} PRIVATE "LINKER:-exported_symbols_list,${_exports}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_exports}")
    elseif(NOT WIN32)
        set(_script "${CMAKE_CURRENT_BINARY_DIR}/packs/${target}.map")
        list(JOIN _exported "; " _globals)
        file(CONFIGURE OUTPUT "${_script}" CONTENT "{ global: ${_globals}; local: *; };\n")
        target_link_options(${target} PRIVATE "LINKER:--version-script=${_script}" "LINKER:--no-undefined")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_script}")
    endif()
```

Every existing module passes no `EXPORTS`, so its lists are byte for byte what they were.

- [ ] **Step 5: The module and the binary**

In `packages/native/cmake/tests.cmake`, after `retroplug-packs-test`'s `add_dependencies` lines, add:

```cmake
# The heap test (docs/downloadable-packs-m5a-plan.md): no heap block crosses the pack boundary. mesen-heap is the Mesen
# package built as a pack again, with a heap of its own (its C++ runtime linked statically on Linux with gcc, Windows'
# static CRT) and a tagging allocator (test/packs/heap/PackHeap.cpp); the test binary tags its own blocks the same way
# (HostHeap.cpp), so a block either side frees that the other allocated is counted. A binary of its own: it replaces the
# global operator new, which retroplug-packs-test must not, and links no static Mesen, so its engines reach rp.mesen.*
# only through the pack. macOS builds no module (libc++ is a system library whose out-of-line members allocate through
# the process's operator new, so the pack would not have a heap of its own) and the cases skip there.
set(_rp_heap_own_runtime 0)
if(RP_PACK_PLATFORM AND NOT APPLE)
    retroplug_pack_module(retroplug-mesen-heap-pack
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/test/packs/heap/mesen-heap.pack.json"
        LIBS     retroplug-mesen
        SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/test/packs/heap/PackHeap.cpp"
        EXPORTS  rp_heap_violations rp_heap_blocks rp_heap_allocate rp_heap_free
        ENTRIES  rp_mesen_package)
    if(WIN32)
        set(_rp_heap_own_runtime 1)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(retroplug-mesen-heap-pack PRIVATE -static-libstdc++ -static-libgcc)
        set(_rp_heap_own_runtime 1)
    endif()
endif()
retroplug_add_test_binary(
    NAME     retroplug-packs-heap-test
    SOURCES  test/packs/heap/HostHeap.cpp       # the host's tagging operator new and delete
             test/packs/heap/PackHeap.test.cpp  # the cases
    LIBS     retroplug-backend                  # Engine, the pack table, the debug service
    PACKAGES ${RP_PRODUCT_PACKAGES_packs-host}
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/test/engine-graph  # GraphDocs.hpp
    DEFS     RP_PACKS_BUILD_DIR="${RP_PACKS_DIR}"
             RP_HEAP_OWN_RUNTIME=${_rp_heap_own_runtime}
             RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/packages/mgb/rom/mGB.gb"
             RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
             RP_BLIPTOASTER_DBG_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.dbg"
             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
             RP_SMS_SAV_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sav")
if(TARGET retroplug-mesen-heap-pack)
    add_dependencies(retroplug-packs-heap-test retroplug-mesen-heap-pack)
endif()
```

`retroplug-mesen-heap-pack` links `retroplug-mesen`, which `packages/mesen/native/CMakeLists.txt` defines before
`tests.cmake` runs (`retroplug-packs-test` links it too). If the configure says otherwise, move the block to where
`retroplug-mesen-pack`'s dependency is added.

The binary is a product (it names `PACKAGES`), so `Packages.test.cpp` needs its row: in
`packages/native/test/engine-graph/golden/product-registrations.json`, add `"retroplug-packs-heap-test"` with exactly
`"retroplug-packs-test"`'s array (the same list), keeping the keys sorted.

- [ ] **Step 6: The cases**

`packages/native/test/packs/heap/PackHeap.test.cpp`:

```cpp
// No heap block crosses the pack boundary (docs/downloadable-packs-m5a-plan.md, the user's decision of 2026-11-25:
// Windows keeps its static CRT, so a pack's library has a heap of its own there, and nothing either side allocates may be
// freed by the other). The mesen-heap pack is the Mesen package built as a pack with a heap of its own, and both it and
// this binary tag every block they allocate (HeapTag.hpp): a delete of the other side's block is counted. The cases
// drive every path a core crosses the boundary on, through the host's own callers - a build with seeds and a long ROM
// name, the build's refusals on both sides, the snapshot registry's reads, the stream layout, the ROM title job, every
// debug service, a seeded rebuild, and the destruction - and hold both counts at zero. Two negative controls prove the
// counting is live in this binary and in the module.
//
// Linux (gcc) and Windows; macOS builds no module (packages/native/cmake/tests.cmake says why) and skips.
//
// Run via `pnpm test:plugin heap` or `build/bin/retroplug-packs-heap-test`.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "GraphDocs.hpp"
#include "HeapTag.hpp"
#include "graph/Packages.hpp"
#include "graph/rp_jobs.h"
#include "host/engine/Engine.hpp"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackTable.hpp"
#include "host/rpc/DebugRpcService.hpp"
#include "system/SystemBase.hpp"

using rp::host::packs::PackTable;
using rp::host::test::DocSystem;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 1024;

std::filesystem::path heapPack() { return std::filesystem::path(RP_PACKS_BUILD_DIR) / "mesen-heap" / "0.1.0"; }

// The pack's counts and its two negative-control hooks (PackHeap.cpp), found in its loaded library.
struct PackHooks {
    std::uint32_t (*violations)() = nullptr;
    std::uint64_t (*blocks)() = nullptr;
    void* (*allocate)(std::size_t) = nullptr;
    void (*release)(void*) = nullptr;
};

template <class Fn>
Fn hook(void* library, const char* name) {
    return reinterpret_cast<Fn>(rp::host::packs::librarySymbol(library, name));
}

PackHooks hooksOf(void* library) {
    PackHooks h;
    h.violations = hook<std::uint32_t (*)()>(library, "rp_heap_violations");
    h.blocks = hook<std::uint64_t (*)()>(library, "rp_heap_blocks");
    h.allocate = hook<void* (*)(std::size_t)>(library, "rp_heap_allocate");
    h.release = hook<void (*)(void*)>(library, "rp_heap_free");
    return h;
}

void render(Engine& e, int blocks) {
    std::vector<float> l(kFrames), r(kFrames);
    float*             outs[2] = {l.data(), r.data()};
    for (int i = 0; i < blocks; ++i) e.processBlock(kFrames, outs, 2);
}

// A settings member naming resource `id` (DocSystem::extra's form).
std::string res(const char* member, std::uint32_t id) {
    return std::string("\"") + member + "\":\"res:" + std::to_string(id) + "\"";
}

// Past any short-string buffer, so a ROM name the wrong side frees is a heap block.
const char* const kLongRomName = R"("romName":"a ROM name longer than any short-string buffer holds")";

}  // namespace

TEST_CASE("the heap test's counts are live: a block freed by the side that did not allocate it is counted",
          "[packs-heap]") {
#if !RP_HEAP_OWN_RUNTIME
    SKIP("no pack with a heap of its own on this platform (packages/native/cmake/tests.cmake)");
#else
    PackTable  table(rp::graph::productPackages());
    const auto load = table.load(heapPack());
    INFO(load.error);
    REQUIRE(load);
    const PackHooks pack = hooksOf(load.pack->library);
    REQUIRE(pack.violations != nullptr);
    REQUIRE(pack.blocks != nullptr);
    REQUIRE(pack.allocate != nullptr);
    REQUIRE(pack.release != nullptr);

    const auto hostBefore = rp::test::heap::hostViolations();
    void*      packBlock = pack.allocate(64);
    REQUIRE(packBlock != nullptr);
    ::operator delete(packBlock);  // the host deleting the pack's block
    CHECK(rp::test::heap::hostViolations() == hostBefore + 1);

    const auto packBefore = pack.violations();
    pack.release(::operator new(64));  // the pack deleting the host's block
    CHECK(pack.violations() == packBefore + 1);
#endif
}

TEST_CASE("no heap block crosses the boundary: a Mesen pack with a heap of its own is built, run, read, debugged "
          "and destroyed",
          "[packs-heap]") {
#if !RP_HEAP_OWN_RUNTIME
    SKIP("no pack with a heap of its own on this platform (packages/native/cmake/tests.cmake)");
#else
    PackTable  table(rp::graph::productPackages());
    const auto load = table.load(heapPack());
    INFO(load.error);
    REQUIRE(load);
    const PackHooks pack = hooksOf(load.pack->library);
    REQUIRE(pack.violations != nullptr);
    const auto hostBefore = rp::test::heap::hostViolations();
    const auto packBefore = pack.violations();
    const auto packBlocksBefore = pack.blocks();
    {
        Engine e(kRate, table);
        REQUIRE(e.packsRegistered() == 1);

        // Three consoles: a NES with a battery seed and a long ROM name, a Master System with smsggdj's battery and a
        // long ROM name (the one console whose build keeps the name), and a GBA.
        const std::uint32_t nesSram = e.putResource("sram", std::vector<std::uint8_t>(8192, 0x5a));
        const std::uint32_t smsSram = e.putResource("sram", rp::host::test::readFile(RP_SMS_SAV_PATH));
        REQUIRE(nesSram != 0);
        REQUIRE(smsSram != 0);
        rp::host::test::Roms   roms;
        std::vector<DocSystem> systems{
            {1, rp::host::test::kNesType, 0, res("sram", nesSram) + "," + kLongRomName, ""},
            {2, "rp.mesen.sms", 0, res("sram", smsSram) + "," + kLongRomName, ""},
            {3, "rp.mesen.gba", 0, "", ""}};
        rp::host::test::withRoms(e, systems, roms);
        const auto placed = e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2, 3}));
        REQUIRE(placed.ok);
        CHECK(pack.blocks() > packBlocksBefore);  // the pack's cores allocate through the pack's own operator new
        render(e, 40);

        // The snapshot registry's reads (its claim already took each core's savestate and battery), each core's stream
        // layout, and the ROM capability's title job.
        CHECK(e.readState(1));
        (void)e.readSram(1);  // a NES battery only if the cart has one; the claim asked the core either way
        CHECK(e.readSram(2));  // smsggdj keeps one
        CHECK(e.readRam(1));
        for (const SystemId id : {SystemId{1}, SystemId{2}, SystemId{3}}) {
            REQUIRE(e.findSystem(id) != nullptr);
            CHECK_FALSE(e.findSystem(id)->channelLayout().empty());
        }
        rp::host::JobRequest title;
        title.nodeId = "sys-3";
        title.capability = RP_SYSTEM_ROM_INTERFACE;
        title.op = "title";
        CHECK(e.submitJob(title).id != 0);
        (void)e.drainJobs();

        // Every debug service the NES presents, through the service the RPC calls.
        DebugRpcService debug(e);
        CHECK(debug.loadLabels(1, RP_BLIPTOASTER_DBG_PATH));
        (void)debug.symbolAddress(1, "main");
        CHECK_FALSE(debug.getCpuRegisters(1).empty());
        (void)debug.drainCoreBytes(1);
        (void)debug.getApuState(1);
        (void)debug.getExpansionAudioState(1);
        (void)debug.getPpuState(1);
        CHECK(debug.beginProfile(1));
        render(e, 4);
        (void)debug.readProfile(1);
        CHECK(debug.setTrace(1, true));
        render(e, 2);
        (void)debug.readTrace(1, 32);
        CHECK(debug.setTrace(1, false));
        const auto registers = debug.getCpuRegisters(1);
        std::uint32_t pc = 0;
        for (const auto& reg : registers)
            if (reg.name == "pc") pc = reg.value;
        CHECK_FALSE(debug.disassemble(1, pc, 16).empty());
        (void)debug.getCallStack(1);
        (void)debug.drainEvents(1);
        CHECK(debug.setBreakpoints(1, {rp::BreakpointSpec{"execute", pc, 0, ""}}));
        (void)debug.runUntilBreak(1, 100000);
        (void)debug.stepInto(1);
        (void)debug.stepOver(1);
        render(e, 2);
        (void)debug.drainBreakHits(1);
        CHECK(debug.setBreakpoints(1, {}));

        // A seeded rebuild: system 4 boots from system 1's savestate.
        const auto state = e.readState(1);
        REQUIRE(state);
        const std::uint32_t stateRes = e.putResource("state", *state);
        REQUIRE(stateRes != 0);
        systems.push_back({4, rp::host::test::kNesType, 0, res("state", stateRes), ""});
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2, 3, 4})).ok);
        render(e, 4);

        // The pack's build refusing: a NES node handed a Game Boy ROM ("the ROM is not a NES ROM").
        const std::uint32_t gbRom = e.putResource("rom", rp::host::test::readFile(RP_MGB_ROM_PATH));
        REQUIRE(gbRom != 0);
        auto wrongConsole = systems;
        wrongConsole.push_back({5, rp::host::test::kNesType, gbRom, kLongRomName, ""});
        CHECK_FALSE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(wrongConsole),
                                                            {1, 2, 3, 4, 5}))
                        .ok);
        // The host refusing before the build: system 1 rebuilt (a fresh put moves its ROM ref) while its core is live.
        const std::uint32_t freshRom = e.putResource("rom", rp::host::test::readFile(RP_BLIPTOASTER_ROM_PATH));
        REQUIRE(freshRom != 0);
        auto liveId = systems;
        liveId[0].rom = freshRom;
        CHECK_FALSE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(liveId), {1, 2, 3, 4})).ok);

        // Every core destroyed: an empty document, and the reclaim that frees the plans that held them.
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument({}), {})).ok);
        e.reclaim();
        CHECK(e.liveSystemCount() == 0);
        CHECK(e.faultCount() == 0);
    }
    const auto hostCrossed = rp::test::heap::hostViolations() - hostBefore;
    const auto packCrossed = pack.violations() - packBefore;
    INFO("the host freed " << hostCrossed << " of the pack's blocks; the pack freed " << packCrossed
                           << " of the host's (break on crossed() in HostHeap.cpp / PackHeap.cpp to see where)");
    // Today's boundary crosses (the claim's savestate and battery vectors, the stream layout, the debug service's
    // containers, the build request's by-value members); Task 5 makes this `hostCrossed == 0` and `packCrossed == 0`.
    CHECK(hostCrossed + packCrossed > 0);
#endif
}
```

`GraphDocs.hpp` provides `readFile`; if it lives in another namespace or header in the tree, use it from there (the
packs tests' `MesenPack.test.cpp` reaches it the same way). If `DocSystem` has a different member order than `{id,
type, rom, extra, params}`, build the entries by member name. A `Roms` entry is put by `withRoms` once per console, so
system 4 shares system 1's ROM resource, which is what a rebuild needs to be a fresh node and not a moved ref.

- [ ] **Step 7: Build and run: the negative controls pass, the scenario fails**

Run:
```sh
cmake --build build --target retroplug-packs-heap-test retroplug-engine-graph-test -j$(nproc)
build/bin/retroplug-packs-heap-test -s 2>&1 | tail -40
build/bin/retroplug-engine-graph-test 2>&1 | tail -3
```
Expected:
- The module links (on Linux `ldd build/packs/mesen-heap/0.1.0/native/linux-x64/rp-mesen-heap.so` lists no
  `libstdc++`).
- The first case passes.
- The second passes **because it fails**: Catch2 reports it as a failure that was expected. Its INFO line reports
  non-zero counts. Record the two numbers in the task report. Expected sources, all fixed by Tasks 3 to 5: the claim's
  `saveStateBytes`/`saveSramBytes` vectors, `channelLayout`'s vector, the debug service's vectors and strings
  (`readProfile`, `disassemble`, `readTrace`, the PPU state's palette bytes, the CPU registers' vector), the refusal
  strings written into the host's `std::string`, and the long `romName` the NES and GBA builds leave in the host's
  request.
- If the run **aborts** instead (a crash inside a `delete`), stop and report: that is a crossing the leak path does not
  cover.
- The products golden's case passes with the new row.

To see where one violation comes from: `gdb -batch -ex 'break crossed' -ex run -ex bt build/bin/retroplug-packs-heap-test`.

- [ ] **Step 8: Commit**

```sh
git add packages/native/test/packs/heap cmake/packs.cmake packages/native/cmake/tests.cmake \
        packages/native/test/engine-graph/golden/product-registrations.json
git commit -m "packs: the heap test, a Mesen pack with a heap of its own whose blocks no host may free (expected to fail until the boundary is converted)"
```

---

### Task 2: The sink, and the scan that holds the contract headers to it

**Files:**
- Create: `packages/system/native/src/system/Sink.hpp`
- Modify: `packages/native/test/packs/BoundaryRules.test.cpp`

- [ ] **Step 1: The sink header**

`packages/system/native/src/system/Sink.hpp`:

```cpp
#pragma once

// What crosses a pack's boundary in place of a returned container (docs/downloadable-packs-m5a-plan.md). A pack's
// library has a heap of its own wherever its C runtime is its own (Windows' static CRT, a runtime linked statically),
// so no heap ownership crosses the boundary: a block one side allocated is never freed by the other. A virtual a pack
// implements therefore returns no owning type. It is handed a sink the caller made over its own container and gives the
// sink what it has, by const reference or by pointer and size; the sink copies, and the callee keeps and frees what it
// gave. The helpers below are inline, so the sink's function is compiled in the module that makes it, and its copy
// allocates in that module's heap. A sink is valid for the call it is passed to: a callee never keeps one.
//
// Both are C-shaped (a context pointer and a plain function pointer), as a C system ABI would need them.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rp {

// Bytes, appended in order: a callee may write more than once.
struct ByteSink {
    void* ctx = nullptr;
    void (*write)(void* ctx, const std::uint8_t* data, std::size_t size) = nullptr;
    void operator()(const std::uint8_t* data, std::size_t size) const {
        if (write != nullptr && size != 0) write(ctx, data, size);
    }
};

// Items, one at a time, each lent for the call: the sink copies it before it returns.
template <class T>
struct Sink {
    void* ctx = nullptr;
    void (*put)(void* ctx, const T& item) = nullptr;
    void operator()(const T& item) const {
        if (put != nullptr) put(ctx, item);
    }
};

// A byte sink appending to `out`.
inline ByteSink appendBytesTo(std::vector<std::uint8_t>& out) {
    return ByteSink{&out, [](void* ctx, const std::uint8_t* data, std::size_t size) {
                        auto& bytes = *static_cast<std::vector<std::uint8_t>*>(ctx);
                        bytes.insert(bytes.end(), data, data + size);
                    }};
}

// A sink appending each item to `out`.
template <class T>
Sink<T> appendTo(std::vector<T>& out) {
    return Sink<T>{&out, [](void* ctx, const T& item) { static_cast<std::vector<T>*>(ctx)->push_back(item); }};
}

// A sink assigning the item it is given to `out`: a single result that holds memory (a later item replaces it).
template <class T>
Sink<T> assignTo(T& out) {
    return Sink<T>{&out, [](void* ctx, const T& item) { *static_cast<T*>(ctx) = item; }};
}

}  // namespace rp
```

- [ ] **Step 2: The sink's own cases, and the contract scan (failing)**

Append to `packages/native/test/packs/BoundaryRules.test.cpp`. Add the includes `<algorithm>`, `<set>`,
`<sstream>`, `<type_traits>`, `"system/DebugTarget.hpp"`, `"system/Sink.hpp"`, `"system/SystemBase.hpp"`, and extend
the file's header comment with: "And no heap ownership crosses (docs/downloadable-packs-m5a-plan.md): the contract
headers' public virtuals return no owning type and take none but by const reference, and the build path's types hold
none; `retroplug-packs-heap-test` proves it at run time."

```cpp
namespace {

// The contract headers whose classes a pack's systems implement and the host calls.
const char* const kContractHeaders[] = {"SystemBase.hpp", "SystemCapabilities.hpp", "DebugTarget.hpp",
                                        "ChannelTap.hpp"};
// The build path's types, which a pack's node fills and the host passes back to the pack's build function.
const char* const kBuildHeaders[] = {"SystemBuild.hpp", "HostSystems.hpp"};

// A type that owns heap memory: the standard containers, strings (not string_view), the owning pointers and
// functions, and reflect-cpp's types (rfl::Bytestring is a vector).
const std::regex kOwning(
    R"(std::(vector|string\b|basic_string|map|unordered_map|set|unordered_set|list|deque|function|unique_ptr|shared_ptr)|rfl::)");

// The plain types a public virtual may return besides a pointer (a borrowed one, the callee's for its own life). A type
// joins this list only if it holds no heap memory; the structs among them are held trivially copyable below.
const std::set<std::string> kPlainReturns = {
    "void", "bool", "float", "double", "std::size_t", "std::uint8_t", "std::uint16_t", "std::uint32_t",
    "std::uint64_t", "std::int32_t", "std::optional<std::uint8_t>", "std::optional<std::uint32_t>",
    "std::string_view", "std::span<const std::uint8_t>", "std::span<const Region>", "CoreTransportStats",
    "rp::MemoryAccessor", "ApuState", "BreakInfo", "WriteResult"};
static_assert(std::is_trivially_copyable_v<CoreTransportStats>);
static_assert(std::is_trivially_copyable_v<rp::MemoryAccessor>);
static_assert(std::is_trivially_copyable_v<rp::ApuState>);
static_assert(std::is_trivially_copyable_v<rp::BreakInfo>);

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The code of a header: its // and /* */ comments blanked.
std::string withoutComments(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (text.compare(i, 2, "/*") == 0) {
            const auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            out.push_back(' ');
        } else {
            out.push_back(text[i++]);
        }
    }
    return out;
}

std::string trimmed(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// The access in effect at `at`: the last access label, or, where a class or struct opened after it, that one's default.
std::string accessAt(const std::string& code, std::size_t at) {
    static const std::regex kLabel(R"(\b(public|protected|private)\s*:)");
    static const std::regex kOpen(R"(\b(class|struct)\s+\w+[^;{()]*\{)");
    std::string access = "public";
    std::size_t last = 0;
    bool        any = false;
    const auto  end = code.cbegin() + static_cast<std::ptrdiff_t>(at);
    for (auto m = std::sregex_iterator(code.cbegin(), end, kLabel); m != std::sregex_iterator(); ++m) {
        last = static_cast<std::size_t>(m->position(0));
        any = true;
        access = (*m)[1].str();
    }
    for (auto m = std::sregex_iterator(code.cbegin(), end, kOpen); m != std::sregex_iterator(); ++m) {
        const auto pos = static_cast<std::size_t>(m->position(0));
        if (!any || pos >= last) {
            last = pos;
            any = true;
            access = (*m)[1].str() == "struct" ? "public" : "private";
        }
    }
    return access;
}

// A parameter list's parameters, split at the commas outside angle brackets, each without its default argument.
std::vector<std::string> parametersOf(const std::string& list) {
    std::vector<std::string> out;
    std::string              current;
    int                      depth = 0;
    for (const char c : list) {
        if (c == '<') ++depth;
        if (c == '>') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    for (auto& p : out) p = trimmed(p.substr(0, p.find('=')));
    std::erase_if(out, [](const std::string& p) { return p.empty() || p == "void"; });
    return out;
}

// Why a parameter crosses with ownership, or empty: an rvalue reference, or an owning type that is not a const
// reference. A sink's item type is lent by const reference by construction, so a Sink<...> is judged as the sink.
std::string parameterFault(std::string p) {
    if (p.find("&&") != std::string::npos) return "takes an rvalue reference";
    p = std::regex_replace(p, std::regex(R"((rp::)?Sink<[^>]*>)"), "Sink");
    if (!std::regex_search(p, kOwning)) return {};
    if (p.rfind("const ", 0) == 0 && p.find('&') != std::string::npos) return {};
    return "takes an owning type other than by const reference";
}

}  // namespace

TEST_CASE("a sink copies into the container of the side that made it", "[packs-boundary]") {
    std::vector<std::uint8_t> bytes;
    const rp::ByteSink        byteSink = rp::appendBytesTo(bytes);
    const std::uint8_t        a[] = {1, 2, 3};
    byteSink(a, 3);
    byteSink(a, 0);  // an empty write is nothing
    byteSink(a + 1, 2);
    CHECK(bytes == std::vector<std::uint8_t>{1, 2, 3, 2, 3});

    std::vector<std::string> items;
    const auto               itemSink = rp::appendTo(items);
    {
        const std::string lent = "a string lent for the call, past any short-string buffer";
        itemSink(lent);
    }  // the lender's copy is gone; the sink's is the caller's own
    REQUIRE(items.size() == 1);
    CHECK(items[0] == "a string lent for the call, past any short-string buffer");

    std::string one = "before";
    const auto  assign = rp::assignTo(one);
    assign(std::string("first"));
    assign(std::string("second"));
    CHECK(one == "second");

    const rp::ByteSink none{};
    none(a, 3);  // a sink with no function takes nothing and does not crash
}

TEST_CASE("the contract headers hand no heap ownership across a pack's boundary", "[packs-boundary]") {
    const fs::path dir = fs::path(RP_REPO_ROOT) / "packages" / "system" / "native" / "src" / "system";
    static const std::regex kVirtual(R"(virtual\s+([^;{}()]*?)\s*\b(\w+)\s*\(([^()]*)\))");
    std::vector<std::string> found;
    std::size_t              publicVirtuals = 0;
    for (const char* header : kContractHeaders) {
        const std::string code = withoutComments(readText(dir / header));
        REQUIRE_FALSE(code.empty());
        for (auto m = std::sregex_iterator(code.cbegin(), code.cend(), kVirtual); m != std::sregex_iterator(); ++m) {
            const std::string ret = trimmed((*m)[1].str());
            const std::string name = (*m)[2].str();
            if (ret.empty() || ret.back() == '~') continue;  // a destructor
            if (accessAt(code, static_cast<std::size_t>(m->position(0))) != "public") continue;
            ++publicVirtuals;
            const std::string where = std::string(header) + ": " + ret + " " + name + "(" + (*m)[3].str() + ")";
            if (ret.back() != '*' && !kPlainReturns.contains(ret)) found.push_back(where + ": returns " + ret);
            for (const auto& p : parametersOf((*m)[3].str()))
                if (const auto fault = parameterFault(p); !fault.empty()) found.push_back(where + ": " + p + " " + fault);
        }
    }
    for (const char* header : kBuildHeaders) {
        // The one object that crosses is a core, carried out of a build function in a unique_ptr and deleted through
        // its virtual destructor (the pack's deleter); nothing else in the build path may own memory.
        std::string code = withoutComments(readText(dir / header));
        REQUIRE_FALSE(code.empty());
        code = std::regex_replace(code, std::regex(R"(std::unique_ptr<SystemBase>)"), "CoreHandle");
        if (std::regex_search(code, kOwning)) found.push_back(std::string(header) + ": holds or passes an owning type");
        if (code.find("&&") != std::string::npos) found.push_back(std::string(header) + ": takes an rvalue reference");
    }
    CHECK(publicVirtuals >= 50);  // the scan read the interfaces, not an empty match
    for (const auto& f : found) UNSCOPED_INFO(f);
    // Today's contract hands ownership across; docs/downloadable-packs-m5a-plan.md Task 5 makes this CHECK(found.empty()).
    CHECK_FALSE(found.empty());
}
```

`SystemCapabilities.hpp`'s `WriteResult` and `Region` are nested names, which is why the list spells them bare. If
`static_assert(std::is_trivially_copyable_v<rp::MemoryAccessor>)` fails now (the patch vector pointer does not stop it,
but check), move that one assert to Task 3, which removes the vector.

- [ ] **Step 3: Build and run**

Run:
```sh
cmake --build build --target retroplug-packs-test -j$(nproc)
build/bin/retroplug-packs-test "[packs-boundary]" -s 2>&1 | grep -E "returns|takes|holds|passed|failed" | head -60
```
Expected: all three cases pass (the contract case because it finds faults). To read its list, temporarily change
the `CHECK_FALSE` to `CHECK` locally (do not commit that) or print `found`. The list
should name: `SystemBase.hpp`'s five owning returns (`channelLayout`, `drainCoreBytes`, `saveSramBytes`,
`saveStateBytes`, `getCpuRegisters`), `SystemCapabilities.hpp`'s `title` returning `std::string`, `DebugTarget.hpp`'s
eight owning returns, and `SystemBuild.hpp` and `HostSystems.hpp` holding an owning type and an rvalue reference.
(`IDebugTarget`'s `const std::string&` and `const std::vector&` inputs are const references, which the rule allows;
Task 4 makes them views anyway.) Record the list in the task report, and the count of public virtuals scanned (at
least 50).

- [ ] **Step 4: Commit**

```sh
git add packages/system/native/src/system/Sink.hpp packages/native/test/packs/BoundaryRules.test.cpp
git commit -m "system: a sink carries a result across a pack's boundary into the caller's heap, and a scan holds the contract headers to it (expected to fail until the boundary is converted)"
```

---

### Task 3: `SystemBase`'s results through sinks

**Files:**
- Modify: `packages/system/native/src/system/SystemBase.hpp`
- Modify: `packages/system/native/src/system/MemoryAccessor.hpp`, `MemoryType.hpp`
- Modify: `packages/sameboy/native/src/system/sameboy/SameBoySystem.{hpp,cpp}`
- Modify: `packages/mesen/native/src/system/mesen/MesenNesSystem.{hpp,cpp}`, `MesenGbaSystem.{hpp,cpp}`,
  `MesenSmsSystem.{hpp,cpp}`
- Modify: `packages/native/test/engine-graph/EngineGraph.test.cpp`, `SystemNode.test.cpp` (test doubles, one count)

- [ ] **Step 1: The virtuals and their helpers**

In `SystemBase.hpp`, add `#include "system/Sink.hpp"` and replace the five virtuals. Keep each one's comment above the
new virtual (reworded from "returns" to "hands `out`"), and put the helper right after it.

Replace line 111 (`virtual std::vector<ChannelStream> channelLayout() const ...`) with:

```cpp
    virtual void channelLayoutInto(rp::Sink<ChannelStream> out) const { out(ChannelStream{"Mix", true}); }
    // channelLayoutInto's streams, in a vector of the caller's (system/Sink.hpp: nothing a pack allocates is freed by
    // the host). Inline and not virtual, so the vector is the calling module's.
    std::vector<ChannelStream> channelLayout() const {
        std::vector<ChannelStream> streams;
        channelLayoutInto(rp::appendTo(streams));
        return streams;
    }
```

Replace line 142 (`drainCoreBytes`):

```cpp
    virtual void drainCoreBytesInto(rp::ByteSink /*out*/) {}
    // drainCoreBytesInto's bytes, in the caller's vector.
    std::vector<std::uint8_t> drainCoreBytes() {
        std::vector<std::uint8_t> bytes;
        drainCoreBytesInto(rp::appendBytesTo(bytes));
        return bytes;
    }
```

Replace lines 185 and 188 (`saveSramBytes`, `saveStateBytes`):

```cpp
    virtual void saveSramInto(rp::ByteSink /*out*/) const {}
    std::vector<std::uint8_t> saveSramBytes() const {
        std::vector<std::uint8_t> bytes;
        saveSramInto(rp::appendBytesTo(bytes));
        return bytes;
    }

    // Savestate, byte for byte. Nothing when the backend doesn't support savestates.
    virtual void saveStateInto(rp::ByteSink /*out*/) const {}
    std::vector<std::uint8_t> saveStateBytes() const {
        std::vector<std::uint8_t> bytes;
        saveStateInto(rp::appendBytesTo(bytes));
        return bytes;
    }
```

Replace line 218 (`getCpuRegisters`):

```cpp
    virtual void cpuRegistersInto(rp::Sink<rp::CpuRegister> /*out*/) const {}
    std::vector<rp::CpuRegister> getCpuRegisters() const {
        std::vector<rp::CpuRegister> registers;
        cpuRegistersInto(rp::appendTo(registers));
        return registers;
    }
```

Add, at the top of the class comment, one paragraph: "The boundary rule (docs/downloadable-packs-m5a-plan.md): a core
may be a pack's, with a heap of its own, so no public virtual here returns an owning type or takes one but by const
reference. A result goes to a sink the caller made (system/Sink.hpp), and the non-virtual helper beside it gives the
caller a vector of its own. `[packs-boundary]` scans this header for it."

- [ ] **Step 2: `MemoryAccessor`'s patch vector goes**

In `MemoryAccessor.hpp`: delete the constructor's fifth parameter (`std::vector<MemoryPatch>* patches = nullptr`), the
`patches_` member and its initializer, and the three `if (patches_) ...` blocks in `write`, `write` and `clear`. Delete
the comment sentence about patch records. Delete `struct MemoryPatch` from `MemoryType.hpp` (nothing else names it;
`grep -rn MemoryPatch packages` must find nothing after). If a core passes a fifth argument to the constructor,
`grep -rn "MemoryAccessor(" packages` finds it: drop the argument.

- [ ] **Step 3: The cores' overrides**

The pattern, for every core: the old override's body stays, under a private non-virtual name, and the new override
wraps it. Internal callers of the old name inside the core call the private one.

| Core | Old override | Private name | New override |
|---|---|---|---|
| `SameBoySystem` | `channelLayout()` | (none: writes its four streams) | `channelLayoutInto` |
| `SameBoySystem` | `saveSramBytes()` | `sramBytes()` | `saveSramInto` |
| `SameBoySystem` | `saveStateBytes()` | `stateBytes()` | `saveStateInto` |
| `SameBoySystem` | `getCpuRegisters()` | (none: hands each register) | `cpuRegistersInto` |
| `MesenNesSystem` | `channelLayout()` | (none) | `channelLayoutInto` |
| `MesenNesSystem` | `drainCoreBytes()` | `takeCoreBytes()` | `drainCoreBytesInto` |
| `MesenNesSystem` | `saveSramBytes()`, `saveStateBytes()` | `sramBytes()`, `stateBytes()` | `saveSramInto`, `saveStateInto` |
| `MesenNesSystem` | `getCpuRegisters()` | `cpuRegisters()` | `cpuRegistersInto` |
| `MesenGbaSystem` | `saveSramBytes()`, `saveStateBytes()`, `getCpuRegisters()` | `sramBytes()`, `stateBytes()`, `cpuRegisters()` | the three `...Into` |
| `MesenSmsSystem` | `saveSramBytes()`, `saveStateBytes()` | `sramBytes()`, `stateBytes()` | `saveSramInto`, `saveStateInto` |

Worked example, `SameBoySystem`. In the header, replace the four declarations:

```cpp
    void cpuRegistersInto(rp::Sink<rp::CpuRegister> out) const override;
    void channelLayoutInto(rp::Sink<ChannelStream> out) const override;
    void saveSramInto(rp::ByteSink out) const override;
    void saveStateInto(rp::ByteSink out) const override;
```

and in its `private:` section add:

```cpp
    // The battery and the savestate as this core reads them; saveSramInto / saveStateInto hand them over.
    std::vector<std::uint8_t> sramBytes() const;
    std::vector<std::uint8_t> stateBytes() const;
```

In the .cpp, rename `SameBoySystem::saveSramBytes` to `SameBoySystem::sramBytes` and `saveStateBytes` to `stateBytes`
(bodies unchanged), and add:

```cpp
void SameBoySystem::saveSramInto(rp::ByteSink out) const {
    const std::vector<std::uint8_t> bytes = sramBytes();
    out(bytes.data(), bytes.size());
}

void SameBoySystem::saveStateInto(rp::ByteSink out) const {
    const std::vector<std::uint8_t> bytes = stateBytes();
    out(bytes.data(), bytes.size());
}

void SameBoySystem::channelLayoutInto(rp::Sink<ChannelStream> out) const {
    // GB_channel_t order: GB_SQUARE_1, GB_SQUARE_2, GB_WAVE, GB_NOISE.
    for (const ChannelStream& s : {ChannelStream{"Pulse 1", true}, ChannelStream{"Pulse 2", true},
                                   ChannelStream{"Wave", true}, ChannelStream{"Noise", true}})
        out(s);
}

void SameBoySystem::cpuRegistersInto(rp::Sink<rp::CpuRegister> out) const {
    if (!gb_) return;
    const GB_registers_t* r = GB_get_registers(gb_);
    for (const rp::CpuRegister& reg : {rp::CpuRegister{"af", r->af, 16}, rp::CpuRegister{"bc", r->bc, 16},
                                       rp::CpuRegister{"de", r->de, 16}, rp::CpuRegister{"hl", r->hl, 16},
                                       rp::CpuRegister{"sp", r->sp, 16}, rp::CpuRegister{"pc", r->pc, 16}})
        out(reg);
}
```

(delete the old `channelLayout` and `getCpuRegisters` definitions). `MesenNesSystem::channelLayoutInto` is the old
body with each `return {...};` turned into handing each stream to `out` and returning:

```cpp
void MesenNesSystem::channelLayoutInto(rp::Sink<ChannelStream> out) const {
    const std::size_t streams = captureStreamCount();
    // Mode 3 (§5b): the 5 individual core channels as raw mono stems ("does not sum").
    if (streams == 5) {
        for (const char* name : {"Square1", "Square2", "Triangle", "Noise", "DMC"}) out(ChannelStream{name, false});
        return;
    }
    // The two 2A03 stereo-mod output pins + the lumped expansion term, as mono streams (spec/10 §5). Mode 2
    // (native/test-only) appends the mix-reference stream for the fidelity test.
    if (streams == 4 || streams == 3) {
        for (const char* name : {"Pulse", "TND", "Expansion"}) out(ChannelStream{name, false});
        if (streams == 4) out(ChannelStream{"MixRef", false});
        return;
    }
    out(ChannelStream{"Mix", true});
}
```

The byte overrides of the Mesen cores and `MesenNesSystem::drainCoreBytesInto` follow `saveSramInto` above (call the
private function, hand its bytes to `out`). `cpuRegistersInto` on the Mesen cores is
`for (const rp::CpuRegister& r : cpuRegisters()) out(r);`. In each Mesen core's `captureStateSnapshot`, change
`dst = saveStateBytes();` to `dst = stateBytes();` (so the capture pays no copy it did not pay before). Then
`grep -n "saveStateBytes\|saveSramBytes\|getCpuRegisters\|drainCoreBytes\|channelLayout()" packages/{sameboy,mesen}/native/src`
must list only calls of the base helpers from code that wants a vector (if a core calls one on itself, it may keep it:
it allocates and frees in its own module).

- [ ] **Step 4: The test doubles and one count**

`grep -n "override" packages/native/test/engine-graph/*.cpp packages/native/test/engine-graph/*.hpp | grep -E "channelLayout|saveStateBytes|saveSramBytes|getCpuRegisters|drainCoreBytes"`
lists the doubles that override the old virtuals (`EngineGraph.test.cpp`'s `StubCore`/`StateCore` among them). Convert
each to the `...Into` form as in Step 3 (a double returning a fixed vector hands its bytes or items to `out`).

`SystemNode.test.cpp`'s case "a freshly built core's first block costs the boot savestate and nothing else" counts the
allocations of `twin.saveStateBytes()`, which now also allocates the caller's vector. Measure the core's capture alone,
into a vector sized beforehand, so the count is what it was. Replace:

```cpp
        rp::test::beginCountingAllocations(true);
        const auto bytes = twin.saveStateBytes();
        const auto counted = rp::test::endCountingAllocations();
        REQUIRE(!bytes.empty());
```

with:

```cpp
        // Into a vector sized beforehand, so what is counted is the core's capture and not the caller's copy of it.
        std::vector<std::uint8_t> bytes;
        bytes.reserve(twin.stateSnapshotCapacity());
        rp::test::beginCountingAllocations(true);
        twin.saveStateInto(rp::appendBytesTo(bytes));
        const auto counted = rp::test::endCountingAllocations();
        REQUIRE(!bytes.empty());
```

If `stateSnapshotCapacity()` is smaller than the capture (a core that grows), use a fixed `bytes.reserve(16 << 20)`.

- [ ] **Step 5: Build and run**

```sh
cmake --build build --target retroplug-engine-graph-test retroplug-sameboy-test retroplug-mesen-test retroplug-audio-test retroplug-packs-test retroplug-packs-heap-test -j$(nproc)
build/bin/retroplug-engine-graph-test 2>&1 | tail -3
build/bin/retroplug-sameboy-test 2>&1 | tail -3
build/bin/retroplug-mesen-test 2>&1 | tail -3
build/bin/retroplug-audio-test 2>&1 | tail -3
build/bin/retroplug-packs-test "[packs-mesen],[packs-boundary]" 2>&1 | tail -3
build/bin/retroplug-packs-heap-test -s 2>&1 | grep -E "freed|passed|failed" | tail -5
```
Expected: every suite passes (the heap scenario's and the scan's interim checks still find crossings). The heap test's counts drop (the
claim's and the layout's crossings are gone); record them.

- [ ] **Step 6: Commit**

```sh
git add packages/system packages/sameboy packages/mesen packages/native/test/engine-graph
git commit -m "system: a core hands its stream layout, core bytes, battery, savestate and registers to a sink of the caller's"
```

---

### Task 4: The ROM title and the debug target

**Files:**
- Modify: `packages/system/native/src/system/SystemCapabilities.{hpp,cpp}`
- Modify: `packages/system/native/src/system/DebugTarget.hpp`
- Modify: `packages/mesen/native/src/system/mesen/MesenNesDebugSession.{hpp,cpp}`
- Modify: `packages/native/src/host/rpc/DebugRpcService.cpp`
- Modify: `packages/native/src/host/engine/Engine.cpp` (the title job)

- [ ] **Step 1: The title is a view**

`SystemCapabilities.hpp`: `RomCapability::title()` becomes

```cpp
    // [any] The cartridge's title field as the ROM stores it, up to its first byte that is not printable ASCII, or
    // empty for a console whose header has none (a NES's iNES header, a Master System's): a view into the ROM image,
    // valid for the core's life.
    virtual std::string_view title() const = 0;
```

and `CoreRom::title() const override` returns `std::string_view`. Replace `#include <string>` with
`#include <string_view>` if nothing else needs it. `SystemCapabilities.cpp`:

```cpp
std::string_view CoreRom::title() const {
    std::size_t length = 0;
    for (std::size_t i = titleOffset_; i < titleOffset_ + titleLength_ && i < rom_.size(); ++i, ++length) {
        const std::uint8_t c = rom_[i];
        // Printable ASCII to the first byte that is not: a Game Boy title that fills its 15 bytes leaves the CGB flag
        // (0x80 or 0xC0) at $0143, which is not part of the title.
        if (c < 0x20 || c > 0x7e) break;
    }
    if (length == 0) return {};
    return {reinterpret_cast<const char*>(rom_.data() + titleOffset_), length};
}
```

`Engine.cpp`'s `"title"` op: `const std::string_view title = rom->title();` (the vector it builds from `title.begin()`
and `title.end()` is unchanged).

- [ ] **Step 2: The debug target's results and inputs**

In `DebugTarget.hpp`, add `#include <span>`, `#include <string_view>`, `#include "system/Sink.hpp"`, and replace the
`IDebugTarget` member declarations (keep every comment, changing "returns" to "hands `out`" where it says so):

```cpp
class IDebugTarget {
public:
    virtual ~IDebugTarget() = default;

    virtual void beginProfile() = 0;
    virtual void readProfileInto(Sink<ProfiledFunction> out) = 0;

    virtual bool loadLabels(std::string_view path) = 0;

    virtual std::optional<std::uint32_t> symbolAddress(std::string_view /*name*/) { return std::nullopt; }

    virtual void disassembleInto(std::uint32_t addr, std::uint32_t count, Sink<DisasmLine> out) = 0;

    virtual void setTraceEnabled(bool on) = 0;
    virtual void readTraceInto(std::uint32_t count, Sink<TraceLine> out) = 0;

    virtual void callStackInto(Sink<CallFrame> out) = 0;

    virtual ApuState getApuState() = 0;

    // Default: nothing (the caller's default state, chip "" ) for backends / carts with no expansion audio.
    virtual void expansionAudioStateInto(Sink<ExpansionAudioState> /*out*/) {}

    // Default: nothing for backends without a NES PPU.
    virtual void ppuStateInto(Sink<PpuState> /*out*/) {}

    virtual void setBreakpoints(std::span<const BreakpointSpec> bps) = 0;
    virtual BreakInfo runUntilBreak(std::uint64_t maxCycles) = 0;

    // The firings to `out`, oldest first; returns how many were seen beyond what was handed over.
    virtual std::uint32_t drainBreakHitsInto(Sink<BreakHit> /*out*/) { return 0; }

    virtual BreakInfo step() = 0;
    virtual BreakInfo stepOver() = 0;
    virtual BreakInfo stepOut() = 0;

    virtual void drainEventsInto(Sink<DebugEvent> /*out*/) {}
};
```

Add to the class comment: "No result is returned in a container: each is handed to a sink of the caller's
(system/Sink.hpp), and each input is a view, since the session may be a pack's, with a heap of its own
(docs/downloadable-packs-m5a-plan.md)."

- [ ] **Step 3: Mesen's session**

In `MesenNesDebugSession.hpp`: the old declarations (`readProfile`, `disassemble`, `readTrace`, `getCallStack`,
`getExpansionAudioState`, `getPpuState`, `drainEvents`, `drainBreakHits`) lose `override` and move to the `private:`
section, bodies unchanged. Add the public overrides:

```cpp
    void readProfileInto(rp::Sink<rp::ProfiledFunction> out) override;
    bool loadLabels(std::string_view path) override;
    std::optional<std::uint32_t> symbolAddress(std::string_view name) override;
    void disassembleInto(std::uint32_t addr, std::uint32_t count, rp::Sink<rp::DisasmLine> out) override;
    void readTraceInto(std::uint32_t count, rp::Sink<rp::TraceLine> out) override;
    void callStackInto(rp::Sink<rp::CallFrame> out) override;
    void expansionAudioStateInto(rp::Sink<rp::ExpansionAudioState> out) override;
    void ppuStateInto(rp::Sink<rp::PpuState> out) override;
    void setBreakpoints(std::span<const rp::BreakpointSpec> bps) override;
    std::uint32_t drainBreakHitsInto(rp::Sink<rp::BreakHit> out) override;
    void drainEventsInto(rp::Sink<rp::DebugEvent> out) override;
```

In the .cpp, each wraps the private one:

```cpp
void MesenNesDebugSession::readProfileInto(rp::Sink<rp::ProfiledFunction> out) {
    for (const rp::ProfiledFunction& f : readProfile()) out(f);
}
void MesenNesDebugSession::disassembleInto(std::uint32_t addr, std::uint32_t count, rp::Sink<rp::DisasmLine> out) {
    for (const rp::DisasmLine& line : disassemble(addr, count)) out(line);
}
void MesenNesDebugSession::readTraceInto(std::uint32_t count, rp::Sink<rp::TraceLine> out) {
    for (const rp::TraceLine& line : readTrace(count)) out(line);
}
void MesenNesDebugSession::callStackInto(rp::Sink<rp::CallFrame> out) {
    for (const rp::CallFrame& frame : getCallStack()) out(frame);
}
void MesenNesDebugSession::expansionAudioStateInto(rp::Sink<rp::ExpansionAudioState> out) {
    out(getExpansionAudioState());
}
void MesenNesDebugSession::ppuStateInto(rp::Sink<rp::PpuState> out) { out(getPpuState()); }
std::uint32_t MesenNesDebugSession::drainBreakHitsInto(rp::Sink<rp::BreakHit> out) {
    std::uint32_t overflow = 0;
    for (const rp::BreakHit& hit : drainBreakHits(overflow)) out(hit);
    return overflow;
}
void MesenNesDebugSession::drainEventsInto(rp::Sink<rp::DebugEvent> out) {
    for (const rp::DebugEvent& event : drainEvents()) out(event);
}
```

`loadLabels`, `symbolAddress` and `setBreakpoints` change their parameter type in place: where the body needs a
`std::string` (opening the file, a `symbols_` lookup), build one (`std::string(path)`, `symbols_.find(std::string(name))`);
where it keeps the breakpoints, copy the span into its own vector (`std::vector<rp::BreakpointSpec>(bps.begin(),
bps.end())`). Callers inside Mesen's package that called the old public names (`grep -rn "readProfile\|disassemble(\|getPpuState\|drainEvents\|drainBreakHits" packages/mesen/native`)
keep calling the private ones from inside the class; a caller outside the class (a Mesen test) switches to the
`...Into` form with `rp::appendTo`.

- [ ] **Step 4: The debug service**

In `DebugRpcService.cpp`, add `#include "system/Sink.hpp"` and change the eight readers:

```cpp
rp::ExpansionAudioState DebugRpcService::getExpansionAudioState(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    rp::ExpansionAudioState state;
    dbg->expansionAudioStateInto(rp::assignTo(state));
    return state;
}

rp::PpuState DebugRpcService::getPpuState(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    rp::PpuState state;
    dbg->ppuStateInto(rp::assignTo(state));
    return state;
}

std::vector<rp::DebugEvent> DebugRpcService::drainEvents(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();  // null on SameBoy/GBA
    if (!dbg) return {};
    std::vector<rp::DebugEvent> events;
    dbg->drainEventsInto(rp::appendTo(events));  // events Mesen logged for the frame just rendered
    return events;
}

rp::BreakHitBatch DebugRpcService::drainBreakHits(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    rp::BreakHitBatch batch;
    batch.overflow = dbg->drainBreakHitsInto(rp::appendTo(batch.hits));
    return batch;
}

std::vector<rp::TraceLine> DebugRpcService::readTrace(std::uint32_t id, std::uint32_t count) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    std::vector<rp::TraceLine> lines;
    dbg->readTraceInto(count, rp::appendTo(lines));
    return lines;
}

std::vector<rp::ProfiledFunction> DebugRpcService::readProfile(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    std::vector<rp::ProfiledFunction> functions;
    dbg->readProfileInto(rp::appendTo(functions));
    return functions;
}

std::vector<rp::DisasmLine> DebugRpcService::disassemble(std::uint32_t id, std::uint32_t addr, std::uint32_t count) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    std::vector<rp::DisasmLine> lines;
    dbg->disassembleInto(addr, count, rp::appendTo(lines));
    return lines;
}

std::vector<rp::CallFrame> DebugRpcService::getCallStack(std::uint32_t id) {
    SystemBase* sys = engine_.findSystem(id);
    if (!sys) return {};
    rp::IDebugTarget* dbg = sys->debugTarget();
    if (!dbg) return {};
    std::vector<rp::CallFrame> frames;
    dbg->callStackInto(rp::appendTo(frames));
    return frames;
}
```

`loadLabels`, `symbolAddress` and `setBreakpoints` compile unchanged (a `std::string` converts to a view and a vector to
a span). `getCpuRegisters` and `drainCoreBytes` use `SystemBase`'s helpers and stay as they are.

- [ ] **Step 5: Build and run**

```sh
cmake --build build --target retroplug-host retroplug-engine-graph-test retroplug-mesen-test retroplug-packs-test retroplug-packs-heap-test -j$(nproc)
build/bin/retroplug-engine-graph-test 2>&1 | tail -3
build/bin/retroplug-mesen-test 2>&1 | tail -3
build/bin/retroplug-packs-heap-test -s 2>&1 | grep -E "freed|passed|failed" | tail -5
node packages/retroplug/scripts/run-native-tests.mjs cli-profile cli-trace cli-breakpoints cli-labels cli-drain-events ppu-state expansion-audio-state nes-core-bytes nes-invariants app-play-nes-channels 2>&1 | tail -5
```
Expected: all pass. The native-tier files listed exercise every converted debug reader through the RPC (`pnpm
test:native` runs them by slug the same way; if the runner takes slugs differently, run `pnpm test:native` whole).

- [ ] **Step 6: Commit**

```sh
git add packages/system packages/mesen packages/native/src
git commit -m "system: the ROM title is a view and the debug target hands its results to sinks, so a debugger session in a pack frees nothing of the host's"
```

---

### Task 5: The build path is views

**Files:**
- Modify: `packages/system/native/src/system/SystemBuild.hpp`, `HostSystems.hpp`, `SystemNode.hpp`
- Modify: `packages/sameboy/native/src/system/sameboy/SameBoyBackend.{hpp,cpp}`
- Modify: `packages/mesen/native/src/system/mesen/MesenBackend.{hpp,cpp}`
- Modify: `packages/native/src/host/engine/Engine.{hpp,cpp}`
- Modify: `packages/native/test/engine-graph/{NodeRig.hpp,EngineGraph.test.cpp,SystemNode.test.cpp}`
- Modify: every file naming `rp.host.systems/2` under `packages/` and `spec/` (Step 6)

- [ ] **Step 1: The request and the build function**

Replace `SystemBuild.hpp`'s includes, `SystemBuildFn` and `SystemBuildRequest` with:

```cpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#include "graph/rp_node.h"         // rp_error
#include "system/SystemTypes.hpp"  // SystemId

class SystemBase;

struct SystemBuildRequest;

// (keep the paragraph "A system to build (docs/modular-graph-phase-c3-design.md section 5) ..." as it is, and replace
// its last sentence, "A build function is a plain function pointer ... copied a second time.", with:)
// A build function is a plain function pointer, with no capture, because the node's type names it. The request is
// views of what the node holds for the call: the build copies what its core keeps, since the node may be a pack's,
// with a heap of its own (docs/downloadable-packs-m5a-plan.md: no heap block crosses a pack's boundary).

// `id` is the system the core is built under, `request` what to build it from (its `config` is the console's, which
// only this function reads), `sampleRate` the rate the plan runs at, and `why` receives a refusal's reason
// (refuseBuild). Returns an already-onActivate'd core, or null: for a ROM the console's gate refuses, a boot the core
// refuses, or a request that carried no config. The core is the caller's, deleted through SystemBase's virtual
// destructor, whose deleting form is this module's: that is the one object that crosses the boundary, with its deleter.
using SystemBuildFn = std::unique_ptr<SystemBase> (*)(SystemId id, const SystemBuildRequest& request,
                                                      double sampleRate, rp_error* why);

struct SystemBuildRequest {
    // Views of the bytes the node's resources pin, alive for the call.
    std::span<const std::uint8_t> rom;
    std::span<const std::uint8_t> sram;       // battery RAM seed (may be empty)
    std::span<const std::uint8_t> savestate;  // savestate seed (may be empty)
    // The ROM's file stem (a view of the node's setting). A Master System or Game Gear core stages its ROM as a file
    // and takes its battery stem from that name, and now that its ROM is bytes it has nothing else to name the file
    // by; empty keeps the core's "rom".
    std::string_view romName;
    // (maxBlockFrames, build, config and nodeType: unchanged, with their comments)
    std::uint32_t maxBlockFrames = 0;
    SystemBuildFn build  = nullptr;
    const void*   config = nullptr;
    const char*   nodeType = nullptr;
};

// A build function's refusal: `reason` copied into `why` (cut to its buffer), before the function returns null.
inline void refuseBuild(rp_error* why, std::string_view reason) {
    if (why == nullptr) return;
    const std::size_t n = std::min(reason.size(), sizeof why->message - 1);
    std::memcpy(why->message, reason.data(), n);
    why->message[n] = '\0';
}
```

`retroplug-system` must see `graph/rp_node.h`; `HostSystems.hpp` already includes it, so the include path is there. If
the build says otherwise, link the target that `HostSystems.hpp`'s users get it from.

- [ ] **Step 2: The host's systems interface**

`HostSystems.hpp`:

```cpp
#define RP_HOST_SYSTEMS_INTERFACE "rp.host.systems/3"
```

and the member:

```cpp
    // (keep the comment, and add:) The request is the node's, read for the call (SystemBuild.hpp).
    //
    // /3: the request is views and passed by pointer, where /2 passed a request of vectors by value, which a pack's
    // node and the host freed on each other's heaps (docs/downloadable-packs-m5a-plan.md).
    SystemBase* (*build)(void* ctx, std::uint64_t system, const SystemBuildRequest* request, rp_error* err);
```

- [ ] **Step 3: The node fills views**

`SystemNode.hpp`'s `build()` and `bytesOf`:

```cpp
    void build(const void* config, std::uint32_t maxBlockFrames) {
        if (!systems) throw std::runtime_error("the host offers no system builder (rp.host.systems/3)");
        // Views of what this instance's resources pin and its settings hold, alive for the call: the build function
        // copies what its core keeps (SystemBuild.hpp).
        SystemBuildRequest request;
        request.rom = bytesOf(settings->rom);
        request.sram = bytesOf(settings->sram);
        request.savestate = bytesOf(settings->state);
        if (settings->romName) request.romName = *settings->romName;
        request.maxBlockFrames = maxBlockFrames;
        request.build = Self::kBuild;
        request.config = config;
        request.nodeType = Self::id;
        rp_error err{};
        SystemBase* made = systems->build(systems->ctx, settings->system, &request, &err);
        // (the rest unchanged)
    }

    static std::span<const std::uint8_t> bytesOf(const auto& field) {
        if (const rp_bytes* bytes = field.bytes()) return {bytes->data, bytes->size};
        return {};
    }
```

Update the comment above `build()` ("through rp.host.systems/2, with the bytes ...") to `/3` and "with views of the
bytes ...".

- [ ] **Step 4: The build functions**

`SameBoyBackend.hpp` and `.cpp`:

```cpp
std::unique_ptr<SystemBase> buildCore(SystemId id, const SystemBuildRequest& request, double sampleRate, rp_error* why);
```

```cpp
std::unique_ptr<SystemBase> buildCore(SystemId id, const SystemBuildRequest& request, double sampleRate,
                                      rp_error* why) {
    // The node that carries this function is the one that made the config, so a request without one is a broken build
    // rather than a document's mistake.
    const auto* cfg = static_cast<const SameBoyConfig*>(request.config);
    if (!cfg) {
        refuseBuild(why, "the Game Boy build was given no Game Boy config");
        return nullptr;
    }
    // The ROM gate every SameBoy build passes (the embedded mGB carries the same Nintendo logo a cart does): bytes
    // that are positively another console's, or nothing at all, never reach the core.
    if (!isGameBoyRom(request.rom)) {
        refuseBuild(why, "the ROM is not a Game Boy ROM");
        return nullptr;
    }
    SameBoyConfig typed = *cfg;
    // Cores are bare - feature roles (LSDj sync, MGB passthrough, ...) run as TS protocol script nodes. The seeds and
    // the ROM are copied: the request's bytes are the node's.
    typed.sram.assign(request.sram.begin(), request.sram.end());
    typed.savestate.assign(request.savestate.begin(), request.savestate.end());
    auto sys = std::make_unique<SameBoySystem>(id, std::move(typed),
                                               std::vector<std::uint8_t>(request.rom.begin(), request.rom.end()));
    if (request.maxBlockFrames != 0) sys->reserveAudio(request.maxBlockFrames);
    sys->onActivate(sampleRate);       // boots gb_ + restores cfg.sram then cfg.savestate
    sys->enableStateSnapshot();        // publish a tear-free savestate each block so Duplicate can clone
                                       // off the snapshot while the audio thread runs (getFrame's analogue)
    return sys;
}
```

`MesenBackend.hpp` declares the three with `const SystemBuildRequest& request, double sampleRate, rp_error* why`.
`MesenBackend.cpp`:

```cpp
namespace {

// A copy of the bytes a request lends, for the core to keep.
std::vector<std::uint8_t> copyOf(std::span<const std::uint8_t> bytes) { return {bytes.begin(), bytes.end()}; }

template <typename T>
std::unique_ptr<SystemBase> bootMesen(std::unique_ptr<T> sys, double sampleRate, std::uint32_t maxBlockFrames,
                                      rp_error* why) {
    // (body unchanged but the refusal:)
    if (maxBlockFrames != 0) sys->reserveAudio(maxBlockFrames);
    sys->onActivate(sampleRate);
    if (!sys->activated()) {  // core rejected the ROM at LoadRom → fail the construct
        refuseBuild(why, "Mesen refused the ROM");
        return nullptr;
    }
    sys->enableStateSnapshot();
    return sys;
}

std::unique_ptr<SystemBase> buildNesSystem(SystemId id, MesenNesConfig cfg, std::span<const std::uint8_t> rom,
                                           double sampleRate, std::uint32_t maxBlockFrames, rp_error* why) {
    if (!isNesRom(rom)) {
        refuseBuild(why, "the ROM is not a NES ROM");
        return nullptr;
    }
    return bootMesen(std::make_unique<MesenNesSystem>(id, std::move(cfg), copyOf(rom)), sampleRate, maxBlockFrames,
                     why);
}
// buildGbaSystem and buildSmsSystem: the same change (a span, refuseBuild, copyOf(rom)), their gates and comments kept.

}  // namespace

std::unique_ptr<SystemBase> buildNesCore(SystemId id, const SystemBuildRequest& request, double sampleRate,
                                         rp_error* why) {
    const auto* cfg = static_cast<const MesenNesConfig*>(request.config);
    if (!cfg) {
        refuseBuild(why, "the NES build was given no NES config");
        return nullptr;
    }
    MesenNesConfig typed = *cfg;
    typed.sram = copyOf(request.sram);
    typed.savestate = copyOf(request.savestate);
    return buildNesSystem(id, std::move(typed), request.rom, sampleRate, request.maxBlockFrames, why);
}
// buildGbaCore: the same. buildSmsCore: the same, plus `typed.romName = std::string(request.romName);`.
```

- [ ] **Step 5: The host and the rig**

`Engine.hpp`: `SystemBase* buildSystem(std::uint64_t system, const SystemBuildRequest& request, std::string* why);`

`Engine.cpp`, the systems lambda:

```cpp
        [](void* ctx, std::uint64_t id, const SystemBuildRequest* request, rp_error* err) -> SystemBase* {
            auto* self = static_cast<Engine*>(ctx);
            if (request == nullptr) {
                refuseBuild(err, "the build names no request (rp.host.systems/3)");
                return nullptr;
            }
            std::string why;
            SystemBase* core = self->buildSystem(id, *request, &why);
            if (!core) refuseBuild(err, why);
            return core;
        },
```

and in `Engine::buildSystem`, the call into the build function:

```cpp
    const char* const nodeType = request.nodeType;  // a literal the node's type names, alive for the build
    rp_error built{};
    std::unique_ptr<SystemBase> core = build(id, request, sampleRate_, &built);
    if (!core) {
        if (why) *why = built.message[0] != '\0' ? std::string(built.message) : std::string("the core did not build");
        return nullptr;
    }
```

(the rest unchanged; `const SystemBuildFn build = request.build;` stays).

`NodeRig.hpp`'s `buildSystem`:

```cpp
    static SystemBase* buildSystem(void* ctx, std::uint64_t id, const SystemBuildRequest* request, rp_error* err) {
        auto& rig = *static_cast<NodeRig*>(ctx);
        if (id == 0) {
            std::snprintf(err->message, sizeof err->message, "0 is not a system id");
            return nullptr;
        }
        if (id > UINT32_MAX) {
            std::snprintf(err->message, sizeof err->message, "system %llu is beyond a system id",
                          static_cast<unsigned long long>(id));
            return nullptr;
        }
        if (request == nullptr || !request->build) {
            std::snprintf(err->message, sizeof err->message, "the request names no build function (rp.host.systems/3)");
            return nullptr;
        }
        std::unique_ptr<SystemBase> core = request->build(static_cast<SystemId>(id), *request, rig.rate, err);
        if (!core) {
            if (err->message[0] == '\0') std::snprintf(err->message, sizeof err->message, "the core did not build");
            return nullptr;
        }
        rig.built.push_back(core.get());
        return core.release();
    }
```

- [ ] **Step 6: The tests' builds**

The views point at bytes the caller holds, so **a request must never view a temporary**: `request.rom =
readFile(...)` compiles and dangles. Every such line gets a named vector that outlives the build.

- `EngineGraph.test.cpp`:
  - `buildFixtureCore(SystemId id, const SystemBuildRequest& request, double sampleRate, rp_error*)`.
  - `FixtureConsoleWithoutCategory::activate`: `systems->build(systems->ctx, system, &request, &err)`.
  - The case "a build outside the setGraph that activates it is refused ...": hold the ROM first:
    `const std::vector<std::uint8_t> romBytes = readFile(RP_MGB_ROM_PATH);` and `request.rom = romBytes;`.
- `SystemNode.test.cpp`:
  - The boot bench (`struct Boot`): `std::function<std::unique_ptr<SystemBase>(SystemBuildRequest)> build;` stays;
    each lambda ends `return request.build(1, request, kRate, nullptr);`. `request.rom = boot.rom;` views the `Boot`'s
    own vector, which lives for the loop. Its comment "The request is the caller's, its ROM copied before the clock
    starts ..." becomes "The request views the boot's ROM, which the build copies into the core: what is timed is the
    console's build, that copy included, as a node's build pays it."
  - The first-block case: the `build` lambda becomes

    ```cpp
        std::string why;
        // The request's own build function, as the node that made it hands it over: what rp.host.systems/3 runs.
        const auto build = [&why](const SystemBuildRequest& request) {
            rp_error err{};
            auto core = request.build(1, request, kRate, &err);
            why = err.message;
            return core;
        };
    ```

    and the ROMs are read once, before the request factories, which view them:

    ```cpp
        const std::vector<std::uint8_t> nesRom = readFile(RP_BLIPTOASTER_ROM_PATH);
        const std::vector<std::uint8_t> smsRom = readFile(RP_SMS_ROM_PATH);
        const std::vector<std::uint8_t> ggRom = readFile(RP_GG_ROM_PATH);
        const std::vector<std::uint8_t> gbRom = readFile(RP_MGB_ROM_PATH);
    ```

    with `request.rom = nesRom;` (and `gameGear ? ggRom : smsRom`, `gbRom`) in the factories. The GBA section's
    `build(std::move(request))` becomes `build(request)`.
- `grep -rn "SystemBuildRequest\|std::string\* why\|SystemBuildFn" packages --include=*.cpp --include=*.hpp` must list
  no other site of the old signatures.

- [ ] **Step 7: The interface's id everywhere**

```sh
grep -rl "rp.host.systems/2" packages spec | xargs sed -i 's#rp\.host\.systems/2#rp.host.systems/3#g'
grep -rn "rp.host.systems/2" packages spec
```
Expected: the second command prints nothing. The `docs/` plans and designs are history and keep `/2`.

- [ ] **Step 8: Build and run**

```sh
cmake --build build -j$(nproc)
cmake --build build --target retroplug-engine-graph-test retroplug-sameboy-test retroplug-mesen-test retroplug-audio-test retroplug-packs-test retroplug-packs-heap-test retroplug-device-mode-test -j$(nproc)
build/bin/retroplug-engine-graph-test 2>&1 | tail -3
build/bin/retroplug-packs-test 2>&1 | tail -3
build/bin/retroplug-packs-heap-test -s 2>&1 | grep -E "freed|passed|failed" | tail -5
build/bin/retroplug-packs-test "[packs-boundary]" -s 2>&1 | grep -E "returns|takes|holds|passed|failed"
```
With the conversion done, flip the two interim checks: in `PackHeap.test.cpp` replace `CHECK(hostCrossed + packCrossed
> 0);` and its comment with `CHECK(hostCrossed == 0);` and `CHECK(packCrossed == 0);`, and in `BoundaryRules.test.cpp`
replace `CHECK_FALSE(found.empty());` and its comment with `CHECK(found.empty());`. Rebuild and rerun.

Expected: everything passes, the heap test's counts are 0 and 0, and the scan finds nothing. If either is not, find the crossing
with `gdb -batch -ex 'break crossed' -ex run -ex bt build/bin/retroplug-packs-heap-test` and fix it in this task
(report what it was).

- [ ] **Step 9: Commit**

```sh
git add packages spec
git commit -m "system: the build request is views of what the node holds, a refusal is an rp_error, and rp.host.systems/3 takes the request by pointer"
```

---

### Task 6: The API level, the two checks for good, and Windows

**Files:**
- Modify: `packages/node-sdk/native/src/graph/rp_pack.h`
- Modify: `packages/native/test/packs/heap/PackHeap.test.cpp`, `packages/native/test/packs/BoundaryRules.test.cpp`
- Modify: `packages/native/test/packs/MesenPack.test.cpp`
- Modify: `package.json`, `packages/retroplug/scripts/run-plugin-tests.mjs`, `packages/native/cmake/tests.cmake`
  (comments)

- [ ] **Step 1: The API level**

`rp_pack.h`: `#define RP_API_LEVEL 2u`. Nothing else names the number: the fixture variants compute theirs from it
(`fixture/CMakeLists.txt`), the pack.json templates are stamped at configure, and `PackRoots.test.cpp` holds CMake's
reading to the header's.

- [ ] **Step 2: The checks hold for good**

Confirm Task 5 flipped both interim checks: `grep -n "> 0);\|CHECK_FALSE(found.empty" packages/native/test/packs/heap/PackHeap.test.cpp packages/native/test/packs/BoundaryRules.test.cpp` prints nothing.

- [ ] **Step 3: Mesen's pack on Windows**

In `MesenPack.test.cpp`:
- Delete the PCM case's `#if defined(_WIN32) SKIP(...) #else` and its `#endif`, so the body runs everywhere.
- `vtableFile` gains a Windows form, so the case's two vtable checks run there too:

```cpp
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
```

```cpp
// The file the code behind a system's vtable was loaded from: a core built by the pack's own copy of the package has
// its vtable in the module, one built by the static build has it in this binary.
std::string vtableFile(SystemBase* core) {
    if (!core) return {};
    const void* vtable = *reinterpret_cast<void* const*>(core);
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(vtable), &module))
        return {};
    wchar_t     path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
    return n == 0 ? std::string() : std::filesystem::path(std::wstring(path, n)).string();
#else
    Dl_info info{};
    if (dladdr(vtable, &info) == 0 || !info.dli_fname) return {};
    return info.dli_fname;
#endif
}
```

- Rewrite the file header's "And it does not run on Windows: ..." sentence to: "It runs on Windows too: no heap
  ownership crosses the boundary (docs/downloadable-packs-m5a-plan.md), so the static CRT's heap per module is sound,
  which retroplug-packs-heap-test proves." Keep the `retroplug-sdl` sentence but say a pack there is now held to the
  same rule (design question 4).

- [ ] **Step 4: The heap test joins `test:plugin`**

- Root `package.json`, `"test:plugin"`: add `retroplug-packs-heap-test` after `retroplug-packs-test` in the build list.
- `run-plugin-tests.mjs` `BINARIES`: add `"retroplug-packs-heap-test"` after `"retroplug-packs-test"`.
- `packages/native/cmake/tests.cmake`'s header: "Seventeen Catch2 binaries" becomes eighteen, "ten here" becomes
  eleven.

- [ ] **Step 5: Run the whole tier**

```sh
cmake --build build -j$(nproc)
pnpm test:plugin 2>&1 | tail -25
```
Expected: every binary passes, `retroplug-packs-heap-test` among them, and both checks holding zero.

- [ ] **Step 6: Commit**

```sh
git add packages/node-sdk/native/src/graph/rp_pack.h packages/native/test/packs packages/native/cmake/tests.cmake \
        package.json packages/retroplug/scripts/run-plugin-tests.mjs
git commit -m "packs: API level 2, no heap ownership crosses the boundary, held by the heap test and the contract scan, and the Mesen pack's PCM case runs on Windows"
```

---

### Task 7: Docs, verification and the CI gate

**Files:**
- Modify: `docs/downloadable-packs-loading-design.md` (section 2)
- Modify: `spec/02-native-host.md` (Packs), `spec/06-build-test.md`, `spec/07-remaining-work.md`, `AGENTS.md`
- Modify: `docs/downloadable-packs-m5a-plan.md` (After the reviews, design questions)

- [ ] **Step 1: The design**

In `docs/downloadable-packs-loading-design.md` section 2:
- The toolchain bullet: "MSVC 14.5x with the dynamic C runtime on Windows" becomes "MSVC 14.5x with the static C
  runtime (`/MT`) on Windows, so each module has a heap of its own".
- The crossing-rules bullet: replace "An object is freed by the side that made it (a system is already destroyed
  through the `rp.host.systems` build service that built it)." with "No heap ownership crosses (decided 2026-11-25,
  [the M5a plan](downloadable-packs-m5a-plan.md)): a container crosses only by const reference or view, read during
  the call; a result is handed to a sink the caller made (`system/Sink.hpp`); the build request is views. The one
  object that crosses is a core, deleted through its virtual destructor, whose deleting form is the pack's."

- [ ] **Step 2: spec/02**

In the Packs section, replace the sentence from "The boundary between a pack's systems and the engine is C++
(`SystemBase`), pinned by the toolchain and the API level, and heap ownership crosses it ..." through "... and the Mesen
PCM case skips there." with:

> The boundary between a pack's systems and the engine is C++ (`SystemBase`, its capability and debug interfaces, and
> the build request), pinned by the toolchain and the API level, and no heap ownership crosses it, since a pack's
> library has a heap of its own wherever its C runtime is its own (Windows' `/MT`): a public virtual returns no owning
> type but hands its result to a sink of the caller's ([system/Sink.hpp](../packages/system/native/src/system/Sink.hpp)),
> with an inline helper of the old name giving the caller a vector; a parameter is a view or a const reference; the
> build request is views of what the node holds, and a refusal an `rp_error` (`rp.host.systems/3`). A core is the one
> object that crosses, deleted through its virtual destructor, which is the pack's. `[packs-boundary]` scans the
> contract headers for an owning return or parameter, and `retroplug-packs-heap-test` loads the Mesen package as a
> pack with a heap of its own (`-static-libstdc++` on Linux, `/MT` on Windows; macOS skips) under a tagging allocator
> on both sides and holds the blocks each frees to its own, through builds, refusals, snapshots, debug services and
> destruction.

and change the last sentence's "on Linux and macOS" to "on every platform".

- [ ] **Step 3: spec/06**

- Add a row for `retroplug-packs-heap-test` after `retroplug-packs-test`'s in the binaries table: "executable | No heap
  ownership crosses a pack's boundary ([spec/02](02-native-host.md#packs-a-packages-native-half-loaded-at-run-time)):
  the `mesen-heap` pack (the Mesen package as a pack with a heap of its own) and this binary each tag their blocks, and
  every boundary path is driven with the counts held at zero. Replaces the global `operator new`, so it is a binary of
  its own; macOS skips."
- `test:plugin`'s row: "seventeen" becomes "eighteen" and the list gains `-packs-heap-test`.

- [ ] **Step 4: spec/07 and AGENTS.md**

- spec/07's Downloadable packs entry: delete ", plus the allocator the pinned C++ boundary needs on Windows (the M1
  plan's design question 1)", change "M5 (Mesen and BlipToaster loaded as packs ...)" to "M5b to M5d (Mesen and
  BlipToaster loaded as packs ...; M5a, no heap ownership across the boundary, is done)", and add, before "Open from
  M2f", an "Open from M5a ([its plan](../docs/downloadable-packs-m5a-plan.md)'s design questions): ..." sentence
  listing this plan's design questions in one line each.
- `AGENTS.md`, in Workflow rules after the `RP_API_LEVEL` rule (keep the file under 200 lines):
  "- **No heap ownership crosses a pack's boundary**: a virtual a pack implements returns no owning type (a result goes
  to a sink, `system/Sink.hpp`) and takes one only by const reference; `[packs-boundary]` and
  `retroplug-packs-heap-test` hold it ([spec/02](spec/02-native-host.md#packs-a-packages-native-half-loaded-at-run-time))."

- [ ] **Step 5: Verify**

```sh
pnpm typecheck && pnpm check:boundaries && pnpm check:doc-links
cmake --build build -j$(nproc)
mkdir -p .superpowers/verify/runs/m5a
env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=.superpowers/verify/runs/m5a/tv bash .superpowers/verify/tip-verify-par.sh
bash .superpowers/verify/compare.sh .superpowers/verify/runs/m2f/tv .superpowers/verify/runs/m5a/tv
```
The compare must differ from M2f.s run only by additions: `retroplug-packs-heap-test` and the new `[packs-boundary]`
cases (run it in the background with a completion notification; it takes about 10 minutes). Known: the GBA app-cores case fails on this machine as it did before M5a. Run
`pnpm sdl:controller` on its own (the tip verify skips it). Run the sanitizer leg the packs binary has:

```sh
tools/run-sanitizer.sh thread 2>&1 | tail -5
tools/run-sanitizer.sh address 2>&1 | tail -5
```
(each mode builds and runs `retroplug-packs-test` whole beside its default slugs; the heap test is not run under a
sanitizer, whose allocator it would replace).

- [ ] **Step 6: The plan's own record**

Append "## After the reviews" (what the reviews changed, as the M2 plans do) and "## Design questions this plan
found" to this plan, with at least these, each answered or left open for the user:

1. **The core's deleter is its vtable.** A C system ABI (Rust) would need an explicit `destroy` in the vtable, since
   a foreign type has no C++ deleting destructor.
2. **The host runs `SystemBase`'s non-virtual snapshot members on a pack's core** (`readStateSnapshot`,
   `stateSnapshotCapacity`, `stateRegions`, `rearmStateSnapshot`, `runUntilPc`), compiled into the host from the
   static `retroplug-system`. They allocate nothing, so no ownership crosses, but they read the pack's object layout;
   a C ABI would make them calls.
3. **A `Sink<T>`'s records hold strings** (`ProfiledFunction::label`, `CpuRegister::name`, `ExpansionAudioState`,
   `PpuState`'s `rfl::Bytestring`), lent by const reference, so their layout must match on both sides: the pinned
   toolchain gives that, a foreign-language pack would need view records.
4. **`retroplug-sdl` links `-static-libstdc++`** (the M1 plan's question 14). With no ownership crossing, a pack there
   is sound by the same argument as the heap test's, provided the layouts match (the same libstdc++ version): whether
   M5b loads packs in that host is its to decide.
5. **The heap test covers `operator new`, not `malloc`.** A C allocation crossing (a block one side `malloc`s and the
   other `free`s) is not tagged; the C interfaces (`rp_node.h`) free what they allocate by their own reading.
6. **No heap test on macOS** (libc++ is a system library). The boundary rule holds there by the same code; only the
   proof is missing.
7. **The heap test's Linux module needs a PIC `libstdc++.a`** (Ubuntu's gcc 13 has one). A runner without one fails
   the module's link; the fallback is building it without the static runtime and skipping there.
8. **Other branches implement `SystemBase`** (feature/snes, feature/wonderswan, feature/megadrive, feature/m8's cores
   if any): each needs this conversion when it merges (the `...Into` virtuals, a view request, `rp_error`), and the
   scan and the heap test catch what a merge misses.
9. **The build copies the ROM in the build function** rather than in the node: the same one copy. spec/07's "P3:
   cores borrow their pinned ROM" can now be had by a core keeping the span instead, if the pin's lifetime is made the
   core's.

10. **The Linux heap leg cannot see a by-value owning parameter.** On Itanium the caller destroys a by-value
    argument; on MSVC the callee does. So a by-value container parameter crossing shows only on Windows: the contract
    scan (`parameterFault`) and the Windows CI gate hold that class (the Task 1 review).

- [ ] **Step 7: Commit**

```sh
git add docs spec AGENTS.md
git commit -m "docs: no heap ownership crosses a pack's boundary, the heap test, and M5a's design questions"
```

- [ ] **Step 8: The CI gate**

Ask the user to push `feature/modularize`. Then dispatch `build.yml` on it and read every job: linux, linux-arm64,
macos, and windows `/W4 /WX`. On Windows the heap test and `[packs-mesen]`'s PCM case must **run and pass** (not
skip): read their lines in the `test.bat` plugin step's log. On linux and linux-arm64 the heap test runs with the
static-runtime module; on macos it skips.

## After the reviews

M5a was built as `9a8321d80..38614cf65` (Tasks 1 to 6) and the docs commit after it (Task 7); Task 7's CI gate waits for
the user's go-ahead to push. Where the task blocks above and the tree differ, the tree wins. What the per-task reviews
changed from the plan while it ran, and why:

- **Task 1.** The heap scenario counts today's crossings (`> 0`) instead of `[!shouldfail]`, which passes on any failure
  and would have hidden a broken load during the rewrite. The module needed `RP_BOUNDARY_DECLARED mesen` (native owns the
  target and does not declare mesen). The heap test joined the `/W4 /WX` targets. `allocateTagged` has an oversize
  guard. The helpers are compiled only where a heap pack exists. The negative control also proves the host's `operator
  new` is the tagged one. Measured at the start: the host freed 57 of the pack's blocks (6 in `SnapshotRegistry::claim`,
  6 the request's by-value `romName`, 45 the stream layout and the debug service's containers) and the pack freed 0; 46
  after Task 3, 6 after Task 4, 0 after Task 5.
- **Task 2.** The scan's `CHECK_FALSE(found.empty())` interim is likewise a count of faults, not a should-fail. An `enum
  class` inside `SramCapability` made the parser read the rest as private and skip `SramCapability::write`, so the
  public virtuals are 61, not 60. Every `virtual` token must parse (a parenthesised parameter type now fails the scan
  instead of hiding). The non-public virtuals are a pinned set. A pointer return to an owning type is a fault. By-value
  parameters are held to an allow list. `SystemBuildRequest` by value is named a fault (`HostSystems.hpp` had no `std::`
  token).
- **Task 3.** The first-block allocation case measures the block's own capture (`captureStateSnapshot` into an empty
  vector, through a derived probe) rather than the helper plus an adjustment.
- **Task 4.** Comments only: the expansion-audio default, `readTraceInto`, the title view's lifetime.
- **Task 5.** Comments and nits: the request is copied, not moved; `NodeRig`'s null-request text matches `Engine`'s.
- **Task 6.** The Mesen PCM case runs on Windows, and `RP_API_LEVEL` is 2.

## Design questions this plan found

1. **The core's deleter is its vtable.** A C system ABI (Rust) would need an explicit `destroy` in the vtable, since a
   foreign type has no C++ deleting destructor. Open.
2. **The host runs `SystemBase`'s non-virtual snapshot members on a pack's core** (`readStateSnapshot`,
   `stateSnapshotCapacity`, `stateRegions`, `rearmStateSnapshot`, `runUntilPc`), compiled into the host from the static
   `retroplug-system`. They allocate nothing, so no ownership crosses, but they read the pack's object layout; a C ABI
   would make them calls. Open.
3. **A `Sink<T>`'s records hold strings** (`ProfiledFunction::label`, `CpuRegister::name`, `ExpansionAudioState`,
   `PpuState`'s `rfl::Bytestring`), lent by const reference, so their layout must match on both sides: the pinned
   toolchain gives that, a foreign-language pack would need view records. Open.
4. **`retroplug-sdl` links `-static-libstdc++`** (the M1 plan's question 14). With no ownership crossing, a pack there
   is sound by the same argument as the heap test's, provided the layouts match (the same libstdc++ version): whether
   M5b loads packs in that host is its to decide. Open.
5. **The heap test covers `operator new`, not `malloc`.** A C allocation crossing (a block one side `malloc`s and the
   other `free`s) is not tagged; the C interfaces (`rp_node.h`) free what they allocate by their own reading. Open.
6. **No heap test on macOS** (libc++ is a system library). The boundary rule holds there by the same code; only the
   proof is missing. Open.
7. **The heap test's Linux module needs a PIC `libstdc++.a`** (Ubuntu's gcc 13 has one). A runner without one fails the
   module's link; the fallback is building it without the static runtime and skipping there. Open.
8. **Other branches implement `SystemBase`** (feature/snes, feature/wonderswan, feature/megadrive, feature/m8's cores if
   any): each needs this conversion when it merges (the `...Into` virtuals, a view request, `rp_error`), and the scan
   and the heap test catch what a merge misses. Open.
9. **The build copies the ROM in the build function** rather than in the node: the same one copy. spec/07's "P3: cores
   borrow their pinned ROM" can now be had by a core keeping the span instead, if the pin's lifetime is made the core's.
   Open.
10. **The Linux heap leg cannot see a by-value owning parameter.** On Itanium the caller destroys a by-value argument;
    on MSVC the callee does. So a by-value container parameter crossing shows only on Windows: the contract scan
    (`parameterFault`) and the Windows CI gate hold that class (the Task 1 review). Answered by those two checks.
