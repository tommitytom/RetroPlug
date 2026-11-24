# Downloadable packs M2d: a pack's device script half, and packs in the render worker. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pack's `script` half, a device script pack (MicroQuickJS nodes, as BlipToaster's is), ships as the ES5
source the device-pack toolchain writes and is compiled to bytecode when the pack loads, then registered in every Engine
of the process as a built-in device pack is; and a render job's worker loads the packs' `app` and `consoles` halves as
the control plane does, so a render sees a pack's consoles, roles and ROM providers.

**Architecture:** The MicroQuickJS host gains `compileScript`, the same parse and bytecode emission `mqjs -o` runs (both
are upstream's engine, already in every desktop binary). The native pack loader compiles a script half's two bundles,
fills an `rp_mqjs_pack_source` and registers it through `rp::mqjs::package`, so its package joins the pack's packages and
every Engine registers it at `syncPacks`, render jobs' included. The render worker becomes the host of a fourth shared
module set (`render`: `rp:render` and the shell contracts), evaluated as a module on its bare QuickJS runtime, with a
txiki-free variant of the pack loader; its entry awaits `loadAppPacks()` before it composes the session.

**Tech Stack:** C++20, MicroQuickJS (vendored, unpatched: `deps/mquickjs`, wrapped by `rp_mqjs_engine.c`), QuickJS-ng
(`JS_SetModuleLoaderFunc2`, `JS_ReadObject`, `JS_EvalFunction`, `JS_PromiseState`), M1's `retroplug-packs`, M2b's shared
module sets and `retroplug-modc`, M2c's pack loader and TS loader, esbuild, the device-pack toolchain
(`build-device-pack.mjs`), Catch2 v3, the shell's TS and CLI suites.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones: M1
native packs, M2a live registries, M2b shared contract modules, M2c a pack's TS halves (each done, with its plan beside
this one), then:

- **M2d (this plan, decided 2026-11-25):** the device script half, compiled at load; the render worker loading the
  packs' `app` and `consoles` halves. This is what M5 needs of M2: Mesen's pack has native, app, consoles, ui and cli
  halves (a Mesen render needs its consoles half in the worker), BlipToaster's has app, ui, script and cli.
- **M2e (next):** the protocol half (today's `PROTOCOL_NODES`, `DECLARED_NODE_TYPES`, `ROLE_NODES` and `KIND_DATA` are
  frozen constants `buildGraph` and its readers take; a pack's protocol nodes, which are QuickJS script nodes, would
  reach every Engine through `Engine::loadScriptPack`'s path, and its placer the live `placersRegistry`), and the
  contribution kinds (`accept` runs once; a kind item's runtime half lives in the declaring package's script context,
  which another pack's script pack cannot reach). Neither M5 pack has a protocol member, a placer or a kind.
- M3 placeholders, M4 archives, commands and Settings > Packs, M5 the real packages as packs, as before.

**What exists.** M1's `PackTable` loads a pack's native half and refuses a `protocol` or `script` half by name
(`PackTable.cpp`, "which this RetroPlug does not load yet"). A device script pack today is built only at build time:
`cmake/packages.cmake`'s `retroplug_device_script_pack` runs `build-device-pack.mjs` (bundle, lower to ES5, scan for
library calls the engine lacks, `mqjs -o` at 64 and 32 bits, a transcript run on `retroplug-mqjs-run`), writes
`<name>.pack64.c`, and a generated `<name>-script.cpp` registers it with `rp::mqjs::package(rp_script_<name>)`
(`MqjsHost.cpp`'s `registerPack`: on the desktop it loads the whole pack in a control context and refuses one whose
definitions are not the recorded type ids). The render worker (`src/render/worker.ts`) is a global-code IIFE embedded as
`rp_render_worker_bundle` and run synchronously by `RenderHost` on a bare QuickJS runtime with no module loader; its
Engine already registers the process's native packs (`Engine::syncPacks` over `processPacks()`), but its TS has none of
their halves.

**Execution model.** Sonnet implements each task from its text alone; Opus reviews each task's diff before the next one
starts. The code here was not compiled or run as written: where a step meets the tree differently (a helper's name, an
include, a type's exact field), the implementer follows the tree, keeps the step's intent, and reports the deviation.
Task 10 ends with a CI gate on Windows and macOS, which needs the user's go-ahead to push.

## Decisions this plan makes

- **A script half is a device script pack, shipped as source and compiled at load** (decided with the user 2026-11-25).
  The pack carries the two ES5 bundles the device-pack toolchain writes: the whole pack (`<name>.js`, which the desktop's
  control context runs) and the device's runtime (`<name>.device.js`, which every node runs). The loader compiles the
  whole pack and the runtime at 64 bits, and the runtime at 32 bits only for its size (the box's package memory figure,
  `box_bytecode_size`). The compiler is upstream's own `JS_Parse` and `JS_PrepareBytecode`, already linked into every
  desktop binary; no new dependency. Source survives an `mquickjs` pin bump that bytecode would not, keeps the design's
  rule that halves are source, and avoids loading a downloaded pack's bytecode, which MicroQuickJS does not check
  (`JS_LoadBytecode`: "the bytecode is not checked so it should come from a trusted source"). The toolchain's checks
  (the ES5 lowering, the library-call scan, the transcript and heap runs) stay in the pack's build, where
  `tools/build-pack-halves.mjs` runs them.
- **The design's `script` half was a QuickJS `loadScriptPack` pack; this plan makes it the device pack**, because that
  is what a package's `retroplug.script` member is (BlipToaster's, devtest's), and a desktop QuickJS script pack is
  reserved (`cmake/packages.cmake` refuses `device: false`). QuickJS script nodes a pack brings are its protocol nodes,
  which M2e loads through `loadScriptPack`'s path.
- **The manifest names the runtime, the heap and the types in a `script` member.** `halves.script` is the whole pack's
  file; `script` is `{ "runtime": "<file>", "heapBytes": <n>, "nodeTypes": [<id>, ...] }`, the runtime's file inside the
  pack, the package manifest's `heapBytes`, and the ids the definitions register, in their order (the order a device
  registers them in, with no JavaScript run). Each must be in `provides.nodeTypes`. Either one without the other is
  refused. The member is new and additive (a host before M2d refuses a script half anyway), so the API level does not
  move.
- **The script half compiles against the host's own standard library, under its package names.** A MicroQuickJS
  bytecode's references to the standard library's atoms are the compiling table's (upstream's relocator carries an
  "XXX: add atom checksum" for it), so a half compiled here is bound to the very table that runs it, `rp_mqjs_stdlib`;
  `mqjs -o` compiles against upstream's `mqjs_stdlib` and its bytes differ (found by Task 1's first run, 2026-11-25:
  same sizes, different atom references). Another reason to ship source. The loader names the two bundles
  `<pack id>.js` and `<pack id>.device.js` when it compiles them, the toolchain's names, which the bytecode records
  for its errors. What holds the compile is that its output is deterministic, that the host registers it (the control
  context runs the whole pack against its type ids), and that BlipToaster's pack behaves as its static build (Task 5).
- **The script package joins the pack's packages.** It goes through the checks a native package does (each type
  declared in `provides.nodeTypes`, none held by a built-in or an earlier pack, the scratch registry's trial), and
  every Engine registers it at `syncPacks`, so render jobs and every plugin instance get it as they get native nodes.
  Its id is `rp.<pack id>.script`, as `registerPack` writes it.
- **A script half is held for the life of the process, refused or not.** `registerPack` keeps a pointer to the source,
  and compares sources by address, so the table owns every compiled half it handed to `package()` (as it keeps every
  library it opened), and a refusal after the registration costs one of the process's 32 MicroQuickJS pack slots.
- **The render worker is a shared module set's host.** `tools/build-shared-modules.mjs` gains a `render` set
  (`rp:render`, from `src/render/worker.ts`, and `SHELL_IMPORTS`, as the `cli` set), compiled by `retroplug-modc` into
  `rp_render_modules` and `rp_render_bundle`; `RenderHost` registers the set (`registerSharedModules` uses QuickJS alone)
  and evaluates the entry as a module, pumping the job queue to the end, as before. A pack's halves link to the set's
  contracts by name, as they do in the control plane.
- **The bare pack loader is the txiki one without txiki.** `installBarePackLoader` serves `pack:<id>/<half>` the same
  way and applies the same guard; a bare name is passed through as written (QuickJS finds a registered module by its
  name), anything else is not found. It installs `__rp_packList` (with the process's budget skips) and no
  `__rp_packRefused`: a render's refusals go to the job's console, since the context that loaded those halves first (the
  control plane, the CLI's dispatcher) recorded its own.
- **The render worker loads its packs before it composes, with no budget.** Its `main` awaits `loadAppPacks()` (the
  control plane's builders: `app` and `consoles`), then runs the render as before. A bare runtime has no timers, so the
  TS loader skips the budget there (a half has no timer to wait on either: it settles in the job queue or never), and a
  worker that never reports is reported as such, as it was; the job's cancel interrupt stays the backstop for a half
  stuck in a loop. The entry calls `main()` without a top-level await: the bundles target es2020, where esbuild refuses
  one.
- **`pack:<id>/script` is no module.** The script half is MicroQuickJS code, so both module loaders refuse to serve it,
  as they refuse the native half.

## Commands used throughout

- The TS suite: `pnpm test` or `pnpm test <slug>` (a file's path under `test/` without `.test.ts`, or a directory; a
  slug that matches nothing exits 1 with "no tests match"). `pnpm test` builds `retroplug-devtest-script-pack`, which
  builds `mqjs` and `retroplug-mqjs-run`.
- The native suite: `pnpm test:native [slug]` (test-native/, on `retroplug-host`). The CLI: `pnpm test:cli-ts`. The
  plugin binaries: `pnpm test:plugin [binary]`, e.g. `pnpm test:plugin packs-test`.
- One binary: `cmake --build build --target <t> -j$(nproc)`, then e.g. `build/bin/retroplug-packs-test "[packs-script]"`.
  After a CMake change: `pnpm configure`. A UI change to the standalone needs the bare `cmake --build build -j$(nproc)`.
  Never `rm -rf build`.
- `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`.
- `deps/sameboy` shows as modified after a configure: expected; never stage it. Generated modules and `build/` are
  gitignored outputs.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/native/src/host/packs/PackScript.{hpp,cpp}` | `ScriptHalf`, `loadScriptHalf`: a pack's script half read, compiled and registered |
| `packages/native/test/packs/PackScript.test.cpp` | the compiler on the toolchain's bundles; a script half in the table and in Engines; BlipToaster's pack against its static build |
| `packages/native/test/packs/script-fixture/` | the script fixture packs (`scriptfix` and its variants, from devtest's script pack; `bliptoaster`, its script half alone), with their CMake |
| `packages/native/test/packs/ts-fixture/render/` | `tsfix-render`: an app half whose role and ROM provider a render reads |

Modified: `packages/graph-runtime/native/src/script/MqjsHost.{hpp,cpp}` (`compileScript`),
`packages/native/src/host/packs/{PackManifest,PackTable}.{hpp,cpp}`, `packages/native/CMakeLists.txt` (the
`retroplug-packs` sources and links), `packages/native/cmake/tests.cmake`, `packages/native/cmake/product-bundles.cmake`,
`packages/native/test/packs/{PackManifest,PackLoader}.test.cpp`, `packages/native/test/packs/fixture/` (one manifest's
comment), `packages/native/src/host/js/PackModules.{hpp,cpp}`, `packages/native/test/plugin/PackModules.test.cpp`,
`packages/native/src/host/render/RenderHost.{hpp,cpp}`, `packages/native/test/packs/ts-fixture/{CMakeLists.txt,tsconfig.json}`,
`packages/retroplug/scripts/build-device-pack.mjs`, `cmake/packages.cmake` (one byproduct), `tools/build-pack-halves.mjs`,
`tools/build-shared-modules.mjs`, `packages/retroplug/src/render/worker.ts`, `packages/retroplug/src/packs/{loadPacks,packHost}.ts`,
`packages/retroplug/test/packs/load-packs.test.ts`, `packages/retroplug/test/script/{build-pack-halves.test.ts,bundle-split.mjs}`,
`tools/run-cli-ts-tests.sh`, `package.json` (`test:cli-ts`'s targets), and the docs (spec/02, spec/06, spec/07, the
loading design's section 3).

---

### Task 1: The MicroQuickJS host compiles a script

**Files:**
- Modify: `packages/graph-runtime/native/src/script/MqjsHost.hpp`, `packages/graph-runtime/native/src/script/MqjsHost.cpp`
- Create: `packages/native/test/packs/PackScript.test.cpp`
- Modify: `packages/native/cmake/tests.cmake` (the packs test's sources, links, definitions and dependencies)

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/packs/PackScript.test.cpp`:

```cpp
// A pack's script half (docs/downloadable-packs-m2d-plan.md): a device script pack shipped as the ES5 source the
// device-pack toolchain writes, compiled when the pack loads. Here: the host's compiler, run on the toolchain's own
// bundles. Its bytes are not mqjs -o's: mqjs compiles against upstream's standard library and this host against
// RetroPlug's (upstream's tables and the node SDK's names), and a bytecode's references to the library's atoms are the
// table's own (deps/mquickjs's relocation: "add atom checksum to avoid problems if the stdlib is modified"). Compiled
// here, a pack is bound to the table that runs it; what holds it is that it is the same each time and that the host
// loads it, and BlipToaster's pack behaving as its static build (below).
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-script]"`.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "script/MqjsHost.hpp"
#include "script/rp_mqjs_pack.h"

namespace fs = std::filesystem;

namespace {

// The device-pack toolchain's outputs for a package's script pack (cmake/packages.cmake's retroplug_device_script_pack).
fs::path toolchainOut(const std::string& name) { return fs::path(RP_SCRIPT_PACKS_BUILD_DIR) / name / "script"; }

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A compiled pack as the MicroQuickJS host takes it. The host keeps a pointer to the source for the life of the
// process, so every one this file registers is kept too.
struct Compiled {
    std::string               name;
    std::vector<std::uint8_t> runtime, control;
    std::vector<std::string>  ids;
    std::vector<const char*>  idPtrs;
    rp_mqjs_pack_source       source{};
};
std::vector<std::unique_ptr<Compiled>>& kept() {
    static std::vector<std::unique_ptr<Compiled>> all;
    return all;
}

}  // namespace

TEST_CASE("compileScript compiles a pack's bundles to the same bytes each time, and the host registers them",
          "[packs-script]") {
    struct Pack {
        std::string              name;
        std::uint32_t            heapBytes;
        std::vector<std::string> ids;
    };
    for (const Pack& pack : {Pack{"devtest", 57344,
                                  {"rp.devtest.script-transpose", "rp.devtest.script-nest", "rp.devtest.script-loop"}},
                             Pack{"bliptoaster", 24576, {"rp.bliptoaster.midi-policy"}}}) {
        INFO(pack.name);
        const fs::path out = toolchainOut(pack.name);
        const std::string whole = readText(out / (pack.name + ".js"));
        const std::string runtime = readText(out / (pack.name + ".device.js"));
        const std::string wholeName = pack.name + ".js", runtimeName = pack.name + ".device.js";
        std::string why;
        const auto control = rp::mqjs::compileScript(whole, wholeName.c_str(), false, &why);
        INFO(why);
        REQUIRE(control);
        const auto device = rp::mqjs::compileScript(runtime, runtimeName.c_str(), false, &why);
        REQUIRE(device);
        const auto box = rp::mqjs::compileScript(runtime, runtimeName.c_str(), true, &why);
        REQUIRE(box);
        // The same bytes every time: the heap's address never reaches them (relocated to 0, the header zeroed).
        CHECK(*control == *rp::mqjs::compileScript(whole, wholeName.c_str(), false, &why));
        CHECK(*device == *rp::mqjs::compileScript(runtime, runtimeName.c_str(), false, &why));
        CHECK(*box == *rp::mqjs::compileScript(runtime, runtimeName.c_str(), true, &why));
        CHECK(box->size() < device->size());  // the box's words are half the width

        // Registered as a pack's are: the desktop's control context loads the whole pack and holds its definitions to
        // the ids, and a node's context loads the runtime when a plan places one (the cases below).
        auto c = std::make_unique<Compiled>();
        c->name = "compiled-" + pack.name;
        c->runtime = *device;
        c->control = *control;
        c->ids = pack.ids;
        for (const auto& id : c->ids) c->idPtrs.push_back(id.c_str());
        c->source = rp_mqjs_pack_source{c->name.c_str(), c->runtime.data(), static_cast<std::uint32_t>(c->runtime.size()),
                                        pack.heapBytes, static_cast<std::uint32_t>(c->idPtrs.size()), c->idPtrs.data(),
                                        static_cast<std::uint32_t>(box->size()), c->control.data(),
                                        static_cast<std::uint32_t>(c->control.size())};
        kept().push_back(std::move(c));
        const rp_package* registered = rp::mqjs::package(kept().back()->source);
        INFO(rp::mqjs::packageRefusal());
        REQUIRE(registered != nullptr);
        CHECK(registered->node_type_count == pack.ids.size());
    }
}

TEST_CASE("compileScript refuses a script that does not parse, naming the file", "[packs-script]") {
    std::string why;
    CHECK_FALSE(rp::mqjs::compileScript("var x = ;", "broken.js", false, &why));
    CHECK(why.find("broken.js") != std::string::npos);
}
```

In `packages/native/cmake/tests.cmake`, in `retroplug_add_test_binary(NAME retroplug-packs-test ...)`: add
`test/packs/PackScript.test.cpp  # the script half: compiled at load, in the table and in Engines` to `SOURCES`,
`retroplug-mqjs-host` to `LIBS` (the compiler; Task 3 makes it a link of `retroplug-packs`, and this line can stay), and
`RP_SCRIPT_PACKS_BUILD_DIR="${CMAKE_BINARY_DIR}/packages"` to `DEFS`; after the binary, beside the fixture targets:

```cmake
# The device-pack toolchain's own outputs, whose ES5 bundles PackScript.test.cpp compiles.
add_dependencies(retroplug-packs-test retroplug-devtest-script-pack retroplug-bliptoaster-script-pack)
```

(The field order of `rp_mqjs_pack_source` is `script/rp_mqjs_pack.h`'s; if it differs from the initializer's, assign
the members by name.)

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL to compile: `rp::mqjs::compileScript` is not declared.

- [ ] **Step 3: Implement**

In `MqjsHost.hpp`, inside `#ifndef RP_DEVICE`, beside `exactPackage` (add `<optional>`, `<string>`, `<string_view>` and
`<vector>` to its includes if they are not there):

```cpp
// `source` (ES5 global code, as the device-pack toolchain lowers a pack) compiled to MicroQuickJS bytecode in the steps
// `mqjs -o` takes for a file: parsed in a compiling context with this host's standard library (so the bytecode's
// references to the library's atoms are the table that runs it: mqjs's, compiled against upstream's library, differ),
// then written at this build's width, relocated to address 0 (the same bytes every time), or, with `box32`, at the
// box's 32 bits.
// `filename` is the name the bytecode records (the toolchain's is the file's own name, `<pack>.js` or
// `<pack>.device.js`). Nothing, with the parse error and the file named in *why, for a script that does not parse.
// What a pack loaded at run time ships is source (docs/downloadable-packs-m2d-plan.md): its bytecode is made here.
std::optional<std::vector<std::uint8_t>> compileScript(std::string_view source, const char* filename, bool box32,
                                                       std::string* why);
```

In `MqjsHost.cpp`, inside `namespace rp::mqjs` and `#ifndef RP_DEVICE` (beside `runScript`), the same steps as
`deps/mquickjs/mqjs.c`'s `compile_file`:

```cpp
std::optional<std::vector<std::uint8_t>> compileScript(std::string_view source, const char* filename, bool box32,
                                                       std::string* why) {
    // mqjs -o's own heap for a compile, which a pack's bundle never nears; freed before this returns.
    constexpr std::size_t kCompileBytes = std::size_t{16} << 20;
    std::vector<std::uint64_t> memory(kCompileBytes / sizeof(std::uint64_t));
    JSContext* ctx = JS_NewContext2(memory.data(), kCompileBytes, &rp_mqjs_stdlib, 1);
    if (ctx == nullptr) {
        if (why) *why = std::string(filename) + ": no context could be made to compile it";
        return std::nullopt;
    }
    // JS_Parse reads `length` bytes; mqjs hands it a whole file, NUL-terminated, which the copy keeps too.
    const std::string text(source);
    JSValue code = JS_Parse(ctx, text.c_str(), text.size(), filename, 0);
    if (JS_IsException(code)) {
        if (why) *why = std::string(filename) + ": " + /* the pending exception's message */;
        JS_FreeContext(ctx);
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    const std::uint8_t* data = nullptr;
    std::uint32_t length = 0;
    const auto append = [&](const void* bytes, std::size_t size) {
        const auto* b = static_cast<const std::uint8_t*>(bytes);
        out.insert(out.end(), b, b + size);
    };
    if (box32) {
#if JSW == 8
        JSBytecodeHeader32 header{};
        if (JS_PrepareBytecode64to32(ctx, &header, &data, &length, code) != 0) {
            if (why) *why = std::string(filename) + ": its bytecode could not be converted to 32 bits";
            JS_FreeContext(ctx);
            return std::nullopt;
        }
        append(&header, sizeof header);
#else
        if (why) *why = "a 32-bit build compiles at its own width only";
        JS_FreeContext(ctx);
        return std::nullopt;
#endif
    } else {
        JSBytecodeHeader header{};  // zeroed: mqjs writes the padding after `version` from an uninitialised union
        JS_PrepareBytecode(ctx, &header, &data, &length, code);
        // Relocated to address 0, as mqjs does, so the bytes do not depend on where this heap was.
        JS_RelocateBytecode2(ctx, &header, const_cast<std::uint8_t*>(data), length, 0, 0);
        append(&header, sizeof header);
    }
    append(data, length);  // the data lives in the context's heap: copied before the context goes
    JS_FreeContext(ctx);
    return out;
}
```

The exception's message is read the way the file already reads one (the control context's refusals in `openControl`
and its helpers turn a pending exception into text): use that helper, and keep the message's file and line. The engine's
names (`JS_NewContext2`, `JS_Parse`, `JS_PrepareBytecode`, `JS_PrepareBytecode64to32`, `JS_RelocateBytecode2`,
`JSBytecodeHeader`, `JSBytecodeHeader32`, `JSW`) come through `script/rp_mqjs.h` and `rp_mqjs_names.h` as the file's other
calls do; if one is not renamed there (`rp_mqjs_names.h` lists the public ones), it is an engine symbol the desktop
binary already links (`nm build/bin/retroplug-cli | grep rp_mqjs_JS_Parse`), and the rename list gains it the way its
siblings are listed. `deps/mquickjs` is never edited.

The bytes are not `mqjs -o`'s, by design: a first run found them differing from the header's `unique_strings` field on,
with equal sizes, because `mqjs` compiles against upstream's `mqjs_stdlib` and this host against `rp_mqjs_stdlib`, and a
bytecode's references to the library's atoms are that table's (decided 2026-11-25: a pack compiled here is bound to
the table that runs it).

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-script]"`
Expected: `All tests passed`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add packages/graph-runtime/native/src/script/MqjsHost.hpp packages/graph-runtime/native/src/script/MqjsHost.cpp \
  packages/native/test/packs/PackScript.test.cpp packages/native/cmake/tests.cmake
git commit -m "script: the MicroQuickJS host compiles a script at load, against its own standard library"
```

(If `rp_mqjs_names.h` gained a name, add it too.)

---

### Task 2: pack.json's script member

**Files:**
- Modify: `packages/native/src/host/packs/PackManifest.hpp`, `packages/native/src/host/packs/PackManifest.cpp`
- Test: `packages/native/test/packs/PackManifest.test.cpp`

- [ ] **Step 1: Write the failing test**

In `PackManifest.test.cpp`, after the existing cases (the file's helper `manifestWith(member, value)` builds the good
manifest, id `demo`, `provides.nodeTypes` `["rp.demo.tone"]`, with one member replaced, or dropped for an empty value):

```cpp
namespace {

// The good manifest (manifestWith's base, rp.demo.tone declared) with `halves` replaced and a `script` member added
// (none for an empty one).
std::string scriptManifest(const std::string& halves, const std::string& script) {
    std::string m = manifestWith("halves", halves);
    m.pop_back();  // the closing brace
    if (!script.empty()) m += R"(,"script":)" + script;
    return m + "}";
}

std::string refusedFor(const std::string& manifest) {
    std::string why;
    const auto m = parsePackManifest(manifest, &why, std::nullopt);
    return m ? std::string("<parsed>") : why;
}

}  // namespace

TEST_CASE("a script half names its runtime, its heap and its types, each declared", "[packs-folders]") {
    const std::string half = R"({"script":"js/script.js"})";
    const std::string member = R"({"runtime":"js/script.device.js","heapBytes":24576,"nodeTypes":["rp.demo.tone"]})";
    std::string why;
    const auto good = parsePackManifest(scriptManifest(half, member), &why, std::nullopt);
    INFO(why);
    REQUIRE(good);
    REQUIRE(good->script);
    CHECK(good->script->runtime == "js/script.device.js");
    CHECK(good->script->heapBytes == 24576u);
    CHECK(good->script->nodeTypes == std::vector<std::string>{"rp.demo.tone"});
    CHECK(good->halves.at("script") == "js/script.js");

    using Catch::Matchers::ContainsSubstring;
    CHECK_THAT(refusedFor(scriptManifest(half, "")),
               ContainsSubstring("halves.script needs the script member"));
    CHECK_THAT(refusedFor(scriptManifest(R"({"native":"rp-demo"})", member)),
               ContainsSubstring("script is set, and halves names no script half"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"heapBytes":24576,"nodeTypes":["rp.demo.tone"]})")),
               ContainsSubstring("script.runtime is required"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"runtime":"../x.js","heapBytes":24576,"nodeTypes":["rp.demo.tone"]})")),
               ContainsSubstring("script.runtime is \"../x.js\", which is not a path inside the pack"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"runtime":"js/r.js","nodeTypes":["rp.demo.tone"]})")),
               ContainsSubstring("script.heapBytes is required"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"runtime":"js/r.js","heapBytes":0,"nodeTypes":["rp.demo.tone"]})")),
               ContainsSubstring("script.heapBytes is 0"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"runtime":"js/r.js","heapBytes":24576,"nodeTypes":[]})")),
               ContainsSubstring("script.nodeTypes is empty"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"runtime":"js/r.js","heapBytes":24576,"nodeTypes":["rp.demo.other"]})")),
               ContainsSubstring("script.nodeTypes names rp.demo.other, which provides.nodeTypes does not declare"));
    CHECK_THAT(refusedFor(scriptManifest(half, R"({"runtime":"js/r.js","heapBytes":24576,"nodeTypes":["rp.demo.tone"],"heap":1})")),
               ContainsSubstring("script holds \"heap\", which is no member of it"));
}
```

(`manifestWith` writes a JSON object ending in its closing brace, so the member is appended before it. The file
already includes `<catch2/matchers/catch_matchers_string.hpp>` and has `using Catch::Matchers::ContainsSubstring;` at
the top, which makes the local `using` redundant: drop it.)

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-folders]"`
Expected: FAIL to compile: `PackManifest` has no member `script`.

- [ ] **Step 3: Implement**

In `PackManifest.hpp`, before `struct PackManifest`:

```cpp
// A script half's member (docs/downloadable-packs-m2d-plan.md): a device script pack, its whole pack the file
// halves.script names, compiled with its runtime when the pack loads.
struct ScriptSpec {
    std::string              runtime;         // the device's runtime of the pack: a file inside the version folder
    std::uint32_t            heapBytes = 0;   // the package manifest's heapBytes: the box's context (MqjsHost.hpp)
    std::vector<std::string> nodeTypes;       // the ids its definitions register, in their order; each in provides
};
```

and in `struct PackManifest`, after `halves`:

```cpp
    // The script half's runtime, heap and types: present exactly when halves names a script half.
    std::optional<ScriptSpec> script;
```

Reword the header comment's sentence about the halves: "What the loader (PackTable) enforces beyond the shape is the
native half, the script half (compiled and registered when the pack loads), provides.nodeTypes and that each half's
file is there; it refuses the protocol half, which M2e loads."

In `PackManifest.cpp`, in `namespace detail::manifest`:

```cpp
// `script` is closed as requires and provides are: a misspelt member would otherwise drop a field without a word.
struct RawScript {
    std::optional<std::string>              runtime;
    std::optional<std::int64_t>             heapBytes;
    std::optional<std::vector<std::string>> nodeTypes;
    rfl::ExtraFields<rfl::Generic>          extra;
};
```

and `std::optional<RawScript> script;` as `RawManifest`'s last member. Then, in `parsePackManifest`, after the halves are
copied into `m.halves` and before the `halves names no half` check:

```cpp
    // The largest heap a script half may declare: far past any box's (a box has 264 KiB of RAM), small enough that the
    // desktop's context (desktopContextBytes: four times it at 64 bits) stays a sane allocation.
    constexpr std::int64_t kMaxScriptHeap = std::int64_t{16} << 20;
    const bool scriptHalf = m.halves.count("script") > 0;
    if (const auto& s = raw->script) {
        if (!scriptHalf) {
            refuse(why, "script is set, and halves names no script half");
            return std::nullopt;
        }
        if (const auto& extra = s->extra; !extra.empty()) {
            refuse(why, "script holds \"" + echoed(extra.begin()->first) + "\", which is no member of it");
            return std::nullopt;
        }
        ScriptSpec spec;
        if (!s->runtime || s->runtime->empty()) {
            refuse(why, "script.runtime is required (the device runtime's file)");
            return std::nullopt;
        }
        if (!isInsidePath(*s->runtime)) {
            refuse(why, "script.runtime is \"" + echoed(*s->runtime) + "\", which is not a path inside the pack");
            return std::nullopt;
        }
        spec.runtime = *s->runtime;
        if (!s->heapBytes) {
            refuse(why, "script.heapBytes is required (the box's context, in bytes)");
            return std::nullopt;
        }
        if (*s->heapBytes < 1 || *s->heapBytes > kMaxScriptHeap) {
            refuse(why, "script.heapBytes is " + std::to_string(*s->heapBytes) + ", outside 1.." +
                            std::to_string(kMaxScriptHeap));
            return std::nullopt;
        }
        spec.heapBytes = static_cast<std::uint32_t>(*s->heapBytes);
        if (!checkList(s->nodeTypes, "script.nodeTypes", false, spec.nodeTypes, why)) return std::nullopt;
        if (spec.nodeTypes.empty()) {
            refuse(why, "script.nodeTypes is empty: a script half registers node types");
            return std::nullopt;
        }
        for (const auto& t : spec.nodeTypes) {
            if (std::find(m.providesNodeTypes.begin(), m.providesNodeTypes.end(), t) == m.providesNodeTypes.end()) {
                refuse(why, "script.nodeTypes names " + echoed(t) + ", which provides.nodeTypes does not declare");
                return std::nullopt;
            }
        }
        m.script = std::move(spec);
    } else if (scriptHalf) {
        refuse(why, "halves.script needs the script member (its runtime, heapBytes and nodeTypes)");
        return std::nullopt;
    }
```

(`<algorithm>` is included already.)

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: every case passes except `[packs-loader]`'s `packfixture-script` expectation, which now sees the parse's
reason ("halves.script needs the script member") rather than M1's: change that `checkRefused` line in
`PackLoader.test.cpp` (around line 200) to expect `"halves.script needs the script member"`, keep the protocol line,
and change the comment above them to "The protocol half is M2e's: a pack with one is refused whole; a script half needs
its member (docs/downloadable-packs-m2d-plan.md).". In `test/packs/fixture/CMakeLists.txt`'s header, change
`packfixture-script`'s line to `a script half with no script member`. Run again: `All tests passed`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/packs/PackManifest.hpp packages/native/src/host/packs/PackManifest.cpp \
  packages/native/test/packs/PackManifest.test.cpp packages/native/test/packs/PackLoader.test.cpp \
  packages/native/test/packs/fixture/CMakeLists.txt
git commit -m "packs: pack.json's script member names a script half's runtime, heap and node types"
```

---

### Task 3: The loader compiles and registers a script half

**Files:**
- Create: `packages/native/src/host/packs/PackScript.hpp`, `packages/native/src/host/packs/PackScript.cpp`
- Create: `packages/native/test/packs/script-fixture/CMakeLists.txt`, its variants' `pack.json.in` files and
  `variants/scriptfix-syntax/js/*.js`
- Modify: `packages/native/src/host/packs/PackTable.hpp`, `packages/native/src/host/packs/PackTable.cpp`,
  `packages/native/CMakeLists.txt` (`retroplug-packs`), `packages/native/cmake/tests.cmake`,
  `packages/native/src/host/js/PackModules.cpp`, `packages/retroplug/src/packs/packHost.ts` (a comment)
- Test: `packages/native/test/packs/PackScript.test.cpp`, `packages/native/test/plugin/PackModules.test.cpp`

- [ ] **Step 1: Stage the script fixture packs**

The fixture is devtest's script pack (`packages/devtest`, listed by no product, so no built-in holds its types in
`retroplug-packs-test`'s packs-host product), its two ES5 bundles copied out of the toolchain's output. Its types, in
their order (`build/packages/devtest/script/devtest.pack64.c`'s `typeIds`): `rp.devtest.script-transpose`,
`rp.devtest.script-nest`, `rp.devtest.script-loop`; its `heapBytes` is 57344.

Create `packages/native/test/packs/script-fixture/scriptfix.pack.json.in`:

```json
{
  "id": "@FIX_ID@", "name": "Script fixture pack (@FIX_ID@)", "version": "1.0.0", "apiLevel": @RP_API_LEVEL@,
  "license": "MIT", "source": "packages/native/test/packs/script-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "roles": [],
                "nodeTypes": [@FIX_PROVIDES@] },
  "halves": { "script": "js/script.js" },
  "script": { "runtime": "@FIX_RUNTIME@", "heapBytes": 57344, "nodeTypes": [@FIX_TYPES@] }
}
```

and `packages/native/test/packs/script-fixture/CMakeLists.txt`:

```cmake
# The script fixture packs (docs/downloadable-packs-m2d-plan.md): devtest's device script pack as a pack's script half,
# its two ES5 bundles copied from the device-pack toolchain's output (retroplug-devtest-script-pack), and its broken
# variants, each staged under build/packs/<id>/1.0.0/ as a development pack. No product lists any of them.
#
#   scriptfix         the good pack: devtest's three types
#   scriptfix-twin    the same script under another id: its types are scriptfix's, so it is refused after it
#   scriptfix-types   declares two of the three types: the host's control context finds the third and refuses it
#   scriptfix-syntax  a whole pack that does not parse
#   scriptfix-nofile  a runtime file the pack does not have
set(_sf_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_sf_from "${CMAKE_BINARY_DIR}/packages/devtest/script")
set(_sf_all "\"rp.devtest.script-transpose\", \"rp.devtest.script-nest\", \"rp.devtest.script-loop\"")
set(_sf_two "\"rp.devtest.script-transpose\", \"rp.devtest.script-nest\"")

function(_sf_claim id)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${id}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the script fixture pack ${id}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${id}/1.0.0")
endfunction()

# One variant's pack.json, and (COPY) devtest's two bundles as its script half.
set(_sf_outputs "")
function(_sf_variant id)
    cmake_parse_arguments(V "COPY" "PROVIDES;TYPES;RUNTIME" "" ${ARGN})
    _sf_claim(${id})
    set(FIX_ID "${id}")
    set(FIX_PROVIDES "${V_PROVIDES}")
    set(FIX_TYPES "${V_TYPES}")
    set(FIX_RUNTIME "${V_RUNTIME}")
    set(_out "${RP_PACKS_DIR}/${id}/1.0.0")
    configure_file("${_sf_dir}/scriptfix.pack.json.in" "${_out}/pack.json" @ONLY)
    if(V_COPY)
        add_custom_command(
            OUTPUT "${_out}/js/script.js" "${_out}/js/script.device.js"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}/js"
            COMMAND ${CMAKE_COMMAND} -E copy "${_sf_from}/devtest.js" "${_out}/js/script.js"
            COMMAND ${CMAKE_COMMAND} -E copy "${_sf_from}/devtest.device.js" "${_out}/js/script.device.js"
            DEPENDS retroplug-devtest-script-pack "${_sf_from}/devtest.js" "${_sf_from}/devtest.device.js"
            COMMENT "Staging the script fixture pack ${id}"
            VERBATIM)
        set(_sf_outputs ${_sf_outputs} "${_out}/js/script.js" "${_out}/js/script.device.js" PARENT_SCOPE)
    endif()
endfunction()

_sf_variant(scriptfix COPY PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/script.device.js")
_sf_variant(scriptfix-twin COPY PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/script.device.js")
_sf_variant(scriptfix-types COPY PROVIDES "${_sf_two}" TYPES "${_sf_two}" RUNTIME "js/script.device.js")
_sf_variant(scriptfix-nofile COPY PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/missing.js")
_sf_variant(scriptfix-syntax PROVIDES "${_sf_all}" TYPES "${_sf_all}" RUNTIME "js/script.device.js")
foreach(_js script.js script.device.js)
    configure_file("${_sf_dir}/variants/scriptfix-syntax/js/${_js}" "${RP_PACKS_DIR}/scriptfix-syntax/1.0.0/js/${_js}"
                   COPYONLY)
endforeach()

add_custom_target(retroplug-script-fixture-packs DEPENDS ${_sf_outputs})
```

Create `packages/native/test/packs/script-fixture/variants/scriptfix-syntax/js/script.js` and `script.device.js`, each:

```js
// A script half that does not parse (docs/downloadable-packs-m2d-plan.md): the loader refuses it, naming the file.
var x = ;
```

In `packages/native/cmake/tests.cmake`, beside the two fixture `add_subdirectory` lines:

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test/packs/script-fixture ${CMAKE_BINARY_DIR}/packs-script-fixture
                 EXCLUDE_FROM_ALL)
```

and after `retroplug-packs-test`'s definition: `add_dependencies(retroplug-packs-test retroplug-script-fixture-packs)`.
(`RP_API_LEVEL` and `RP_PACKS_DIR` are the variables `ts-fixture/CMakeLists.txt` reads; if the `.in` substitution of
`RP_API_LEVEL` needs it in scope under another name, follow the ts-fixture's `pack.json.in`.)

- [ ] **Step 2: Write the failing tests**

Append to `PackScript.test.cpp` (add `#include <catch2/matchers/catch_matchers_string.hpp>`, `"GraphDocs.hpp"`,
`"PackTestSupport.hpp"`, `"graph/Packages.hpp"`, `"host/engine/Engine.hpp"`, `"host/packs/PackTable.hpp"`):

```cpp
using Catch::Matchers::ContainsSubstring;
using rp::host::packs::PackLoad;
using rp::host::packs::PackTable;
using rp::host::packs::test::fixture;

TEST_CASE("a script half compiles at load and its package joins the pack's", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(fixture("scriptfix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->packages.size() == 1);
    const rp_package* p = load.pack->packages[0];
    REQUIRE(p->node_type_count == 3);
    CHECK(std::string(p->node_types[0]->id) == "rp.devtest.script-transpose");
    CHECK(std::string(p->node_types[2]->id) == "rp.devtest.script-loop");
    CHECK(load.pack->packageIds == std::vector<std::string>{"rp.scriptfix.script"});
}

TEST_CASE("a script half is refused, and the pack with it, for each fault", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(fixture("scriptfix")));
    const auto refused = [&](const char* id, const char* reason) {
        INFO(id);
        const PackLoad load = table.load(fixture(id));
        INFO(load.error);
        CHECK_FALSE(load);
        CHECK_THAT(load.error, ContainsSubstring(reason));
        CHECK(table.find(id) == nullptr);
    };
    refused("scriptfix-twin", "registers node type rp.devtest.script-transpose, which package rp.scriptfix.script "
                              "has registered already");
    refused("scriptfix-types", "its script half is refused");
    refused("scriptfix-syntax", "its script half (js/script.js) does not compile: scriptfix-syntax.js");
    refused("scriptfix-nofile", "script.runtime names js/missing.js, which the pack does not have");
}

TEST_CASE("every Engine registers a loaded script half, and a document places its types", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    PackTable none(rp::graph::productPackages());
    REQUIRE(table.load(fixture("scriptfix")));
    Engine bare(48000.0, none);
    Engine e(48000.0, table);
    CHECK(e.nodeTypeCount() == bare.nodeTypeCount() + 3);
    const std::string doc =
        R"({"nodes":[{"id":"in","type":"rp.host.midi-in"},{"id":"t","type":"rp.devtest.script-transpose",)"
        R"("settings":{"semitones":12}},{"id":"out","type":"rp.host.midi-out"}],)"
        R"("wires":[{"from":"in.midi","to":"t.in"},{"from":"t.out","to":"out.midi"}]})";
    const auto placed = e.setGraph(rp::host::test::graphRequest(doc, {}));
    INFO((placed.errors.empty() ? std::string() : placed.errors[0].message));
    CHECK(placed.ok);
}
```

The port names (`in.midi`, `out.midi`) and the request helper follow the tree's: read `test/engine-graph/GraphDocs.hpp`
and a case that wires `rp.host.midi-in` / `rp.host.midi-out` (grep the engine-graph tests) and use their spellings. The
refusal texts are the ones Step 3 writes; where `registerPack`'s own refusal is quoted after "its script half is
refused: ", the test checks only that prefix.

In `packages/native/test/plugin/PackModules.test.cpp`'s "a half the pack does not have" section, the fixture `pmgood`
has no script half, so a script import already fails at the lookup; the loader's refusal is checked by reading (one
line in Step 3).

- [ ] **Step 3: Run them to see them fail**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-script]"`
Expected: FAIL: the fixtures load as "has a script half (js/script.js), which this RetroPlug does not load yet".

- [ ] **Step 4: Implement**

Create `packages/native/src/host/packs/PackScript.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "graph/rp_node.h"
#include "host/packs/PackManifest.hpp"
#include "script/rp_mqjs_pack.h"

// A pack's script half (docs/downloadable-packs-m2d-plan.md): a device script pack, shipped as the two ES5 bundles the
// device-pack toolchain writes (the whole pack, halves.script, and the device's runtime, script.runtime), compiled to
// MicroQuickJS bytecode here and registered on the MicroQuickJS host (rp::mqjs::package) as a built-in device pack's
// generated registration does. What it holds is pointed at by that registration for the life of the process, so a
// ScriptHalf is never freed once compiled (PackTable keeps every one).
namespace rp::host::packs {

struct ScriptHalf {
    std::string               name;      // the pack's id: the MicroQuickJS pack's name, its package "rp.<id>.script"
    std::vector<std::uint8_t> runtime;   // the device runtime's bytecode at this build's width
    std::vector<std::uint8_t> control;   // the whole pack's, which the desktop's control context runs
    std::vector<std::string>  typeIds;   // script.nodeTypes, in their order
    std::vector<const char*>  typeIdPtrs;
    rp_mqjs_pack_source       source{};  // what package() was handed: points into the members above
    const rp_package*         package = nullptr;  // set once registered
};

// The pack's script half read from `dir` and compiled (null, with the reason in *why, for a file it cannot read or a
// bundle that does not compile). Not registered yet: registerScriptHalf does that, once the caller holds the half.
std::unique_ptr<ScriptHalf> compileScriptHalf(const std::filesystem::path& dir, const PackManifest& manifest,
                                              std::string* why);

// Register a compiled half on the MicroQuickJS host, which loads its whole pack in a control context and refuses one
// whose definitions are not its type ids. False with the host's reason in *why.
bool registerScriptHalf(ScriptHalf& half, std::string* why);

}  // namespace rp::host::packs
```

Create `packages/native/src/host/packs/PackScript.cpp`:

```cpp
#include "host/packs/PackScript.hpp"

#include <fstream>
#include <iterator>
#include <optional>

#include "host/packs/PackDirs.hpp"
#include "script/MqjsHost.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

std::optional<std::string> readText(const fs::path& file, std::string* why) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (why) *why = "cannot read " + displayPath(file);
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

std::unique_ptr<ScriptHalf> compileScriptHalf(const fs::path& dir, const PackManifest& manifest, std::string* why) {
    const ScriptSpec& spec = *manifest.script;
    const std::string& wholeFile = manifest.halves.at("script");
    const auto whole = readText(dir / fs::path(wholeFile), why);
    if (!whole) return nullptr;
    const auto runtime = readText(dir / fs::path(spec.runtime), why);
    if (!runtime) return nullptr;
    // The names the device-pack toolchain compiles the two bundles under (the bytecode records its file's name), so a
    // compile error names the file as the toolchain's would.
    const std::string wholeName = manifest.id + ".js";
    const std::string runtimeName = manifest.id + ".device.js";
    std::string err;
    auto control = rp::mqjs::compileScript(*whole, wholeName.c_str(), false, &err);
    if (!control) {
        if (why) *why = "its script half (" + wholeFile + ") does not compile: " + err;
        return nullptr;
    }
    auto device = rp::mqjs::compileScript(*runtime, runtimeName.c_str(), false, &err);
    auto box = device ? rp::mqjs::compileScript(*runtime, runtimeName.c_str(), true, &err) : std::nullopt;
    if (!device || !box) {
        if (why) *why = "its script half's runtime (" + spec.runtime + ") does not compile: " + err;
        return nullptr;
    }
    auto half = std::make_unique<ScriptHalf>();
    half->name = manifest.id;
    half->runtime = std::move(*device);
    half->control = std::move(*control);
    half->typeIds = spec.nodeTypes;
    for (const auto& t : half->typeIds) half->typeIdPtrs.push_back(t.c_str());
    half->source = rp_mqjs_pack_source{half->name.c_str(),
                                       half->runtime.data(),
                                       static_cast<std::uint32_t>(half->runtime.size()),
                                       spec.heapBytes,
                                       static_cast<std::uint32_t>(half->typeIdPtrs.size()),
                                       half->typeIdPtrs.data(),
                                       static_cast<std::uint32_t>(box->size()),
                                       half->control.data(),
                                       static_cast<std::uint32_t>(half->control.size())};
    return half;
}

bool registerScriptHalf(ScriptHalf& half, std::string* why) {
    half.package = rp::mqjs::package(half.source);
    if (half.package == nullptr) {
        if (why) *why = std::string("its script half is refused: ") + rp::mqjs::packageRefusal();
        return false;
    }
    return true;
}

}  // namespace rp::host::packs
```

(`displayPath` is `PackDirs.hpp`'s, as `PackTable.cpp` uses it; if the field order of `rp_mqjs_pack_source` in
`script/rp_mqjs_pack.h` differs from the initializer's, assign the members by name.)

In `packages/native/CMakeLists.txt`, add `src/host/packs/PackScript.cpp` to `retroplug-packs`'s sources and
`retroplug-mqjs-host` to its `PUBLIC` links (`target_link_libraries(retroplug-packs PUBLIC retroplug-graph
retroplug-mqjs-host PRIVATE ${CMAKE_DL_LIBS})`). The native package already declares `@retroplug/graph-runtime`, which
owns `retroplug-mqjs-host`, so `pnpm check:boundaries` and the configure's boundary check pass; if the configure
refuses the link, follow its message.

In `PackTable.hpp`, include `"host/packs/PackScript.hpp"`, add to `LoadedPack` after `packages`:

```cpp
    const ScriptHalf*              script = nullptr;  // its script half, compiled and registered; the table owns it
```

and to `PackTable`'s private members:

```cpp
    // Every script half this table compiled, loaded or refused: the MicroQuickJS host keeps a pointer to each one it
    // registered for the life of the process (and compares halves by address), so none is ever freed.
    std::vector<std::unique_ptr<ScriptHalf>> scripts_;
```

In `PackTable.cpp`'s `loadLocked`:

1. In the halves loop, the script half is no longer refused; only the protocol half is, with the same message (its
   comment: "The protocol half is M2e's, and a pack loads whole or not at all."). After the loop:

   ```cpp
    // A script half's runtime is a file of its own (the loop above checked the whole pack's).
    if (manifest->script) {
        std::error_code ec;
        if (!fs::is_regular_file(candidate.dir / fs::path(manifest->script->runtime), ec))
            return refuseHere(id, pack + ": script.runtime names " + manifest->script->runtime +
                                      ", which the pack does not have");
    }
   ```

2. Move the per-package checks out of the native block, so the script half's package meets the same ones. The native
   block keeps the library, the entry, its level and `loaded->packages.assign(list, list + count)`. After the native
   block, add the script half:

   ```cpp
    // The script half: compiled now, then registered on the MicroQuickJS host, which loads the whole pack in a control
    // context and refuses definitions that are not its type ids. Held by the table from its registration on, loaded or
    // refused (registerPack keeps a pointer to it).
    if (manifest->script) {
        auto half = compileScriptHalf(candidate.dir, *manifest, &why);
        if (!half) return refuseHere(id, pack + ": " + why);
        ScriptHalf& held = *half;
        scripts_.push_back(std::move(half));
        if (!registerScriptHalf(held, &why)) return refuseHere(id, pack + ": " + why);
        loaded->packages.push_back(held.package);
        loaded->script = &held;
    }
   ```

   Then the checks that were inside the native block run over `loaded->packages` whatever brought them: the `holder`
   map (built-ins, then every loaded pack's packages), the null-package and ABI skips, the declared-type and held-type
   refusals (filling `registered`), and the scratch registry's trial with its `packageIds`. Their messages stay as they
   are ("registers node type X, which its pack.json's provides.nodeTypes does not declare", "... which package Y has
   registered already"). The `provides` truthfulness loop after it is unchanged and now counts the script half's types.

   Order: a pack's native packages first, then its script package, which is the order an Engine registers them in.

3. The header comment of `LoadedPack::packages` becomes "what rp_pack_entry returned, in its order, then its script
   half's package; empty with neither".

In `packages/native/src/host/js/PackModules.cpp`'s `load`, the script half is MicroQuickJS code, never a module:

```cpp
    if (!pack || half == "native" || half == "script") return notFound(ctx, name);
```

and the header comment of `installPackLoader` in `PackModules.hpp` says "(the native and script halves are no
modules ...)" where it says "(the native half is no module ...)".

In `packages/retroplug/src/packs/packHost.ts`, `HalfKind`'s comment becomes: "The halves a context may load (the
protocol half is M2e's; the script half is MicroQuickJS code the native loader registers, never a context's)."

- [ ] **Step 5: Run them to see them pass**

Run: `cmake --build build --target retroplug-packs-test retroplug-plugin-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed`, exit 0 (every tag: the loader's other refusals keep their messages). Then
`pnpm test:plugin plugin-test` for `[pack-modules]`: exit 0.

- [ ] **Step 6: Commit**

```bash
git add packages/native/src/host/packs/PackScript.hpp packages/native/src/host/packs/PackScript.cpp \
  packages/native/src/host/packs/PackTable.hpp packages/native/src/host/packs/PackTable.cpp \
  packages/native/CMakeLists.txt packages/native/cmake/tests.cmake packages/native/test/packs/PackScript.test.cpp \
  packages/native/test/packs/script-fixture packages/native/src/host/js/PackModules.cpp \
  packages/native/src/host/js/PackModules.hpp packages/retroplug/src/packs/packHost.ts
git commit -m "packs: a script half compiles at load and registers in every Engine as a device pack"
```

---

### Task 4: Building a package's script half as a pack's

**Files:**
- Modify: `packages/retroplug/scripts/build-device-pack.mjs` (one output), `cmake/packages.cmake` (its byproduct)
- Modify: `tools/build-pack-halves.mjs`
- Test: `packages/retroplug/test/script/build-pack-halves.test.ts`; Create
  `packages/native/test/packs/script-fixture/bliptoaster.pack.json`

- [ ] **Step 1: Write the failing test**

Create the template `packages/native/test/packs/script-fixture/bliptoaster.pack.json` (Task 5 stages it too):

```json
{
  "id": "bliptoaster", "name": "BlipToaster (script fixture)", "version": "1.0.0", "apiLevel": 0,
  "license": "MIT", "source": "packages/bliptoaster",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": ["rp.bliptoaster.midi-policy"], "roles": [] },
  "halves": {}
}
```

In `packages/retroplug/test/script/build-pack-halves.test.ts`, beside its cases (it already spawns the tool through
`tjs.spawn` with the runner's node, `RP_NODE`; reuse its helpers, and the temporary-folder helper its cases use):

```ts
test("a package's device script pack becomes the pack's script half: the two ES5 bundles and the script member", async () => {
  const out = await tempDir();
  const run = await runTool([
    "packages/bliptoaster", out, "packages/native/test/packs/script-fixture/bliptoaster.pack.json",
    "--only", "script",
  ]);
  expect(run.status, run.stderr).toBe(0);
  const pack = JSON.parse(await readText(`${out}/pack.json`));
  expect(pack.halves).toEqual({ script: "js/script.js" });
  expect(pack.script).toEqual({ runtime: "js/script.device.js", heapBytes: 24576, nodeTypes: ["rp.bliptoaster.midi-policy"] });
  // The bundles are the toolchain's ES5: no module syntax, the strict directive first.
  const whole = await readText(`${out}/js/script.js`);
  expect(whole.startsWith('"use strict"')).toBe(true);
  expect(/\bexport\b|\bimport\b/.test(whole)).toBe(false);
  expect((await readText(`${out}/js/script.device.js`)).length).toBeGreaterThan(0);
});

test("a script half whose types the template does not declare is refused, naming the type", async () => {
  const out = await tempDir();
  const template = `${out}/template.json`;
  await writeText(template, JSON.stringify({
    id: "bliptoaster", name: "x", version: "1.0.0", apiLevel: 0, license: "MIT", source: "x",
    requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: [], roles: [] }, halves: {},
  }));
  const run = await runTool(["packages/bliptoaster", `${out}/v`, template, "--only", "script"]);
  expect(run.status).toBe(1);
  expect(run.stderr).toContain("the script half defines rp.bliptoaster.midi-policy, which the template's provides.nodeTypes does not declare");
});
```

`runTool`, `tempDir`, `readText` and `writeText` stand for what the file has: if it has no file read or write helper,
declare `tjs.readFile` / `tjs.writeFile` locally as `test/graph/device-pack.test.ts` does, and decode with
`TextDecoder`. The tool runs from the repository root, as the file's other cases run it. The `"use strict"` check
follows `build-device-pack.mjs`'s `STRICT` prefix; if its spelling differs (a single-quoted directive, a comment first),
match the tool's.

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test script/build-pack-halves`
Expected: FAIL: the tool ignores `--only` and the template's `halves` stays empty (no script half built).

- [ ] **Step 3: Implement**

In `packages/retroplug/scripts/build-device-pack.mjs`, beside the other `keep(...)` outputs at the end:

```js
// The ids the definitions register, in their order, as JSON: what a pack's script member lists (tools/build-pack-halves.mjs).
keep(join(OUT_DIR, `${NAME}.types.json`), `${JSON.stringify(typeIds)}\n`);
```

(If `keep` takes bytes only, pass `Buffer.from(...)`.) In `cmake/packages.cmake`'s `retroplug_device_script_pack`, add
`"${_out}/${name}.types.json"` to the custom target's `BYPRODUCTS`.

In `tools/build-pack-halves.mjs`:

- The header's usage line becomes
  `node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template> [--only <half,...>] [--mqjs <path>] [--runner <path>]`,
  and the header gains: "A package's `retroplug.script`, a device script pack, becomes the script half: the device-pack
  toolchain (packages/retroplug/scripts/build-device-pack.mjs, with its bundling, ES5 lowering, library-call scan and
  transcript run) builds it under the pack's id, and the pack ships its two ES5 bundles (js/script.js, the whole pack;
  js/script.device.js, the device's runtime), which the host compiles to bytecode when it loads the pack, and a `script`
  member naming the runtime, the heap and the types. `--only` builds the halves it names alone."
- Imports: `copyFileSync`, `mkdtempSync`, `rmSync` from `node:fs`, `tmpdir` from `node:os`, `spawnSync` from
  `node:child_process`.
- `buildPackHalves({ packageDir, outDir, template, only, mqjs, runner })`: a half is built when `!only ||
  only.includes(half)`, for the TS halves and for `script`. After the TS halves:

  ```js
  if ((!only || only.includes("script")) && block.script !== undefined) {
    halves.script = buildScriptHalf({ packageDir, outDir, pack, script: block.script, mqjs, runner });
  }
  ```

  and:

  ```js
  /** The package's device script pack as the pack's script half (see the header). Returns the whole pack's file. */
  function buildScriptHalf({ packageDir, outDir, pack, script, mqjs, runner }) {
    if (typeof script !== "object" || script === null || script.device !== true || typeof script.entry !== "string" ||
        !Number.isInteger(script.heapBytes)) {
      throw new Error("retroplug.script is not a device pack ({ entry, device: true, heapBytes }): a desktop script " +
        "pack is reserved");
    }
    const work = mkdtempSync(join(tmpdir(), "rp-pack-script-"));
    try {
      const args = [join(REPO, "packages/retroplug/scripts/build-device-pack.mjs"),
        "--entry", join(packageDir, script.entry), "--name", pack.id, "--heap-bytes", String(script.heapBytes),
        "--out-dir", work, "--pack-dir", packageDir];
      if (mqjs) args.push("--mqjs", mqjs);
      if (runner) args.push("--runner", runner);
      const run = spawnSync(process.execPath, args, { encoding: "utf8" });
      if (run.status !== 0) {
        throw new Error(`the device-pack toolchain refused ${pack.id}'s script half:\n${run.stderr}${run.stdout}`);
      }
      const types = JSON.parse(readFileSync(join(work, `${pack.id}.types.json`), "utf8"));
      const declared = pack.provides?.nodeTypes ?? [];
      for (const type of types) {
        if (!declared.includes(type)) {
          throw new Error(`the script half defines ${type}, which the template's provides.nodeTypes does not declare`);
        }
      }
      mkdirSync(join(outDir, "js"), { recursive: true });
      copyFileSync(join(work, `${pack.id}.js`), join(outDir, "js", "script.js"));
      copyFileSync(join(work, `${pack.id}.device.js`), join(outDir, "js", "script.device.js"));
      pack.script = { runtime: "js/script.device.js", heapBytes: script.heapBytes, nodeTypes: types };
      return "js/script.js";
    } finally {
      rmSync(work, { recursive: true, force: true });
    }
  }
  ```

- The command line takes the three positional arguments and the three flags (`--only` split on commas; an unknown flag
  or a missing value is the usage error, exit 2) and passes them on.

The template's `apiLevel` is rewritten as before; a `script` member a template carries is replaced when the half is
built and dropped when it is not (`delete pack.script` before the halves are built).

- [ ] **Step 4: Run it to see it pass**

Run: `pnpm test script/build-pack-halves`
Expected: every case passes, exit 0. Then `pnpm test script/device-pack` (the toolchain's own cases, which the new
output must not disturb): exit 0.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/scripts/build-device-pack.mjs cmake/packages.cmake tools/build-pack-halves.mjs \
  packages/retroplug/test/script/build-pack-halves.test.ts packages/native/test/packs/script-fixture/bliptoaster.pack.json
git commit -m "packs: build-pack-halves builds a package's device script pack as the pack's script half"
```

---

### Task 5: BlipToaster's script half as a pack, held to its static build

**Files:**
- Modify: `packages/native/test/packs/script-fixture/CMakeLists.txt`, `packages/native/cmake/tests.cmake`
- Test: `packages/native/test/packs/PackScript.test.cpp`

- [ ] **Step 1: Stage the pack**

In `script-fixture/CMakeLists.txt`, header line `#   bliptoaster       BlipToaster's script half alone, built by tools/build-pack-halves.mjs (M5 builds its whole pack here)`,
and:

```cmake
# BlipToaster's script half, built as a package's is (tools/build-pack-halves.mjs --only script), which compiles it with
# the device-pack toolchain under the pack's id: so the host's compile of it is the static build's bytecode.
_sf_claim(bliptoaster)
set(_bt_out "${RP_PACKS_DIR}/bliptoaster/1.0.0")
file(GLOB_RECURSE _bt_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/packages/bliptoaster/ts/*.ts")
add_custom_command(
    OUTPUT "${_bt_out}/pack.json" "${_bt_out}/js/script.js" "${_bt_out}/js/script.device.js"
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${CMAKE_SOURCE_DIR}/packages/bliptoaster ${_bt_out} ${_sf_dir}/bliptoaster.pack.json --only script
            --mqjs $<TARGET_FILE:mqjs> --runner $<TARGET_FILE:retroplug-mqjs-run>
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_sf_dir}/bliptoaster.pack.json
            ${CMAKE_SOURCE_DIR}/packages/bliptoaster/package.json ${_bt_sources}
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/build-device-pack.mjs
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            mqjs retroplug-mqjs-run
    COMMENT "Building BlipToaster's script half as a pack's"
    VERBATIM)
list(APPEND _sf_outputs "${_bt_out}/pack.json" "${_bt_out}/js/script.js" "${_bt_out}/js/script.device.js")
```

(before the `add_custom_target(retroplug-script-fixture-packs ...)` line, so the target depends on it).

In `tests.cmake`, add `retroplug-bliptoaster-script` to `retroplug-packs-test`'s `LIBS` (BlipToaster's static script
pack, `rp_bliptoaster_script_package`, which no engine of the packs-host product registers until a case hands it over,
as `retroplug-mesen` is linked for `MesenPack.test.cpp`); the comment beside it says so.

- [ ] **Step 2: Write the failing test**

Append to `PackScript.test.cpp`:

```cpp
// BlipToaster's static script pack, as this binary links it (retroplug-bliptoaster-script), beside the pack's.
extern "C" const rp_package* rp_bliptoaster_script_package(void);

TEST_CASE("BlipToaster's script half as a pack filters MIDI exactly as its static build does", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(fixture("bliptoaster"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->script);

    PackTable none(rp::graph::productPackages());
    Engine fromPack(48000.0, table);
    Engine fromStatic(48000.0, none);
    REQUIRE(fromStatic.addNodePackage(rp_bliptoaster_script_package()));
    // host MIDI in -> the policy -> host MIDI out, in both; then one block's worth of messages, each kind it drops or
    // passes: a note on and off, poly aftertouch, a CC, channel aftertouch, a clock, a SysEx, a song position.
    const std::string doc = /* midi-in -> rp.bliptoaster.midi-policy -> midi-out, as the scriptfix case wires one */;
    REQUIRE(fromPack.setGraph(rp::host::test::graphRequest(doc, {})).ok);
    REQUIRE(fromStatic.setGraph(rp::host::test::graphRequest(doc, {})).ok);
    const std::vector<std::vector<std::uint8_t>> messages = {
        {0x90, 60, 100}, {0xA0, 60, 20}, {0xB0, 7, 99}, {0xD0, 40}, {0xF8}, {0xF0, 0x7D, 0x01, 0xF7},
        {0xF2, 0x10, 0x00}, {0x80, 60, 0}};
    for (Engine* e : {&fromPack, &fromStatic}) {
        std::uint32_t frame = 0;
        for (const auto& m : messages) e->stageMidi(frame++, m);
    }
    const auto outOf = [](Engine& e) { /* run one block and return its host MIDI out, each message's frame and bytes */ };
    const auto packOut = outOf(fromPack);
    const auto staticOut = outOf(fromStatic);
    CHECK(packOut == staticOut);
    REQUIRE(packOut.size() == 4);  // the note on, the CC, the SysEx, the note off: aftertouch and the system messages dropped
}
```

The document and `outOf` follow the tree: the scriptfix case's document with the policy's type (its ports are `in` and
`out`, `packages/bliptoaster/ts/index.ts`), and the Engine's host MIDI out read the way the engine-graph cases read one
(grep `rp.host.midi-out` in `packages/native/test/engine-graph/` for the call; `MesenPack.test.cpp` stages host MIDI with
`stageMidi`). If the policy's documented behaviour (`ts/index.ts`'s header: aftertouch, system common but SysEx, and
real time dropped) gives another count, the count follows it; the equality of the two engines is the point.

- [ ] **Step 3: Run it to see it fail, then pass**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-script]"`
Expected before Step 1's staging exists: the case fails on the missing fixture; after it: `All tests passed`, exit 0.
(The pack and the static build are two registrations of one MicroQuickJS pack name, each in its own Engine: no clash.)

- [ ] **Step 4: Commit**

```bash
git add packages/native/test/packs/script-fixture/CMakeLists.txt packages/native/cmake/tests.cmake \
  packages/native/test/packs/PackScript.test.cpp
git commit -m "packs: BlipToaster's script half loads as a pack and filters MIDI as its static build does"
```

---

### Task 6: The render worker is a shared module set's host

**Files:**
- Modify: `tools/build-shared-modules.mjs`, `packages/native/cmake/product-bundles.cmake`,
  `packages/native/src/host/render/RenderHost.hpp`, `packages/native/src/host/render/RenderHost.cpp`,
  `packages/retroplug/src/render/worker.ts`, `packages/retroplug/test/script/bundle-split.mjs`, `CMakeLists.txt` (one
  comment)

No new behaviour: the render worker runs as a module over the `render` set, and every render's bytes stay what they are.
The proof is the existing render suites.

- [ ] **Step 1: Run the render suites to record the baseline**

Run: `pnpm test:native render && cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[engine-jobs]"`
Expected: exit 0 on both (the render goldens: `render-baseline`, `render-chunks`, `render-cli`, and the engine jobs).

- [ ] **Step 2: Implement the set**

In `tools/build-shared-modules.mjs`, `setOf`:

```js
  if (name === "render") {
    // The render worker (src/render/worker.ts): a render job's session, on a bare QuickJS runtime with no txiki
    // (host/render/RenderHost), whose pack halves link to the shell contracts as the CLI's do.
    return {
      hosts: [{ name: "rp:render", entry: path.join(SHELL, "src/render/worker.ts") }],
      contracts: [...new Set(SHELL_IMPORTS.map((e) => e.specifier))],
      external: [],
    };
  }
```

and the error's list of sets and the header's set list gain `render` ("render: the render worker (rp:render) and
SHELL_IMPORTS, for a render job's bare QuickJS runtime").

In `packages/native/cmake/product-bundles.cmake`, replace the render-worker bundle (its comment, the
`retroplug-render-worker-bundle${_suffix}` target and its two dependency lines) with the render set, written as the app
set's block is:

```cmake
        # --- the render worker's shared module set (docs/downloadable-packs-m2d-plan.md): src/render/worker.ts (rp:render)
        # and the shell's contracts, one esbuild code-splitting build whose outputs are named modules, so a render job's
        # bare QuickJS runtime (host/render/RenderHost) holds one copy of each and a pack's app and consoles halves link
        # to them by name. retroplug-modc writes the table (rp_render_modules) and the entry's array (rp_render_bundle).
        # The same sources for every product, built against the product's registries through its generated tree.
        set(_render_dir "${_dir}/render-modules")
        set(_render_c "${_dir}/render-bundle_data.c")
        set(_render_modules_c "${_dir}/render-modules_data.c")
        set_source_files_properties(${_render_c} ${_render_modules_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-render-modules${_suffix}
            BYPRODUCTS ${_render_c} ${_render_modules_c} ${_render_c}.new ${_render_modules_c}.new
                       ${_render_dir}/shared.tsv ${_render_dir}/hosts.tsv
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-shared-modules.mjs render ${_render_dir}
                    ${_product_args}
            COMMAND ${_MODC_COMMAND} table -s rp_render_modules ${_render_dir}/shared.tsv ${_render_modules_c}.new
            COMMAND ${_MODC_COMMAND} array -s rp:render rp_render_bundle ${_render_dir}/js/rp/render.js ${_render_c}.new
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_render_modules_c}.new ${_render_modules_c}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_render_c}.new ${_render_c}
            COMMENT "Building the render worker's shared module set${_suffix} (esbuild split + retroplug-modc)"
            VERBATIM)
        if(TARGET ${_MODC_DEP})
            add_dependencies(retroplug-render-modules${_suffix} ${_MODC_DEP})
        endif()
        add_dependencies(retroplug-render-modules${_suffix} ${_generator_target})
```

The object library takes `${_script_c} ${_render_c} ${_render_modules_c}` and depends on `retroplug-script-bundle${_suffix}
retroplug-render-modules${_suffix}`; `RP_BUNDLE_${name}_RENDER_TARGET` names `retroplug-render-modules${_suffix}`; the
file's header comments that say the render worker is a global-code bundle say it is the render set's host. In the root
`CMakeLists.txt`'s comment at the `_TJSC_COMMAND` line (around line 286), the render worker moves from the tjsc users to
the shared sets ("the shared module sets (app, cli, render)").

- [ ] **Step 3: Implement the host**

In `RenderHost.cpp`, the bundle declarations become the set's (include `"host/js/SharedModules.hpp"`):

```cpp
// The render worker's shared module set (packages/native/cmake/product-bundles.cmake, retroplug-render-modules): the
// shell contracts as a table of named modules, registered before the entry, and the entry, src/render/worker.ts, as
// module bytecode (rp:render) importing them by name. Bare QuickJS has no module loader of txiki's: a registered module
// is found by its name, and a pack's half through the bare pack loader (host/js/PackModules).
extern "C" {
extern const rp_js_module  rp_render_modules[];
extern const std::uint32_t rp_render_modules_count;
extern const std::uint8_t  rp_render_bundle[];
extern const std::uint32_t rp_render_bundle_size;
}
```

In `runWith`, the `else` branch (no test `source`) becomes:

```cpp
    } else {
        // The set's contract modules first, each unevaluated, dependencies first: the entry's imports resolve to them.
        const std::string why = rp::host::registerSharedModules(ctx_, rp_render_modules, rp_render_modules_count);
        if (!why.empty()) {
            result_.status = "error";
            result_.message = "render worker: " + why;
            return result_;
        }
        JSValue mod = JS_ReadObject(ctx_, rp_render_bundle, rp_render_bundle_size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(mod)) {
            dumpError(ctx_);
            result_.status = "error";
            result_.message = "render worker bytecode read failed";
            return result_;
        }
        if (JS_ResolveModule(ctx_, mod) < 0) {
            dumpError(ctx_);
            JS_FreeValue(ctx_, mod);
            result_.status = "error";
            result_.message = "render worker's imports did not resolve";
            return result_;
        }
        // A module's evaluation answers a promise, which the job loop below settles: a throw in the module's body
        // rejects it rather than returning an exception here.
        res = JS_EvalFunction(ctx_, mod);
        if (!JS_IsException(res)) entry = JS_DupValue(ctx_, res);
    }
```

with `JSValue entry = JS_UNDEFINED;` declared beside `res`. The job loop's error branch no longer reports an interrupt
as a script error:

```cpp
    JSContext* jc = nullptr;
    while (!interrupted_) {
        const int r = JS_ExecutePendingJob(rt_, &jc);
        if (r == 0) break;
        if (r < 0) {
            if (interrupted_) JS_FreeValue(jc ? jc : ctx_, JS_GetException(jc ? jc : ctx_));
            else dumpError(jc ? jc : ctx_);
            break;
        }
    }
```

and after it, before the `interrupted_` check, the entry's outcome:

```cpp
    // A throw in the entry module's own body (the worker's failure: main catches the render's) rejects its evaluation.
    if (!interrupted_ && JS_IsObject(entry) && JS_PromiseState(ctx_, entry) == JS_PROMISE_REJECTED) {
        JS_Throw(ctx_, JS_PromiseResult(ctx_, entry));
        dumpError(ctx_);
        if (result_.status.empty()) {
            result_.status = "error";
            result_.message = "render worker eval failed";
        }
    }
    JS_FreeValue(ctx_, entry);
```

(If QuickJS-ng's names differ, `JS_PromiseState`, `JS_PromiseResult` and the enum are in `quickjs.h`; follow it.
`dumpError` reads the pending exception, which `JS_Throw` sets.) The existing "render worker did not report a result"
message, for a run that ends with no result, gains why it may happen: "render worker did not report a result (it may
be waiting on something that never settled)". The file's header comment and `RenderHost.hpp`'s say the worker is the
render set's entry, evaluated as a module, rather than global-code bytecode with no module loader. `runScript` keeps
evaluating global code as before.

In `packages/retroplug/src/render/worker.ts`, the header's first lines say it is the render set's entry (`rp:render`,
built by `tools/build-shared-modules.mjs render`) evaluated as a module. Its last line, `main();`, stays: the bundles
target es2020, where esbuild refuses a top-level await, and `main` reports its own result through `__rp_renderResult`.

In `packages/retroplug/test/script/bundle-split.mjs`, the render worker is a set's host now: drop it from `BUNDLES` and
from the product list, and read its closure from the set:

```js
    const render = await buildSharedModules({ set: "render", outDir: unused, productDir, write: false });
    out["render-worker"] = render.closures["rp:render"];
```

(for the desktop and for a product alike); the header comment's list of surfaces says so.

- [ ] **Step 4: Build and run the render suites**

Run: `pnpm configure && cmake --build build -j$(nproc) && pnpm test:native render && build/bin/retroplug-engine-graph-test "[engine-jobs]" && pnpm test script/bundle-ui-split && pnpm test script/bundle-view-package`
Expected: exit 0 on each, the render goldens' bytes unchanged. Then `pnpm test:cli-ts` (its 6d section renders through
the real binary against the golden): exit 0.

- [ ] **Step 5: Commit**

```bash
git add tools/build-shared-modules.mjs packages/native/cmake/product-bundles.cmake \
  packages/native/src/host/render/RenderHost.hpp packages/native/src/host/render/RenderHost.cpp \
  packages/retroplug/src/render/worker.ts packages/retroplug/test/script/bundle-split.mjs CMakeLists.txt
git commit -m "render: the worker is the render module set's entry, evaluated as a module on its bare runtime"
```

---

### Task 7: The pack loader on a bare QuickJS runtime

**Files:**
- Modify: `packages/native/src/host/js/PackModules.hpp`, `packages/native/src/host/js/PackModules.cpp`
- Test: `packages/native/test/plugin/PackModules.test.cpp`

- [ ] **Step 1: Write the failing test**

In `PackModules.test.cpp`, after the txiki cases:

```cpp
namespace {

// A bare QuickJS runtime, as a render job's (host/render/RenderHost): no txiki, no timers, a job queue run here.
struct BareRuntime {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    ~BareRuntime() {
        JS_RunGC(rt);  // as ~RenderHost: a pending half's callbacks go before the modules they hold (bellard/quickjs#280)
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
    void drain() {
        JSContext* jc = nullptr;
        while (JS_ExecutePendingJob(rt, &jc) > 0) {}
    }
    std::string importResult(const std::string& name) {
        const std::string code = "import(" + quoted(name) + ").then((m) => { globalThis.__pm = 'ok ' + "
                                 "JSON.stringify(m.default); }, (e) => { globalThis.__pm = 'error ' + e.message; });";
        JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<bare-test>", JS_EVAL_TYPE_MODULE);
        REQUIRE_FALSE(JS_IsException(v));
        JS_FreeValue(ctx, v);
        drain();
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue s = JS_GetPropertyStr(ctx, g, "__pm");
        const char* c = JS_ToCString(ctx, s);
        std::string out = c ? c : "<none>";
        JS_FreeCString(ctx, c);
        JS_FreeValue(ctx, s);
        JS_SetPropertyStr(ctx, g, "__pm", JS_UNDEFINED);
        JS_FreeValue(ctx, g);
        return out;
    }
};

}  // namespace

TEST_CASE("on a bare runtime a pack's half loads by name, held to the same guard", "[pack-modules]") {
    PackTable table;
    for (const char* id : {"pmgood", "pmforeign", "pmrelative", "pmthrows", "pmpending"})
        REQUIRE(table.load(fixture(id)));
    BareRuntime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx, rp_fx_modules, rp_fx_modules_count).empty());
    const rp_js_module contracts[] = {rp_fx_modules[0], rp_fx_modules[1], {"rp:fx-a", nullptr, 0}};
    rp::host::installBarePackLoader(rt.ctx, table, contracts, 3);

    CHECK(rt.importResult("pack:pmgood/app").rfind("ok {\"half\":\"app\"", 0) == 0);
    CHECK(rt.importResult("pack:pmgood/cli").rfind("error could not load 'pack:pmgood/cli'", 0) == 0);
    CHECK(rt.importResult("pack:pmforeign/app") ==
          "error pack:pmforeign/app imports 'fx:nope', which is not a RetroPlug contract module");
    CHECK(rt.importResult("pack:pmrelative/app") ==
          "error pack:pmrelative/app imports './other.js', which is not a RetroPlug contract module");
    CHECK(rt.importResult("pack:pmthrows/app") == "error the fixture half throws");
    // A name that is neither a pack's nor registered is not found: there is no file loader on a bare runtime.
    CHECK(rt.importResult("fx:dep").rfind("error could not load 'fx:dep'", 0) == 0);
    // The pack list is installed, the refusal hook is not: a render's refusals are its console's.
    JSValue g = JS_GetGlobalObject(rt.ctx);
    JSValue list = JS_GetPropertyStr(rt.ctx, g, "__rp_packList");
    JSValue refused = JS_GetPropertyStr(rt.ctx, g, "__rp_packRefused");
    CHECK(JS_IsFunction(rt.ctx, list));
    CHECK(JS_IsUndefined(refused));
    JS_FreeValue(rt.ctx, list);
    JS_FreeValue(rt.ctx, refused);
    JS_FreeValue(rt.ctx, g);
    // A half left pending when the runtime goes: freed without a fault (the destructor collects first).
    rt.importResult("pack:pmpending/app");
}
```

The fixture ids are the ones the file's txiki case loads (`pmpending` is the fixture whose half never settles; if its
name differs, use the one there). If the fixture modules' `fx:dep` is a module of `rp_fx_modules` (registered, so
found), pick a name no module has, e.g. `fx:absent`, and keep the assertion's meaning.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build --target retroplug-plugin-test -j$(nproc)`
Expected: FAIL to compile: `installBarePackLoader` is not declared.

- [ ] **Step 3: Implement**

In `PackModules.hpp`, after `installPackLoader`:

```cpp
/** installPackLoader for a bare QuickJS runtime, which has no txiki: a render job's (host/render/RenderHost). The same
 *  `pack:<id>/<half>` loader and the same guard, after registerSharedModules registered `contracts` there; any other
 *  name is passed through as written (QuickJS finds a registered module by its name) and is not found if no module has
 *  it, since a bare runtime loads no file. Installs `__rp_packList` as above and no `__rp_packRefused`: a render's
 *  refusals are its job's console lines, the context that loaded those halves first having recorded its own. */
void installBarePackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count);
```

In `PackModules.cpp`, split what the two loaders share and add the bare pair:

- `load`'s body for a `pack:` name becomes `JSModuleDef* loadPackHalf(JSContext* ctx, const char* name, bool txiki)`,
  everything from the prefix parse to the compile; with `txiki` it sets `import.meta` through
  `js_module_set_import_meta` as now, without it it leaves `import.meta` empty (a bare runtime has no txiki
  `import.meta`, and a half's name is no path).
- `load` (txiki's) calls `tjs_module_loader` for a name without the prefix and `loadPackHalf(ctx, name, true)` for one
  with it.
- The bare pair:

  ```cpp
  char* bareNormalize(JSContext* ctx, const char* base, const char* name, void*) {
      if (base && startsWith(base, kPackPrefix) && !isContract(ctx, name)) {
          JS_ThrowReferenceError(ctx, "%s imports '%s', which is not a RetroPlug contract module", base, name);
          return nullptr;
      }
      // A bare runtime's modules import one another by their registered names, and the guard has judged a pack's.
      return js_strdup(ctx, name);
  }

  JSModuleDef* bareLoad(JSContext* ctx, const char* name, void*, JSValueConst) {
      if (startsWith(name, kPackPrefix)) return loadPackHalf(ctx, name, false);
      JS_ThrowReferenceError(ctx, "could not load '%s': no module of this runtime has that name", name);
      return nullptr;
  }
  ```

  (`js_strdup` is QuickJS-ng's, in `quickjs.h`; if it is not exported, `js_malloc` the length plus one and copy.)
- The globals' installation (the frozen contracts object, `__rp_packList`, and for txiki `__rp_packRefused`) becomes
  `installGlobals(JSContext* ctx, const rp_js_module* contracts, std::size_t count, bool refusals)`, called by both.
- `installBarePackLoader`:

  ```cpp
  void installBarePackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts,
                             std::size_t count) {
      g_table.store(&table);
      JS_SetModuleLoaderFunc2(JS_GetRuntime(ctx), bareNormalize, bareLoad, nullptr, nullptr);
      installGlobals(ctx, contracts, count, false);
  }
  ```

(If `JS_SetModuleLoaderFunc2`'s attribute checker may not be null in this QuickJS-ng, pass a function that accepts
every attribute; `tjs_module_attr_checker` is txiki's and is not linked to a bare host's meaning.)

- [ ] **Step 4: Run it to see it pass**

Run: `cmake --build build --target retroplug-plugin-test -j$(nproc) && pnpm test:plugin plugin-test`
Expected: exit 0, `[pack-modules]` included.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/js/PackModules.hpp packages/native/src/host/js/PackModules.cpp \
  packages/native/test/plugin/PackModules.test.cpp
git commit -m "packs: the pack loader and its guard on a bare QuickJS runtime"
```

---

### Task 8: A render job loads the packs' app and consoles halves

**Files:**
- Modify: `packages/retroplug/src/packs/loadPacks.ts`, `packages/retroplug/src/render/worker.ts`,
  `packages/native/src/host/render/RenderHost.cpp`
- Create: `packages/native/test/packs/ts-fixture/render/{package.json,pack.json,ts/app.ts}`
- Modify: `packages/native/test/packs/ts-fixture/CMakeLists.txt`, `packages/native/test/packs/ts-fixture/tsconfig.json`,
  `tools/run-cli-ts-tests.sh`
- Test: `packages/retroplug/test/packs/load-packs.test.ts`, `tools/run-cli-ts-tests.sh`

- [ ] **Step 1: Write the failing TS test**

In `packages/retroplug/test/packs/load-packs.test.ts` (it drives `loadPacks` with a fake `PackHost`; reuse its fake and
its builders):

```ts
test("a runtime with no timers loads its packs with no budget", async () => {
  const g = globalThis as unknown as { setTimeout?: unknown; clearTimeout?: unknown };
  const saved = { setTimeout: g.setTimeout, clearTimeout: g.clearTimeout };
  g.setTimeout = undefined;
  g.clearTimeout = undefined;
  try {
    const host = /* the file's fake host, offering one pack whose app half contributes one role */;
    await loadPacks(host, /* the file's app builders */);
    expect(packStatus().loaded[/* its id */]).toEqual(["app"]);
  } finally {
    g.setTimeout = saved.setTimeout;
    g.clearTimeout = saved.clearTimeout;
  }
});
```

Use a pack id no other case of the file uses (the loader's state is per module set and outlives a case).

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test packs/load-packs`
Expected: FAIL: the pack is refused ("timers.setTimeout is not a function").

- [ ] **Step 3: Implement the TS**

In `loadPacks.ts`, `importBefore`:

```ts
async function importBefore(host: PackHost, id: string, kind: HalfKind, deadline: number): Promise<unknown> {
  // The shell's tsconfig carries no timer types (txiki provides the globals), so they are reached by name.
  const timers = globalThis as unknown as {
    setTimeout?(fn: () => void, ms: number): unknown;
    clearTimeout?(id: unknown): void;
  };
  const imported = host.importHalf(id, kind);
  imported.catch(noteFailedImport);
  // A runtime with no timers (a render job's bare QuickJS) has no budget: a half there has no timer to wait on either,
  // so it settles in the job queue its host runs, or never, which the host reports (host/render/RenderHost).
  if (!timers.setTimeout || !timers.clearTimeout) return imported;
  let timer: unknown;
  const stuck = new Promise<never>((_, reject) => {
    timer = timers.setTimeout!(() => reject(new BudgetError(
      `the packs' load ran past its ${loadBudgetMs / 1000} s budget in its ${kind} half`)), Math.max(0, deadline - Date.now()));
  });
  try {
    return await Promise.race([imported, stuck]);
  } finally {
    timers.clearTimeout!(timer);
  }
}
```

The `loadBudgetMs` comment gains: "A runtime with no timers has none (importBefore)." The file's header gains a line:
"The render worker loads the control plane's halves (`loadAppPacks`) on a bare runtime with no timers
(docs/downloadable-packs-m2d-plan.md)."

In `worker.ts`, `main` becomes async and loads the packs first:

```ts
import { loadAppPacks } from "../packs/loadPacks";
...
async function main(): Promise<void> {
  let opts: RenderOpts;
  try {
    opts = normalize(readSpec());
  } catch (e) {
    __rp_renderResult?.("error", (e as Error)?.message ?? String(e), []);
    return;
  }
  // The packs' app and consoles halves (docs/downloadable-packs-m2d-plan.md), committed before the session composes,
  // so a pack's consoles, roles and ROM providers are this render's as they are the app's. Never rejects: a refused
  // pack is named on the job's console and the render goes on without it.
  await loadAppPacks();
  ...the rest of main as it is
}

// No top-level await (the bundles target es2020): main reports its own result, and a throw it did not catch (the
// session's boot, outside its try) is reported here rather than lost as an unhandled rejection.
main().catch((e) => __rp_renderResult?.("error", (e as Error)?.message ?? String(e), []));
```

and its header names the step: "the worker loads the packs' app and consoles halves, boots a control plane, ...".

- [ ] **Step 4: Run the TS test to see it pass**

Run: `pnpm test packs/load-packs`
Expected: every case passes, exit 0.

- [ ] **Step 5: Wire the host**

In `RenderHost.cpp`, after `registerSharedModules` succeeds in `runWith`'s module branch (include
`"host/js/PackModules.hpp"`; `ProcessPacks.hpp` is included already):

```cpp
        // A pack's halves, served by name and held to the set's contracts: the process's packs, as every Engine of it
        // registers their native halves (docs/downloadable-packs-m2d-plan.md).
        rp::host::installBarePackLoader(ctx_, rp::host::packs::processPacks(), rp_render_modules,
                                        rp_render_modules_count);
```

and `~RenderHost`:

```cpp
RenderHost::~RenderHost() {
    if (rt_) {
        // A pack's half left pending (it awaited what never settled) holds module structs that QuickJS-ng frees with
        // the context before its callbacks (bellard/quickjs#280): collected first, as settleForFree does for a txiki
        // runtime (host/js/PackModules).
        JS_RunGC(rt_);
        JS_FreeContext(ctx_);
        JS_FreeRuntime(rt_);
    }
}
```

- [ ] **Step 6: The render fixture pack and the CLI case**

Create `packages/native/test/packs/ts-fixture/render/package.json`:

```json
{
  "name": "@retroplug-test/ts-fixture-render",
  "private": true,
  "retroplug": { "app": "ts/app.ts" }
}
```

`packages/native/test/packs/ts-fixture/render/pack.json` (the template, as the ts-fixture's own):

```json
{
  "id": "tsfix-render", "name": "TS fixture pack (render)", "version": "1.0.0", "apiLevel": 0,
  "license": "MIT", "source": "packages/native/test/packs/ts-fixture/render",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": [], "roles": ["tsfix-render"] },
  "halves": {}
}
```

and `packages/native/test/packs/ts-fixture/render/ts/app.ts`:

```ts
// The render fixture pack's app half (docs/downloadable-packs-m2d-plan.md): a feature role every ROM is given, whose
// construct hook says so on the console, so a render's console shows the worker loaded, committed and used the pack.
// The role has no protocol node and changes nothing of the system it is on, so the render's bytes are a render's
// without it.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({
  roles: [{
    kind: "tsfix-render",
    category: "feature",
    schema: { parse: () => ({}) },
    onConstruct: (spec) => {
      console.log("tsfix-render: constructed");
      return spec;
    },
  }],
  romProviders: [() => [{ kind: "tsfix-render", config: {} }]],
});
```

(If the role type's `schema` needs more than `parse` to typecheck, follow `ts-fixture/ts/app.ts`, which passes the UI's
role check with this shape.) In `ts-fixture/CMakeLists.txt`, build it as `tsfix` is built, into
`${RP_PACKS_DIR}/tsfix-render/1.0.0`, claimed with `_tsfix_claim(tsfix-render)`, its outputs added to
`retroplug-ts-fixture-packs`'s `DEPENDS`, and its header line: `tsfix-render  an app half whose role every ROM gets,
which a render's console shows`. In `ts-fixture/tsconfig.json`, include `render/ts/*.ts` beside `ts/*.ts`.

In `tools/run-cli-ts-tests.sh`'s packs section (section 8, where the TS fixture's packs are checked), add, after its
existing cases and using the variables that section and section 6d use (`$CLI`, `$TMP`, `$FIX`, `check`, `contains`,
the packs folder `$PACKS` or its equivalent there):

```sh
# A render job's worker loads the packs' app and consoles halves (docs/downloadable-packs-m2d-plan.md): the fixture's
# role reaches the system the job builds, and adds nothing to its bytes.
RR="$TMP/pack-render"
mkdir -p "$RR"
cp "$FIX/smsggdj.sms" "$RR/rom.sms"
(cd "$RR" && RETROPLUG_DETERMINISTIC=1 "$CLI" render rom.sms --duration 1s --out plain.wav >plain.out 2>plain.err)
check "a render with no pack exits 0" 0 $?
(cd "$RR" && RETROPLUG_PACK_PATH="$PACKS/tsfix-render/1.0.0" RETROPLUG_DETERMINISTIC=1 \
	"$CLI" render rom.sms --duration 1s --out packed.wav >packed.out 2>packed.err)
check "a render with the render fixture pack exits 0" 0 $?
contains "the render job's worker constructed the pack's role" "$RR/packed.out" "tsfix-render: constructed"
if cmp -s "$RR/plain.wav" "$RR/packed.wav"; then echo "ok   - the pack's role leaves the render's bytes as they were"
else echo "FAIL - the pack's role changed the render's bytes"; FAILED=1; fi
(cd "$RR" && RETROPLUG_PACK_PATH="$PACKS/tsfix-throws/1.0.0" RETROPLUG_DETERMINISTIC=1 \
	"$CLI" render rom.sms --duration 1s --out thrown.wav >thrown.out 2>thrown.err)
check "a render beside a pack whose half throws still exits 0" 0 $?
contains "the render job's console names the refused pack" "$RR/thrown.err" "pack tsfix-throws is refused"
```

The ROM's fixture name, the failure flag, the folder variable and whether the job's console lines reach the tool's
stdout or stderr follow the script (6d copies its ROM from `$FIX`; `cli/sessions/render.ts` prints the job's console
lines as they arrive; a `console.log` line and a `console.warn` line may land on different streams: assert on the
stream each actually reaches, or on both files concatenated). The project's `package.json` `test:cli-ts` already builds
`retroplug-ts-fixture-packs`.

- [ ] **Step 7: Run it all**

Run: `pnpm configure && cmake --build build -j$(nproc) && pnpm test:cli-ts && pnpm test:native render && pnpm test packs`
Expected: exit 0 on each; the new CLI checks print `ok`.

- [ ] **Step 8: Commit**

```bash
git add packages/retroplug/src/packs/loadPacks.ts packages/retroplug/src/render/worker.ts \
  packages/retroplug/test/packs/load-packs.test.ts packages/native/src/host/render/RenderHost.cpp \
  packages/native/test/packs/ts-fixture tools/run-cli-ts-tests.sh
git commit -m "render: a render job loads the packs' app and consoles halves before it composes"
```

---

### Task 9: The docs

**Files:**
- Modify: `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`,
  `docs/downloadable-packs-loading-design.md` (section 3's script half)

- [ ] **Step 1: Write them**

- `spec/02-native-host.md`, the packs section: a pack's script half is a device script pack shipped as source: the
  manifest's `halves.script` (the whole pack) and `script` member (`runtime`, `heapBytes`, `nodeTypes`, each type in
  `provides.nodeTypes`); `PackTable` compiles both bundles at load with `rp::mqjs::compileScript` (against the host's own
  standard library, which binds the bytecode to the table that runs it, under the toolchain's names `<id>.js` /
  `<id>.device.js`), registers the source with `rp::mqjs::package` (the
  control context's check of its definitions included), and holds every compiled half for the life of the process; its
  package (`rp.<id>.script`) goes through a native package's checks and every Engine registers it at `syncPacks`. Why
  source: the pin, the design's rule, MicroQuickJS's unchecked bytecode, and bytecode bound to the library table
  that compiled it. Both module loaders refuse `pack:<id>/script`.
  The render worker: the `render` set's entry on a bare runtime, `installBarePackLoader` (the same loader and guard, bare
  names passed through, no `__rp_packRefused`), its `main` awaiting `loadAppPacks()` with no budget, a run that never
  reports said to be one, and `~RenderHost` collecting before it frees (bellard/quickjs#280).
- `spec/06-build-test.md`: the shared module sets gain `render` (the section's last sentence about the render worker
  being global code goes; the script host's stays); the targets table's `-render-worker-bundle` becomes
  `retroplug-render-modules` (`render-modules_data.c`, `render-bundle_data.c`); the TS loader section says the render
  worker loads `app` and `consoles` with no budget; `tools/build-pack-halves.mjs` builds a package's `retroplug.script`
  through the device-pack toolchain (`--only`, `--mqjs`, `--runner`) and `build-device-pack.mjs` writes
  `<name>.types.json`; the fixtures: `tsfix-render` and the CLI's render-with-packs checks, and
  `packages/native/test/packs/script-fixture/` (`scriptfix` and its variants from devtest's pack, `bliptoaster`'s
  script half, `retroplug-script-fixture-packs`) with `PackScript.test.cpp` (`[packs-script]`).
- `spec/07-remaining-work.md`, the Downloadable packs entry: what is left of part 1 is M2e (the protocol half: the frozen
  protocol tables, a pack's protocol nodes through `loadScriptPack`'s path into every Engine, its placer; the
  contribution kinds, whose `accept` runs once and whose items' runtime halves a pack's script pack cannot reach), M3,
  M4, M5 and the Windows allocator. Delete the script half and the render worker from the M2d text. Add what this plan
  leaves open (its design questions below, as spec/07's style puts them). In "The app loads no script pack", the
  sentence on a render job's Engine stays for QuickJS script packs (M2e's), and loading a device script pack into every
  Engine is built (say so, or drop the half of the sentence it no longer holds).
- `docs/downloadable-packs-loading-design.md` section 3: "`script` into the engine's script host through
  `loadScriptPack`" becomes: the `script` half is a device script pack (MicroQuickJS), shipped as ES5 source and
  compiled at load (docs/downloadable-packs-m2d-plan.md); a pack's QuickJS script nodes are its protocol nodes, which
  reach the engine through `loadScriptPack`'s path (M2e). And "Bundles are JavaScript source, not bytecode" holds for
  the script half too.

- [ ] **Step 2: Run**

Run: `pnpm check:docs`
Expected: exit 0, no warning in the files touched.

- [ ] **Step 3: Commit**

```bash
git add spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md docs/downloadable-packs-loading-design.md
git commit -m "docs: a pack's script half compiles at load, and a render job loads the packs' halves"
```

---

### Task 10: Verify, then the CI gate

- [ ] **Step 1: The local gate**

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 .superpowers/verify/tip-verify-par.sh` (the parallel tip verify),
then `pnpm sdl:controller` on its own (the parallel verify skips it), then `pnpm typecheck && pnpm check:boundaries &&
pnpm check:docs`.
Expected: every leg exit 0.

- [ ] **Step 2: The CI gate (needs the user's go-ahead to push)**

Ask the user to push `feature/modularize`; then dispatch `build.yml` on it and read every job (linux, linux-arm64,
macos, windows `/W4 /WX`). The Windows job is the first build of `compileScript` and `PackScript.cpp` under MSVC and of
the render set on clang-cl's engine there; a warning is fixed, not suppressed.
Expected: all four jobs succeed.

---

## Design questions this plan found

1. **No bytecode cache yet.** A script half is compiled at every launch (milliseconds for BlipToaster's), and a TS half
   at every import; the design's cache folder is unbuilt.
2. **A refused script half keeps its MicroQuickJS slot.** `registerPack` keeps the half for the process (32 packs at
   most on the desktop, `RP_MQJS_MAX_PACKS`); a pack refused after its registration (a type clash) costs one.
3. **The render worker's guard is the shell contracts' alone.** The `render` set holds `SHELL_IMPORTS`, as the CLI's
   does; an `app` half that imports a UI contract (`react`) loads in the plugin's control plane and is refused in a
   render. App halves should not reach UI modules; no rule says so yet.
4. **A render's refusals are console lines.** The render worker records nothing in the table, so a pack refused only in
   a render (a guard difference, question 3) is visible on the job's console and not in Settings > Packs (M4).
5. **A render has no pack budget.** A bare runtime has no timers, so a half that never settles ends the render with no
   result ("did not report a result") rather than being refused by name; a half stuck in a synchronous loop is ended by
   the job's cancel interrupt only once a cancel is asked for.
6. **The BlipToaster fixture stages at `build/packs/bliptoaster/1.0.0`,** where M5 builds the whole pack; M5 replaces
   the fixture's target rather than claiming the folder twice.
7. **The script half's toolchain checks run at the pack's build only.** The host trusts that the ES5 it compiles was
   scanned for library calls the engine lacks; a hand-edited half that calls one fails at its first use in a node, as a
   built-in pack would at its first run under a changed engine.

8. **The built-in device packs are compiled against another library than the one that runs them.** The device-pack
   toolchain compiles with `mqjs`, over upstream's `mqjs_stdlib`, and the host runs the bytecode over `rp_mqjs_stdlib`
   (upstream's tables plus the node SDK's names), relying on the relocator's atom matching (`update_atoms`), which
   upstream marks "XXX: add atom checksum". The transcript run passes today; whether the toolchain should compile against
   RetroPlug's library (as a loaded pack now is) is open.

## Self-review

**Scope** (the user's split, 2026-11-25): the device script half (Tasks 1 to 5) and the render worker (Tasks 6 to 8);
the protocol half and the kinds are M2e's and are not touched.

**Spec coverage:** compiled at load with upstream's compiler against the host's library, deterministic and registered
(Task 1); the manifest member
and its rules (Task 2); compiled, registered, checked as a native package, every Engine (Task 3); the pack build tool
(Task 4); a real package's script half equal to its static build (Task 5); the render worker as a set's host with its
bytes unchanged (Task 6); the bare loader and guard (Task 7); the worker's packs, no budget, the CLI end to end
(Task 8); the docs (Task 9); CI (Task 10).

**Placeholder scan:** every new module, function and fixture is given whole. Where a step leans on the tree (the
exception-to-text helper in `MqjsHost.cpp`, the host MIDI out read in the engine-graph tests, the CLI script's
variables, the load-packs test's fake host), it names the file to read and what to follow; the two test bodies that
read the engine's host MIDI out say what they must assert.

**Type and name consistency:** `rp::mqjs::compileScript(source, filename, box32, why)` (Task 1, used in Tasks 3 and 5);
`ScriptSpec` and `PackManifest::script` (Task 2, read in Task 3); `ScriptHalf`, `compileScriptHalf`,
`registerScriptHalf`, `LoadedPack::script`, `PackTable::scripts_` (Task 3, read in Task 5); `scriptfix`,
`scriptfix-twin`, `-types`, `-syntax`, `-nofile`, `retroplug-script-fixture-packs` (Task 3); `--only`, `--mqjs`,
`--runner`, `<name>.types.json` (Task 4, used in Task 5); `render` set, `rp:render`, `rp_render_modules`,
`rp_render_bundle`, `retroplug-render-modules` (Task 6); `installBarePackLoader` (Task 7, used in Task 8);
`tsfix-render` (Task 8).

## After the reviews

M2d was built as `3c476512d..bfd956ce3` (Tasks 1 to 8) and the docs commit after it (Task 9); Task 10's CI gate waits for
the user's go-ahead to push. Where the task blocks above and the tree differ, the tree wins. What changed from the plan
while it ran, and why:

- A script half is compiled against the host's own standard library (`rp_mqjs_stdlib`), so its bytes are not `mqjs -o`'s
  (f6169f6c5, c4574aa46): a MicroQuickJS bytecode's references to the library's atoms belong to the compiling table, and
  upstream's relocator has an "XXX: add atom checksum" comment about it. This is a further reason to ship source.
  `compileScript` also zeroes the bytes that round a string or byte array up to the word
  (`rp_mqjs_clear_bytecode_padding`): the engine wrapper collects before each growth of the unique-string table, and those
  collections leave the compile heap's leftover words there, so clearing them makes a compile give the same bytes wherever
  its heap lay.
- `PackTable` refuses a script half whose node types clash before it compiles it, since a MicroQuickJS registration takes
  one of 32 process slots for good, and it owns every compiled half (e5e465ab7, bc4ef0d54). `~PackTable` keeps them alive
  on purpose, because the MicroQuickJS host points into them and matches by address.
- The pack loader split (8591464d2): its txiki-free part is `host/js/PackLoaderCore.{hpp,cpp}` in `retroplug-packs`, which
  now links `qjs` and `retroplug-mqjs-host` and holds the table pointer, the guard, `loadPackHalf`, `__rp_packList`,
  `__rp_packRefused`, the globals and `installBarePackLoader`; `host/js/PackModules` is the txiki wrapper
  (`installPackLoader`, `settleForFree`).
- `SharedModules.cpp` moved to `retroplug-backend`, since `RenderHost` uses it, and `retroplug-classid-test` compiles it
  itself and holds the pack-modules tests, the bare-runtime case included (3fc1ee56d, 8591464d2).
- `retroplug-render-modules` writes `render-modules_data.c` and `render-bundle_data.c`. The worker calls `main()` without a
  top-level await, because the bundles target es2020, and a run that never reports says so: "render worker did not
  report a result (it may be waiting on something that never settled)" (3fc1ee56d, bfd956ce3).
- The device-pack test slug is `graph/device-pack`; the script fixtures are `packages/native/test/packs/script-fixture/`
  (`scriptfix`, `-twin`, `-types`, `-syntax`, `-nofile`, `bliptoaster`; target `retroplug-script-fixture-packs`), tested by
  `PackScript.test.cpp` `[packs-script]` (f057304d7, 2ac551763); the render fixture is `tsfix-render` under
  ts-fixture/render/, and the CLI cases are in `tools/run-cli-ts-tests.sh`, guarded on the repo's smsggdj ROM (bfd956ce3).
- Task 6 had already updated spec/06's targets table entry and spec/11's render worker description.

Fixed after the reviews (fa14ef4f0): a development pack whose manifest does not parse was refused under its folder's name, "1.0.0" for the `build/packs/<id>/<version>` layout; `PackTable::refuse` now uses the parent folder's name when the folder's name is a pack version, as the installed branch does.
