# Downloadable packs M2b: shared contract modules. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the control plane, the UI and the contract modules a pack may import as ONE code-split set of named ES
modules, compiled to an ordered bytecode table that each txiki host registers at boot, so one context holds one copy of
every `@retroplug/*` module (one set of registries, one React), and a pack's ES modules can later link to them by name.
The CLI's dispatcher gets the same treatment. No pack loads yet.

**Architecture:** `tools/build-shared-modules.mjs` runs one esbuild splitting build per set (`app`: the control plane,
the UI and every contract specifier; `cli`: the dispatcher and the shell's contracts), names every entry by its
specifier (`@retroplug/core/definePackage`) or by `rp:<host>`, rewrites every relative chunk import to a bare
`rp:chunks/...` name, and orders the outputs dependencies first. A small C tool, `retroplug-modc`, compiles a module
whose imports are external (stock `tjsc` refuses one) and writes the ordered table as one C file. At boot a host calls
`rp::host::registerSharedModules` on its context before evaluating its entry: each module is deserialised unevaluated
into QuickJS's loaded-module table, where a bare import finds it, and runs the first time something imports it.

**Tech Stack:** esbuild (code splitting, metafile), QuickJS-ng (`JS_ReadObject`, `JS_WriteObject`, module loader
hooks), txiki (`TjsHostRuntime`), CMake custom targets, Catch2 v3, the shell's TS test harness.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones: M1
native packs (done), M2a live registries (done, [plan](downloadable-packs-m2a-plan.md)), then:

- **M2b, shared contract modules (this plan):** the build and boot change the spike proved
  (`.superpowers/spike-esm/`, its README and `run-all.sh`): one shared module set per txiki host context, the CLI's
  dispatcher on the same scheme. It resolves M2a's design question 1 and its finding 10 for the desktop context (the
  control plane's and the UI's registries become one copy).
- **M2c, loading a pack's TS halves (next plan):** reading a loaded pack's halves, evaluating them as ES modules that
  link to the shared set, staging all halves into one commit per bundle, the protocol half and the contribution kinds,
  the guard that refuses a pack's import of anything but a contract, and launch wiring.
- M3 placeholders, M4 archives and commands, M5 the real packages as packs, as before.

**What the spike proved** (on the real txiki, linked against this tree's `libtjs_core.a` and `libqjs.a`, and on Node 22
and 26): two host modules and a pack module evaluated later share one registry instance, one seed evaluation and one
React context when the shared modules are registered by name; the same outputs built the way the bundles are today
reproduce the split. At real scale the shared app set is 66 modules, 1.29 MB of bytecode stripped, against 2.03 MB for
today's two bundles (everything the control plane reaches is also reached by the UI, so it is carried twice today);
registering it takes about 3.4 ms.

**What it found the build must do:**

1. Stock `tjsc` cannot compile these modules: QuickJS resolves a module's imports at compile time and `tjsc`'s loader
   answers only `tjs:` names (`tjsc -m` on `import {x} from "@retroplug/core/registry"` fails with `could not load
   module filename`). A compile that answers every import with an empty module is byte-identical to `tjsc` for a module
   `tjsc` accepts.
2. Reading module bytecode resolves its imports (`JS_ReadModule` calls `js_host_resolve_imported_module_atom` as it
   reads), so the shared modules are read dependencies first.
3. Relative imports between in-memory modules are unsafe on Windows (txiki's normalizer converts a relative name to
   `\` separators and splits the importer's name on `\`), so every import between shared modules is a bare name.
4. Naming each contract module by its own specifier needs no import map: an unmatched bare name passes through the
   normalizer unchanged and is found in the loaded-module table, the `retroplug-cli` precedent (`cli/main.cpp`
   `registerSdkModule`).

**Execution model.** Sonnet implements each task from its text alone; Opus reviews each task's diff before the next one
starts. The code here follows the spike's working harness and build script; it was not run as written: where a step
meets the tree differently, the implementer follows the tree and reports it. Task 9 ends with a CI gate on Windows and
macOS (the C tool and the generated C are new there), which needs the user's go-ahead to push.

## Decisions this plan makes

- **Two shared sets, one per kind of context.** The `app` set serves the context the control plane and the UI share
  (the plugin, the SDL standalone, the UI test harness); the `cli` set serves the CLI's dispatcher. Each set is built
  per product list, as the bundles are today. The render worker and the script host run global code on bare QuickJS,
  with no module loader, and stay as they are (a pack's halves there are M2c's question).
- **Hosts are not in the table.** The table holds only the shared modules; each host entry (`rp:cp`, `rp:ui`, `rp:cli`)
  stays its own bytecode array under its existing symbol (`rp_cp_bundle`, `rp_ui_bundle`, `rp_cli_bundle`), evaluated
  as today with `evalModuleBytecode`. So the boot sequence changes by one call (register the table, then evaluate as
  before), the plugin's lazy UI evaluation at the first editor open is unchanged, and a host's imports resolve to the
  registered names when its bytecode is read.
- **Names.** A contract module is named by its specifier; a host by `rp:<host>`; a chunk by `rp:chunks/<name>-<hash>`.
  A name never starts with `.` (relative) or `tjs:` (txiki's builtins), and the tool refuses any name outside
  `[A-Za-z0-9@/:._-]`.
- **A CommonJS host module gets an ESM facade.** `react` and `react/jsx-runtime` are CommonJS, so esbuild's entry for
  either is `export default require_react();` with no named exports, and a pack's `import { useState } from "react"` (or
  the automatic JSX runtime's `import { jsx } from "react/jsx-runtime"`) would not link under QuickJS's ESM. The build
  generates, for each, a facade entry that re-exports the default and every own key the module has when Node loads it
  (`Object.keys(require(...))`, enumerated at build time), importing the real module so the shell and a pack share one
  React; a test holds the facades to exporting `useState`, `jsx`, `jsxs` and `Fragment`.
- **The contract set is the boundary check's.** `SHELL_IMPORTS` and `UI_IMPORTS` move out of
  `scripts/check-boundaries.mjs` into `scripts/lib/contracts.mjs`, which the check and the build both import; the host
  modules are `host-modules.json`'s, minus `retroplug-cli` (the CLI binary's own module). The `app` set takes all
  three; the `cli` set takes `SHELL_IMPORTS`.
- **One option set for the control plane and the UI.** The build takes the UI's options (`uiBuildOptions`: no
  `tsconfigRaw`, a tsconfig read per module, because dpf.js's lv_binding_js classes need define semantics). The shell's
  and the packages' tsconfig is `tsconfig.base.json` (`target ES2020`, `strict`), whose class-field semantics equal the
  control plane's pinned `tsOptions` (`useDefineForClassFields: false` is TypeScript's default below ES2022), and an ES
  module is strict whatever `alwaysStrict` says, so the control plane's code compiles the same. Task 2 checks it: no
  module the control plane reaches carries esbuild's `__publicField` helper.
- **`retroplug-modc` is RetroPlug's own tool, not a `tjsc` flag.** `tjsc` lives in a nested submodule
  (txiki under lv_binding_js under dpf.js); a flag there is a three-commit pointer bump. The tool links `qjs`, as `tjsc`
  does, and has a cross-compile override (`RP_MODC_EXECUTABLE`) beside `TJSC_EXECUTABLE`.
- **Registering is idempotent per context.** `registerSharedModules` marks the context (a global property,
  `__rp_sharedModulesRegistered`, non-enumerable) and does nothing the second time, so the plugin's
  editor, which shares the control plane's context, can call it too without registering twice.
- **The seeds check their own order.** esbuild documents that code splitting can change evaluation order across chunks,
  and the registries' checks read registries seeded earlier. The generated `packages.ts` throws if the console and
  placer registries are not seeded when it runs, and `menus.ts` if the roles are not, so an order change fails loudly at
  boot in every suite rather than passing a check that read an empty registry.
- **The new loader test lives in `retroplug-classid-test`,** the binary that already tests the host runtime and links
  `retroplug-tjs-runtime` alone; its comment is widened to say so. No new test binary, so `test:plugin`'s list and
  spec/06's count do not change.

## Commands used throughout

- The TS suite: `pnpm test` or `pnpm test <slug>` (a file's path under `test/` without `.test.ts`, or a directory; a
  filter followed by a dash matches a dashed slug, so `script-bundle` takes every `script/bundle-*` file; a slug that
  matches nothing exits 1 with "no tests match").
- The UI suite: `pnpm test:ui [slug]`. The plugin binaries: `pnpm test:plugin`. The CLI: `pnpm test:cli-ts`.
- A single target: `cmake --build build --target <t> -j$(nproc)`. After a CMake change: `pnpm configure`. Never
  `rm -rf build`.
- `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`.
- `deps/sameboy` shows as modified after a configure: expected; never stage it. The generated modules
  (`packages/retroplug/{src,ui,cli}/generated/`) and everything under `build/` are gitignored outputs.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/native/modc/modc.c` | `retroplug-modc`: compile a module with external imports; write one array or the ordered table as C |
| `packages/native/src/host/js/SharedModules.{hpp,cpp}` | `rp_js_module`, `registerSharedModules` |
| `packages/native/test/plugin/SharedModules.test.cpp` | the loader over a fixture table |
| `packages/native/test/plugin/shared-modules-fixture/*.js` | the fixture modules |
| `packages/retroplug/scripts/lib/contracts.mjs` | `SHELL_IMPORTS`, `UI_IMPORTS` (moved), `hostModuleSpecifiers` |
| `tools/build-shared-modules.mjs` | the split build: names, rewrite, order, manifests, closures |
| `packages/retroplug/test/script/shared-modules.mjs` | the Node driver the shared-set tests run |
| `packages/retroplug/test/script/shared-modules.test.ts` | the shared set's shape |
| `packages/retroplug/test/script/modc.test.ts` | the tool against `tjsc` |

Modified: root `CMakeLists.txt` (`RP_MODC_EXECUTABLE`), `packages/native/CMakeLists.txt` (the tool, the loader's
source, the SDL and UI-test sources, the CLI's bundle), `packages/native/cmake/product-bundles.cmake` (the app set's
target), `packages/native/cmake/plugin.cmake`, `packages/native/cmake/tests.cmake`, `package.json` (`test`'s prebuilt
targets), `packages/native/plugin/{PluginDSP,PluginUI}.cpp`, `packages/native/sdl/{main,window}.cpp`,
`packages/native/test/ui/UiHarness.cpp`, `packages/native/cli/main.cpp`, `packages/retroplug/scripts/check-boundaries.mjs`,
`packages/retroplug/scripts/lib/packages.mjs` (the two seed guards), `packages/retroplug/test/packages/fixtures/expected/{packages,menus}.ts`,
`packages/retroplug/test/script/bundle-split.mjs`, the comments that say each bundle has its own copy, and the docs.

---

### Task 1: `retroplug-modc`, the module compiler

**Files:**
- Create: `packages/native/modc/modc.c`, `packages/retroplug/test/script/modc.test.ts`
- Modify: root `CMakeLists.txt` (beside the `TJSC_EXECUTABLE` block), `packages/native/CMakeLists.txt` (the target),
  `packages/native/cmake/warnings.cmake` (if it lists our targets by name: hold this one to `/W4 /WX` too), `package.json`
  (`test` builds the tool first)

- [ ] **Step 1: The tool**

Create `packages/native/modc/modc.c`:

```c
/* retroplug-modc: QuickJS module bytecode for modules whose imports are OTHER modules a host registers at run time
 * (docs/downloadable-packs-m2b-plan.md). QuickJS resolves a module's imports while compiling it, and tjsc's loader
 * answers only tjs: builtins, so tjsc refuses `import ... from "@retroplug/core/registry"`. This tool answers every import
 * with an empty module, which a compile never evaluates, so the bytecode it writes is what tjsc writes for a module tjsc
 * accepts (test/script/modc.test.ts holds the two byte for byte) and the imports are left for the host to resolve by
 * name.
 *
 *   retroplug-modc compile [-s|-ss] <module-name> <in.js> <out.bin>
 *       One module's bytecode, raw: what the tests compare with tjsc.
 *   retroplug-modc array [-s|-ss] <module-name> <symbol> <in.js> <out.c>
 *       One module as `const uint8_t <symbol>[]` and `const uint32_t <symbol>_size`, the shape tjsc -o writes: a host
 *       entry (rp_cp_bundle, rp_ui_bundle, rp_cli_bundle).
 *   retroplug-modc table [-s|-ss] <symbol> <manifest.tsv> <out.c>
 *       Every module the manifest lists (one `<module-name>\t<in.js>` per line, dependencies first) as one ordered
 *       `const rp_js_module <symbol>[]` and `const uint32_t <symbol>_count`, which registerSharedModules reads in order
 *       (reading a module's bytecode resolves its imports, so a dependency must come first).
 *
 * -s strips the source (tjsc -s), -ss the debug information too. A module name is refused unless it is
 * [A-Za-z0-9@/:._-]+ and starts with neither "." (a relative name) nor "tjs:" (txiki's builtins). */
#include "quickjs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *open_file(const char *path, const char *mode) {
#ifdef _MSC_VER
    FILE *f = NULL;
    if (fopen_s(&f, path, mode) != 0) return NULL;
    return f;
#else
    return fopen(path, mode);
#endif
}

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = open_file(path, "rb");
    if (!f) {
        fprintf(stderr, "retroplug-modc: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    *len = (size_t) size;
    unsigned char *buf = (unsigned char *) malloc(*len + 1);
    if (!buf || fread(buf, 1, *len, f) != *len) {
        fprintf(stderr, "retroplug-modc: short read of %s\n", path);
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[*len] = 0;
    fclose(f);
    return buf;
}

static int valid_name(const char *name) {
    if (!name[0] || name[0] == '.' || strncmp(name, "tjs:", 4) == 0) return 0;
    for (const char *c = name; *c; c++) {
        const int ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                       *c == '@' || *c == '/' || *c == ':' || *c == '.' || *c == '_' || *c == '-';
        if (!ok) return 0;
    }
    return 1;
}

/* An import's stand-in during a compile: never initialised, because a compile-only eval links and runs nothing. */
static int stub_init(JSContext *ctx, JSModuleDef *m) {
    (void) ctx;
    (void) m;
    return -1;
}

static JSModuleDef *stub_loader(JSContext *ctx, const char *name, void *opaque) {
    (void) opaque;
    return JS_NewCModule(ctx, name, stub_init);
}

/* Compile `in` as the module `name`; on success *out is a js_malloc'd buffer the caller frees with js_free(ctx, ...),
 * and *ctx_out the context it belongs to (freed with free_compiler). */
typedef struct Compiler {
    JSRuntime *rt;
    JSContext *ctx;
} Compiler;

static int new_compiler(Compiler *c) {
    c->rt = JS_NewRuntime();
    if (!c->rt) return 0;
    c->ctx = JS_NewContext(c->rt);
    if (!c->ctx) {
        JS_FreeRuntime(c->rt);
        return 0;
    }
    JS_SetModuleLoaderFunc(c->rt, NULL, stub_loader, NULL);
    return 1;
}

static void free_compiler(Compiler *c) {
    JS_FreeContext(c->ctx);
    JS_FreeRuntime(c->rt);
}

static uint8_t *compile(Compiler *c, int strip, const char *name, const char *in, size_t *out_len) {
    if (!valid_name(name)) {
        fprintf(stderr, "retroplug-modc: %s is not a module name a host can register\n", name);
        return NULL;
    }
    size_t len = 0;
    unsigned char *src = slurp(in, &len);
    if (!src) return NULL;
    JSValue obj = JS_Eval(c->ctx, (const char *) src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (JS_IsException(obj)) {
        JSValue e = JS_GetException(c->ctx);
        const char *msg = JS_ToCString(c->ctx, e);
        fprintf(stderr, "retroplug-modc: %s (%s): %s\n", in, name, msg ? msg : "an exception");
        JS_FreeCString(c->ctx, msg);
        JS_FreeValue(c->ctx, e);
        return NULL;
    }
    int flags = JS_WRITE_OBJ_BYTECODE;
    if (strip >= 1) flags |= JS_WRITE_OBJ_STRIP_SOURCE;
    if (strip >= 2) flags |= JS_WRITE_OBJ_STRIP_DEBUG;
    uint8_t *buf = JS_WriteObject(c->ctx, out_len, obj, flags);
    JS_FreeValue(c->ctx, obj);
    if (!buf) fprintf(stderr, "retroplug-modc: %s: could not write its bytecode\n", in);
    return buf;
}

static void write_bytes(FILE *f, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) fprintf(f, "%s0x%02x,", i % 16 == 0 ? "\n  " : " ", buf[i]);
    fprintf(f, "\n");
}

static int cmd_compile(int strip, const char *name, const char *in, const char *out) {
    Compiler c;
    if (!new_compiler(&c)) return 1;
    size_t len = 0;
    uint8_t *buf = compile(&c, strip, name, in, &len);
    int rc = 1;
    if (buf) {
        FILE *f = open_file(out, "wb");
        if (f && fwrite(buf, 1, len, f) == len) rc = 0;
        if (f) fclose(f);
        js_free(c.ctx, buf);
    }
    free_compiler(&c);
    return rc;
}

static int cmd_array(int strip, const char *name, const char *symbol, const char *in, const char *out) {
    Compiler c;
    if (!new_compiler(&c)) return 1;
    size_t len = 0;
    uint8_t *buf = compile(&c, strip, name, in, &len);
    int rc = 1;
    if (buf) {
        FILE *f = open_file(out, "wb");
        if (f) {
            fprintf(f, "/* Generated by retroplug-modc from %s (module %s). Do not edit. */\n", in, name);
            fprintf(f, "#include <stdint.h>\n\n");
            fprintf(f, "const uint32_t %s_size = %lu;\n\n", symbol, (unsigned long) len);
            fprintf(f, "const uint8_t %s[%lu] = {", symbol, (unsigned long) len);
            write_bytes(f, buf, len);
            fprintf(f, "};\n");
            rc = fclose(f) == 0 ? 0 : 1;
        }
        js_free(c.ctx, buf);
    }
    free_compiler(&c);
    return rc;
}

static int cmd_table(int strip, const char *symbol, const char *manifest, const char *out) {
    size_t mlen = 0;
    char *text = (char *) slurp(manifest, &mlen);
    if (!text) return 1;
    FILE *f = open_file(out, "wb");
    if (!f) {
        free(text);
        return 1;
    }
    Compiler c;
    if (!new_compiler(&c)) {
        fclose(f);
        free(text);
        return 1;
    }
    fprintf(f, "/* Generated by retroplug-modc from %s. Do not edit. */\n", manifest);
    fprintf(f, "#include <stdint.h>\n\n");
    fprintf(f, "typedef struct rp_js_module {\n  const char *name;\n  const uint8_t *bytecode;\n  uint32_t size;\n} rp_js_module;\n");
    /* One pass over the manifest's lines writes each module's array and records its name and size; the table naming
     * them follows. A line is "<name>\t<path>"; an empty line is skipped. */
    int count = 0, rc = 0;
    char *names[4096];
    size_t sizes[4096];
    for (char *line = strtok(text, "\r\n"); line && rc == 0; line = strtok(NULL, "\r\n")) {
        char *tab = strchr(line, '\t');
        if (!tab) {
            fprintf(stderr, "retroplug-modc: %s: a line without a tab: %s\n", manifest, line);
            rc = 1;
            break;
        }
        *tab = 0;
        if (count == 4096) {
            fprintf(stderr, "retroplug-modc: %s lists more than 4096 modules\n", manifest);
            rc = 1;
            break;
        }
        size_t len = 0;
        uint8_t *buf = compile(&c, strip, line, tab + 1, &len);
        if (!buf) {
            rc = 1;
            break;
        }
        fprintf(f, "\nstatic const uint8_t %s_%d[%lu] = {", symbol, count, (unsigned long) len);
        write_bytes(f, buf, len);
        fprintf(f, "};\n");
        js_free(c.ctx, buf);
        names[count] = line;
        sizes[count] = len;
        count++;
    }
    if (rc == 0) {
        fprintf(f, "\nconst rp_js_module %s[%d] = {\n", symbol, count > 0 ? count : 1);
        for (int i = 0; i < count; i++) {
            fprintf(f, "  { \"%s\", %s_%d, %lu },\n", names[i], symbol, i, (unsigned long) sizes[i]);
        }
        if (count == 0) fprintf(f, "  { 0, 0, 0 },\n");
        fprintf(f, "};\n\nconst uint32_t %s_count = %d;\n", symbol, count);
    }
    if (fclose(f) != 0) rc = 1;
    free_compiler(&c);
    free(text);
    if (rc != 0) remove(out);
    return rc;
}

static int strip_flag(const char *arg) {
    if (strcmp(arg, "-s") == 0) return 1;
    if (strcmp(arg, "-ss") == 0) return 2;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) goto usage;
    const char *cmd = argv[1];
    int i = 2;
    int strip = i < argc ? strip_flag(argv[i]) : 0;
    if (strip) i++;
    const int rest = argc - i;
    if (strcmp(cmd, "compile") == 0 && rest == 3) return cmd_compile(strip, argv[i], argv[i + 1], argv[i + 2]);
    if (strcmp(cmd, "array") == 0 && rest == 4) return cmd_array(strip, argv[i], argv[i + 1], argv[i + 2], argv[i + 3]);
    if (strcmp(cmd, "table") == 0 && rest == 3) return cmd_table(strip, argv[i], argv[i + 1], argv[i + 2]);
usage:
    fprintf(stderr,
            "usage: retroplug-modc compile [-s|-ss] <name> <in.js> <out.bin>\n"
            "       retroplug-modc array [-s|-ss] <name> <symbol> <in.js> <out.c>\n"
            "       retroplug-modc table [-s|-ss] <symbol> <manifest.tsv> <out.c>\n");
    return 2;
}
```

(The `names` and `sizes` arrays are on the stack at 4096 entries, far over the app set's 66; a symbol is a C identifier
the caller chooses. If the compiler flags a `goto` over the declarations as an error in C, move `cmd`, `i`, `strip` and
`rest` above the first `goto`.)

- [ ] **Step 2: The target and its cross-compile override**

In the root `CMakeLists.txt`, after the `_TJSC_COMMAND` block, add:

```cmake
# Cross-compile knob for retroplug-modc, the module compiler the shared module sets are built with
# (docs/downloadable-packs-m2b-plan.md): a host build of it, as TJSC_EXECUTABLE is a host build of tjsc.
set(RP_MODC_EXECUTABLE "" CACHE FILEPATH
    "Path to a host-built retroplug-modc. Leave empty to use the in-tree target.")
if(CMAKE_CROSSCOMPILING AND NOT RP_MODC_EXECUTABLE)
    message(FATAL_ERROR "Cross-compiling: set -DRP_MODC_EXECUTABLE=/path/to/host-retroplug-modc")
endif()
if(RP_MODC_EXECUTABLE)
    set(_MODC_COMMAND "${RP_MODC_EXECUTABLE}")
    set(_MODC_DEP "${RP_MODC_EXECUTABLE}")
else()
    set(_MODC_COMMAND "$<TARGET_FILE:retroplug-modc>")
    set(_MODC_DEP retroplug-modc)
endif()
```

In `packages/native/CMakeLists.txt`, BEFORE `include(cmake/product-bundles.cmake)` (about line 43; `RP_QUICKJS_INCLUDE` is set
above it, at line 25): the desktop's set is made when that file is included, and its `if(TARGET ${_MODC_DEP})` edge to
the tool is only added if the target already exists, so a tool defined later would leave the desktop's set unordered
against it and a clean build would race:

```cmake
# --- retroplug-modc: the module compiler the shared module sets are built with (docs/downloadable-packs-m2b-plan.md).
# A build tool, like tjsc: it links only QuickJS, and every set's target depends on it.
add_executable(retroplug-modc EXCLUDE_FROM_ALL modc/modc.c)
target_link_libraries(retroplug-modc PRIVATE qjs)
target_include_directories(retroplug-modc PRIVATE ${RP_QUICKJS_INCLUDE})
set_target_properties(retroplug-modc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
```

Read `packages/native/cmake/warnings.cmake`: if it applies `/W4 /WX` (and the gcc/clang equivalents) to a list of our
targets by name, add `retroplug-modc` to it; if it applies them by a property or a directory rule, check the target
gets them. In the root `package.json`, the `test` script's prebuilt targets
(`node scripts/cmake-build.js tjs-cli retroplug-devtest-script-pack`) gain `retroplug-modc tjsc`, so the TS suite's tool
test finds both binaries.

Run: `pnpm configure && cmake --build build --target retroplug-modc tjsc -j$(nproc)`
Expected: both build with no warning; `build/bin/retroplug-modc` with no arguments prints the usage and exits 2.

- [ ] **Step 3: Write the tool's test**

Create `packages/retroplug/test/script/modc.test.ts`:

```ts
// retroplug-modc (docs/downloadable-packs-m2b-plan.md, Task 1): what tjsc writes for a module tjsc accepts, and what tjsc
// refuses (a module importing another module by a name the host registers at run time), and its two C forms. Run as
// child processes, as bundle-protocol.test.ts runs its Node toolchains; the binaries are `pnpm test`'s prebuilt targets.
import { test, expect } from "@retroplug/testing";

declare const tjs: {
  env: Record<string, string | undefined>;
  exePath: string;
  tmpDir: string;
  makeTempDir(template: string): Promise<string>;
  writeFile(path: string, data: Uint8Array | string): Promise<void>;
  readFile(path: string): Promise<Uint8Array>;
  spawn(args: string[], opts: { stdout: "pipe"; stderr: "pipe" }): {
    stdout: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    stderr: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

const BIN = "../../build/bin";
const exe = (name: string) => `${name.includes("/") ? "" : `${BIN}/`}${name}${tjs.exePath.endsWith(".exe") ? ".exe" : ""}`;
const TJSC = exe("../../build/dpfjs/deps/lv_binding_js/deps/txiki/tjsc");
/** A scratch directory under the system's temp directory: never under the shell package, which the boundary check walks. */
const scratch = () => tjs.makeTempDir(`${tjs.tmpDir}/rp-modc-XXXXXX`);

async function text(stream: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null) {
  if (!stream) return "";
  const parts: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) parts.push(value);
  }
  return new TextDecoder().decode(Uint8Array.from(parts.flatMap((p) => [...p])));
}

async function run(args: string[]): Promise<{ status: number; err: string }> {
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  const [, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { status: status.exit_status, err };
}

const same = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((x, i) => x === b[i]);

test("a module tjsc accepts compiles to tjsc's bytes", async () => {
  const dir = await scratch();
  await tjs.writeFile(`${dir}/plain.js`, "export const answer = 6 * 7;\nexport function twice(x) { return x * 2; }\n");
  for (const strip of [[], ["-s"]]) {
    expect((await run([exe("retroplug-modc"), "compile", ...strip, "rp:plain", `${dir}/plain.js`, `${dir}/modc.bin`])).status)
      .toBe(0);
    // tjsc writes C; its `-o` with a .bin name is not a raw form, so the raw bytes are compared through the C array.
    expect((await run([TJSC, "-m", ...strip, "-n", "rp:plain", "-p", "rp_", "-o", `${dir}/tjsc.c`, `${dir}/plain.js`])).status)
      .toBe(0);
    expect((await run([exe("retroplug-modc"), "array", ...strip, "rp:plain", "rp_plain", `${dir}/plain.js`, `${dir}/modc.c`]))
      .status).toBe(0);
    const bytesOf = (c: string) => Uint8Array.from([...c.matchAll(/0x([0-9a-f]{2})/gi)].map((m) => parseInt(m[1], 16)));
    const tjscBytes = bytesOf(new TextDecoder().decode(await tjs.readFile(`${dir}/tjsc.c`)));
    const modcBytes = bytesOf(new TextDecoder().decode(await tjs.readFile(`${dir}/modc.c`)));
    expect(same(modcBytes, tjscBytes)).toBe(true);
    expect(same(await tjs.readFile(`${dir}/modc.bin`), tjscBytes)).toBe(true);
  }
});

test("a module importing a registered name compiles, where tjsc refuses it", async () => {
  const dir = await scratch();
  await tjs.writeFile(`${dir}/host.js`, 'import { box } from "@retroplug/core/registry";\nbox.count++;\n');
  const refused = await run([TJSC, "-m", "-n", "rp:host", "-o", `${dir}/tjsc.c`, `${dir}/host.js`]);
  expect(refused.status === 0).toBe(false);
  expect((await run([exe("retroplug-modc"), "compile", "-s", "rp:host", `${dir}/host.js`, `${dir}/host.bin`])).status)
    .toBe(0);
});

test("a table lists its modules in the manifest's order, and a bad name is refused", async () => {
  const dir = await scratch();
  await tjs.writeFile(`${dir}/a.js`, "export const a = 1;\n");
  await tjs.writeFile(`${dir}/b.js`, 'import { a } from "fx:a";\nexport const b = a + 1;\n');
  await tjs.writeFile(`${dir}/ok.tsv`, `fx:a\t${dir}/a.js\nfx:b\t${dir}/b.js\n`);
  expect((await run([exe("retroplug-modc"), "table", "-s", "rp_fx", `${dir}/ok.tsv`, `${dir}/table.c`])).status).toBe(0);
  const c = new TextDecoder().decode(await tjs.readFile(`${dir}/table.c`));
  expect(c.indexOf('{ "fx:a", rp_fx_0,') < c.indexOf('{ "fx:b", rp_fx_1,')).toBe(true);
  expect(c.includes("const uint32_t rp_fx_count = 2;")).toBe(true);
  for (const bad of ["./relative", "tjs:path", "has space"]) {
    await tjs.writeFile(`${dir}/bad.tsv`, `${bad}\t${dir}/a.js\n`);
    const r = await run([exe("retroplug-modc"), "table", "rp_bad", `${dir}/bad.tsv`, `${dir}/bad.c`]);
    expect(r.status === 0).toBe(false);
    expect(r.err.includes("is not a module name a host can register")).toBe(true);
  }
});
```

(Each case removes its scratch directory in a `finally`, with the txiki call the other script tests use to remove a
tree (`grep -rn "tjs.remove\|rmdir" packages/retroplug/test`). The tests run with the shell's package directory as their working directory, as `bundle-protocol.test.ts`'s do, so
`../../build` is the repository's build directory. If `tjsc`'s path differs, find it with
`find build -name tjsc -type f`, and if `tjs.makeTempDir` / `tjs.writeFile` are spelled differently in this txiki, use
what `packages/retroplug/test/script/*.test.ts` already use for scratch files. If `tjsc` refuses `-n` with `-p`, read its
usage and match the CMake rule's flags, `-m -s -p rp_` plus `-n`.)

- [ ] **Step 4: Run**

Run: `pnpm test script/modc`
Expected: `1..3`, three `ok`, exit 0. If the first case fails on the bytes, compare what `tjsc` sets beyond the strip
flags (`qjsc.c` `compile_file` and its `JS_WriteObject` call) and match it; the spike found them identical.

- [ ] **Step 5: Commit**

```bash
git add packages/native/modc/modc.c CMakeLists.txt packages/native/CMakeLists.txt package.json \
  packages/retroplug/test/script/modc.test.ts
git commit -m "shared modules: retroplug-modc compiles a module whose imports the host registers"
```

(and `packages/native/cmake/warnings.cmake` if Step 2 changed it).

### Task 2: The split build

**Files:**
- Create: `packages/retroplug/scripts/lib/contracts.mjs`, `tools/build-shared-modules.mjs`,
  `packages/retroplug/test/script/shared-modules.mjs`, `packages/retroplug/test/script/shared-modules.test.ts`
- Modify: `packages/retroplug/scripts/check-boundaries.mjs` (imports the two lists)

- [ ] **Step 1: The contract list moves**

Create `packages/retroplug/scripts/lib/contracts.mjs` holding the two arrays exactly as `check-boundaries.mjs` declares
them today (`SHELL_IMPORTS`, from its `/** Rule 9's list for src/ and cli/ ...` comment through its closing `];`, and
`UI_IMPORTS` likewise), exported, with this header:

```js
// The contract modules (docs/downloadable-packs-loading-design.md section 3): every @retroplug/* specifier the shell may
// import, with the reason it is the shell's to reach. The boundary check (check-boundaries.mjs, rule 9) holds the shell
// to these lists, and the shared module sets (tools/build-shared-modules.mjs) build each one as a module named by its
// specifier, which is what a pack's own modules may import (M2c holds a pack to the same set). One list, two readers.
```

plus, at the end:

```js
/** The host modules a pack's UI half may import (host-modules.json), minus the CLI binary's own `retroplug-cli`, which
 *  the CLI serves from its own embedded module and no shared set builds. */
export function hostModuleSpecifiers(hostModulesJson) {
  return hostModulesJson.map((entry) => entry.module).filter((name) => name !== "retroplug-cli");
}
```

In `check-boundaries.mjs`, delete the two declarations (and their doc comments) and import them:
`import { SHELL_IMPORTS, UI_IMPORTS } from "./lib/contracts.mjs";`. Its rule-9 comment at the top that says the lists
are declared "below" says they are declared in `lib/contracts.mjs`.

Run: `pnpm check:boundaries`
Expected: exit 0, the same "the packages hold their boundaries" line as before.

- [ ] **Step 2: The build**

Create `tools/build-shared-modules.mjs`:

```js
// The shared module sets (docs/downloadable-packs-m2b-plan.md): one esbuild code-splitting build per set, whose every
// output is a named ES module a txiki host registers before it evaluates its entry, so one context holds one copy of
// every contract module (one set of registries, one React) and a pack's ES modules can link to them by name (M2c).
//
//   app: the control plane (rp:cp) and the UI (rp:ui) and every contract specifier (SHELL_IMPORTS, UI_IMPORTS and the
//        host modules), for the context the two share in the plugin, the SDL standalone and the UI test harness.
//   cli: the CLI's dispatcher (rp:cli) and SHELL_IMPORTS.
//
// The names: a contract module is named by its specifier, so a bare import of it needs no import map (txiki's
// normalizer passes a bare name through and QuickJS finds it in the loaded-module table); a host by rp:<host>; a chunk by
// rp:chunks/<file stem>. esbuild writes the imports between outputs as relative paths, and txiki's normalizer joins a
// relative name wrongly on Windows (it converts the name to backslashes and splits the importer's name on them), so
// every one is rewritten to the bare name of the output it reaches. The shared outputs are ordered dependencies first:
// reading a module's bytecode resolves its imports, so a dependency must already be registered.
//
//   node tools/build-shared-modules.mjs <app|cli> <out-dir> [--product-dir <dir>]
//
// writes <out-dir>/js/** (the rewritten outputs), <out-dir>/shared.tsv (one "<name>\t<file>" per shared module, in load
// order) and <out-dir>/hosts.tsv (the hosts), which CMake hands to retroplug-modc. buildSharedModules() is the same build
// for the tests, in memory when `write` is false.
import { createRequire } from "node:module";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { SHELL_IMPORTS, UI_IMPORTS, hostModuleSpecifiers } from "../packages/retroplug/scripts/lib/contracts.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, "..");
const require = createRequire(import.meta.url);
const esbuild = require("esbuild");
const { uiBuildOptions } = require("./build-ui.js");

const SHELL = path.join(REPO, "packages/retroplug");
const HOST_MODULES = JSON.parse(fs.readFileSync(path.join(SHELL, "host-modules.json"), "utf8"));

/** Each set's hosts (entry file and name) and contract specifiers. */
function setOf(name) {
  if (name === "app") {
    return {
      hosts: [
        { name: "rp:cp", entry: path.join(SHELL, "src/pluginControlPlane.ts") },
        { name: "rp:ui", entry: path.join(SHELL, "ui/main.tsx") },
      ],
      contracts: [...new Set([
        ...SHELL_IMPORTS.map((e) => e.specifier),
        ...UI_IMPORTS.map((e) => e.specifier),
        ...hostModuleSpecifiers(HOST_MODULES),
      ])],
      external: ["tjs:path"],
    };
  }
  if (name === "cli") {
    return {
      hosts: [{ name: "rp:cli", entry: path.join(SHELL, "cli/cli.ts") }],
      contracts: [...new Set(SHELL_IMPORTS.map((e) => e.specifier))],
      external: ["tjs:path", "retroplug-cli"],
    };
  }
  throw new Error(`build-shared-modules: no set is named ${name} (the sets are app, cli)`);
}

/** The file esbuild resolves each specifier to, with the options the set builds with: one throwaway build of a stdin
 *  importing every specifier, whose resolver records each answer and stops there. */
async function resolveContracts(base, specifiers) {
  const found = new Map();
  const record = {
    name: "record-contracts",
    setup(build) {
      build.onResolve({ filter: /.*/ }, async (args) => {
        if (args.importer !== "<stdin>" || args.pluginData === "inner") return undefined;
        const r = await build.resolve(args.path, { resolveDir: args.resolveDir, kind: args.kind, pluginData: "inner" });
        if (r.errors.length === 0) found.set(args.path, r.path);
        return { path: args.path, external: true };
      });
    },
  };
  await esbuild.build({
    ...base, entryPoints: undefined, outfile: undefined, write: false, metafile: false, logLevel: "silent",
    stdin: { contents: specifiers.map((s) => `import ${JSON.stringify(s)};`).join("\n"), resolveDir: SHELL },
    plugins: [record, ...(base.plugins ?? [])],
  });
  const missing = specifiers.filter((s) => !found.has(s));
  if (missing.length) throw new Error(`build-shared-modules: cannot resolve ${missing.join(", ")}`);
  return found;
}

const posix = (p) => p.split(path.sep).join("/");

/** Build one set. Returns the shared modules in load order and the hosts, each { name, file, text, imports, exports },
 *  the closure of
 *  source files each host reaches through the outputs, and the metafile. */
export async function buildSharedModules({ set, outDir, productDir, write = true }) {
  const spec = setOf(set);
  const base = uiBuildOptions(spec.hosts[0].entry, path.join(outDir, "unused.js"), productDir);
  delete base.outfile;
  base.entryPoints = undefined;
  base.external = spec.external;
  const contracts = await resolveContracts(base, spec.contracts);
  // A CommonJS host module's entry would export only `default` (esbuild cannot see a CommonJS module's names), so a
  // pack's `import { useState } from "react"` would not link. Each gets a facade that imports the real module (one
  // React for the shell and a pack) and re-exports its default and every own key Node finds on it.
  for (const name of ["react", "react/jsx-runtime"].filter((n) => spec.contracts.includes(n))) {
    const real = contracts.get(name);
    const keys = Object.keys(createRequire(real)(real))
      .filter((k) => /^[A-Za-z_$][\w$]*$/.test(k) && k !== "default" && k !== "__esModule");
    const facade = path.join(outDir, "facades", `${name}.js`);
    fs.mkdirSync(path.dirname(facade), { recursive: true });
    fs.writeFileSync(facade, [
      `// Generated by tools/build-shared-modules.mjs: ${name} as an ES module with named exports.`,
      `import * as m from ${JSON.stringify(real)};`,
      "const d = m.default ?? m;",
      "export default d;",
      ...keys.map((k) => `export const ${k} = d.${k};`),
      "",
    ].join("\n"));
    contracts.set(name, facade);
  }

  const jsDir = path.join(outDir, "js");
  const entryPoints = [
    ...spec.hosts.map((h) => ({ in: h.entry, out: `rp/${h.name.slice(3)}` })),
    ...spec.contracts.map((s) => ({ in: contracts.get(s), out: s })),
  ];
  const result = await esbuild.build({
    ...base,
    entryPoints,
    outdir: jsDir,
    splitting: true,
    format: "esm",
    chunkNames: "chunks/[name]-[hash]",
    metafile: true,
    write: false,
    logLevel: "error",
  });

  // Every output's module name, by its absolute path.
  const outputs = Object.entries(result.metafile.outputs).filter(([o]) => o.endsWith(".js"));
  const nameOf = new Map();
  for (const [o, meta] of outputs) {
    const abs = path.resolve(base.absWorkingDir ?? REPO, o);
    const rel = posix(path.relative(jsDir, abs)).replace(/\.js$/, "");
    if (meta.entryPoint) {
      const host = spec.hosts.find((h) => `rp/${h.name.slice(3)}` === rel);
      nameOf.set(abs, host ? host.name : rel);
    } else {
      nameOf.set(abs, `rp:${rel}`);
    }
  }
  const textOf = new Map(result.outputFiles.filter((f) => f.path.endsWith(".js")).map((f) => [f.path, f.text]));

  // Rewrite each relative import to the bare name of the output it reaches, then refuse any relative specifier left.
  const deps = new Map();
  const externalsOf = new Map();
  const exportsOf = new Map();
  for (const [o, meta] of outputs) {
    const abs = path.resolve(base.absWorkingDir ?? REPO, o);
    let text = textOf.get(abs);
    const mine = [];
    for (const imp of meta.imports) {
      if (imp.external) continue;
      if (imp.kind !== "import-statement") {
        throw new Error(`build-shared-modules: ${o} has a ${imp.kind} of ${imp.path}: only static imports can be named`);
      }
      const target = path.resolve(base.absWorkingDir ?? REPO, imp.path);
      let rel = posix(path.relative(path.dirname(abs), target));
      if (!rel.startsWith(".")) rel = `./${rel}`;
      const quoted = JSON.stringify(rel);
      const re = new RegExp(`(\\bfrom\\s*|\\bimport\\s*)${quoted.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`, "g");
      text = text.replace(re, `$1${JSON.stringify(nameOf.get(target))}`);
      mine.push(target);
    }
    // (A dynamic import is refused above, from the metafile: a text test for `import(` would also match a method named
    // import, which the real UI calls.)
    if (/\b(?:from|import)\s*["']\.\.?\//.test(text)) {
      throw new Error(`build-shared-modules: ${o} keeps a relative import after the rewrite`);
    }
    textOf.set(abs, text);
    deps.set(abs, mine);
    externalsOf.set(abs, meta.imports.filter((i) => i.external).map((i) => i.path));
    exportsOf.set(abs, meta.exports ?? []);
  }

  // Dependencies first; a cycle cannot be read in any order, so it is refused naming its modules.
  const isHost = (abs) => spec.hosts.some((h) => h.name === nameOf.get(abs));
  const order = [];
  const state = new Map();
  const visit = (abs, trail) => {
    if (state.get(abs) === "done") return;
    if (state.get(abs) === "open") {
      throw new Error(`build-shared-modules: an import cycle: ${[...trail, abs].map((a) => nameOf.get(a)).join(" -> ")}`);
    }
    state.set(abs, "open");
    for (const d of deps.get(abs) ?? []) visit(d, [...trail, abs]);
    state.set(abs, "done");
    order.push(abs);
  };
  for (const [abs] of nameOf) visit(abs, []);
  // Each module as the tests read it: its name, file and text, the names it imports (the set's modules, then what the
  // set leaves external) and the names it exports.
  const moduleOf = (abs) => ({
    name: nameOf.get(abs), file: abs, text: textOf.get(abs),
    imports: [...(deps.get(abs) ?? []).map((d) => nameOf.get(d)), ...(externalsOf.get(abs) ?? [])],
    exports: exportsOf.get(abs) ?? [],
  });
  const shared = order.filter((abs) => !isHost(abs)).map(moduleOf);
  const hosts = spec.hosts.map((h) => moduleOf([...nameOf].find(([, n]) => n === h.name)[0]));

  // Each host's closure: the source inputs of every output it reaches.
  const inputsOf = new Map(outputs.map(([o, meta]) => [path.resolve(base.absWorkingDir ?? REPO, o), Object.keys(meta.inputs)]));
  const closures = {};
  for (const h of hosts) {
    const seen = new Set();
    const walk = (abs) => {
      if (seen.has(abs)) return;
      seen.add(abs);
      for (const d of deps.get(abs) ?? []) walk(d);
    };
    walk(h.file);
    closures[h.name] = [...new Set([...seen].flatMap((abs) => inputsOf.get(abs) ?? []))]
      .map((i) => path.resolve(base.absWorkingDir ?? REPO, i));
  }

  if (write) {
    fs.rmSync(jsDir, { recursive: true, force: true });
    for (const m of [...shared, ...hosts]) {
      fs.mkdirSync(path.dirname(m.file), { recursive: true });
      fs.writeFileSync(m.file, m.text);
    }
    fs.writeFileSync(path.join(outDir, "shared.tsv"), shared.map((m) => `${m.name}\t${m.file}`).join("\n") + "\n");
    fs.writeFileSync(path.join(outDir, "hosts.tsv"), hosts.map((m) => `${m.name}\t${m.file}`).join("\n") + "\n");
  }
  return { shared, hosts, closures, metafile: result.metafile };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const argv = process.argv.slice(2);
  const at = argv.indexOf("--product-dir");
  const productDir = at === -1 ? undefined : argv.splice(at, 2)[1];
  const [set, outDir] = argv;
  if (!set || !outDir || (at !== -1 && !productDir)) {
    console.error("usage: node tools/build-shared-modules.mjs <app|cli> <out-dir> [--product-dir <dir>]");
    process.exit(2);
  }
  buildSharedModules({ set, outDir: path.resolve(outDir), productDir: productDir && path.resolve(productDir) })
    .then((r) => console.log(`shared modules (${set}): ${r.shared.length} shared, ${r.hosts.length} hosts -> ${outDir}`))
    .catch((e) => {
      console.error(e.message);
      process.exit(1);
    });
}
```

Notes for the implementer, checked against the spike (`.superpowers/spike-esm/real-build.mjs`, `build.mjs`):
- `uiBuildOptions` sets `absWorkingDir: REPO_ROOT`, so the metafile's paths are relative to the repository; resolve
  them against it (as above).
- esbuild names an entry `{ in, out: "@retroplug/core/definePackage" }` at `js/@retroplug/core/definePackage.js`, so the
  name derived from the output path is the specifier itself. A host's `out` is `rp/cp`, mapped back to `rp:cp` above.
- `textOf` is keyed by `outputFiles[].path`, which esbuild gives absolute; if it is not, resolve it the same way.
- The rewrite regex matches `from "./x.js"` and `import "./x.js"` (the review found these are the only forms esbuild
  emits here). The check after it refuses anything left, so a form the regex misses fails the build rather than shipping
  a relative import. The real build has no dynamic import (the metafile check refuses one) and no import cycle; the review
  counted 64 shared modules for the desktop's app set, 62 for the plugin's and packs-host's, 52 for the CLI's.

- [ ] **Step 3: The shared set's tests**

Create `packages/retroplug/test/script/shared-modules.mjs` (the Node driver):

```js
#!/usr/bin/env node
// Drives tools/build-shared-modules.mjs for test/script/shared-modules.test.ts: builds a set in memory and prints one
// JSON line describing it. The build is Node's (esbuild), so the txiki test spawns this, as bundle-protocol.test.ts
// spawns its driver.
//
//   node test/script/shared-modules.mjs <app|cli> [product]
import { mkdtempSync, realpathSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const [set, product] = process.argv.slice(2);
const REPO = resolve(process.cwd(), "../..");
const { buildSharedModules } = await import("../../../../tools/build-shared-modules.mjs");
let work;
let productDir;
if (product) {
  const { generate } = await import("../../scripts/lib/packages.mjs");
  work = realpathSync(mkdtempSync(join(tmpdir(), "rp-shared-")));
  generate({ root: REPO, product, buildDir: work });
  productDir = join(work, "products", product, "packages/retroplug");
}
try {
  const run = () => buildSharedModules({ set, outDir: join(tmpdir(), "rp-shared-unused"), productDir, write: false });
  const a = await run();
  const b = await run();
  // The outputs the set's first host reaches, by name: the ones whose class fields its pinned options used to compile.
  const byName = new Map([...a.shared, ...a.hosts].map((m) => [m.name, m]));
  const reached = new Set();
  const walk = (name) => {
    if (reached.has(name) || !byName.has(name)) return;
    reached.add(name);
    for (const imp of byName.get(name).imports) walk(imp);
  };
  walk(a.hosts[0].name);
  process.stdout.write(JSON.stringify({
    shared: a.shared.map((m) => ({ name: m.name, imports: m.imports, exports: m.exports })),
    hosts: a.hosts.map((m) => ({ name: m.name, imports: m.imports, exports: m.exports })),
    deterministic: JSON.stringify([a.shared, a.hosts].map((l) => l.map((m) => [m.name, m.text])))
      === JSON.stringify([b.shared, b.hosts].map((l) => l.map((m) => [m.name, m.text]))),
    closures: Object.fromEntries(Object.entries(a.closures).map(([k, v]) => [k, v.map((p) => p.split("\\").join("/"))])),
    // A CALL of the helper, not its definition: esbuild's shared runtime-helper chunk defines __publicField beside
    // __toESM, and the control plane imports that chunk for __toESM.
    firstHostPublicField: [...reached].filter((name) => /\b__publicField\(/.test(byName.get(name).text)),
  }));
} finally {
  if (work) rmSync(work, { recursive: true, force: true });
}
```

Create `packages/retroplug/test/script/shared-modules.test.ts`:

```ts
// The shared module sets (docs/downloadable-packs-m2b-plan.md, Task 2): every contract is a module named by its own
// specifier, nothing imports by a relative path, every import names a module of the set (or a txiki builtin, or what
// the set leaves external), the load order is dependencies first, the build is deterministic, and each host reaches
// only what it did as its own bundle: the control plane no UI, the dispatcher no UI, the UI no CLI tool.
import { test, expect } from "@retroplug/testing";

declare const tjs: {
  env: Record<string, string | undefined>;
  spawn(args: string[], opts: { stdout: "pipe"; stderr: "pipe" }): {
    stdout: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    stderr: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

interface Described {
  shared: { name: string; imports: string[]; exports: string[] }[];
  hosts: { name: string; imports: string[]; exports: string[] }[];
  deterministic: boolean;
  closures: Record<string, string[]>;
  /** The outputs the set's first host (rp:cp, rp:cli) reaches that carry esbuild's define-semantics helper. */
  firstHostPublicField: string[];
}

async function text(stream: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null) {
  if (!stream) return "";
  const parts: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) parts.push(value);
  }
  let n = 0;
  for (const p of parts) n += p.length;
  const all = new Uint8Array(n);
  let at = 0;
  for (const p of parts) (all.set(p, at), (at += p.length));
  return new TextDecoder().decode(all);
}

async function describe(set: string, product?: string): Promise<Described> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/script/shared-modules.mjs", set, ...(product ? [product] : [])],
    { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.exit_status !== 0) throw new Error(`the shared-module driver failed: ${err}`);
  return JSON.parse(out) as Described;
}

const ALLOWED_EXTERNAL = new Set(["tjs:path", "retroplug-cli"]);

function holdsTheShape(d: Described): void {
  const names = new Set([...d.shared.map((m) => m.name), ...d.hosts.map((m) => m.name)]);
  const position = new Map(d.shared.map((m, i) => [m.name, i]));
  for (const m of [...d.shared, ...d.hosts]) {
    for (const imp of m.imports) {
      expect(imp.startsWith("."), `${m.name} imports ${imp} by a relative path`).toBe(false);
      expect(names.has(imp) || ALLOWED_EXTERNAL.has(imp), `${m.name} imports ${imp}, which no module of the set is`)
        .toBe(true);
      if (position.has(m.name) && position.has(imp)) {
        expect(position.get(imp)! < position.get(m.name)!, `${m.name} is read before its dependency ${imp}`).toBe(true);
      }
    }
  }
  expect(d.deterministic).toBe(true);
}

test("the app set names every contract by its specifier and holds the shape", async () => {
  const d = await describe("app");
  holdsTheShape(d);
  const names = new Set(d.shared.map((m) => m.name));
  for (const contract of ["@retroplug/core/definePackage", "@retroplug/core/systemRoles", "@retroplug/ui/contribution",
    "@retroplug/ui-react", "react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"]) {
    expect(names.has(contract), `the app set has no module named ${contract}`).toBe(true);
  }
  expect(d.hosts.map((h) => h.name)).toEqual(["rp:cp", "rp:ui"]);
  // The CommonJS host modules link by name through their facades.
  const exportsOf = (name: string) => d.shared.find((m) => m.name === name)?.exports ?? [];
  for (const n of ["useState", "useEffect", "createContext"]) expect(exportsOf("react").includes(n), `react exports no ${n}`).toBe(true);
  for (const n of ["jsx", "jsxs", "Fragment"]) expect(exportsOf("react/jsx-runtime").includes(n), `react/jsx-runtime exports no ${n}`).toBe(true);
});

test("the control plane reaches no UI, and the UI no CLI tool", async () => {
  const d = await describe("app");
  const cp = d.closures["rp:cp"];
  expect(cp.some((p) => p.includes("/packages/retroplug/ui/")), "the control plane reaches the shell's ui/").toBe(false);
  expect(cp.some((p) => /\/node_modules\/react\//.test(p)), "the control plane reaches React").toBe(false);
  expect(cp.some((p) => p.endsWith("src/contributions/registry.ts"))).toBe(true);
  // The control plane's classes compile to assignments, as its pinned options compiled them: the shell's tsconfig
  // (tsconfig.base.json, target ES2020) gives the same semantics, and only lv_binding_js's own classes use define.
  expect(d.firstHostPublicField).toEqual([]);
  const ui = d.closures["rp:ui"];
  expect(ui.some((p) => p.includes("/packages/retroplug/cli/")), "the UI reaches the CLI").toBe(false);
  expect(ui.some((p) => p.endsWith("ui/generated/views.ts"))).toBe(true);
});

test("the plugin's app set is built from its own generated tree", async () => {
  const d = await describe("app", "plugin");
  holdsTheShape(d);
  const cp = d.closures["rp:cp"];
  expect(cp.some((p) => p.includes("/products/plugin/packages/retroplug/src/generated/packages.ts"))).toBe(true);
  expect(cp.some((p) => p.includes("/packages/launchpad/")), "the plugin's control plane reaches the Launchpad").toBe(false);
});

test("the cli set holds the shape and reaches no UI", async () => {
  const d = await describe("cli");
  holdsTheShape(d);
  expect(d.hosts.map((h) => h.name)).toEqual(["rp:cli"]);
  expect(d.closures["rp:cli"].some((p) => p.includes("/packages/retroplug/ui/"))).toBe(false);
});
```

The second case's `firstHostPublicField` check is the one the option-set decision rests on: an output the control plane
reaches that carries `__publicField` is a class compiled with define semantics, which its pinned options never
produced. If it fails, report which outputs and stop: the shell's class fields would initialise differently.

- [ ] **Step 4: Run**

Run: `pnpm test script/shared-modules`
Expected: `1..4`, four `ok`, exit 0. Print the counts once while developing (`d.shared.length`): the spike's real app
set had 64 shared modules; a very different number means a contract resolved somewhere unexpected.

Run: `node tools/build-shared-modules.mjs app /tmp/rp-shared-app && head -3 /tmp/rp-shared-app/shared.tsv && cat /tmp/rp-shared-app/hosts.tsv`
Expected: exit 0, a line like `shared modules (app): 64 shared, 2 hosts`, the first names chunks or small contracts, and
`rp:cp` and `rp:ui` in `hosts.tsv`.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/scripts/lib/contracts.mjs packages/retroplug/scripts/check-boundaries.mjs \
  tools/build-shared-modules.mjs packages/retroplug/test/script/shared-modules.mjs \
  packages/retroplug/test/script/shared-modules.test.ts
git commit -m "shared modules: one split build names every contract, hosts and chunks, in load order"
```

### Task 3: The seeds check their own order

A shared set is one esbuild splitting build, and esbuild documents that splitting can change evaluation order across
chunks. The registries' checks read registries seeded earlier (M2a's "a registry not yet seeded reads as empty"), so a
seed that ran too early would pass a check over an empty registry without a word. The two seeds that depend on another
now refuse to run out of order.

**Files:**
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderPackages`, `renderMenus`)
- Regenerate: `packages/retroplug/test/packages/fixtures/expected/{packages,menus}.ts`

- [ ] **Step 1: The guards**

In `renderPackages`, the two side-effect imports

```js
    'import "./consoles";',
    'import "./placers";',
```

become named imports, and the module body's first statement (before `PACKAGE_IDS`) is the guard:

```js
    'import { consolesRegistry } from "./consoles";',
    'import { placersRegistry } from "./placers";',
```

```js
    "// The consoles and the placers seed first (this module imports them first), because the app's checks read them: a",
    "// bundle whose module order moved them after this one would check the app against two empty registries, so it is",
    "// refused here, at load (docs/downloadable-packs-m2b-plan.md).",
    "if (!consolesRegistry.seeded || !placersRegistry.seeded) {",
    "  throw new Error(\"the app's registrations were evaluated before the consoles' and the placers' (the bundle's \" +",
    "    \"module order changed)\");",
    "}",
    "",
```

In `renderMenus`, the side-effect import of the generated `packages.ts` (`import "<rel>/src/generated/packages";`)
becomes `import { rolesRegistry } from "<the same specifier>";`, and the module body's first statement is:

```js
    "// The app's registries seed first (this module imports their seed first), because the menus' checks read the roles",
    "// and the trackers: refused at load if a bundle's module order moved them after this one.",
    "if (!rolesRegistry.seeded) {",
    "  throw new Error(\"the menu contributions were evaluated before the app's registrations (the bundle's module \" +",
    "    \"order changed)\");",
    "}",
    "",
```

(`packages.ts` already re-exports `rolesRegistry`; `consoles.ts` and `placers.ts` re-export theirs.)

- [ ] **Step 2: Regenerate and refresh the two expected modules**

```bash
node packages/retroplug/scripts/lib/packages.mjs
node -e '
const { execFileSync } = require("child_process");
const fs = require("fs");
const out = JSON.parse(execFileSync("node", ["packages/retroplug/test/packages/run-generator.mjs", "generate"], { encoding: "utf8" }));
for (const name of ["packages.ts", "menus.ts"]) {
  fs.writeFileSync("packages/retroplug/test/packages/fixtures/expected/" + name, out.files[name]);
}'
```

Read the diff of the two expected modules: only the imports and the guard changed.

- [ ] **Step 3: Run**

Run: `pnpm test packages` then `pnpm test contributions` then `pnpm typecheck`
Expected: all exit 0 (the generator suite's 135 cases; the guards never fire in a correctly ordered bundle).

- [ ] **Step 4: Commit**

```bash
git add packages/retroplug/scripts/lib/packages.mjs packages/retroplug/test/packages/fixtures/expected/packages.ts \
  packages/retroplug/test/packages/fixtures/expected/menus.ts
git commit -m "contributions: the app and menu seeds refuse to run before the registries their checks read"
```

### Task 4: The host loader

**Files:**
- Create: `packages/native/src/host/js/SharedModules.hpp`, `packages/native/src/host/js/SharedModules.cpp`,
  `packages/native/test/plugin/SharedModules.test.cpp`, `packages/native/test/plugin/shared-modules-fixture/`
  (`state.js`, `dep.js`, `host-a.js`, `host-b.js`, `table.tsv`, `misordered.tsv`)
- Modify: `packages/native/CMakeLists.txt` (`retroplug-tjs-runtime` gains the source and its include directory),
  `packages/native/cmake/tests.cmake` (`retroplug-classid-test` gains the test and the fixture's arrays)

- [ ] **Step 1: The fixture**

`packages/native/test/plugin/shared-modules-fixture/state.js`:

```js
// fx:state: a module with state of its own, and a count of how many times its body ran in this context.
export const box = { count: 0 };
globalThis.__fxStateRuns = (globalThis.__fxStateRuns || 0) + 1;
```

`dep.js`:

```js
// fx:dep: a shared module importing another shared module by name, so the table's order matters.
import { box } from "fx:state";
export function bump() {
  box.count += 1;
  return box.count;
}
```

`host-a.js` (and `host-b.js`, the same with `__fxB`):

```js
// rp:fx-a: a host entry, evaluated as the control plane is, importing the shared modules by name.
import { box } from "fx:state";
import { bump } from "fx:dep";
bump();
globalThis.__fxA = box;
```

`table.tsv` (dependencies first) and `misordered.tsv` (the dependent first), names and paths relative to the fixture
directory, tab-separated:

```
fx:state	state.js
fx:dep	dep.js
```

```
fx:dep	dep.js
fx:state	state.js
```

- [ ] **Step 2: The loader's test**

Create `packages/native/test/plugin/SharedModules.test.cpp`:

```cpp
// The shared module sets' host loader (docs/downloadable-packs-m2b-plan.md, Task 4): a table registered in a context
// gives every host evaluated there one instance of each module, registering twice is a no-op, a table read out of order
// is refused naming the module, and a host whose import no table provides does not evaluate. The fixture is four tiny
// modules compiled by retroplug-modc at build time (test/plugin/shared-modules-fixture/).
#include <cstdint>
#include <string>

#include "dpfjs/host/TjsHostRuntime.hpp"
#include "host/js/SharedModules.hpp"
#include "quickjs.h"

// txiki's utils.h defines a CHECK macro, which Catch2's would redefine (ClassIdReserve.test.cpp does the same).
#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

extern "C" {
extern const rp_js_module rp_fx_modules[];
extern const std::uint32_t rp_fx_modules_count;
extern const rp_js_module rp_fx_misordered[];
extern const std::uint32_t rp_fx_misordered_count;
extern const std::uint8_t rp_fx_host_a[];
extern const std::uint32_t rp_fx_host_a_size;
extern const std::uint8_t rp_fx_host_b[];
extern const std::uint32_t rp_fx_host_b_size;
}

namespace {

/** `expr` evaluated as global code in `ctx`, as JSON. */
std::string json(JSContext* ctx, const char* expr) {
    const std::string code = std::string("JSON.stringify(") + expr + ")";
    JSValue v = JS_Eval(ctx, code.c_str(), code.size(), "<probe>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return "<threw>";
    }
    const char* s = JS_ToCString(ctx, v);
    std::string out = s ? s : "<null>";
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, v);
    return out;
}

}  // namespace

TEST_CASE("two hosts over one table share one instance of each module", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    JSContext* ctx = host.context();
    REQUIRE(rp::host::registerSharedModules(ctx, rp_fx_modules, rp_fx_modules_count).empty());
    REQUIRE(host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) == 0);
    REQUIRE(host.evalModuleBytecode(rp_fx_host_b, rp_fx_host_b_size) == 0);
    CHECK(json(ctx, "globalThis.__fxA === globalThis.__fxB") == "true");
    CHECK(json(ctx, "globalThis.__fxA.count") == "2");
    CHECK(json(ctx, "globalThis.__fxStateRuns") == "1");
}

TEST_CASE("registering a table twice in one context does nothing the second time", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    JSContext* ctx = host.context();
    REQUIRE(rp::host::registerSharedModules(ctx, rp_fx_modules, rp_fx_modules_count).empty());
    REQUIRE(rp::host::registerSharedModules(ctx, rp_fx_modules, rp_fx_modules_count).empty());
    REQUIRE(host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) == 0);
    CHECK(json(ctx, "globalThis.__fxStateRuns") == "1");
}

TEST_CASE("a table read out of order is refused naming the module", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    const std::string why = rp::host::registerSharedModules(host.context(), rp_fx_misordered, rp_fx_misordered_count);
    CHECK(why.find("fx:dep") != std::string::npos);
}

TEST_CASE("a host whose import no table provides does not evaluate", "[shared-modules]") {
    TjsHostRuntime host;
    REQUIRE(host.init());
    CHECK(host.evalModuleBytecode(rp_fx_host_a, rp_fx_host_a_size) != 0);
}
```

(`TjsHostRuntime` is in the global namespace, as `ClassIdReserve.test.cpp` uses it. Each case builds its own runtime, as
the class-id cases do.)

- [ ] **Step 3: The loader**

Create `packages/native/src/host/js/SharedModules.hpp`:

```cpp
// The shared module sets' host half (docs/downloadable-packs-m2b-plan.md): a txiki context's contract modules, built by
// tools/build-shared-modules.mjs as named ES modules and written by retroplug-modc as one ordered table, registered
// before the context evaluates its entry so every host there (the control plane and the editor's UI, or the CLI's
// dispatcher) links to one copy of each.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "quickjs.h"

extern "C" {
/** One module of a set, as retroplug-modc's `table` writes it. The generated C declares the same struct. */
typedef struct rp_js_module {
    const char* name;
    const std::uint8_t* bytecode;
    std::uint32_t size;
} rp_js_module;
}

namespace rp::host {

/** Register `modules` in `ctx`, in order, each unevaluated: deserialising a module's bytecode puts it in QuickJS's
 *  loaded-module table, where a bare import of its name finds it, and resolves its own imports, so a dependency must come
 *  first (the table's order is the build's, dependencies first). A module's body runs the first time something imports
 *  it. A context is registered once: a second call finds its mark and returns at once, so the plugin's editor, which
 *  shares the control plane's context, may call it too. Returns "" or why a module could not be registered, naming it;
 *  the modules before it stay registered. */
std::string registerSharedModules(JSContext* ctx, const rp_js_module* modules, std::size_t count);

}  // namespace rp::host
```

Create `packages/native/src/host/js/SharedModules.cpp`:

```cpp
#include "host/js/SharedModules.hpp"

namespace rp::host {

namespace {

/** The global property a registered context is marked with. A string key, not a symbol: nothing in JS reads it, and a
 *  name under the shell's `__rp_` prefix says whose it is. */
constexpr const char* kMark = "__rp_sharedModulesRegistered";

std::string pendingMessage(JSContext* ctx) {
    JSValue e = JS_GetException(ctx);
    const char* s = JS_ToCString(ctx, e);
    std::string out = s ? s : "an exception with no message";
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, e);
    return out;
}

}  // namespace

std::string registerSharedModules(JSContext* ctx, const rp_js_module* modules, std::size_t count) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mark = JS_GetPropertyStr(ctx, global, kMark);
    const bool registered = JS_ToBool(ctx, mark) > 0;
    JS_FreeValue(ctx, mark);
    if (registered) {
        JS_FreeValue(ctx, global);
        return {};
    }
    for (std::size_t i = 0; i < count; ++i) {
        const rp_js_module& m = modules[i];
        JSValue mod = JS_ReadObject(ctx, m.bytecode, m.size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(mod)) {
            JS_FreeValue(ctx, global);
            return std::string("the shared module ") + m.name + " could not be registered: " + pendingMessage(ctx);
        }
        if (JS_VALUE_GET_TAG(mod) != JS_TAG_MODULE) {
            JS_FreeValue(ctx, mod);
            JS_FreeValue(ctx, global);
            return std::string("the shared module ") + m.name + " is not module bytecode";
        }
        // Marked resolved, as registerSdkModule marks the SDK: QuickJS frees every UNRESOLVED module in the context when
        // any later module load fails (js_free_modules with JS_FREE_MODULE_NOT_RESOLVED), so a set registered by reading
        // alone would lose the modules no host had reached yet (the UI's, until the first editor opens) to an unrelated
        // failed load. Cheap: the imports were bound when the bytecode was read.
        const int resolved = JS_ResolveModule(ctx, mod);
        JS_FreeValue(ctx, mod);
        if (resolved < 0) {
            JS_FreeValue(ctx, global);
            return std::string("the shared module ") + m.name + " could not be resolved: " + pendingMessage(ctx);
        }
    }
    JS_DefinePropertyValueStr(ctx, global, kMark, JS_TRUE, 0);
    JS_FreeValue(ctx, global);
    return {};
}

}  // namespace rp::host
```

(Freeing the read value is safe: the loaded-module table keeps the module, as `registerSdkModule` and the spike rely on.)

- [ ] **Step 4: Build wiring**

In `packages/native/CMakeLists.txt`, `retroplug-tjs-runtime` compiles the loader beside the host runtime (both are the
shell's host runtime library; the dpf.js file stays where it is, so no submodule changes):

```cmake
add_library(retroplug-tjs-runtime STATIC
    ${DPFJS_PATH}/src/dpfjs/host/TjsHostRuntime.cpp
    src/host/js/SharedModules.cpp)    # the shared module sets' loader (docs/downloadable-packs-m2b-plan.md)
target_include_directories(retroplug-tjs-runtime PUBLIC ${DPFJS_PATH}/src ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

(keep its existing link and property lines). In `packages/native/cmake/tests.cmake`, before `retroplug-classid-test`:

```cmake
# The shared module loader's fixture (test/plugin/shared-modules-fixture): two tables and two host entries, compiled by
# retroplug-modc as the real sets are.
set(_fx_dir "${CMAKE_CURRENT_SOURCE_DIR}/test/plugin/shared-modules-fixture")
set(_fx_out "${CMAKE_BINARY_DIR}/test/shared-modules-fixture")
set(_fx_c "${_fx_out}/fx-table_data.c" "${_fx_out}/fx-misordered_data.c"
          "${_fx_out}/fx-host-a_data.c" "${_fx_out}/fx-host-b_data.c")
add_custom_command(
    OUTPUT ${_fx_c}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_fx_out}
    COMMAND ${_MODC_COMMAND} table -s rp_fx_modules table.tsv ${_fx_out}/fx-table_data.c
    COMMAND ${_MODC_COMMAND} table -s rp_fx_misordered misordered.tsv ${_fx_out}/fx-misordered_data.c
    COMMAND ${_MODC_COMMAND} array -s rp:fx-a rp_fx_host_a host-a.js ${_fx_out}/fx-host-a_data.c
    COMMAND ${_MODC_COMMAND} array -s rp:fx-b rp_fx_host_b host-b.js ${_fx_out}/fx-host-b_data.c
    WORKING_DIRECTORY ${_fx_dir}
    DEPENDS ${_MODC_DEP} ${_fx_dir}/state.js ${_fx_dir}/dep.js ${_fx_dir}/host-a.js ${_fx_dir}/host-b.js
            ${_fx_dir}/table.tsv ${_fx_dir}/misordered.tsv
    COMMENT "Compiling the shared module loader's fixture"
    VERBATIM)
```

and the binary's `SOURCES` gain `test/plugin/SharedModules.test.cpp ${_fx_c}`. Its comment becomes: "Guards the host
runtime: `TjsHostRuntime::init`'s class-id counter sync (the plugin-editor blank-UI fix) and the shared module sets'
loader (`host/js/SharedModules`); it needs the real txiki host, which is the shell's own library, not the backend: the
binary links no engine at all, so it is an ordinary binary (a test of the host runtime, not of the engine)."

Read `retroplug_add_test_binary` (`cmake/tests.cmake` or `cmake/packages.cmake`) for whether it compiles `.c` sources
and holds them to `/W4 /WX`; a generated array must compile clean there (it is only declarations and initialisers).

- [ ] **Step 5: Run**

Run: `pnpm configure && cmake --build build --target retroplug-classid-test -j$(nproc) && build/bin/retroplug-classid-test "[shared-modules]"`
Expected: 4 test cases, all passed, exit 0. Then `build/bin/retroplug-classid-test` (every case): all passed.

- [ ] **Step 6: Commit**

```bash
git add packages/native/src/host/js/SharedModules.hpp packages/native/src/host/js/SharedModules.cpp \
  packages/native/test/plugin/SharedModules.test.cpp packages/native/test/plugin/shared-modules-fixture \
  packages/native/CMakeLists.txt packages/native/cmake/tests.cmake
git commit -m "shared modules: the host registers a set's modules unevaluated, in order, once per context"
```

### Task 5: The app set replaces the control-plane and UI bundles

**Files:**
- Modify: `packages/native/cmake/product-bundles.cmake`, `packages/native/cmake/plugin.cmake`,
  `packages/native/CMakeLists.txt` (`retroplug-sdl`, `retroplug-ui-test`), `packages/native/plugin/PluginDSP.cpp`,
  `packages/native/plugin/PluginUI.cpp`, `packages/native/sdl/app.hpp`, `packages/native/sdl/main.cpp`,
  `packages/native/test/ui/UiHarness.cpp`

- [ ] **Step 1: One target per list**

In `product-bundles.cmake`, replace the two blocks `# --- control-plane bundle` (through its `add_dependencies(...
${_generator_target})`) and `# --- the UI bundle` (likewise) with:

```cmake
        # --- the app's shared module set (docs/downloadable-packs-m2b-plan.md): the control plane (rp:cp), the UI (rp:ui)
        # and every contract module, one esbuild code-splitting build whose outputs are named modules, so the context the
        # two share holds ONE copy of each (one set of registries, one React) and a pack's modules can link to them by
        # name. retroplug-modc writes the shared modules as one ordered table (rp_app_modules, registered by the host
        # before it evaluates anything: host/js/SharedModules) and each host entry as the array it always was
        # (rp_cp_bundle, rp_ui_bundle), now importing the table's modules by name. Built for every list, as the control
        # plane always was.
        set(_app_dir "${_dir}/app-modules")
        set(_cp_c "${_dir}/cp-bundle_data.c")
        set(_ui_c "${_dir}/ui-bundle_data.c")
        set(_modules_c "${_dir}/app-modules_data.c")
        set_source_files_properties(${_cp_c} ${_ui_c} ${_modules_c} PROPERTIES GENERATED TRUE)
        add_custom_target(retroplug-app-modules${_suffix} ALL
            BYPRODUCTS ${_cp_c} ${_ui_c} ${_modules_c} ${_cp_c}.new ${_ui_c}.new ${_modules_c}.new
                       ${_app_dir}/shared.tsv ${_app_dir}/hosts.tsv
            COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-shared-modules.mjs app ${_app_dir} ${_product_args}
            COMMAND ${_MODC_COMMAND} table -s rp_app_modules ${_app_dir}/shared.tsv ${_modules_c}.new
            COMMAND ${_MODC_COMMAND} array -s rp:cp rp_cp_bundle ${_app_dir}/js/rp/cp.js ${_cp_c}.new
            COMMAND ${_MODC_COMMAND} array -s rp:ui rp_ui_bundle ${_app_dir}/js/rp/ui.js ${_ui_c}.new
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_modules_c}.new ${_modules_c}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cp_c}.new ${_cp_c}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_ui_c}.new ${_ui_c}
            COMMENT "Building the app's shared module set${_suffix} (esbuild split + retroplug-modc)"
            VERBATIM)
        if(TARGET ${_MODC_DEP})
            add_dependencies(retroplug-app-modules${_suffix} ${_MODC_DEP})
        endif()
        add_dependencies(retroplug-app-modules${_suffix} ${_generator_target})
```

and, in the caller's view at the function's end, the two target variables name the one target and a third names the
table:

```cmake
    set(RP_BUNDLE_${name}_CP_TARGET "retroplug-app-modules${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_UI_TARGET "retroplug-app-modules${_suffix}" PARENT_SCOPE)
    set(RP_BUNDLE_${name}_MODULES_C "${_dir}/app-modules_data.c" PARENT_SCOPE)
```

and after the desktop's `set(UI_BUNDLE_C ...)`: `set(APP_MODULES_C "${RP_BUNDLE_desktop_MODULES_C}")`. Update the file's
header comment ("Every product has four embedded bundles: the control plane, the React UI, ...") to say the control
plane and the UI are now the app's shared module set's two hosts, built in one step with the modules they share.

`grep -rn "retroplug-cp-bundle\|retroplug-ui-bundle" --include=CMakeLists.txt --include=*.cmake --include=*.js --include=*.mjs --include=*.json . | grep -v node_modules`
lists every other reference to the two old target names (the `package.json` scripts, `scripts/cmake-build.js` callers,
spec text): each becomes `retroplug-app-modules` (or `-plugin` for the plugin's), or is left where it is the
`RP_BUNDLE_*_TARGET` variable that now names it.

- [ ] **Step 2: The binaries link the table**

- `cmake/plugin.cmake`: `FILES_DSP` gains `${RP_BUNDLE_plugin_MODULES_C}      # the app's shared modules (rp_app_modules)`.
- `retroplug-sdl` gains `${APP_MODULES_C}` beside `${CP_BUNDLE_C}`; `retroplug-ui-test` gains it beside `${UI_BUNDLE_C}`,
  and `retroplug-tjs-runtime` in its `target_link_libraries` (it links `lvgl-js-native`, which compiles its own copy of
  the host runtime, and the backend, neither of which carries the loader; `retroplug-sdl` already links both).
  Their `add_dependencies` on the two old targets become `retroplug-app-modules`.

- [ ] **Step 3: The hosts register it**

Each host declares the table beside its other bundle externs:

```cpp
#include "host/js/SharedModules.hpp"
```

```cpp
extern const rp_js_module rp_app_modules[];
extern const std::uint32_t rp_app_modules_count;
```

(inside the existing `extern "C" { ... }` block that declares `rp_cp_bundle` / `rp_ui_bundle`).

- `plugin/PluginDSP.cpp`, `bootControlPlane()`: right before `host_.evalModuleBytecode(rp_cp_bundle, ...)`:

```cpp
        // The app's shared modules first (docs/downloadable-packs-m2b-plan.md): the control plane's imports, and later the
        // editor's, resolve to them by name, so the two share one copy of every contract module.
        if (const std::string why = rp::host::registerSharedModules(ctx, rp_app_modules, rp_app_modules_count);
            !why.empty()) {
            d_stderr("[retroplug] %s", why.c_str());
            return;
        }
```

- `plugin/PluginUI.cpp`: right before its `jsEngine.evalModuleBytecode(rp_ui_bundle, ...)`, the same call on `ctx`
  (a no-op when the DSP registered this context, which is every time it shares the DSP's host; the registration that
  matters when the editor fell back to a host of its own), reporting with `d_stderr("[ui] %s", ...)` and skipping the UI
  evaluation if it fails.
- `sdl/main.cpp` `bootControlPlane()`: right before `a.host.evalModuleBytecode(rp_cp_bundle, ...)`, the same, reporting
  with `std::fprintf(stderr, "[retroplug-sdl] %s\n", ...)` and returning false. (`sdl/window.cpp` evaluates the UI in
  the same context and needs nothing.) The externs go in `sdl/app.hpp`'s block.
- `test/ui/UiHarness.cpp`: right before `engine.evalModuleBytecode(rp_ui_bundle, ...)`, the same on its context,
  returning false if it fails. Its externs block gains the table.

- [ ] **Step 4: Build and run what boots the app**

Run: `pnpm configure && cmake --build build -j$(nproc)`
Expected: exit 0; `build/native/app-modules_data.c`, `cp-bundle_data.c` and `ui-bundle_data.c` exist, and so do the
plugin's under `build/products/plugin/native/`. Note the three files' sizes against the old two (`git stash` is not
used: compare with the numbers in the spike's report, 2.03 MB of bytecode for the old pair).

Run: `pnpm test:ui`
Expected: every file passes, exit 0 (the UI test harness registers the table and boots the UI from it).

Run: `pnpm test:plugin`
Expected: exit 0 (`retroplug-classid-test` included).

Run the SDL smoke the way CI runs it (`grep -n "sdl:smoke" package.json` for the command; it runs under the device
guard): expected exit 0, which proves the standalone's control plane and UI boot from the table.

- [ ] **Step 5: Commit**

```bash
git add packages/native/cmake/product-bundles.cmake packages/native/cmake/plugin.cmake packages/native/CMakeLists.txt \
  packages/native/plugin/PluginDSP.cpp packages/native/plugin/PluginUI.cpp packages/native/sdl/app.hpp \
  packages/native/sdl/main.cpp packages/native/test/ui/UiHarness.cpp
git commit -m "shared modules: the control plane and the UI boot from one shared module set"
```

(plus any file Step 1's grep changed).

### Task 6: What one copy changes

Before this milestone the control plane and the UI each ran their own copy of every module both reach; now each such
module runs once per context. Most of that is the point (one set of registries), but every module-level side effect and
every comment that assumed two copies is checked here.

**Files:** the ones listed below, as each needs.

- [ ] **Step 1: Read each module-level state the two bundles both reached, and decide**

For each, read the code, state in the commit message what one copy changes, and fix what is wrong:

- `packages/retroplug/src/realBackend.ts`: `pendingBrowse` and `browseResolverInstalled` (around lines 32-40, whose
  comment says "module singletons do NOT - each bundle gets its own") and the `globalThis.__rp_onFileBrowserResult` it
  installs; `realJobs` (around 108-116, whose comment's precondition is "one bundle per engine creating emulator
  clients") and `realOffline` (around 128). With one copy, the control plane and the UI share one browse resolver and one
  job/offline client; check that a browse the UI starts resolves in the UI, and that the job pump's single owner still
  holds (`test:ui`'s file-browser and render-job cases exercise both).
- `packages/retroplug/src/capability.ts` (`liveJobs`, `jobWait`), `src/offline.ts` (around 224, 233),
  `src/systemsStore.ts` (`nextSystemId`, "ONE id space per control-plane JS context": now true of the UI's copy too,
  which the plugin never used), `src/graph/directGraph.ts` (`shared`), `src/fileBrowser.ts` (around 14-15, 50),
  the warn-once sets in `projectConfig.ts`, `configSections.ts`, `projectStore.ts`, `buildGraph.ts` (one warning per
  context now, not per bundle: correct), `src/render/render.ts` (`partTokens`).
- `packages/launchpad/ts/control/placer.ts`'s `syncData` and the generated kind block's `kindOwners_controllerApps` and
  `accept()` call: they now run once per context, which is correct (the UI's copy only repeated the control plane's).
- The registries: `src/contributions/registry.ts`'s `stateOf` error says "each bundle has its own registries". It now
  means each context's module set (a render worker or a script host still has its own); reword to "the registry ... is
  not this context's (each context's module set has its own registries)". Its header's "the control plane and the UI each
  inline their own copy" sentence, if any, goes.
- `ui/main.tsx` installs `__rp_commitUiContributions` (`commitByName`): it now reaches the one copy the control plane's
  stores read too, which is what M2a's finding 10 asked for in this context. Its comment, `nativeHooks.ts`'s doc of the
  hook, and `test-ui/contributions-live.test.ts`'s header ("the UI bundle's registries are its own copy") say so.
- `ui/stores/StoreProvider.tsx`'s fallback `ns?.stores ?? composeAppStores(...)` is unchanged (the UI test harness has no
  control plane); its comment, if it says the UI's copy is a second graph, says it is the context's one copy.

Run `grep -rn "each bundle\|its own copy\|own copy of\|two copies\|per bundle" packages/retroplug/src packages/retroplug/ui packages/retroplug/cli packages/*/ts`
and fix every comment that describes the control plane and the UI as separate copies.

- [ ] **Step 2: Run**

Run: `pnpm test` then `pnpm test:ui` then `pnpm typecheck`
Expected: all exit 0.

- [ ] **Step 3: Commit**

```bash
git add <each file changed>
git commit -m "shared modules: module state and comments follow one copy per context"
```

### Task 7: The CLI's dispatcher on the shared scheme

**Files:**
- Modify: `packages/native/CMakeLists.txt` (`retroplug-cli-bundle`, `retroplug-cli`'s sources), `packages/native/cli/main.cpp`

- [ ] **Step 1: The target**

Replace the `retroplug-cli-bundle` target's body (keep its name, so every reference to it stands) with the `cli` set:

```cmake
# The root CLI dispatcher (cli/cli.ts) and the shell's contract modules as the CLI's shared module set
# (docs/downloadable-packs-m2b-plan.md): rp_cli_modules is the ordered table the launcher registers before it evaluates
# the dispatcher (rp_cli_bundle, now importing those modules by name), so a pack's CLI half can link to the same
# contracts (M2c). Derived; never committed. Adding a command is pure TS (cli/tools.ts): no change here.
set(CLI_MODULES_DIR "${CMAKE_BINARY_DIR}/native/cli-modules")
set(CLI_BUNDLE_C  "${CMAKE_BINARY_DIR}/native/cli-bundle_data.c")
set(CLI_MODULES_C "${CMAKE_BINARY_DIR}/native/cli-modules_data.c")
set_source_files_properties(${CLI_BUNDLE_C} ${CLI_MODULES_C} PROPERTIES GENERATED TRUE)
add_custom_target(retroplug-cli-bundle
    BYPRODUCTS ${CLI_BUNDLE_C} ${CLI_BUNDLE_C}.new ${CLI_MODULES_C} ${CLI_MODULES_C}.new
               ${CLI_MODULES_DIR}/shared.tsv ${CLI_MODULES_DIR}/hosts.tsv
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-shared-modules.mjs cli ${CLI_MODULES_DIR}
    COMMAND ${_MODC_COMMAND} table -s rp_cli_modules ${CLI_MODULES_DIR}/shared.tsv ${CLI_MODULES_C}.new
    COMMAND ${_MODC_COMMAND} array -s rp:cli rp_cli_bundle ${CLI_MODULES_DIR}/js/rp/cli.js ${CLI_BUNDLE_C}.new
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLI_MODULES_C}.new ${CLI_MODULES_C}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLI_BUNDLE_C}.new ${CLI_BUNDLE_C}
    COMMENT "Building the CLI's shared module set (esbuild split + retroplug-modc)"
    VERBATIM)
if(TARGET ${_MODC_DEP})
  add_dependencies(retroplug-cli-bundle ${_MODC_DEP})
endif()
```

(keep the existing `add_dependencies(retroplug-cli-bundle ${RP_BUNDLE_desktop_GENERATOR})` and its comment; drop the
`_TJSC_DEP` edge, which the target no longer uses, and `CLI_BUNDLE_JS`). `add_executable(retroplug-cli ...)` gains
`${CLI_MODULES_C}`.

- [ ] **Step 2: The launcher registers it**

In `cli/main.cpp`, the `extern "C"` block gains

```cpp
// The CLI's shared module set (docs/downloadable-packs-m2b-plan.md): the contract modules the dispatcher imports by name.
extern const rp_js_module  rp_cli_modules[];
extern const std::uint32_t rp_cli_modules_count;
```

with `#include "host/js/SharedModules.hpp"`, and the dispatcher branch becomes:

```cpp
    } else {
        // The dispatcher's contract modules first: its imports resolve to them by name.
        const std::string why = rp::host::registerSharedModules(ctx, rp_cli_modules, rp_cli_modules_count);
        if (!why.empty()) {
            std::fprintf(stderr, "retroplug-cli: %s\n", why.c_str());
            rc = 1;
        } else {
            rc = host.evalModuleBytecode(rp_cli_bundle, rp_cli_bundle_size);
        }
    }
```

(A session file's branch is unchanged: it registers the SDK module and evaluates the file, which bundles its own copy of
what it imports; M2c decides what a pack's CLI half needs there.)

- [ ] **Step 3: Run**

Run: `cmake --build build --target retroplug-cli -j$(nproc) && build/bin/retroplug-cli --help`
Expected: the command index prints, exit 0.

Run: `pnpm test:cli-ts` then `pnpm test cli`
Expected: both exit 0.

- [ ] **Step 4: Commit**

```bash
git add packages/native/CMakeLists.txt packages/native/cli/main.cpp
git commit -m "shared modules: the CLI's dispatcher boots from its own shared module set"
```

### Task 8: The bundle-shape tests and the docs

**Files:**
- Modify: `packages/retroplug/test/script/bundle-split.mjs` (and `bundle-ui-split.test.ts` only if its assertions'
  wording names the old bundles), `spec/06-build-test.md`, `spec/02-native-host.md`, `spec/07-remaining-work.md`,
  `tools/build-controlplane.js` and `tools/build-ui.js` (their header comments)

- [ ] **Step 1: The split test reads what ships**

`bundle-split.mjs` builds the control plane with `build-session.js`'s options and the UI with `build-ui.js`'s, neither
of which is what the app now ships. Replace those two (and `cli-dispatcher`, if the driver builds it) with the shared
build's closures:

```js
const { buildSharedModules } = await import("../../../../tools/build-shared-modules.mjs");
const app = await buildSharedModules({ set: "app", outDir: join(tmpdir(), "rp-split-unused"), productDir, write: false });
out["control-plane"] = app.closures["rp:cp"];
out.ui = app.closures["rp:ui"];
// (desktop only) the dispatcher:
const cli = await buildSharedModules({ set: "cli", outDir: join(tmpdir(), "rp-split-unused"), write: false });
out["cli-dispatcher"] = cli.closures["rp:cli"];
```

with `import { join, resolve } from "node:path";` and `import { tmpdir } from "node:os";` at the file's top (today it
imports only `resolve` there, and `join` and `tmpdir` only inside the product branch), and `cli-dispatcher` only on the
desktop path (the product branch builds three entries and the UI), keeping the render worker, the script nodes, the CLI SDK and the planner as they are (they are not shared sets). The
test file's assertions are unchanged: they now hold the shipped hosts' reach to the same rules (no Launchpad in the
plugin's, no UI in the control plane's, no CLI in the UI's, the views only in the UI's). Paths: `closures` are absolute,
as the driver's other entries are made absolute with `resolve(process.cwd(), input)`.

Run: `pnpm test script-bundle`
Expected: every case `ok`, exit 0.

- [ ] **Step 2: The docs**

- `spec/06-build-test.md`: where it describes the four embedded bundles (the paragraph after the generated registries,
  and "The packages" / the build sections that name `cp-bundle` and `ui-bundle`), say the control plane and the UI are
  the two hosts of the app's shared module set: one esbuild code-splitting build of the two entries and every contract
  module (`SHELL_IMPORTS`, `UI_IMPORTS`, the host modules: `scripts/lib/contracts.mjs`), each output a named module
  (a contract by its specifier, a host `rp:cp`/`rp:ui`, a chunk `rp:chunks/...`, every import between them bare),
  compiled by `retroplug-modc` (RetroPlug's module compiler: `tjsc` refuses a module with external imports) into the
  ordered table `rp_app_modules` and the two host arrays; a host registers the table (`host/js/SharedModules`) before
  it evaluates, so the context holds one copy of each module. The CLI's dispatcher is the `cli` set the same way. Name
  the seed guards (Task 3) and `RP_MODC_EXECUTABLE` (a cross build takes the host build tree's `retroplug-modc`, as it
  takes that tree's `tjsc` for `TJSC_EXECUTABLE`; the local arm64 cross build needs one before it configures). In the test tiers' table, `retroplug-classid-test`'s description
  gains the shared-module loader.
- `spec/02-native-host.md`: where it describes the plugin's and the standalone's boot (`bootControlPlane`, the editor's
  lazy UI evaluation), add the registration step before the control plane's evaluation.
- `spec/07-remaining-work.md`: the "Downloadable packs" entry's M2b becomes M2c (loading a pack's TS halves, the
  protocol half and the contribution kinds among them, the pack-import guard), citing this plan's design questions.
- `tools/build-controlplane.js` and `tools/build-ui.js`: their headers say what still uses them (the tests'
  `bundleInputs`, if any; else note that CMake no longer runs them and the shared build reuses `uiBuildOptions`). If
  `build-controlplane.js` has no remaining caller (`grep -rn "build-controlplane" --include=*.cmake --include=*.mjs --include=*.js --include=*.json .`),
  delete it.

Run: `pnpm check:docs`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add packages/retroplug/test/script/bundle-split.mjs spec/06-build-test.md spec/02-native-host.md \
  spec/07-remaining-work.md tools/build-ui.js
git commit -m "docs: the app and the CLI boot from shared module sets"
```

(plus `tools/build-controlplane.js`'s deletion or edit.)

### Task 9: Verify, then the CI gate

- [ ] **Step 1: The whole tree, locally**

As M2a's Task 10: `pnpm test`, `pnpm test:ui`, `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`,
`pnpm test:cli-ts`, `pnpm test:native`, `pnpm test:plugin`, `pnpm test script-bundle`, the SDL smoke, and the parallel
tip-verify (`.superpowers/verify/README.md`), each exit status read, logs and a summary under
`.superpowers/verify/runs/m2b/`. Expected: the same pass set as M2a's run (`runs/m2a/`), the known GBA app-cores case
apart. A parity, kit or render golden that moves is a real change and stops the task.

- [ ] **Step 2: The CI gate (Windows and macOS)**

`retroplug-modc` and its generated C are new on MSVC and Apple clang, and the shared set's bare names are what keeps
txiki's module resolution off its Windows relative-path join: CI is the first place either runs. Ask the user before
pushing `feature/modularize`; with their go-ahead, push and dispatch `build.yml` (it runs on `workflow_dispatch`), and
read every job's result. A failure is fixed in the task it belongs to.

## Design questions this plan found

**For M2c:**

1. **A pack's import that matches no module falls through to txiki's disk loader** (the spike: `could not load
   '@retroplug/core/nope'` after looking on disk). A pack must be refused any import but a contract: a normalizer wrapper
   in C that refuses a non-contract bare name, or any relative name, when the importer is a pack module (`pack:*`), set
   on the runtime after `TJS_NewRuntime` without changing txiki (`JS_SetModuleLoaderFunc2` takes the normalizer).
2. **The render worker and the script host are not module contexts.** A pack console's render job builds its stores in
   the render worker (bare QuickJS, global code), which will need the pack's app and consoles halves too: either the
   worker becomes a module context with its own shared set, or a pack's halves are also built as global code for it.
3. **A pack's halves evaluate as source** (`TJS_EvalModuleContent` under `pack:<id>/<half>.js`), compiled at load (the
   design's bytecode cache keyed by pack, version and QuickJS version is M2c's or later).
4. **Node:** the CLI SDK bundle and the Node host need the same named modules to link a pack's halves there
   (`module.registerHooks`, Node 22.15 and later; `module.register` is deprecated in 26), which the spike proved and this
   milestone does not build.
5. **The UI half loads only after the UI exists**: a pack's UI half imports `lvgljs-ui`, whose modules read the
   lvgljs namespace at module scope, so in the plugin it waits for the first editor open, as the shell's UI does.

**Open:**

6. **The app set carries the CLI-only contracts** (`@retroplug/testing`, `@retroplug/n8/device`, `@retroplug/core/cliTool`)
   into every app binary, the plugin's included; harmless (no host reaches them), but a product list without `n8` would
   fail to resolve `@retroplug/n8/device`. A set could take only the `SHELL_IMPORTS` its hosts reach, plus `UI_IMPORTS`.
7. **Windows is unproven until the CI gate** (the spike read txiki's normalizer; it did not run on Windows).
8. **Bytecode stripping:** the sets are built with `-s` (source stripped), as the bundles were; `-ss` saves another 8%
   and drops debug information, which stack traces from a user's crash report would miss.

## Self-review

**Spec coverage** (the spike's recommended mechanism): one split build per set with entries named by specifier and
bare chunk names (Task 2); the module compiler and one ordered table (Task 1); the host reading the table in order,
unevaluated, before its entry (Task 4), wired into every txiki host of the app (Task 5) and the CLI (Task 7); the
evaluation-order risk guarded at boot (Task 3); the one-copy consequences audited (Task 6); the bundle-shape tests on
what ships and the docs (Task 8); Windows and macOS through CI (Task 9). The pack-import guard and Node are M2c's
(design questions 1 and 4).

**Placeholder scan:** every new file is given whole; every edit names its file, the text it replaces and the new text;
each step names its command and what it prints. Task 6 is a reading task by nature: it lists every state the map found
and what to check for each.

**Name consistency:** `retroplug-modc` and its three commands (`compile`, `array`, `table`); `rp_js_module`,
`registerSharedModules`; the symbols `rp_app_modules` / `rp_app_modules_count`, `rp_cli_modules` / `rp_cli_modules_count`,
`rp_cp_bundle`, `rp_ui_bundle`, `rp_cli_bundle`, the fixture's `rp_fx_modules`, `rp_fx_misordered`, `rp_fx_host_a`,
`rp_fx_host_b`; the module names `rp:cp`, `rp:ui`, `rp:cli`, `rp:chunks/...`; the CMake names `_MODC_COMMAND`,
`_MODC_DEP`, `RP_MODC_EXECUTABLE`, `retroplug-app-modules${_suffix}`, `RP_BUNDLE_<name>_MODULES_C`, `APP_MODULES_C`,
`CLI_MODULES_C`; `buildSharedModules({ set, outDir, productDir, write })` with its `shared`, `hosts`, `closures`.

## After the final review

M2b was built as `250852a29..8def14772` (plus the final review's fixes). Where the task blocks above and the tree
differ, the tree wins: `retroplug-modc` builds as C11 (QuickJS's header needs it under MSVC's C mode), refuses a
duplicate module name and reports a failed write; `tools/arm-build.sh` builds and passes it; the React facades are
served from memory by an esbuild plugin, their export list taken from React's production build; the CLI's dispatcher
registers its set without the SDK module (no shared module imports `retroplug-cli`; session files alone register it);
`tools/build-controlplane.js` is deleted.

The final review proved the milestone's point on the real tables: every app-set module compiled with `retroplug-modc`
and registered by the spike's harness, two pack sources evaluated afterwards (`pack:a/app.js`, `pack:b/app.js`) that
import `react`, `react/jsx-runtime`, `@retroplug/core/definePackage` and `@retroplug/core/systemRoles` link, and both
get the shell's instances. Its findings for M2c, beside design questions 1 to 5:

9. **The registration mark is per context, not per set.** A second table in the same context (a pack's own set, a
   bytecode cache) would be skipped; key the mark by the table's symbol or set name before M2c registers another.
10. **The pack-import guard (question 1) must also refuse `rp:*` names** (`rp:cp`, `rp:ui` and the hashed chunks resolve
    by bare name from the loaded-module table) **and run before txiki's import-map resolver**, which answers bare names
    first and which `tjs.setImportMap` lets JS change, so a pack could otherwise remap a contract.
11. **zod is not a contract, but `@retroplug/core/configFields` hands out zod schemas**, and every package that becomes
    a pack imports `zod` itself, so each pack would carry its own copy beside the shell's. Nothing uses `instanceof` or
    zod's global registry across that line today; decide whether `zod` becomes a host module of the set.
12. **No development JSX runtime is served** (`react/jsx-dev-runtime` is not in the set): a pack is built with the
    production JSX transform and `NODE_ENV=production`, matching the facade's export list.
13. **A CLI session file never gets the cli set**: it registers only the SDK module, whose bundle inlines its own copies
    of the contracts, so a pack's CLI half run from a session file would not link to the shared modules (question 4's
    problem, on txiki rather than Node).
14. **A pack source is evaluated with `use_realpath` off** (`TJS_EvalModuleContent` under a `pack:` name, which is not a
    path), as the spike's harness does.
15. **A module whose evaluation throws stays failed** in QuickJS: a contract that throws when the control plane
    evaluates it first (question 5's lvgljs module-scope reads) is broken for the UI too.
16. **Per instance, the app set is read at construct**: each plugin instance's context deserialises the whole set
    (64 modules, about 1.1 MB of bytecode, about 3 ms), the UI's modules included, where the UI's bytecode used to be read
    at the first editor open; nothing is evaluated early, and the binaries are 0.7 MB smaller.
