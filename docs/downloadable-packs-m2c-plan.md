# Downloadable packs M2c: loading a pack's TS halves. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pack's `app`, `consoles`, `ui`, `views` and `cli` halves, ES module bundles in its version folder, load at
launch into every txiki context that holds the live registries (the plugin's and the standalone's control plane and UI,
the UI test harness, the CLI's dispatcher), link to the host's own contract modules, pass the pack's `provides` and
`requires`, and commit all or nothing; an import of anything but a contract is refused.

**Architecture:** Each host loads its launch packs once per process (`ensureLaunchPacks`, over M1's
`loadLaunchPacks`), registers its shared module set (M2b), then installs the pack loader (`installPackLoader`): a
module-loader wrapper over txiki's that answers `pack:<id>/<half>` from the pack's folder and refuses, from a pack
module, any import that is not a contract this context registered, and two globals, the pack list and the refusal
report. In TS, `src/packs/loadPacks.ts` imports each pack's halves by name, maps each half's default export onto
registry additions through an explicit table (the shell's `src`, `ui` and `cli` trees each add their own), checks the
pack's manifest against what it contributes, and commits every pack of the context in one batch, falling back to one
commit per pack to name the one refused. A small esbuild tool builds a package's halves as a pack's, every contract
external; it builds the fixture pack whose good and broken variants the tests load.

**Tech Stack:** C++20 over QuickJS-ng and txiki (`JS_SetModuleLoaderFunc2`, `JS_Eval` compile-only), M1's
`retroplug-packs`, TypeScript and the live registries (M2a), esbuild, Catch2 v3, the shell's TS, UI and CLI suites.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones: M1
native packs, M2a live registries, M2b shared contract modules (each done, with its plan beside this one), then:

- **M2c, loading a pack's TS halves (this plan):** launch loading on every txiki host, the pack module loader and its
  import guard, the halves that fill the live registries (`app`, `consoles`, `ui`, `views`, `cli`) in the app and CLI
  contexts, the manifest's `provides` and `requires` held to what the halves contribute, all or nothing per pack.
- **M2d, the rest of the TS halves (decided 2026-11-24):** the protocol half (today's `PROTOCOL_NODES`, `ROLE_NODES`
  and `KIND_DATA` are frozen constants `buildGraph` and others read), the contribution kinds (`accept` runs once), the
  script half (`loadScriptPack` from the app, span labels at load) and the render worker (bare QuickJS, no module
  loader: a pack console's render job needs the pack's app and consoles halves there).
- M3 placeholders, M4 archives, commands and Settings > Packs, M5 the real packages as packs, as before.

**What exists.** M1's `PackTable` loads a pack's native half and refuses any other (`PackTable.cpp`, "has a TS half
... which this RetroPlug does not load yet"); `loadLaunchPacks` has no production caller. M2b's shared module sets give
each context one copy of every contract module, so a module evaluated later that imports one by its specifier links to
the shell's instance (its final review evaluated two pack sources against the real app table and proved it). M2a's
registries are live: a commit after composition reaches the stores (`adoptContributions`), the role registry (lazily),
the menus and views (the UI re-renders on the commit counter) and the CLI's tool list.

**Execution model.** Sonnet implements each task from its text alone; Opus reviews each task's diff before the next one
starts. The code here was not compiled or run as written: where a step meets the tree differently (a helper's name, an
include, a type's exact field), the implementer follows the tree, keeps the step's intent, and reports the deviation.
Task 9 ends with a CI gate on Windows and macOS, which needs the user's go-ahead to push.

## Decisions this plan makes

- **Halves load where their registries live.** The control plane loads `app` and `consoles`; the UI loads `ui` and
  `views`, and `app` and `consoles` too when no control plane ran before it (the UI test harness); the CLI's dispatcher
  loads `app`, `consoles` and `cli`. A half already loaded in a context is not loaded again (the plugin's control plane
  and UI share one context and one loader state since M2b). CLI session files (`retroplug-cli file.ts`) load no pack:
  they never get the cli set (M2b finding 13), and M2d's Node and session work decides them.
- **A pack half is a module named `pack:<id>/<half>`,** compiled from its file the first time a context imports that
  name (txiki's module loader, wrapped), never cached to disk (the bytecode cache is a later milestone's). The name is
  not a path, so `import.meta` is set without a real path and nothing resolves relative to it.
- **The import guard is in the module normalizer, before txiki's import map.** From a module named `pack:...`, a
  specifier is accepted only if it names a module of the context's registered shared set that is not `rp:`-prefixed (a
  contract, or a host module's facade); `rp:cp`, `rp:ui`, a chunk, `tjs:` builtins, relative names and every other bare
  name are refused, naming the pack module and the specifier. The accepted set is written into the context as a frozen
  object when the loader is installed, from the same table `registerSharedModules` read, so it is the context's own
  set: the CLI's holds the shell's contracts, the app's the UI's too.
- **Native first, then TS, and a TS refusal does not unload native code.** A pack's native half loads at launch, before
  any context exists; its TS halves load when a context evaluates. A pack whose TS half is refused is recorded in the
  table's refusals (`PackTable::recordRefusal`) and commits nothing; its node types stay registered in the Engines
  (native code is never unloaded), unreachable because nothing places them. A pack refused in its UI half after its app
  half committed keeps the app half's contributions (the UI is evaluated later, at the first editor open in the plugin);
  it is reported refused.
- **One commit per context, then one per pack.** Every pack's additions of a context go in one `commitContributions`
  (M2a finding 12: every commit runs every seeded check). If it throws, the packs commit one at a time in load order and
  each one that throws is refused with the message, so the culprit is named and the rest still load (all or nothing is
  per pack). A pack whose `requires.packs` names a refused pack is refused too.
- **The manifest is held to the halves.** In the context that loads a pack's `app` half: every role kind it
  contributes is in `provides.roles`, and every kind `provides.roles` names is contributed. In the context that loads
  its `consoles` half: every console's platforms are in `provides.platforms` and its node type in
  `provides.nodeTypes`, every platform `provides.platforms` names is contributed, and every platform
  `requires.platforms` names is a console platform already registered (built in, or a pack earlier in load order). A
  pack that declares roles or platforms with no half to contribute them is refused.
- **What a pack cannot contribute yet is refused by name**: an `app` half's `embeds` (a pack cannot bake a ROM into the
  binary, M2a finding 9; a pack-carried ROM needs a rule of its own), and an `app` member named after a contribution
  kind (`controllerApps`; M2d makes the kinds live). The protocol and script halves are refused by the native loader,
  naming M2d.
- **`views` is a half of its own.** The design's manifest lists `app`, `consoles`, `protocol`, `ui`, `cli` and `script`;
  a package's views are a module of their own (`retroplug.views`), so the manifest gains `views`. It is additive, so the
  API level does not move.
- **The loader never rejects, and the host waits for it.** The control plane sets `__rp_ready` once its packs have
  committed (the stores are composed first and adopt the commit, M2a), so a project load, which a host makes only after
  ready, finds the packs' roles. The CLI routes once its packs have committed. The UI starts its load after it
  installs its hooks and re-renders when the commit lands (M2a's counter).
- **Tests never read the user's packs.** Every runner that starts a host binary sets `RETROPLUG_PACKS_DIR` to an
  empty scratch folder and clears `RETROPLUG_PACK_PATH`, unless the test names the packs it loads.
- **The fixture pack is built by the tool M5 will use.** `tools/build-pack-halves.mjs` builds a package's halves (one
  ES module each, every contract and host module external, any other `@retroplug/*` import refused) into a version
  folder and writes its `pack.json`. The broken variants are hand-written modules, because the tool would refuse them.

## Commands used throughout

- The TS suite: `pnpm test` or `pnpm test <slug>` (a file's path under `test/` without `.test.ts`, or a directory; a
  slug that matches nothing exits 1 with "no tests match").
- The UI suite: `pnpm test:ui [slug]`. The plugin binaries: `pnpm test:plugin`. The CLI: `pnpm test:cli-ts`.
- One binary: `cmake --build build --target <t> -j$(nproc)`, then e.g. `build/bin/retroplug-packs-test`. After a CMake
  change: `pnpm configure`. A UI change to the standalone needs the bare `cmake --build build -j$(nproc)`. Never
  `rm -rf build`.
- `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`.
- `deps/sameboy` shows as modified after a configure: expected; never stage it. Generated modules and `build/` are
  gitignored outputs.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/native/src/host/js/PackModules.{hpp,cpp}` | `installPackLoader`: the `pack:` loader, the import guard, the pack list and refusal globals |
| `packages/native/test/plugin/PackModules.test.cpp` | the loader and the guard over fixture packs |
| `packages/native/test/plugin/pack-modules-fixture/` | the loader test's packs: manifests and modules |
| `packages/retroplug/src/packs/packHost.ts` | `PackInfo`, `PackHost`, `nativePackHost()` |
| `packages/retroplug/src/packs/loadPacks.ts` | the loader: imports, checks, the batch commit, `packStatus`, `APP_BUILDERS`, `loadAppPacks` |
| `packages/retroplug/ui/packs/uiPacks.ts` | `UI_BUILDERS`, `loadUiPacks` |
| `packages/retroplug/cli/packs/cliPacks.ts` | `CLI_BUILDERS`, `loadCliPacks` |
| `packages/retroplug/test/packs/load-packs.test.ts` | the loader against a fake host |
| `tools/build-pack-halves.mjs` | a package's halves built as a pack's, its `pack.json` written |
| `packages/retroplug/test/script/build-pack-halves.test.ts` | the tool's output and refusals |
| `packages/native/test/packs/ts-fixture/` | the TS fixture pack's package and its broken variants, with their CMake |
| `packages/retroplug/test-ui/packs-ts-fixture.test.ts` | the UI loads the fixture pack's halves |

Modified: `packages/native/src/host/packs/{PackManifest,PackTable,ProcessPacks}.{hpp,cpp}`,
`packages/native/test/packs/{PackManifest,PackLoader}.test.cpp`, `packages/native/test/packs/fixture/` (one manifest),
`packages/native/CMakeLists.txt` (the loader's source and link), `packages/native/cmake/tests.cmake`,
`packages/native/plugin/{PluginDSP,PluginUI}.cpp`, `packages/native/sdl/main.cpp`, `packages/native/test/ui/UiHarness.cpp`,
`packages/native/cli/main.cpp`, `packages/retroplug/src/pluginControlPlane.ts`, `packages/retroplug/ui/main.tsx`,
`packages/retroplug/ui/host/nativeHooks.ts`, `packages/retroplug/cli/cli.ts`, the test runners
(`packages/retroplug/scripts/run-{tests,ui-tests,plugin-tests}.mjs`, `tools/run-cli-ts-tests.sh`,
`tools/run-sdl-{smoke,pipewire,controller}.sh`, `tools/reaper-env.sh`), `package.json` (`test:cli-ts`'s targets), and
the docs (spec/02, spec/06, spec/07).

---

### Task 1: The native loader accepts the TS halves

**Files:**
- Modify: `packages/native/src/host/packs/PackManifest.cpp` (`knownHalves`, `isInsidePath`),
  `packages/native/src/host/packs/PackManifest.hpp` (the header comment), `packages/native/src/host/packs/PackTable.cpp`
- Modify: `packages/native/test/packs/fixture/manifests/packfixture-tshalf.json`, and add
  `packages/native/test/packs/fixture/manifests/packfixture-protocol.json` with its variant line in
  `packages/native/test/packs/fixture/CMakeLists.txt`
- Test: `packages/native/test/packs/PackManifest.test.cpp`, `packages/native/test/packs/PackLoader.test.cpp`

- [ ] **Step 1: Write the failing tests**

In `PackManifest.test.cpp`, beside the existing halves cases, add the case below. The file's helper
`manifestWith(member, value)` builds the good manifest (id `demo`) with one member replaced; its native half must be
named `rp-demo` (a native half is always `rp-<id>`), or every case would be refused for that instead.

```cpp
TEST_CASE("a half's path is printable ASCII and a name Windows can hold", "[packs-folders]") {
    const auto parses = [](const std::string& halves) {
        std::string why;
        return parsePackManifest(manifestWith("halves", halves), &why, std::nullopt).has_value();
    };
    // views is a half of its own (docs/downloadable-packs-m2c-plan.md).
    CHECK(parses(R"({"native":"rp-demo","views":"js/views.js"})"));
    for (const char* bad : {"js/con.js", "js/CON", "js/aux.txt", "js/nul", "js/com1.js", "js/LPT9", "js/app.js.",
                            "js/app.js ", "js/app\x01.js", "js/\xc3\xa9.js"}) {
        INFO(bad);
        CHECK_FALSE(parses(std::string(R"({"native":"rp-demo","app":")") + bad + "\"}"));
    }
    CHECK(parses(R"({"native":"rp-demo","app":"js/console.js"})"));  // a prefix of a reserved name is a name
    CHECK(parses(R"({"native":"rp-demo","app":"js/com10.js"})"));
}
```

(If `manifestWith` takes the value in another form, an already-serialised JSON or a parsed one, follow it; the file's
other halves cases show how.)

In `PackLoader.test.cpp`, the `packfixture-tshalf` case (around line 195) expects the M1 refusal "has a TS half". Change
it, and add the protocol variant beside it:

```cpp
    // A TS half loads now (docs/downloadable-packs-m2c-plan.md), but every file the manifest names must be there: the
    // variant names js/app.js and ships none.
    checkRefused(table, "packfixture-tshalf", "halves.app names js/app.js, which the pack does not have");
    // The protocol and script halves are M2d's: a pack with one is refused whole.
    checkRefused(table, "packfixture-protocol", "has a protocol half (js/protocol.js), which this RetroPlug does not "
                                                "load yet");
```

(`checkRefused` is the file's helper; keep its argument order.)

Add the manifest `packages/native/test/packs/fixture/manifests/packfixture-protocol.json`, a copy of
`packfixture-tshalf.json` with `"id": "packfixture-protocol"`, `"name": "Pack fixture (packfixture-protocol)"`, its two
node types renamed `rp.packfixture-protocol.tone` / `.gain`, and `"halves": {"native": "rp-packfixture-protocol",
"protocol": "js/protocol.js"}`; and in `packages/native/test/packs/fixture/CMakeLists.txt`, beside the tshalf variant's
line, the same form tshalf's takes (a manifest staged at configure, with no library and no target):

```cmake
retroplug_pack_module(retroplug-packfixture-protocol-pack
    MANIFEST ${_rp_fixture_dir}/manifests/packfixture-protocol.json
    NO_LIBRARY)
```

and its line
in the header's list: `#   packfixture-protocol      a protocol half, which M2c does not load (M2d's)`. Update the tshalf
line to `#   packfixture-tshalf        a TS half naming a file the pack does not have`.

- [ ] **Step 2: Run them to see them fail**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: FAIL (run unfiltered: the file's tags are `[packs-folders]`, `[packs-loader]` and others): the views manifest
is refused ("no half this RetroPlug loads"), the reserved names parse, and the two loader cases see the M1 message.

- [ ] **Step 3: Implement**

In `PackManifest.cpp`, `knownHalves`:

```cpp
const std::vector<std::string>& knownHalves() {
    static const std::vector<std::string> kHalves = {"native", "app",    "consoles", "protocol",
                                                     "ui",     "views",  "cli",      "script"};
    return kHalves;
}
```

and `isInsidePath` gains two rules (the M2a plan's design question 4):

```cpp
// A segment Windows cannot hold as written: a reserved device name (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9,
// alone or before an extension, in any case), or one ending in a dot or a space, which Windows strips, so two
// spellings would open one file.
bool isWindowsReserved(std::string_view seg) {
    if (seg.back() == '.' || seg.back() == ' ') return true;
    std::string stem(seg.substr(0, seg.find('.')));
    for (char& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return true;
    return stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '1' &&
           stem[3] <= '9';
}

// A half's path: relative, inside the version folder, '/'-separated, printable ASCII (a path is opened as given on
// every platform, and Windows reads a narrow path in its ANSI code page), no empty, "." or ".." segment, and no segment
// Windows cannot hold.
bool isInsidePath(std::string_view p) {
    if (p.empty() || p.front() == '/') return false;
    for (const char c : p)
        if (c < ' ' || c > '~') return false;
    if (p.find('\\') != std::string_view::npos || p.find(':') != std::string_view::npos) return false;
    std::size_t at = 0;
    while (at <= p.size()) {
        std::size_t end = p.find('/', at);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view seg = p.substr(at, end - at);
        if (seg.empty() || seg == "." || seg == ".." || isWindowsReserved(seg)) return false;
        at = end + 1;
    }
    return true;
}
```

(`<cctype>` if the file does not include it.) The header's comment in `PackManifest.hpp` says M1's loader enforces the
native half; reword its last two sentences to: "What the loader (PackTable) enforces beyond the shape is the native
half, provides.nodeTypes and that each TS half's file is there; it refuses the protocol and script halves, which M2d
loads. provides.platforms and .roles, and requires.platforms, are held to the TS halves when a context loads them
(src/packs/loadPacks.ts)." and the `knownHalves` comment to "The kinds of half this host knows (the members halves may
name)."

In `PackTable.cpp`, replace the M1 loop ("Only the native half loads in M1 ...") with:

```cpp
    // The TS halves load when a context imports them (host/js/PackModules, docs/downloadable-packs-m2c-plan.md), long
    // after this load, so each file the manifest names is checked here, where a refusal still leaves nothing loaded.
    // The protocol and script halves are M2d's, and a pack loads whole or not at all.
    for (const auto& [kind, file] : manifest->halves) {
        if (kind == "native") continue;
        if (kind == "protocol" || kind == "script")
            return refuseHere(id, pack + " has a " + kind + " half (" + file +
                                      "), which this RetroPlug does not load yet; a pack loads whole or not at all");
        std::error_code ec;
        if (!fs::is_regular_file(candidate.dir / fs::path(file), ec))
            return refuseHere(id, pack + ": halves." + kind + " names " + file + ", which the pack does not have");
    }
```

(`fs` is the file's alias for `std::filesystem`; `file` is printable ASCII by the manifest rule above, so a narrow
`fs::path` names it on every platform.)

- [ ] **Step 4: Run them to see them pass**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test`
Expected: `All tests passed`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/packs/PackManifest.cpp packages/native/src/host/packs/PackManifest.hpp \
  packages/native/src/host/packs/PackTable.cpp packages/native/test/packs/PackManifest.test.cpp \
  packages/native/test/packs/PackLoader.test.cpp packages/native/test/packs/fixture/CMakeLists.txt \
  packages/native/test/packs/fixture/manifests/packfixture-tshalf.json \
  packages/native/test/packs/fixture/manifests/packfixture-protocol.json
git commit -m "packs: the loader accepts the app, consoles, ui, views and cli halves, each file present"
```

(`packfixture-tshalf.json` may be unchanged; leave it out of `git add` if so.)

---

### Task 2: Each host loads its launch packs once per process

**Files:**
- Modify: `packages/native/src/host/packs/ProcessPacks.{hpp,cpp}`
- Modify: `packages/native/plugin/PluginDSP.cpp`, `packages/native/sdl/main.cpp`, `packages/native/cli/main.cpp`,
  `packages/native/test/ui/UiHarness.cpp`
- Modify: `packages/retroplug/scripts/run-tests.mjs`, `packages/retroplug/scripts/run-ui-tests.mjs`,
  `packages/retroplug/scripts/run-plugin-tests.mjs`, `tools/run-cli-ts-tests.sh`, `tools/run-sdl-smoke.sh`,
  `tools/run-sdl-pipewire.sh`, `tools/run-sdl-controller.sh`, `tools/reaper-env.sh`

- [ ] **Step 1: The once-per-process load**

`ProcessPacks.hpp`, after `processPacks()`:

```cpp
// The launch's load into processPacks(), once per process whoever asks first (every plugin instance's construction,
// the standalone's start, the CLI dispatcher's, the UI test harness's boot): the installed packs and
// RETROPLUG_PACK_PATH's development folders (loadLaunchPacks), each refusal printed to stderr. The CLI's --pack and
// Settings' Development packs are M4's. A test points RETROPLUG_PACKS_DIR at a scratch folder, so no test reads the
// user's packs.
void ensureLaunchPacks();
```

and reword the `processPacks()` comment's last sentence to: "It starts empty: a host's launch fills it
(ensureLaunchPacks) and a live install adds to it (M4); nothing here scans a folder until a host asks, so a binary that
never does reads no user directory."

`ProcessPacks.cpp` (add `#include <cstdio>`, `#include <mutex>`, `#include "host/packs/PackDiscovery.hpp"` as needed):

```cpp
void ensureLaunchPacks() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (const PackRefusal& refusal : loadLaunchPacks(processPacks(), defaultPackRoots(), {}))
            std::fprintf(stderr, "[packs] %s\n", refusal.reason.c_str());
    });
}
```

(Each refusal's reason already names its pack: "pack <id> ...".)

- [ ] **Step 2: The hosts call it before their control plane**

- `PluginDSP.cpp`: first statement of `bootControlPlane()` (before `host_.init()`):
  `rp::host::packs::ensureLaunchPacks();  // before any context: a pack's TS halves are read from what it loaded`
  with `#include "host/packs/ProcessPacks.hpp"`. Every instance calls it; only the first loads.
- `sdl/main.cpp`: first statement of `bootControlPlane` (its body is `:42-81`), the same line and include.
- `cli/main.cpp`: in the dispatcher branch (`else {` of `if (runFile)`), before `registerSharedModules`, the same line
  with a comment: "Session files load no pack: they never get the cli set (docs/downloadable-packs-m2c-plan.md)."
- `test/ui/UiHarness.cpp`: in `boot()`, before `registerSharedModules`, the same line.

Each binary links `retroplug-backend` (which holds `ProcessPacks.cpp`); if one does not, link it there and say so in the
report.

- [ ] **Step 3: The runners keep tests off the user's packs**

Every runner that starts one of those binaries sets `RETROPLUG_PACKS_DIR` to an empty folder it owns and removes
`RETROPLUG_PACK_PATH` from the child's environment:

- `run-ui-tests.mjs`: in the spawn's `env`, beside `RETROPLUG_USER_CONFIG_DIR: cfgDir`, add
  `RETROPLUG_PACKS_DIR: join(cfgDir, "packs"), RETROPLUG_PACKS_CACHE_DIR: join(cfgDir, "packs-cache")`, and build the
  env so `RETROPLUG_PACK_PATH` is absent:
  `const { RETROPLUG_PACK_PATH: _ignored, ...inherited } = process.env;` then `env: { ...inherited, ... }`. (Task 7 adds
  the per-file packs.)
- `run-plugin-tests.mjs` (its binaries construct the plugin, which calls `ensureLaunchPacks`) and `run-tests.mjs` (it
  runs files on plain `tjs`, but a test may spawn `retroplug-cli`): the same three changes on their child environment (each makes a temporary
  folder the way the file already does for anything it stages; if it makes none, `mkdtempSync(join(tmpdir(),
  "rp-packs-"))`, removed when the run ends).
- `run-sdl-smoke.sh`, `run-sdl-pipewire.sh`, `run-sdl-controller.sh`: beside each script's
  `export RETROPLUG_USER_CONFIG_DIR=...`, add
  `export RETROPLUG_PACKS_DIR="$TMP/packs" RETROPLUG_PACKS_CACHE_DIR="$TMP/packs-cache"` and `unset RETROPLUG_PACK_PATH`.
- `tools/run-cli-ts-tests.sh`: the same two lines right after the script makes its scratch folder and its trap
  (`TMP=` and `trap`, around lines 80-81; `$TMP` is empty before them), adding no folder or trap of its own.
- `tools/reaper-env.sh` has no `$TMP`: inside `reaper_env_up`, after `REAPER_CFG=` (around line 173),
  `export RETROPLUG_PACKS_DIR="$REAPER_CFG/packs" RETROPLUG_PACKS_CACHE_DIR="$REAPER_CFG/packs-cache"; unset RETROPLUG_PACK_PATH`.
  (`reaper-env.ps1`, the Windows hand runs, is left as it is.)

- [ ] **Step 4: Run**

Run: `cmake --build build -j$(nproc) && pnpm test:plugin && pnpm test:ui && pnpm test:cli-ts && pnpm sdl:smoke`
Expected: all exit 0, unchanged results (no pack is found: the scratch folders are empty). Then, by hand, prove a host
reads the variable: `RETROPLUG_PACKS_DIR=$(mktemp -d) RETROPLUG_PACK_PATH=build/packs/packfixture-protocol/1.0.0
build/bin/retroplug-cli --help 2>&1 >/dev/null | grep '^\[packs\]'` prints
`[packs] pack packfixture-protocol has a protocol half (js/protocol.js), which this RetroPlug does not load yet; a pack
loads whole or not at all` (the variant's folder is staged by a configure).

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/packs/ProcessPacks.hpp packages/native/src/host/packs/ProcessPacks.cpp \
  packages/native/plugin/PluginDSP.cpp packages/native/sdl/main.cpp packages/native/cli/main.cpp \
  packages/native/test/ui/UiHarness.cpp packages/retroplug/scripts/run-tests.mjs \
  packages/retroplug/scripts/run-ui-tests.mjs packages/retroplug/scripts/run-plugin-tests.mjs \
  tools/run-cli-ts-tests.sh tools/run-sdl-smoke.sh tools/run-sdl-pipewire.sh tools/run-sdl-controller.sh \
  tools/reaper-env.sh
git commit -m "packs: every host loads its launch packs once per process, and the test runners point them at nothing"
```

---

### Task 3: The pack module loader and the import guard

**Files:**
- Create: `packages/native/src/host/js/PackModules.hpp`, `packages/native/src/host/js/PackModules.cpp`
- Create: `packages/native/test/plugin/PackModules.test.cpp` and the fixture
  `packages/native/test/plugin/pack-modules-fixture/` (below)
- Modify: `packages/native/CMakeLists.txt` (`retroplug-tjs-runtime`), `packages/native/cmake/tests.cmake`
  (`retroplug-classid-test`)

- [ ] **Step 1: The fixture**

The loader test reuses the shared-module fixture's table (`rp_fx_modules`: `fx:state`, `fx:dep`, see
`test/plugin/shared-modules-fixture/`) as its "contracts", and loads packs from these folders. Each `pack.json.in` is
configured with `@RP_API_LEVEL@` so a level bump needs no edit here.

`pack-modules-fixture/pmgood/pack.json.in`:

```json
{
  "id": "pmgood", "name": "Pack modules fixture (good)", "version": "1.0.0", "apiLevel": @RP_API_LEVEL@,
  "license": "MIT", "source": "packages/native/test/plugin/pack-modules-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": [], "roles": [] },
  "halves": { "app": "js/app.js", "ui": "js/ui.js" }
}
```

`pack-modules-fixture/pmgood/js/app.js` (read `shared-modules-fixture/dep.js` and `state.js` first, and import an export
`dep.js` really has; `depValue` below stands for it):

```js
import { depValue } from "fx:dep";
export default { half: "app", dep: depValue };
```

`pack-modules-fixture/pmgood/js/ui.js`:

```js
export default { half: "ui" };
```

Four broken packs, each a `pack.json.in` like `pmgood`'s with its own `id` and `"halves": { "app": "js/app.js" }`, and
that one module:

| Folder (id) | `js/app.js` | What the test expects |
|---|---|---|
| `pmforeign` | `import x from "fx:nope"; export default x;` | refused: `imports 'fx:nope', which is not a RetroPlug contract module` |
| `pminternal` | `import "rp:fx-a"; export default {};` | refused: `imports 'rp:fx-a'`, though `rp:fx-a` is loaded |
| `pmrelative` | `import y from "./other.js"; export default y;` | refused: `imports './other.js'` |
| `pmthrows` | `throw new Error("the fixture half throws");` | rejected with `the fixture half throws` |

In `tests.cmake`, beside the shared-module fixture block, stage them into the build tree at configure:

```cmake
# The pack loader's fixture (test/plugin/pack-modules-fixture): five development packs with JS halves alone, staged
# with this build's API level so a level bump needs no edit there.
set(_pm_src "${CMAKE_CURRENT_SOURCE_DIR}/test/plugin/pack-modules-fixture")
set(_pm_out "${CMAKE_BINARY_DIR}/test/pack-modules-fixture")
foreach(_pm pmgood pmforeign pminternal pmrelative pmthrows)
    configure_file("${_pm_src}/${_pm}/pack.json.in" "${_pm_out}/${_pm}/1.0.0/pack.json" @ONLY)
    file(GLOB _pm_js RELATIVE "${_pm_src}/${_pm}" CONFIGURE_DEPENDS "${_pm_src}/${_pm}/js/*.js")
    foreach(_js IN LISTS _pm_js)
        configure_file("${_pm_src}/${_pm}/${_js}" "${_pm_out}/${_pm}/1.0.0/${_js}" COPYONLY)
    endforeach()
endforeach()
```

`RP_API_LEVEL` is set by `cmake/packs.cmake`, which the root includes before this file; if it is not in scope here,
read it the way packs.cmake does and say so. The test finds the folders through a define on the binary:
`target_compile_definitions(retroplug-classid-test PRIVATE RP_PACK_MODULES_FIXTURE="${_pm_out}")` after its
`retroplug_add_test_binary` call (or through the macro's own DEFINES argument if it has one).

- [ ] **Step 2: Write the failing test**

`packages/native/test/plugin/PackModules.test.cpp`. Read `SharedModules.test.cpp` first: it makes a `TjsHostRuntime`,
evaluates modules and reads results back with a helper; use the same setup and helpers (their names below are
stand-ins), and the same `extern "C"` declarations of `rp_fx_modules` / `rp_fx_host_a`.

```cpp
// The pack module loader (host/js/PackModules, docs/downloadable-packs-m2c-plan.md): a pack's half is a module named
// pack:<id>/<half>, compiled from its file the first time something imports it, and a pack module may import only a
// contract module of its context's registered set.
// (SharedModules.test.cpp's include block here: TjsHostRuntime.hpp, the CHECK #undef, Catch2.)
#include <string>

#include "host/js/PackModules.hpp"
#include "host/js/SharedModules.hpp"
#include "host/packs/PackTable.hpp"

namespace fs = std::filesystem;
using rp::host::packs::PackTable;

namespace {
fs::path fixture(const char* id) { return fs::path(RP_PACK_MODULES_FIXTURE) / id / "1.0.0"; }

// Import `name` from a module evaluated in the context, pump until it settles, and return "ok <JSON of the default
// export>" or "error <message>".
std::string importResult(Runtime& rt, const std::string& name) {
    const std::string code = "import(" + quoted(name) + ").then((m) => { globalThis.__pm = 'ok ' + "
                             "JSON.stringify(m.default); }, (e) => { globalThis.__pm = 'error ' + e.message; });";
    evalModule(rt, code);
    pumpUntil(rt, "globalThis.__pm !== undefined");
    const std::string out = readString(rt, "globalThis.__pm");
    evalModule(rt, "globalThis.__pm = undefined;");
    return out;
}
}  // namespace

TEST_CASE("a pack's half loads by name and links to the context's contract modules", "[pack-modules]") {
    PackTable table;
    for (const char* id : {"pmgood", "pmforeign", "pminternal", "pmrelative", "pmthrows"})
        REQUIRE(table.load(fixture(id)));
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    // The contracts the loader is given: the fixture table's two modules and an rp:-named one, as a real set has (its
    // hosts and chunks), so the guard's rp: filter is what refuses pminternal, not the name's absence from the table.
    const rp_js_module contracts[] = {rp_fx_modules[0], rp_fx_modules[1], {"rp:fx-a", nullptr, 0}};
    rp::host::installPackLoader(rt.ctx(), table, contracts, 3);

    SECTION("the halves, each once") {
        const std::string app = importResult(rt, "pack:pmgood/app");
        CHECK(app.rfind("ok {\"half\":\"app\"", 0) == 0);
        CHECK(importResult(rt, "pack:pmgood/ui") == "ok {\"half\":\"ui\"}");
        CHECK(importResult(rt, "pack:pmgood/app") == app);  // the same module, not compiled again
    }
    SECTION("a half the pack does not have, or a pack the table does not hold") {
        CHECK(importResult(rt, "pack:pmgood/cli").rfind("error could not load 'pack:pmgood/cli'", 0) == 0);
        CHECK(importResult(rt, "pack:nope/app").rfind("error could not load 'pack:nope/app'", 0) == 0);
        CHECK(importResult(rt, "pack:pmgood/native").rfind("error could not load", 0) == 0);
    }
    SECTION("the guard") {
        CHECK(importResult(rt, "pack:pmforeign/app") ==
              "error pack:pmforeign/app imports 'fx:nope', which is not a RetroPlug contract module");
        evalHostA(rt);  // rp:fx-a is now a loaded module of the context, and still not a pack's to import
        CHECK(importResult(rt, "pack:pminternal/app") ==
              "error pack:pminternal/app imports 'rp:fx-a', which is not a RetroPlug contract module");
        CHECK(importResult(rt, "pack:pmrelative/app") ==
              "error pack:pmrelative/app imports './other.js', which is not a RetroPlug contract module");
    }
    SECTION("a half that throws") {
        CHECK(importResult(rt, "pack:pmthrows/app") == "error the fixture half throws");
    }
    SECTION("the host's own imports are not judged") {
        // A module that is not a pack's still reaches anything txiki's loader serves.
        CHECK(importResult(rt, "fx:dep").rfind("ok", 0) == 0);
    }
}

TEST_CASE("the pack list and the refusal report", "[pack-modules]") {
    PackTable table;
    REQUIRE(table.load(fixture("pmgood")));
    Runtime rt;
    REQUIRE(rp::host::registerSharedModules(rt.ctx(), rp_fx_modules, rp_fx_modules_count).empty());
    rp::host::installPackLoader(rt.ctx(), table, rp_fx_modules, rp_fx_modules_count);
    CHECK(readString(rt, "globalThis.__rp_packList()") ==
          R"([{"id":"pmgood","version":"1.0.0","halves":["app","ui"],)"
          R"("provides":{"platforms":[],"nodeTypes":[],"roles":[]},"requires":{"packs":[],"platforms":[]}}])");
    evalModule(rt, "globalThis.__rp_packRefused('pmgood', 'its app half failed');");
    REQUIRE(table.refusals().size() == 1);
    CHECK(table.refusals()[0].id == "pmgood");
    CHECK(table.refusals()[0].reason == "pack pmgood is refused: its app half failed");
    // The contract set is the context's, frozen, and not enumerable.
    CHECK(readString(rt, "String(Object.isFrozen(globalThis.__rp_packContracts))") == "true");
    CHECK(readString(rt, "String(Object.keys(globalThis).includes('__rp_packContracts'))") == "false");
}
```

`Runtime`, `evalModule`, `pumpUntil`, `readString`, `quoted` and `evalHostA` stand for what `SharedModules.test.cpp`
already has or a few lines over it (evaluating `rp_fx_host_a` is what that file does to load `rp:fx-a`); write the
missing ones there, in an anonymous namespace. Copy `SharedModules.test.cpp`'s include block exactly and in its order:
`dpfjs/host/TjsHostRuntime.hpp` first, then `#ifdef CHECK` / `#undef CHECK` / `#endif` (txiki's `utils.h` defines a
`CHECK` macro that collides with Catch2's), then `catch2/catch_test_macros.hpp`, then the headers above. Add
`test/plugin/PackModules.test.cpp` to `retroplug-classid-test`'s `SOURCES` in `tests.cmake`, and `retroplug-packs` to
its `LIBS` (`LIBS retroplug-tjs-runtime retroplug-packs   # PackTable for the pack loader's test`): the test names the
table, whose header reaches node-sdk's `graph/rp_node.h`, and the runtime links the packs library privately.

- [ ] **Step 3: Run it to see it fail**

Run: `pnpm configure && cmake --build build --target retroplug-classid-test -j$(nproc)`
Expected: FAIL to compile: `host/js/PackModules.hpp` does not exist.

- [ ] **Step 4: Implement**

`packages/native/src/host/js/PackModules.hpp`:

```cpp
// A pack's TS halves in a txiki context (docs/downloadable-packs-m2c-plan.md): the module loader that serves them by
// name, the guard that holds a pack module to the context's contracts, and the two globals the TS loader
// (src/packs/packHost.ts) reads.
#pragma once

#include <cstddef>

#include "quickjs.h"

#include "host/js/rp_js_module.h"

namespace rp::host {

namespace packs {
class PackTable;
}

/** Install the pack loader on `ctx`'s runtime, after registerSharedModules registered `contracts` there:
 *  - a module named `pack:<id>/<half>` is the file `halves.<half>` of the pack `id` in `table`, compiled the first time
 *    something imports the name (the native half is no module, and a half the pack does not name is not found);
 *  - a module whose own name starts with `pack:` may import only a name of `contracts` that does not start with `rp:`
 *    (a contract module, or a host module's facade); anything else, relative names included, is refused, naming the
 *    pack module and the specifier, before txiki's import map is consulted. Every other module's imports go to txiki's
 *    normalizer and loader as before;
 *  - `globalThis.__rp_packList()` returns, as JSON, every pack of `table` with a half other than the native one: its
 *    id, version, half kinds, provides and requires, in load order;
 *  - `globalThis.__rp_packRefused(id, reason)` records "pack <id> is refused: <reason>" in `table`'s refusals.
 *  The table is the process's (processPacks) in every host, and must outlive the runtime. Calling it again on a
 *  context is harmless. */
void installPackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count);

}  // namespace rp::host
```

`packages/native/src/host/js/PackModules.cpp`:

```cpp
#include "host/js/PackModules.hpp"

#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "tjs.h"

#include "host/packs/PackTable.hpp"

// txiki's own loader and normalizer (src/private.h, not an installed header): the pack loader answers what is a pack's
// and hands everything else to them, so every other import behaves exactly as before.
extern "C" {
char*        tjs_module_normalizer(JSContext* ctx, const char* base_name, const char* name, void* opaque);
JSModuleDef* tjs_module_loader(JSContext* ctx, const char* module_name, void* opaque, JSValueConst attributes);
int          tjs_module_attr_checker(JSContext* ctx, void* opaque, JSValueConst attributes);
int          js_module_set_import_meta(JSContext* ctx, JSValue func_val, bool use_realpath, bool is_main);
}

namespace rp::host {

namespace {

constexpr const char* kPackPrefix = "pack:";
// The context's contract names, as a frozen object's keys: written once by installPackLoader from the table the context
// registered, read by the guard. Non-enumerable, non-writable and frozen, so a pack can neither widen nor replace it.
constexpr const char* kContracts = "__rp_packContracts";

// The table every context's loader reads: the process's (processPacks) in every host, a test's own in a test. A
// pointer, not an owner: the table outlives every runtime.
std::atomic<packs::PackTable*> g_table{nullptr};

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

bool isContract(JSContext* ctx, const char* name) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue set = JS_GetPropertyStr(ctx, global, kContracts);
    bool found = false;
    if (JS_IsObject(set)) {
        JSAtom atom = JS_NewAtom(ctx, name);
        found = JS_HasProperty(ctx, set, atom) > 0;
        JS_FreeAtom(ctx, atom);
    }
    JS_FreeValue(ctx, set);
    JS_FreeValue(ctx, global);
    return found;
}

char* normalize(JSContext* ctx, const char* base, const char* name, void* opaque) {
    if (base && startsWith(base, kPackPrefix) && !isContract(ctx, name)) {
        JS_ThrowReferenceError(ctx, "%s imports '%s', which is not a RetroPlug contract module", base, name);
        return nullptr;
    }
    return tjs_module_normalizer(ctx, base, name, opaque);
}

JSModuleDef* notFound(JSContext* ctx, const char* name) {
    JS_ThrowReferenceError(ctx, "could not load '%s': no loaded pack has that half", name);
    return nullptr;
}

JSModuleDef* load(JSContext* ctx, const char* name, void* opaque, JSValueConst attributes) {
    if (!startsWith(name, kPackPrefix)) return tjs_module_loader(ctx, name, opaque, attributes);
    const std::string_view rest(name + std::strlen(kPackPrefix));
    const std::size_t slash = rest.find('/');
    packs::PackTable* table = g_table.load();
    if (slash == std::string_view::npos || !table) return notFound(ctx, name);
    const packs::LoadedPack* pack = table->find(rest.substr(0, slash));
    const std::string half(rest.substr(slash + 1));
    if (!pack || half == "native") return notFound(ctx, name);
    const auto file = pack->manifest.halves.find(half);
    if (file == pack->manifest.halves.end()) return notFound(ctx, name);

    std::ifstream in(pack->dir / std::filesystem::path(file->second), std::ios::binary);
    if (!in) {
        JS_ThrowReferenceError(ctx, "could not load '%s': %s cannot be read", name, file->second.c_str());
        return nullptr;
    }
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Compiled, not evaluated: QuickJS evaluates it when the import that asked for it completes. Its imports are
    // resolved inside this compile, through the guard above, with this module's name as their base; a refused one
    // fails the compile and frees this module alone.
    JSValue val = JS_Eval(ctx, source.c_str(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(val)) return nullptr;
    // The name is no path, so import.meta takes it as written.
    if (js_module_set_import_meta(ctx, val, false, false) != 0) {
        JS_FreeValue(ctx, val);
        return nullptr;
    }
    auto* m = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(val));
    JS_FreeValue(ctx, val);  // the module is referenced by the context's module list
    return m;
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string jsonList(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + jsonString(items[i]);
    return out + "]";
}

JSValue packList(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::string out = "[";
    bool first = true;
    if (packs::PackTable* table = g_table.load()) {
        for (const packs::LoadedPack* pack : table->loadedFrom(0)) {
            std::vector<std::string> halves;
            for (const auto& [kind, file] : pack->manifest.halves)
                if (kind != "native") halves.push_back(kind);
            if (halves.empty()) continue;
            const packs::PackManifest& m = pack->manifest;
            out += std::string(first ? "" : ",") + "{\"id\":" + jsonString(m.id) + ",\"version\":" +
                   jsonString(m.version) + ",\"halves\":" + jsonList(halves) + ",\"provides\":{\"platforms\":" +
                   jsonList(m.providesPlatforms) + ",\"nodeTypes\":" + jsonList(m.providesNodeTypes) +
                   ",\"roles\":" + jsonList(m.providesRoles) + "},\"requires\":{\"packs\":" +
                   jsonList(m.requiresPacks) + ",\"platforms\":" + jsonList(m.requiresPlatforms) + "}}";
            first = false;
        }
    }
    out += "]";
    return JS_NewStringLen(ctx, out.data(), out.size());
}

JSValue packRefused(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    packs::PackTable* table = g_table.load();
    if (!table || argc < 2) return JS_UNDEFINED;
    const char* id = JS_ToCString(ctx, argv[0]);
    const char* reason = JS_ToCString(ctx, argv[1]);
    if (id && reason) {
        const packs::LoadedPack* pack = table->find(id);
        table->recordRefusal({id, pack ? pack->dir : std::filesystem::path(),
                              std::string("pack ") + id + " is refused: " + reason});
    }
    JS_FreeCString(ctx, id);
    JS_FreeCString(ctx, reason);
    return JS_UNDEFINED;
}

}  // namespace

void installPackLoader(JSContext* ctx, packs::PackTable& table, const rp_js_module* contracts, std::size_t count) {
    g_table.store(&table);
    // txiki's opaque is its TJSRuntime; the wrappers hand it on unchanged.
    JS_SetModuleLoaderFunc2(JS_GetRuntime(ctx), normalize, load, tjs_module_attr_checker, TJS_GetRuntime(ctx));

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mark = JS_GetPropertyStr(ctx, global, kContracts);
    const bool installed = JS_IsObject(mark);
    JS_FreeValue(ctx, mark);
    if (!installed) {
        JSValue set = JS_NewObjectProto(ctx, JS_NULL);
        for (std::size_t i = 0; i < count; ++i)
            if (!startsWith(contracts[i].name, "rp:"))
                JS_DefinePropertyValueStr(ctx, set, contracts[i].name, JS_TRUE, 0);
        JS_FreezeObject(ctx, set);
        JS_DefinePropertyValueStr(ctx, global, kContracts, set, 0);
        JS_DefinePropertyValueStr(ctx, global, "__rp_packList", JS_NewCFunction(ctx, packList, "__rp_packList", 0), 0);
        JS_DefinePropertyValueStr(ctx, global, "__rp_packRefused",
                                  JS_NewCFunction(ctx, packRefused, "__rp_packRefused", 2), 0);
    }
    JS_FreeValue(ctx, global);
}

}  // namespace rp::host
```

(`JS_FreezeObject` is declared in this QuickJS's `quickjs.h`; if a call above is spelled differently in the tree's
QuickJS or txiki, follow the tree. `std::vector` needs `<vector>`.)

In `packages/native/CMakeLists.txt`, `retroplug-tjs-runtime` gains the source and the packs library:

```cmake
add_library(retroplug-tjs-runtime STATIC
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp
    src/host/js/SharedModules.cpp     # the shared module sets' loader (docs/downloadable-packs-m2b-plan.md)
    src/host/js/PackModules.cpp)      # a pack's TS halves and their import guard (docs/downloadable-packs-m2c-plan.md)
```

and `target_link_libraries(retroplug-tjs-runtime PUBLIC tjs PRIVATE retroplug-packs)` (PackModules.hpp names the table
by forward declaration only, so the packs headers stay private). If `retroplug-packs` is defined after this point,
CMake still resolves the name at generate time.

- [ ] **Step 5: Run it to see it pass**

Run: `cmake --build build --target retroplug-classid-test -j$(nproc) && build/bin/retroplug-classid-test`
Expected: `All tests passed`, exit 0 (the earlier shared-module cases unchanged). Then prove the guard can fail: change
`normalize` to return `tjs_module_normalizer(...)` unconditionally, rebuild, see the three guard checks fail, restore.

- [ ] **Step 6: Commit**

```bash
git add packages/native/src/host/js/PackModules.hpp packages/native/src/host/js/PackModules.cpp \
  packages/native/test/plugin/PackModules.test.cpp packages/native/test/plugin/pack-modules-fixture \
  packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "packs: a pack's TS half is a module the context loads by name, and may import only a contract"
```

---

### Task 4: The TS loader

**Files:**
- Create: `packages/retroplug/src/packs/packHost.ts`, `packages/retroplug/src/packs/loadPacks.ts`
- Test: `packages/retroplug/test/packs/load-packs.test.ts`

- [ ] **Step 1: Write the failing test**

`packages/retroplug/test/packs/load-packs.test.ts`. The test's bundle carries its own copy of the registries, seeded by
the desktop's generated modules as the app's are, and drives the loader through a fake host.

```ts
// The pack loader (docs/downloadable-packs-m2c-plan.md): a pack's halves imported through the host, held to its
// manifest, and committed all or nothing per pack, in one commit when every pack passes.
import { test, expect } from "@retroplug/testing";
import { loadPacks, packStatus, APP_BUILDERS, type HalfBuilder } from "../../src/packs/loadPacks";
import type { HalfKind, PackHost, PackInfo } from "../../src/packs/packHost";
import { rolesRegistry } from "../../src/contributions/appRegistries";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import { contributionsVersion } from "../../src/contributions/registry";

const role = (kind: string) => ({ kind, category: "feature", schema: { parse: () => ({}) } });

function pack(id: string, over: Partial<PackInfo> = {}): PackInfo {
  return {
    id,
    version: "1.0.0",
    halves: ["app"],
    provides: { platforms: [], nodeTypes: [], roles: [] },
    requires: { packs: [], platforms: [] },
    ...over,
  };
}

/** A host serving `modules[id][half]` as each half's default export (a function is called, so a half can throw). */
function fakeHost(packs: PackInfo[], modules: Record<string, Partial<Record<HalfKind, unknown>>>) {
  const refused: Record<string, string> = {};
  const imported: string[] = [];
  const host: PackHost = {
    list: () => packs,
    importHalf: async (id, half) => {
      imported.push(`${id}/${half}`);
      const m = modules[id]?.[half];
      if (typeof m === "function") return { default: (m as () => unknown)() };
      return { default: m };
    },
    refuse: (id, reason) => { refused[id] = reason; },
  };
  return { host, refused, imported };
}

test("a pack's app half commits its roles, in one commit with the other packs'", async () => {
  const before = contributionsVersion();
  const { host, refused } = fakeHost(
    [pack("lp-a", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } }),
     pack("lp-b", { provides: { platforms: [], nodeTypes: [], roles: ["lp-b-role"] } })],
    { "lp-a": { app: { roles: [role("lp-a-role")] } }, "lp-b": { app: { roles: [role("lp-b-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(rolesRegistry.items().some((r) => r.kind === "lp-a-role")).toBe(true);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-b-role")).toBe(true);
  expect(contributionsVersion()).toBe(before + 1);
  expect(packStatus().loaded["lp-a"]).toEqual(["app"]);
});

test("a half loaded in this context is not imported again", async () => {
  const { host, imported } = fakeHost(
    [pack("lp-a", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } })],
    { "lp-a": { app: { roles: [role("lp-a-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(imported).toEqual([]);
});

test("a pack that fails the commit is refused by name, and the others commit", async () => {
  const builtIn = rolesRegistry.items()[0]!.kind;
  const { host, refused } = fakeHost(
    [pack("lp-dupe", { provides: { platforms: [], nodeTypes: [], roles: [builtIn] } }),
     pack("lp-c", { provides: { platforms: [], nodeTypes: [], roles: ["lp-c-role"] } })],
    { "lp-dupe": { app: { roles: [role(builtIn)] } }, "lp-c": { app: { roles: [role("lp-c-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-dupe"] ?? "").includes(`declare the role kind ${builtIn} twice`)).toBe(true);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-c-role")).toBe(true);
  expect((packStatus().refused["lp-dupe"] ?? "").includes("twice")).toBe(true);
});

test("the manifest is held to the halves", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-undeclared"),
     pack("lp-overdeclared", { provides: { platforms: [], nodeTypes: [], roles: ["lp-ghost"] } }),
     pack("lp-embeds", { provides: { platforms: [], nodeTypes: [], roles: [] } }),
     pack("lp-kind"),
     pack("lp-throws"),
     pack("lp-shape"),
     pack("lp-dependent", { requires: { packs: ["lp-throws"], platforms: [] } }),
     pack("lp-needs-platform", { halves: ["app", "consoles"], requires: { packs: [], platforms: ["lp-nowhere"] } })],
    {
      "lp-undeclared": { app: { roles: [role("lp-undeclared-role")] } },
      "lp-overdeclared": { app: {} },
      "lp-embeds": { app: { embeds: [{ id: "lp-rom" }] } },
      "lp-kind": { app: { controllerApps: [{ id: "x" }] } },
      "lp-throws": { app: () => { throw new Error("the half threw"); } },
      "lp-shape": { app: 42 },
      "lp-dependent": { app: {} },
      "lp-needs-platform": { app: {}, consoles: {} },
    },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-undeclared"] ?? "").includes("contributes the role kind lp-undeclared-role, which its pack.json's " +
    "provides.roles does not declare")).toBe(true);
  expect((refused["lp-overdeclared"] ?? "").includes("provides.roles names lp-ghost, which no half of the pack contributes")).toBe(true);
  expect((refused["lp-embeds"] ?? "").includes("a pack cannot carry an embedded ROM")).toBe(true);
  expect((refused["lp-kind"] ?? "").includes("controllerApps")).toBe(true);
  expect((refused["lp-throws"] ?? "").includes("the half threw")).toBe(true);
  expect((refused["lp-shape"] ?? "").includes("its app half's default export is not an object")).toBe(true);
  expect((refused["lp-dependent"] ?? "").includes("requires the pack lp-throws, which is refused")).toBe(true);
  expect((refused["lp-needs-platform"] ?? "").includes("requires the platform lp-nowhere, which no loaded console provides")).toBe(true);
});

test("a console's platforms and node type are the pack's to declare", async () => {
  // A console needs its core's system role: borrow a built-in console's, on a platform of the pack's own.
  const borrowed = consolesRegistry.items()[0]!;
  const descriptor = { ...borrowed, id: "lp-console", nodeType: "rp.lp.console",
    platforms: [{ ...borrowed.platforms[0]!, id: "lp-plat", extensions: [".lpx"] }] };
  const { host, refused } = fakeHost(
    [pack("lp-console-bad", { halves: ["consoles"], provides: { platforms: [], nodeTypes: [], roles: [] } }),
     pack("lp-console", { halves: ["consoles"],
       provides: { platforms: ["lp-plat"], nodeTypes: ["rp.lp.console"], roles: [] } })],
    { "lp-console-bad": { consoles: { consoles: [descriptor] } }, "lp-console": { consoles: { consoles: [descriptor] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-console-bad"] ?? "").includes("the platform lp-plat, which its pack.json's provides.platforms does " +
    "not declare")).toBe(true);
  expect(refused["lp-console"]).toBe(undefined);
  expect(consolesRegistry.items().some((d) => d.id === "lp-console")).toBe(true);
});

test("a context's builders decide which halves it loads", async () => {
  const only: Partial<Record<HalfKind, HalfBuilder>> = { consoles: APP_BUILDERS.consoles };
  const { host, imported } = fakeHost([pack("lp-app-only", { halves: ["app"] })], { "lp-app-only": { app: {} } });
  await loadPacks(host, only);
  expect(imported).toEqual([]);
});
```

The harness (`packages/testing/ts/harness.ts`) has `toBe`, `toEqual` (which compares `JSON.stringify` output), the
truthiness and comparison matchers, `toBeCloseTo` and `toThrow`, and no `toContain`: a string is checked with
`.includes(...)` and `toBe(true)`, as above.

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test packs/load-packs`
Expected: FAIL: the build cannot resolve `../../src/packs/loadPacks`.

- [ ] **Step 3: Implement**

`packages/retroplug/src/packs/packHost.ts`:

```ts
// The host's half of a pack's TS halves (docs/downloadable-packs-m2c-plan.md): which packs the process loaded, how a
// half is imported, and where a refusal is reported. A txiki host installs the globals (host/js/PackModules.cpp)
// before its entry evaluates; a context without them (a test's bundle, the render worker) has no packs.

/** The halves a context may load (the protocol and script halves are M2d's). */
export type HalfKind = "app" | "consoles" | "ui" | "views" | "cli";

/** A pack as its pack.json declares it: what the loader holds its halves to. */
export interface PackInfo {
  readonly id: string;
  readonly version: string;
  /** Every half the manifest names but the native one. */
  readonly halves: readonly string[];
  readonly provides: { readonly platforms: readonly string[]; readonly nodeTypes: readonly string[];
    readonly roles: readonly string[] };
  readonly requires: { readonly packs: readonly string[]; readonly platforms: readonly string[] };
}

export interface PackHost {
  /** The packs with a TS half, in load order (dependency order). */
  list(): readonly PackInfo[];
  /** The half's module namespace. */
  importHalf(id: string, half: HalfKind): Promise<unknown>;
  /** Report a refusal where Settings > Packs (M4) reads them. */
  refuse(id: string, reason: string): void;
}

interface PackHooks {
  __rp_packList?: () => string;
  __rp_packRefused?: (id: string, reason: string) => void;
}

// A specifier no bundler may resolve: a half is served by the host's module loader, by name.
const importByName = (name: string): Promise<unknown> => import(name);

export function nativePackHost(): PackHost {
  const g = globalThis as PackHooks;
  return {
    list: () => (g.__rp_packList ? (JSON.parse(g.__rp_packList()) as PackInfo[]) : []),
    importHalf: (id, half) => importByName(`pack:${id}/${half}`),
    refuse: (id, reason) => g.__rp_packRefused?.(id, reason),
  };
}
```

`packages/retroplug/src/packs/loadPacks.ts`:

```ts
// The pack loader (docs/downloadable-packs-m2c-plan.md): each pack's halves a context loads, imported by name, held to
// the pack's manifest, and committed into the live registries. Every pack of a call goes in one commit; when that is
// refused, they commit one at a time in load order, so the refused pack is named and the rest still load (all or
// nothing is per pack). A pack refused in a later context (its UI half, at the first editor open) keeps what an earlier
// one committed: native code and a commit are never undone, and the refusal is reported.
//
// State is per module set: the plugin's control plane and its UI share one, so a half the control plane loaded is not
// loaded again for the UI, and a pack the control plane refused is not tried there.
import "../generated/packages";
import { commitContributions, type Addition } from "../contributions/registry";
import { appAdditions } from "../contributions/appRegistries";
import { consolesRegistry, realCartsRegistry } from "../contributions/consoleRegistries";
import { KIND_DATA } from "../generated/protocol";
import type { PackageContributions } from "@retroplug/core/definePackage";
import type { ConsoleDescriptor, ConsolesContribution } from "@retroplug/core/consoles";
import { nativePackHost, type HalfKind, type PackHost, type PackInfo } from "./packHost";

/** A half's default export as the additions a commit takes; throws when the export is not the half's shape. */
export type HalfBuilder = (pkg: string, exported: unknown) => Addition<any>[];

/** The default export as an object, or the refusal that names the half. */
export function objectExport(half: HalfKind, exported: unknown): Record<string, unknown> {
  if (typeof exported !== "object" || exported === null || Array.isArray(exported)) {
    throw new Error(`its ${half} half's default export is not an object`);
  }
  return exported as Record<string, unknown>;
}

/** The halves the control plane loads: the app's registries and the consoles'. The UI's and the CLI's builders add
 *  theirs (ui/packs/uiPacks.ts, cli/packs/cliPacks.ts). */
export const APP_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  app: (pkg, exported) => appAdditions(pkg, objectExport("app", exported) as PackageContributions),
  consoles: (pkg, exported) => {
    const c = objectExport("consoles", exported) as ConsolesContribution;
    return [
      { registry: consolesRegistry, pkg, items: c.consoles ?? [] },
      { registry: realCartsRegistry, pkg, items: c.realCarts ?? [] },
    ];
  },
};

const loaded = new Map<string, Set<HalfKind>>();
const refused = new Map<string, string>();

/** What this module set loaded and refused: each pack's halves, and each refused pack's reason. */
export function packStatus(): { loaded: Record<string, HalfKind[]>; refused: Record<string, string> } {
  return {
    loaded: Object.fromEntries([...loaded].map(([id, halves]) => [id, [...halves]])),
    refused: Object.fromEntries(refused),
  };
}

function refuse(host: PackHost, id: string, reason: string): void {
  refused.set(id, reason);
  console.warn(`[packs] pack ${id} is refused: ${reason}`);
  host.refuse(id, reason);
}

function message(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

/** The pack's manifest against the halves loaded now (`exported`, by kind): throws the first mismatch. */
function checkManifest(pack: PackInfo, exported: Map<HalfKind, unknown>, platforms: ReadonlySet<string>): void {
  if (exported.has("app")) {
    const app = objectExport("app", exported.get("app"));
    if (Array.isArray(app.embeds) && app.embeds.length) {
      throw new Error("its app half declares embeds, and a pack cannot carry an embedded ROM yet (it would be baked " +
        "into the RetroPlug binary)");
    }
    for (const kind of Object.keys(KIND_DATA)) {
      if (Array.isArray(app[kind]) && (app[kind] as unknown[]).length) {
        throw new Error(`its app half contributes ${kind}, a contribution kind a pack cannot add to yet`);
      }
    }
    const roles = ((app.roles ?? []) as { kind: string }[]).map((r) => r.kind);
    for (const kind of roles) {
      if (!pack.provides.roles.includes(kind)) {
        throw new Error(`its app half contributes the role kind ${kind}, which its pack.json's provides.roles does ` +
          "not declare");
      }
    }
    for (const kind of pack.provides.roles) {
      if (!roles.includes(kind)) throw new Error(`provides.roles names ${kind}, which no half of the pack contributes`);
    }
  } else if (pack.provides.roles.length && !pack.halves.includes("app")) {
    throw new Error(`provides.roles names ${pack.provides.roles[0]}, which no half of the pack contributes`);
  }
  if (exported.has("consoles")) {
    const consoles = ((objectExport("consoles", exported.get("consoles")).consoles ?? []) as ConsoleDescriptor[]);
    const mine = new Set<string>();
    for (const d of consoles) {
      if (!pack.provides.nodeTypes.includes(d.nodeType)) {
        throw new Error(`its console ${d.id} names the node type ${d.nodeType}, which its pack.json's ` +
          "provides.nodeTypes does not declare");
      }
      for (const p of d.platforms) {
        if (!pack.provides.platforms.includes(p.id)) {
          throw new Error(`its console ${d.id} declares the platform ${p.id}, which its pack.json's ` +
            "provides.platforms does not declare");
        }
        mine.add(p.id);
      }
    }
    for (const id of pack.provides.platforms) {
      if (!mine.has(id)) throw new Error(`provides.platforms names ${id}, which no half of the pack contributes`);
    }
    for (const id of pack.requires.platforms) {
      if (!platforms.has(id) && !mine.has(id)) {
        throw new Error(`it requires the platform ${id}, which no loaded console provides`);
      }
    }
  } else if (pack.provides.platforms.length && !pack.halves.includes("consoles")) {
    throw new Error(`provides.platforms names ${pack.provides.platforms[0]}, which no half of the pack contributes`);
  }
}

interface Staged {
  readonly pack: PackInfo;
  readonly kinds: readonly HalfKind[];
  readonly additions: Addition<any>[];
}

/** Load every pack's halves `builders` names that this module set has not loaded yet. Never rejects: a pack that fails
 *  is refused, reported, and commits nothing. */
export async function loadPacks(host: PackHost, builders: Partial<Record<HalfKind, HalfBuilder>>): Promise<void> {
  const kinds = Object.keys(builders) as HalfKind[];
  const platforms = new Set(consolesRegistry.items().flatMap((d) => d.platforms.map((p) => p.id)));
  const staged: Staged[] = [];
  for (const pack of host.list()) {
    if (refused.has(pack.id)) continue;
    const missing = pack.requires.packs.find((dep) => refused.has(dep));
    if (missing) {
      refuse(host, pack.id, `it requires the pack ${missing}, which is refused`);
      continue;
    }
    const todo = kinds.filter((k) => pack.halves.includes(k) && !loaded.get(pack.id)?.has(k));
    if (!todo.length) continue;
    try {
      const exported = new Map<HalfKind, unknown>();
      for (const kind of todo) {
        const ns = (await host.importHalf(pack.id, kind)) as { default?: unknown };
        exported.set(kind, ns.default);
      }
      checkManifest(pack, exported, platforms);
      const additions = todo.flatMap((kind) => builders[kind]!(pack.id, exported.get(kind)));
      staged.push({ pack, kinds: todo, additions });
      for (const id of pack.provides.platforms) platforms.add(id);
    } catch (e) {
      refuse(host, pack.id, message(e));
    }
  }
  if (!staged.length) return;
  const accept = (s: Staged): void => {
    const halves = loaded.get(s.pack.id) ?? new Set<HalfKind>();
    for (const kind of s.kinds) halves.add(kind);
    loaded.set(s.pack.id, halves);
  };
  try {
    commitContributions(staged.flatMap((s) => s.additions));
    staged.forEach(accept);
  } catch {
    for (const s of staged) {
      const missing = s.pack.requires.packs.find((dep) => refused.has(dep));
      if (missing) {
        refuse(host, s.pack.id, `it requires the pack ${missing}, which is refused`);
        continue;
      }
      try {
        commitContributions(s.additions);
        accept(s);
      } catch (e) {
        refuse(host, s.pack.id, message(e));
      }
    }
  }
}

/** The control plane's packs: their app and consoles halves. */
export function loadAppPacks(): Promise<void> {
  return loadPacks(nativePackHost(), APP_BUILDERS);
}
```

Check, and follow the tree on: the module that exports `consolesRegistry` and `realCartsRegistry` (`consoleRegistries.ts`)
and whether importing `../generated/packages` seeds every registry `loadPacks` commits to before it runs (it imports the
consoles and placers seeds; if `src/consoles.ts` is the facade the shell's consumers use, import that too); the type
names `PackageContributions` and `ConsolesContribution` and their modules (the generated packages.ts imports them); and
that `KIND_DATA` is exported by `src/generated/protocol.ts` for every product (it is `{}` when no package declares a
kind). `commitContributions`'s typed tuple takes `Addition<any>[]` the way `appAdditions` returns it; if the compiler
wants the spread typed, cast at the call as the generated module does.

- [ ] **Step 4: Run it to see it pass**

Run: `pnpm test packs/load-packs && pnpm typecheck && pnpm check:boundaries`
Expected: every case `ok`, exit 0; typecheck exit 0; boundaries clean.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/src/packs/packHost.ts packages/retroplug/src/packs/loadPacks.ts \
  packages/retroplug/test/packs/load-packs.test.ts
git commit -m "packs: the TS loader imports a pack's halves, holds them to its manifest and commits them per context"
```

---

### Task 5: Building a pack's halves, and the TS fixture pack

**Files:**
- Create: `tools/build-pack-halves.mjs`, `packages/retroplug/test/script/build-pack-halves.test.ts`
- Create: `packages/native/test/packs/ts-fixture/` (the package, the variants, `CMakeLists.txt`)
- Modify: `packages/native/cmake/tests.cmake` (add the fixture's directory), `package.json` (`test:cli-ts`'s targets)

- [ ] **Step 1: The tool**

`tools/build-pack-halves.mjs`:

```js
#!/usr/bin/env node
// A package's TS halves built as a pack's (docs/downloadable-packs-loading-design.md section 3,
// docs/downloadable-packs-m2c-plan.md): each half its package.json names (retroplug.app, consoles, ui, views, cli) one
// ES module bundle, js/<half>.js, with every contract module and host module external, so the pack links to its
// RetroPlug's own; the package's other imports are bundled in, except another @retroplug package's module, which a
// pack may not carry a copy of and is refused. Then pack.json: the template's members, its API level this build's,
// and its halves the ones built (a native half the template names is kept).
//
//   node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template>
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import esbuild from "esbuild";
import { SHELL_IMPORTS, UI_IMPORTS, hostModuleSpecifiers } from "../packages/retroplug/scripts/lib/contracts.mjs";

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const HALVES = ["app", "consoles", "ui", "views", "cli"];

/** RP_API_LEVEL from the header a pack's entry compiles against (cmake/packs.cmake reads the same line). */
function apiLevel() {
  const header = readFileSync(join(REPO, "packages/node-sdk/native/src/graph/rp_pack.h"), "utf8");
  const found = /^#define RP_API_LEVEL (\d+)u\s*$/m.exec(header);
  if (!found) throw new Error("rp_pack.h has no `#define RP_API_LEVEL <n>u` line");
  return Number(found[1]);
}

/** What a pack may import: the contracts and the host modules its contexts register. */
export function packExternals() {
  const hostModules = hostModuleSpecifiers(JSON.parse(readFileSync(join(REPO, "packages/retroplug/host-modules.json"),
    "utf8")));
  // The two lists are { specifier, reason } entries (build-shared-modules.mjs reads them the same way).
  return [...new Set([...SHELL_IMPORTS.map((e) => e.specifier), ...UI_IMPORTS.map((e) => e.specifier), ...hostModules])];
}

export async function buildPackHalves({ packageDir, outDir, template }) {
  const manifest = JSON.parse(readFileSync(join(packageDir, "package.json"), "utf8"));
  const block = manifest.retroplug ?? {};
  const externals = packExternals();
  const pack = JSON.parse(readFileSync(template, "utf8"));
  const halves = pack.halves?.native ? { native: pack.halves.native } : {};
  for (const half of HALVES) {
    const entry = block[half];
    if (typeof entry !== "string") continue;
    const refuseForeign = {
      name: "refuse-foreign-retroplug",
      setup(build) {
        build.onResolve({ filter: /^@retroplug\// }, (args) => {
          if (externals.includes(args.path)) return { path: args.path, external: true };
          return { errors: [{ text: `${args.importer} imports ${args.path}, which is not a contract module: a pack ` +
            "may import only the contracts (scripts/lib/contracts.mjs) and the host modules" }] };
        });
      },
    };
    const outfile = join(outDir, "js", `${half}.js`);
    await esbuild.build({
      entryPoints: [join(packageDir, entry)],
      bundle: true,
      format: "esm",
      platform: "neutral",
      mainFields: ["module", "main"],
      target: "es2020",
      outfile,
      external: externals.filter((name) => !name.startsWith("@retroplug/")),
      plugins: [refuseForeign],
      jsx: "automatic",
      define: { "process.env.NODE_ENV": '"production"' },
      nodePaths: [join(REPO, "node_modules")],
      logLevel: "silent",
    });
    halves[half] = `js/${half}.js`;
  }
  pack.apiLevel = apiLevel();
  pack.halves = halves;
  mkdirSync(outDir, { recursive: true });
  writeFileSync(join(outDir, "pack.json"), `${JSON.stringify(pack, null, 2)}\n`);
  return pack;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [packageDir, outDir, template] = process.argv.slice(2);
  if (!packageDir || !outDir || !template) {
    console.error("usage: node tools/build-pack-halves.mjs <package dir> <version dir> <pack.json template>");
    process.exit(2);
  }
  buildPackHalves({ packageDir: resolve(packageDir), outDir: resolve(outDir), template: resolve(template) })
    .catch((e) => { console.error(e?.message ?? e); process.exit(1); });
}
```

(If the tree's esbuild import is `import * as esbuild from "esbuild"`, as in `tools/build-shared-modules.mjs`, follow it.)

- [ ] **Step 2: The fixture package**

`packages/native/test/packs/ts-fixture/package.json` (no workspace member: it is under `test/`, which pnpm's workspace
globs do not reach; check `pnpm-workspace.yaml`, and if they do, name it so the boundary check skips it and say so):

```json
{
  "name": "@retroplug-test/ts-fixture",
  "private": true,
  "retroplug": { "app": "ts/app.ts", "ui": "ts/ui.ts", "cli": "ts/cli.ts" }
}
```

`packages/native/test/packs/ts-fixture/pack.json` (the template; the tool fills `apiLevel` and `halves`):

```json
{
  "id": "tsfix", "name": "TS fixture pack", "version": "1.0.0", "apiLevel": 0,
  "license": "MIT", "source": "packages/native/test/packs/ts-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": [], "roles": ["tsfix"] },
  "halves": {}
}
```

`ts/app.ts`:

```ts
// The TS fixture pack's app half (docs/downloadable-packs-m2c-plan.md): one feature role, with a schema of its own (no
// zod), which the UI's role check parses.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({
  roles: [{
    kind: "tsfix",
    category: "feature",
    schema: { parse: (c: unknown) => ({ level: typeof (c as { level?: unknown })?.level === "number" ? (c as { level: number }).level : 3 }) },
  }],
});
```

`ts/ui.ts`:

```ts
// The TS fixture pack's UI half: a start-menu row the UI test finds by its label.
import { defineUi } from "@retroplug/ui/contribution";

export default defineUi({
  menus: [{
    placement: "start",
    build: () => [{ id: "tsfix-row", label: "TS Fixture Row", kind: "action", onSelect: () => {} }],
  }],
});
```

`ts/cli.ts`:

```ts
// The TS fixture pack's CLI half: one command.
import { defineCliTools } from "@retroplug/core/cliTool";

export default defineCliTools([{
  name: "tsfix-hello",
  summary: "Say hello from the TS fixture pack",
  help: "usage: retroplug-cli tsfix-hello",
  run: () => { console.log("hello from the ts fixture pack"); },
}]);
```

Read `test-ui/contributions-live.test.ts`'s menu builder and `defineUi`'s `MenuBuilder` type: if a row needs a member
these lack, add it as that test does.

The broken variants, each a folder `packages/native/test/packs/ts-fixture/variants/<id>/` with `pack.json.in` (like the
template, with `"apiLevel": @RP_API_LEVEL@`, its own `id`, and the `halves` and `provides` the table gives) and the
hand-written module(s):

| id | halves | provides / requires | module |
|---|---|---|---|
| `tsfix-throws` | `app: js/app.js` | none | `throw new Error("the fixture half throws");` |
| `tsfix-guard` | `app: js/app.js` | none | `import app from "@retroplug/sameboy/app"; export default app;` |
| `tsfix-undeclared` | `app: js/app.js` | none | `export default { roles: [{ kind: "tsfix-undeclared", category: "feature", schema: { parse: () => ({}) } }] };` |
| `tsfix-cliclash` | `cli: js/cli.js` | none | `export default [{ name: "render", summary: "clash", help: "", run() {} }];` |
| `tsfix-dependent` | `app: js/app.js` | `requires.packs: ["tsfix-throws"]` | `export default {};` |
| `tsfix-protocol` | `protocol: js/protocol.js` | none | `export default [];` |

`packages/native/test/packs/ts-fixture/CMakeLists.txt`:

```cmake
# The TS fixture pack (docs/downloadable-packs-m2c-plan.md): tsfix, built from ts/ by tools/build-pack-halves.mjs as a
# package's halves are, and its broken variants, hand-written (the tool would refuse them), each staged under
# build/packs/<id>/1.0.0/ as a development pack. No product lists any of them.
set(_tsfix_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_tsfix_out "${RP_PACKS_DIR}/tsfix/1.0.0")
file(GLOB _tsfix_sources CONFIGURE_DEPENDS "${_tsfix_dir}/ts/*.ts")
add_custom_command(
    OUTPUT "${_tsfix_out}/pack.json"
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${_tsfix_dir} ${_tsfix_out} ${_tsfix_dir}/pack.json
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_tsfix_dir}/package.json ${_tsfix_dir}/pack.json
            ${_tsfix_sources}
            # what the tool reads besides the package: the API level it stamps and the contracts it keeps external
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs
            ${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json
    COMMENT "Building the TS fixture pack"
    VERBATIM)
add_custom_target(retroplug-ts-fixture-packs DEPENDS "${_tsfix_out}/pack.json")
# Every staged folder is claimed once (cmake/packs.cmake's RP_PACK_STAGED), so two fixtures cannot stage one id.
set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "tsfix/1.0.0")

foreach(_v tsfix-throws tsfix-guard tsfix-undeclared tsfix-cliclash tsfix-dependent tsfix-protocol)
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_v}/1.0.0")
    configure_file("${_tsfix_dir}/variants/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    file(GLOB _v_js RELATIVE "${_tsfix_dir}/variants/${_v}" CONFIGURE_DEPENDS "${_tsfix_dir}/variants/${_v}/js/*.js")
    foreach(_js IN LISTS _v_js)
        configure_file("${_tsfix_dir}/variants/${_v}/${_js}" "${RP_PACKS_DIR}/${_v}/1.0.0/${_js}" COPYONLY)
    endforeach()
endforeach()
```

In `tests.cmake`, beside the M1 fixture's `add_subdirectory`:
`add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test/packs/ts-fixture ${CMAKE_BINARY_DIR}/packs-ts-fixture EXCLUDE_FROM_ALL)`.
`RP_PACKS_DIR` and `NODE_EXECUTABLE` are the names `cmake/packs.cmake` and the bundles use; follow the tree's. In
`package.json`, `test:cli-ts` builds `retroplug-cli retroplug-ts-fixture-packs`, and `test:ui` builds
`retroplug-ui-test retroplug-ts-fixture-packs`.

- [ ] **Step 3: The tool's test**

`packages/retroplug/test/script/build-pack-halves.test.ts` runs the tool in a node child, as
`test/script/build-ts-stripper.test.ts` does (read it: its local `tjs` declaration and `text()` helper are what this
needs), building the fixture into a scratch folder and one refused package:

```ts
// tools/build-pack-halves.mjs (docs/downloadable-packs-m2c-plan.md): a package's halves built as a pack's, each one ES
// module importing only contract modules, and a package importing another package's module refused.
import { test, expect } from "@retroplug/testing";
// (the local tjs declaration and text() helper from build-ts-stripper.test.ts)

async function runNode(script: string): Promise<{ status: number; out: string; err: string }> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, s] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { status: s.exit_status, out, err };
}

test("the fixture's halves are built, each importing only contracts, and its pack.json written", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-halves-"));',
    'const fixture = path.join(repo, "packages/native/test/packs/ts-fixture");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: fixture, outDir: work, template: path.join(fixture, "pack.json") });',
    '  const imports = {};',
    '  for (const half of Object.keys(pack.halves)) {',
    '    const src = fs.readFileSync(path.join(work, pack.halves[half]), "utf8");',
    '    imports[half] = [...src.matchAll(/^import[^"\']*["\']([^"\']+)["\']/gm)].map((x) => x[1]);',
    '  }',
    '  const contracts = m.packExternals();',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '  process.stdout.write(JSON.stringify({ pack, imports, contracts }));',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const { pack, imports, contracts } = JSON.parse(run.out) as {
    pack: { apiLevel: number; halves: Record<string, string> };
    imports: Record<string, string[]>;
    contracts: string[];
  };
  expect(pack.halves).toEqual({ app: "js/app.js", ui: "js/ui.js", cli: "js/cli.js" });
  expect(pack.apiLevel > 0).toBe(true);
  for (const [half, list] of Object.entries(imports)) {
    for (const name of list) {
      if (!contracts.includes(name)) throw new Error(`${half} imports ${name}, which is not a contract`);
    }
  }
  expect(imports.app).toEqual(["@retroplug/core/definePackage"]);
});

test("a package importing another package's module is refused, naming it", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-halves-"));',
    'fs.mkdirSync(path.join(work, "pkg/ts"), { recursive: true });',
    'fs.writeFileSync(path.join(work, "pkg/package.json"), JSON.stringify({ retroplug: { app: "ts/app.ts" } }));',
    'fs.writeFileSync(path.join(work, "pkg/ts/app.ts"), \'import app from "@retroplug/sameboy/app"; export default app;\');',
    'fs.writeFileSync(path.join(work, "pack.json"), JSON.stringify({ id: "x", halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href)',
    '  .then((m) => m.buildPackHalves({ packageDir: path.join(work, "pkg"), outDir: path.join(work, "out"),',
    '    template: path.join(work, "pack.json") }))',
    '  .then(() => { process.stdout.write("built"); }, (e) => { process.stdout.write(String(e.message)); })',
    '  .finally(() => fs.rmSync(work, { recursive: true, force: true }));',
  ].join("\n");
  const run = await runNode(script);
  expect(run.out.includes("@retroplug/sameboy/app, which is not a contract module")).toBe(true);
});
```

(The suite runs from `packages/retroplug`, so the repository is two levels up, as in build-ts-stripper.test.ts.)

- [ ] **Step 4: Run**

Run: `pnpm configure && cmake --build build --target retroplug-ts-fixture-packs -j$(nproc) && ls build/packs/tsfix/1.0.0/js && cat build/packs/tsfix/1.0.0/pack.json && pnpm test script/build-pack-halves`
Expected: `app.js cli.js ui.js`; the pack.json with this build's `apiLevel` and the three halves; both cases `ok`, exit 0.
Also `pnpm check:boundaries` (exit 0; if the fixture trips a rule, say which and how it was resolved).

- [ ] **Step 5: Commit**

```bash
git add tools/build-pack-halves.mjs packages/retroplug/test/script/build-pack-halves.test.ts \
  packages/native/test/packs/ts-fixture packages/native/cmake/tests.cmake package.json
git commit -m "packs: a package's TS halves build as a pack's, and the TS fixture pack with its broken variants"
```

---

### Task 6: The contexts load their packs

**Files:**
- Create: `packages/retroplug/ui/packs/uiPacks.ts`, `packages/retroplug/cli/packs/cliPacks.ts`
- Modify: `packages/retroplug/src/pluginControlPlane.ts`, `packages/retroplug/ui/main.tsx`,
  `packages/retroplug/ui/host/nativeHooks.ts`, `packages/retroplug/cli/cli.ts`
- Modify: `packages/native/plugin/PluginDSP.cpp`, `packages/native/plugin/PluginUI.cpp`, `packages/native/sdl/main.cpp`,
  `packages/native/test/ui/UiHarness.cpp`, `packages/native/cli/main.cpp`

- [ ] **Step 1: The UI's and the CLI's builders**

`packages/retroplug/ui/packs/uiPacks.ts`:

```ts
// The UI's packs (docs/downloadable-packs-m2c-plan.md): their ui and views halves, and their app and consoles halves
// when no control plane loaded them first (the UI test harness). A commit re-renders the UI (M2a), so the load runs
// beside the first render rather than before it.
import "../contributions/menus";
import "../contributions/views";
import { uiRegistry, viewsRegistry } from "../contributions/uiRegistries";
import { APP_BUILDERS, loadPacks, objectExport, type HalfBuilder } from "../../src/packs/loadPacks";
import { nativePackHost, type HalfKind } from "../../src/packs/packHost";
import type { UiContribution, ViewsContribution } from "@retroplug/ui/contribution";

export const UI_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  ...APP_BUILDERS,
  ui: (pkg, exported) => [{ registry: uiRegistry, pkg, items: [objectExport("ui", exported) as UiContribution] }],
  views: (pkg, exported) => [
    { registry: viewsRegistry, pkg, items: (objectExport("views", exported) as unknown as ViewsContribution).views ?? [] },
  ],
};

export function loadUiPacks(): Promise<void> {
  return loadPacks(nativePackHost(), UI_BUILDERS);
}
```

`packages/retroplug/cli/packs/cliPacks.ts`:

```ts
// The CLI dispatcher's packs (docs/downloadable-packs-m2c-plan.md): their cli halves, and the app and consoles halves a
// tool's session reads.
import "../tools";
import { cliToolsRegistry } from "../contributions/toolRegistries";
import { APP_BUILDERS, loadPacks, type HalfBuilder } from "../../src/packs/loadPacks";
import { nativePackHost, type HalfKind } from "../../src/packs/packHost";
import type { CliTool } from "@retroplug/core/cliTool";

export const CLI_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  ...APP_BUILDERS,
  cli: (pkg, exported) => {
    if (!Array.isArray(exported)) throw new Error("its cli half's default export is not a defineCliTools([...]) list");
    return [{ registry: cliToolsRegistry, pkg, items: exported as CliTool[] }];
  },
};

export function loadCliPacks(): Promise<void> {
  return loadPacks(nativePackHost(), CLI_BUILDERS);
}
```

(Follow the tree on the modules that seed the UI's registries (`ui/main.tsx` imports `./contributions/menus` and
`./contributions/views`) and the CLI's (`cli/tools.ts`), and on the module and type names; `pnpm check:boundaries`
judges these imports, and `ui/` and `cli/` may reach `src/`.)

- [ ] **Step 2: The entries wait for, or start, their load**

`pluginControlPlane.ts`: import `loadAppPacks` and `packStatus` from `./packs/loadPacks`, and replace the last line,
`g.__rp_ready = true;`, with:

```ts
// What the packs' halves loaded and refused, for a test and Settings > Packs (M4).
g.__rp_packStatus = packStatus;

// The packs' app and consoles halves commit before the host is told the control plane is ready, so the first project
// load (an autoload, the DAW's setState) finds their roles and consoles; the stores were composed first and adopt the
// commit (docs/downloadable-packs-m2a-plan.md). loadAppPacks never rejects: a pack that fails is refused and reported.
void loadAppPacks().then(() => {
  g.__rp_ready = true;
});
```

and in the header's list of globals, beside `__rp_ready`: `//   __rp_packStatus()           - what the packs' halves
loaded and refused`. (The hosts pump until ready, at most 1000 pumps, `PluginDSP.cpp` and `sdl/main.cpp`; a pack import
resolves in a few.)

`ui/main.tsx`: import `loadUiPacks` from `./packs/uiPacks` and `packStatus` from `../src/packs/loadPacks`; after the
`native.__rp_initialWindowSize = ...` line add:

```ts
native.__rp_packStatus = packStatus;
// The packs' UI halves (and, with no control plane, their app halves): the commit re-renders whatever has mounted.
void loadUiPacks();
```

and in `ui/host/nativeHooks.ts`'s hook interface (the one `native` is typed by), beside `__rp_commitUiContributions`:

```ts
  /** What the packs' halves loaded and refused (src/packs/loadPacks.ts): a test reads it, as Settings > Packs (M4) will. */
  __rp_packStatus?: () => { loaded: Record<string, string[]>; refused: Record<string, string> };
```

`cli/cli.ts`: import `loadCliPacks` from `./packs/cliPacks`; wrap everything from `const [cmd, ...rest] = hostArgs();`
to the end in `function route(): void { ... }`, and end the file with:

```ts
// A pack's tools are commands: its halves commit before the arguments are routed (loadCliPacks never rejects).
void loadCliPacks().then(route);
```

and add to the header's routing comment: "Packs load first (cli/packs/cliPacks.ts), so a pack's tool routes like a
built-in one."

- [ ] **Step 3: The hosts install the loader**

After each successful `registerSharedModules` call and before the entry is evaluated, with
`#include "host/js/PackModules.hpp"` and `#include "host/packs/ProcessPacks.hpp"`:

- `PluginDSP.cpp` (before `host_.evalModuleBytecode(rp_cp_bundle, ...)`):
  `rp::host::installPackLoader(ctx, rp::host::packs::processPacks(), rp_app_modules, rp_app_modules_count);`
  (`ctx` as the surrounding code names the context.)
- `PluginUI.cpp`: in the branch that registers when the editor fell back to a host of its own, the same call before the
  UI bundle is evaluated (on the shared context it is harmless: the loader is the runtime's, and the globals are
  installed once).
- `sdl/main.cpp`, `test/ui/UiHarness.cpp`: the same call after theirs.
- `cli/main.cpp`, dispatcher branch:
  `rp::host::installPackLoader(ctx, rp::host::packs::processPacks(), rp_cli_modules, rp_cli_modules_count);`

- [ ] **Step 4: Run**

Run: `cmake --build build -j$(nproc) && pnpm typecheck && pnpm check:boundaries && pnpm test && pnpm test:ui && pnpm test:plugin && pnpm test:cli-ts && pnpm sdl:smoke`
Expected: every command exits 0 with today's results (no runner gives a pack). Then by hand, the control plane and the
CLI with the fixture:

```bash
P=build/packs; E=$(mktemp -d)
RETROPLUG_PACKS_DIR=$E RETROPLUG_PACK_PATH=$P/tsfix/1.0.0 build/bin/retroplug-cli --help | grep tsfix-hello
RETROPLUG_PACKS_DIR=$E RETROPLUG_PACK_PATH=$P/tsfix/1.0.0 build/bin/retroplug-cli tsfix-hello
```

Expected: the index line for `tsfix-hello`; `hello from the ts fixture pack`.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/ui/packs/uiPacks.ts packages/retroplug/cli/packs/cliPacks.ts \
  packages/retroplug/src/pluginControlPlane.ts packages/retroplug/ui/main.tsx packages/retroplug/ui/host/nativeHooks.ts \
  packages/retroplug/cli/cli.ts packages/native/plugin/PluginDSP.cpp packages/native/plugin/PluginUI.cpp \
  packages/native/sdl/main.cpp packages/native/test/ui/UiHarness.cpp packages/native/cli/main.cpp
git commit -m "packs: the control plane, the UI and the CLI load their packs' halves at launch"
```

---

### Task 7: The fixture pack end to end

**Files:**
- Create: `packages/retroplug/test-ui/packs-ts-fixture.test.ts`
- Modify: `packages/retroplug/scripts/run-ui-tests.mjs`, `tools/run-cli-ts-tests.sh`

- [ ] **Step 1: The UI runner gives a packs test its packs**

In `run-ui-tests.mjs`, beside where the spawn's environment is built (Task 2 made it clear `RETROPLUG_PACK_PATH`):

```js
// A test of pack loading names the development packs it loads (docs/downloadable-packs-m2c-plan.md); every other test
// loads none. The fixture packs are staged under build/packs by retroplug-ts-fixture-packs.
const PACK_TESTS = {
  "packs-ts-fixture": ["tsfix", "tsfix-throws", "tsfix-guard"],
};
const packPath = (slug) =>
  (PACK_TESTS[slug] ?? []).map((id) => join(REPO, "build/packs", id, "1.0.0")).join(process.platform === "win32" ? ";" : ":");
```

and in the env: `...(PACK_TESTS[slug] ? { RETROPLUG_PACK_PATH: packPath(slug) } : {})`. (`slug` is the runner's name
for the file; use its own variable. If the runner takes the build directory from a variable rather than
`join(REPO, "build")`, use that.)

- [ ] **Step 2: The UI test**

`packages/retroplug/test-ui/packs-ts-fixture.test.ts`:

```ts
// A pack's halves in the running UI (docs/downloadable-packs-m2c-plan.md): the TS fixture pack's app and ui halves load
// in the UI test harness (which has no control plane, so the UI loads the app half too), its start-menu row shows, and
// the two broken packs beside it are refused with their reasons while the good one loads. The runner gives this file
// the packs (run-ui-tests.mjs, PACK_TESTS).
import { test, expect, ui } from "ui-harness";

type Status = { loaded: Record<string, string[]>; refused: Record<string, string> };
const status = (): Status => ((globalThis as Record<string, unknown>).__rp_packStatus as () => Status)();

test("the fixture pack's row shows, and the broken packs are refused by name", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(status().loaded.tsfix).toEqual(["app", "ui"]);
  expect(ui.findByText("TS Fixture Row") != null).toBeTruthy();
  expect(status().refused["tsfix-throws"]).toBe("the fixture half throws");
  expect(status().refused["tsfix-guard"]).toBe(
    "pack:tsfix-guard/app imports '@retroplug/sameboy/app', which is not a RetroPlug contract module");
});
```

(The order of `loaded.tsfix` is the builders' order, `app` before `ui`; if the harness boots and pumps differently,
follow `contributions-live.test.ts`.)

- [ ] **Step 3: The CLI section**

At the end of `tools/run-cli-ts-tests.sh`, before its summary, a section in the script's own style (its `check`,
`contains` and output-file conventions; the section numbering continues the file's):

```bash
# Packs (docs/downloadable-packs-m2c-plan.md): the TS fixture pack's tool is a command, and the broken packs beside it
# are refused with their reasons while the CLI goes on. The fixture is built by retroplug-ts-fixture-packs.
PACKS="$REPO/build/packs"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) PSEP=";" ;; *) PSEP=":" ;; esac
PACKPATH="$PACKS/tsfix/1.0.0${PSEP}$PACKS/tsfix-cliclash/1.0.0${PSEP}$PACKS/tsfix-undeclared/1.0.0"
PACKPATH="$PACKPATH${PSEP}$PACKS/tsfix-dependent/1.0.0${PSEP}$PACKS/tsfix-throws/1.0.0${PSEP}$PACKS/tsfix-protocol/1.0.0"
RETROPLUG_PACK_PATH="$PACKPATH" "$CLI" --help >"$TMP/packs-help.out" 2>"$TMP/packs-help.err"
check "packs: --help with the fixture packs" 0 $?
contains "packs: a pack's tool is in the index" "$TMP/packs-help.out" "tsfix-hello"
contains "packs: a tool named after a built-in is refused" "$TMP/packs-help.err" \
  "[packs] pack tsfix-cliclash is refused:"
contains "packs: a role its manifest does not declare is refused" "$TMP/packs-help.err" \
  "contributes the role kind tsfix-undeclared, which its pack.json's provides.roles does not declare"
contains "packs: a pack requiring a refused pack is refused" "$TMP/packs-help.err" \
  "it requires the pack tsfix-throws, which is refused"
contains "packs: a protocol half is refused by the native loader" "$TMP/packs-help.err" \
  "[packs] pack tsfix-protocol has a protocol half"
RETROPLUG_PACK_PATH="$PACKPATH" "$CLI" tsfix-hello >"$TMP/packs-run.out" 2>/dev/null
check "packs: a pack's tool runs" 0 $?
contains "packs: the tool's output" "$TMP/packs-run.out" "hello from the ts fixture pack"
RETROPLUG_PACK_PATH="$PACKPATH" "$CLI" render --help >/dev/null 2>&1
check "packs: a built-in command still routes beside them" 0 $?
```

(`$TMP` is the scratch folder Task 2 gave the script, and `RETROPLUG_PACKS_DIR` already points into it.)

- [ ] **Step 4: Run**

Run: `pnpm test:ui packs-ts-fixture && pnpm test:cli-ts`
Expected: the UI case `ok`, exit 0; the CLI script's new lines `ok`, exit 0. Prove the UI case can fail: run it with the
runner's `PACK_TESTS` entry for it removed and see `TS Fixture Row` not found; restore.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/test-ui/packs-ts-fixture.test.ts packages/retroplug/scripts/run-ui-tests.mjs \
  tools/run-cli-ts-tests.sh
git commit -m "packs: the fixture pack's halves load end to end in the UI and the CLI, and its broken variants are refused"
```

---

### Task 8: The docs

**Files:**
- Modify: `spec/02-native-host.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`

- [ ] **Step 1: Write them**

- `spec/02-native-host.md`, where M1's packs section and M2b's boot describe the host: a host's launch loads its packs
  once per process (`ensureLaunchPacks`: the installed packs and `RETROPLUG_PACK_PATH`'s development folders; the
  CLI's session files load none), then registers its shared set and installs the pack loader (`installPackLoader`:
  `pack:<id>/<half>` served from the pack's folder, a pack module held to the context's contracts, `__rp_packList` and
  `__rp_packRefused`), before its entry evaluates. The native loader now accepts the `app`, `consoles`, `ui`, `views`
  and `cli` halves (each file present) and refuses `protocol` and `script` (M2d).
- `spec/06-build-test.md`: the TS loader (`src/packs/loadPacks.ts`: which halves each context loads, the manifest
  checks, the batch commit and its fallback, `packStatus`); `tools/build-pack-halves.mjs`; the fixture packs (`tsfix`
  and its variants under `build/packs`, `retroplug-ts-fixture-packs`, `pack-modules-fixture` for the loader's native
  test); and the rule that the test runners point `RETROPLUG_PACKS_DIR` at scratch and that a UI test of packs names
  its packs in `run-ui-tests.mjs`'s `PACK_TESTS`; and that the guard judges a pack module's static and dynamic imports,
  not code it evaluates through `eval` or `new Function`.
- `spec/07-remaining-work.md`, the Downloadable packs entry: what is left of part 1 is M2d (the protocol half, the
  contribution kinds, the script half and the render worker; the M2c plan's design questions), M3, M4 (now also the
  CLI's `--pack` and Settings' Development packs, which feed `ensureLaunchPacks`), M5 and the Windows allocator.
  Delete what M2c finished; keep only open work.

- [ ] **Step 2: Run**

Run: `pnpm check:docs`
Expected: exit 0, no warning in the files touched.

- [ ] **Step 3: Commit**

```bash
git add spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md
git commit -m "docs: a pack's app, consoles, ui, views and cli halves load at launch"
```

---

### Task 9: Verify, then the CI gate

- [ ] **Step 1: The whole tree, locally**

As M2b's Task 9: the parallel tip-verify (`.superpowers/verify/README.md`) into `.superpowers/verify/runs/m2c/`, its
summary compared with `runs/m2b/` by `compare.sh`, every exit status read. Expected: the same pass set (the known GBA
app-cores case apart), larger counts for the new tests, no golden moved.

- [ ] **Step 2: The CI gate (Windows and macOS)**

The loader's wrapper over txiki's private functions, the reserved-name rule and the fixture's path separators meet
MSVC, Windows paths and Apple clang first in CI. Ask the user before pushing `feature/modularize`; with their
go-ahead, push and dispatch `build.yml`, and read every job's result. A failure is fixed in the task it belongs to.

## Design questions this plan found

**For M2d:**

1. **The protocol half's tables are constants.** `PROTOCOL_NODES`, `DECLARED_NODE_TYPES`, `ROLE_NODES` and `KIND_DATA`
   (`src/generated/protocol.ts`) are read by `buildGraph`, `protocolNodes.ts` and its readers and the kinds' checks;
   making them live registries, and moving the generator's protocol checks (wiring parity, captures, role to wired
   node) to run on a commit, is the protocol half's work.
2. **A contribution kind's `accept` runs once** (`kindOwners_controllerApps`, Launchpad's `setControllerSyncData`
   replaces rather than merges). M2c refuses a pack contributing to a kind; M2d needs an `add` or a re-`accept` with
   the whole list.
3. **The script half** goes through `Engine::loadScriptPack` (global code source; no app caller today) with span labels
   assigned at load (`spanLabelFor` already numbers a pack's types positionally).
4. **The render worker** holds its own copy of the registries in a bare QuickJS context: a pack console's render job
   needs the pack's app and consoles halves there, as global code or by making the worker a module context.

**Open:**

5. **A pack refused in a later context keeps an earlier one's commit.** In the plugin the UI is evaluated at the first
   editor open, after the control plane committed the pack's app half; a refused UI half is reported but the app half
   stays. Loading every half at launch would need the UI's contract modules evaluated before the editor exists, which
   M2b's design question 5 rules out for `lvgljs-ui`.
6. **A pack refused in TS keeps its native node types registered** in every Engine (native code is never unloaded);
   nothing places them. Settings > Packs (M4) shows the pack refused although `PackTable::find` holds it loaded.
7. **Development packs from Settings and `--pack`** are M4's: `ensureLaunchPacks` passes none yet, so a host sees the
   installed packs and `RETROPLUG_PACK_PATH`'s only.
8. **A pack-carried ROM** (M2a finding 9) is refused by name; it needs a rule of its own and a place in the pack.
9. **The guard trusts the pack's own module name.** A pack module is any module named `pack:...`; a host module that
   imported a `pack:` name it built itself would be judged as a pack. Nothing does, and the loader is the only place
   such names are made. The other way round, code a pack evaluates through `eval` or `new Function` has no `pack:` name,
   so its imports are not judged (`(0, eval)("import('rp:cp')")` from a pack loads it): the guard catches a pack's
   mistakes, not a pack set on going around it, which part 1 does not defend against (packs run in the app's realm).
10. **A refusal is recorded once per context.** Each plugin instance's context imports a pack's halves again, so a pack
    refused in TS adds one `PackTable` refusal per instance; Settings > Packs (M4) shows a pack's refusals once.
11. **The TS fixture is not type-checked.** Nothing compiles `packages/native/test/packs/ts-fixture/ts/*.ts` against
    the contracts (no tsconfig reaches it, and the boundary check scans `packages/<name>/ts`); its shapes were checked
    by hand against `MenuBuilder`, `MenuItem` and `CliTool`. M5's real packages are type-checked as packages.

## Self-review

**Spec coverage** (the design's section 3 for M2c's share, and M2b's findings): the halves load where their registries
live (Tasks 4, 6) through the host's module loader (Task 3), linking to the context's contracts (M2b) and refused any
other import (Task 3, finding 10: `rp:` names and before the import map); one commit per context with a per-pack
fallback (Task 4, finding 12); `provides` and `requires` held to the halves (Task 4); launch wiring on every txiki host
(Task 2); the fixture pack and its variants, built the way M5 will build real ones (Task 5), end to end in the UI and
the CLI (Task 7). The protocol and script halves, the kinds and the render worker are M2d's (decided 2026-11-24); the
mark per set (M2b finding 9) is not needed, since no second table is registered in a context.

**Placeholder scan:** every new module and function is given whole; where a helper of the tree's is used (the Catch2
runtime helpers, the runner's variables, the fixture CMake variables), the step names the file to read and what to
follow.

**Type and name consistency:** `ensureLaunchPacks` (Task 2); `installPackLoader`, `__rp_packContracts`,
`__rp_packList`, `__rp_packRefused`, `pack:<id>/<half>` (Task 3); `HalfKind`, `PackInfo`, `PackHost`,
`nativePackHost`, `HalfBuilder`, `objectExport`, `APP_BUILDERS`, `loadPacks`, `packStatus`, `loadAppPacks` (Task 4);
`buildPackHalves`, `packExternals`, `retroplug-ts-fixture-packs`, `tsfix` and its variants (Task 5); `UI_BUILDERS`,
`loadUiPacks`, `CLI_BUILDERS`, `loadCliPacks`, `__rp_packStatus` (Task 6); `PACK_TESTS` (Task 7).
