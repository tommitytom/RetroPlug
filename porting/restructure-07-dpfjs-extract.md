# restructure-07 — Extract & publish dpf.js

**Status:** Pending.

## Goal

Move the decoupled generic framework (dpf.js) into its own repository, publish it as an npm SOURCE package + template repo, and repoint RetroPlug to consume it via `require.resolve` + `add_subdirectory` — flattening RetroPlug's dependency graph and enabling reuse for other DPF plugins.

## Depends on

- [./restructure-02-js-toolchain.md](./restructure-02-js-toolchain.md) (unified esbuild + workspace structure)
- [./restructure-06-dpfjs-decouple.md](./restructure-06-dpfjs-decouple.md) (in-repo decoupling complete; seams are clean)

## Architecture introduced

**`dpf.js` npm package** (new repository):
- **Generic C++ source tree**: DPF ([deps/dpf](../deps/dpf)), DPF-Widgets ([deps/dpf-widgets](../deps/dpf-widgets)), lvgl-js-native (from current `lvgl-js-native` static lib target), reflect-cpp + rpcpp ([deps/rpcpp](../deps/rpcpp)), msgpack-c ([deps/msgpack-c](../deps/msgpack-c)), efsw ([deps/efsw](../deps/efsw)).
- **Generic TS source**: runtime/lvgljs (index.ts, input.ts — the LVGL JS API surface), esbuild build config + alias plugin (same as restructure-02), tsconfig base extending to consumers.
- **Generic txiki host C++**: the "embed txiki/QuickJS + a native RPC service + run a TS app bundle" launcher (extracted in restructure-05). Used by both the plugin and the CLI to boot the same codebase into different transports.
- **React reconciler glue** (`runtime/lvgljs/react.ts`, etc.): the adapter layer that lives in the framework, not the product.
- **Submodules as dpf.js's own**: DPF, DPF-Widgets, lv_binding_js (with its nested LVGL + txiki), rpcpp (with nested reflect-cpp + catch2), msgpack-c, efsw. Catch2 is shipped in dpf.js's sources but consumers author their own test suites.
- **`package.json` fields-based shipping**: the `"files"` field lists the C++ source tree (src/, deps/ submodules as `.gitmodules` entries are FLATTENED at pack time, never shipped as pointer submodules per [../AGENTS.md](../AGENTS.md) rule). Consumers resolve via `require.resolve('dpf.js/package.json')` + dirname, never hardcode `node_modules/dpf.js`.
- **Version single-source**: `package.json` version field is read by CMake at configure time via `string(JSON ... GET version)` and propagates to `project(VERSION ...)` and a generated `src/dpf_js_version.h` (defines `DPFJS_VERSION_STRING`). Drift (package.json ≠ CMake) is a build error.
- **Exported imported CMake target**: a top-level `CMakeLists.txt` in the package root creates an IMPORTED INTERFACE target `dpfjs::core` that consumers add via `add_subdirectory(path/to/dpf.js)`, NOT `find_package()`. This avoids the ABI-matrix problem: consumers pull source + build it in their own toolchain, bytecode is per-binary.

**RetroPlug consumer side**:
- **Via `require.resolve`** in root `CMakeLists.txt`: `execute_process(COMMAND node -e "console.log(require.resolve('dpf.js/package.json'))" OUTPUT_VARIABLE DPFJS_PACKAGE_JSON ... )`, then dirname it to get the dpf.js source path.
- **pnpm workspace dependency**: `dpf.js` lives as a `"devDependencies"` entry in `package.json` (with `"workspace: *"` if in the monorepo, or a tarball / git ref if external). `pnpm install` fetches it into node_modules; CMake consumes it from there.
- **Submodule pointer rule enforcement**: the npm tarball MUST flatten all `dpf.js/deps/*` submodules into committed source (no `.gitmodules` in the tarball). If dpf.js is consumed from git, submodules init automatically; if from npm, they are already flat.
- **Move miniz rename to RetroPlug side**: Mesen stays in RetroPlug native package, txiki moves to dpf.js. The collision now crosses the package boundary; the `-include mesen_miniz_renames.h` now lives in RetroPlug's `CMakeLists.txt` only (dpf.js doesn't know about Mesen).
- **Remove now-migrated submodules**: delete `deps/dpf`, `deps/dpf-widgets`, `deps/lv_binding_js`, `deps/rpcpp`, `deps/msgpack-c`, `deps/efsw` from RetroPlug's `.gitmodules` and on-disk.

**Template repo** (`create-dpfjs-plugin`):
- Giget / Tiged scaffold: `npm create dpfjs-plugin@latest my-plugin` → clones a minimal plugin repo (or uses a local template).
- Minimal plugin: `DistrhoPluginInfo.h` descriptor, `src/PluginXXX.{cpp,hpp}` empty service stub, `ui/App.tsx` hello-world React UI, `CMakeLists.txt` that does:
  ```cmake
  cmake_minimum_required(VERSION 3.14)
  project(my_plugin)
  
  # Resolve dpf.js from node_modules or a local path
  execute_process(COMMAND node -e "require.resolve('dpf.js/package.json')"
                  OUTPUT_VARIABLE DPFJS_PKG OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(DPFJS_PATH "${DPFJS_PKG}" DIRECTORY)
  
  add_subdirectory("${DPFJS_PATH}" dpf.js)
  
  # Create plugin; link dpfjs::core
  dpf_add_plugin(my_plugin TARGETS vst3 clap
    FILES_DSP src/PluginDSP.cpp
    FILES_UI src/PluginUI.cpp)
  target_link_libraries(my_plugin PRIVATE dpfjs::core)
  ```
- Includes a `package.json` with dpf.js as a devDependency (version pinned or `workspace: *`).
- Includes the esbuild + tsconfig bases from dpf.js (via symlink / copy in the scaffold).
- CI loop (GitHub Actions): `pnpm install && cmake -B build && cmake --build build -j$(nproc) && pnpm validate` — all green = the template is in sync with the dpf.js version.

## Tasks

1. **Create the dpf.js repository** (new empty repo; can be a separate commit or pushed immediately after step 06). Gitignore: node_modules/, build/, *.o, .DS_Store. Add a top-level `README.md` explaining "this is the generic DPF+LVGL+txiki+rpcpp framework; see dpfjs.md for API docs."
2. **Move submodule pointers and source into dpf.js**: copy RetroPlug's `deps/dpf`, `deps/dpf-widgets`, `deps/lv_binding_js` (with its nested txiki/libuv/quickjs/etc.), `deps/rpcpp`, `deps/msgpack-c`, `deps/efsw` into dpf.js; re-init the `.gitmodules` in dpf.js to point to these (or leave them as full submodules if consumed from git; the npm pack step will flatten them).
3. **Author dpf.js source structure**:
   - Copy `src/LvglJsEngine.{cpp,hpp}` from RetroPlug to dpf.js root-level `src/`.
   - Copy `runtime/lvgljs/` to dpf.js (include React reconciler, input handler).
   - Create esbuild config + tsconfig.base.json in dpf.js root (same as restructure-02 but parameterized for a consumer). Alias plugin resolves lvgljs from `../runtime/lvgljs` (relative to the consuming package's node_modules/dpf.js/).
   - Create `src/dpf_js_version.h.in` template file (CMake configure_file will stamp the version).
4. **Write dpf.js `package.json`** with:
   - `"name": "dpf.js"`, `"version": "0.1.0"` (semver-tracked separately from RetroPlug).
   - `"files": ["src/", "runtime/", "CMakeLists.txt", "package.json", "dpfjs.md", "tsconfig.base.json", ...]` (list only committed source; submodules flatten at pack time).
   - `"devDependencies"` for esbuild, TypeScript, etc. (same as restructure-02 workspace).
   - `"scripts"`: `"test"` runs Catch2 validation (if applicable; can be empty for now).
5. **Write dpf.js top-level `CMakeLists.txt`**:
   - Read version from package.json: `file(READ package.json _PKG_JSON) string(JSON _VERSION GET _PKG_JSON version) project(dpfjs VERSION "${_VERSION}") configure_file(src/dpf_js_version.h.in src/dpf_js_version.h)`.
   - Include DPF, LVGL, txiki, rpcpp, msgpack-c, efsw (same add_subdirectory calls as current CMakeLists.txt lines 45–123).
   - Create the lvgl-js-native target (same as current lines 125–203).
   - Export an IMPORTED INTERFACE target: `add_library(dpfjs::core INTERFACE) target_include_directories(dpfjs::core INTERFACE ...) target_link_libraries(dpfjs::core INTERFACE lvgl::lvgl lvgl-js-native tjs rpcpp msgpack-c efsw miniz ...)`.
   - Do **not** create `dpf_add_plugin` variants here; those live in DPF.
6. **Update RetroPlug `CMakeLists.txt`**:
   - Add `execute_process(COMMAND node -e "require.resolve('dpf.js/package.json')" ...)` at the top to resolve dpf.js path.
   - `add_subdirectory("${DPFJS_PATH}" dpf.js)` to pull in dpf.js's targets (dpfjs::core, DPF targets, etc. all become available).
   - Remove the old `add_subdirectory(deps/dpf)`, lines 68–123 (LVGL, txiki, msgpack-c, rpcpp, efsw, lvgl-js-native) — they now come from dpf.js.
   - Keep `add_subdirectory(deps/mesen)`, `deps/r8brain`, `deps/enkiTS`, `deps/miniaudio` (emulator-specific, stay in RetroPlug).
   - Keep lines 221–262 (`rpc-schema-dump`, `sav-schema-dump`, UI regeneration logic) as-is; they still live in RetroPlug because they depend on `PluginRpcService` (RetroPlug-specific).
   - Move `deps/mesen/Utilities/mesen_miniz_renames.h` to a RetroPlug-local path (e.g., `src/system/mesen/miniz_rename_safety.h` or keep it where it is, just ensure the compile_options still apply).
   - Link plugin against `dpfjs::core` in addition to RetroPlug-specific targets: `target_link_libraries(${NAME} PUBLIC dpfjs::core sameboy mesen ...)`.
7. **Update RetroPlug `.gitmodules`**: remove the six entries for `dpf`, `dpf-widgets`, `lv_binding_js`, `rpcpp`, `msgpack-c`, `efsw`. Keep `sameboy` (Game Boy core, RetroPlug-specific), `catch2` (test infra, still needed in RetroPlug for native tests).
8. **Add dpf.js to RetroPlug `package.json`**:
   - As a `"devDependencies"` entry: `"dpf.js": "workspace: *"` (if dpf.js is in the pnpm workspace) or `"dpf.js": "^0.1.0"` / git ref (if external).
   - `pnpm install` will fetch it into `node_modules/dpf.js`.
9. **Create template repo** (`create-dpfjs-plugin`):
   - Scaffold generator script (Giget or Node.js-based) that takes plugin name → creates a new directory with starter files.
   - Starter: `DistrhoPluginInfo.h`, `src/PluginDSP.cpp` (empty service), `src/PluginUI.cpp`, `ui/App.tsx` (hello-world), `CMakeLists.txt` (as above), `package.json` (dpf.js devDep).
   - Optional: GitHub Actions workflow that runs `pnpm install && cmake -B build && cmake --build build -j$(nproc) && pnpm validate` to catch drift.
10. **Verify npm pack behavior**: run `npm pack` on dpf.js and inspect the `.tgz` — confirm submodules are flattened (no `.gitmodules` pointer in the tarball), all C++ source is present, version string matches.
11. **Test local development override** (documented, not shipped): create a `.npmrc` or use `pnpm link` / `yarn link` so a consumer can point to a local dpf.js checkout for editing in place: `pnpm add "file:../dpf.js"` or `CPM_<name>_SOURCE` CMake cache variable pattern if using CPM.
12. **Update [../dpfjs.md](../dpfjs.md)** to note that framework docs are now in `dpf.js` repo; RetroPlug's copy is a legacy reference. Consumers should consult the standalone dpf.js docs.

## Verification

- **RetroPlug builds as a pure node_modules consumer**: after step, a clean clone requires `pnpm install` BEFORE `cmake --build build`; CMake configure fails with a clear error if dpf.js is not present. Build completes with `-j$(nproc)`.
- **All verification loops pass**: `make -C build cli-ts-test` (execute via headless loop), `make -C build ui-ts-test` (headless UI tests), `make -C build validate` (plugin format validators).
- **npm pack flattens submodules**: `cd dpf.js && npm pack && tar tzf *.tgz | grep -E '(\.gitmodules|deps/dpf/)' | wc -l` returns 0 (no pointers in the tarball).
- **Template scaffolds a buildable empty plugin**: `npm create dpfjs-plugin@latest test-plugin && cd test-plugin && pnpm install && cmake -B build && cmake --build build -j$(nproc)` completes successfully.
- **Template CI stays green**: GitHub Actions in `create-dpfjs-plugin` repo runs the build flow and reports pass (proves template tracks dpf.js version drift).
- **Local dpf.js override works**: set up a development RetroPlug instance to consume a local `dpf.js` checkout via pnpm link / `.npmrc` / symlink; edits to dpf.js source rebuild RetroPlug without re-npm install.

## Risks / open questions

- **Cross-repo dev loop friction**: developing dpf.js and RetroPlug in tandem (e.g., adding a new RPC method that needs both C++ and TS) now requires coordination across two repos. Mitigation: use `pnpm link` or `CPM_DPFJS_SOURCE` / `require.resolve` override to point to a local checkout; document this in a `DEVELOPING.md`.
- **npm tarball submodule flattening**: the current plan relies on `npm pack` automatically flattening submodules (it does by default, but only if `.gitmodules` is NOT in the `"files"` array). Verify at pack time and add a pre-publish check script: if `.gitmodules` appears in the tarball, fail the publish.
- **Version drift (package.json ↔ CMake)**: if `package.json` version and CMakeLists.txt disagree, the build fails at configure time (load-bearing). Keep them in sync; consider a pre-commit hook in dpf.js to validate they match.
- **Template repo can rot vs dpf.js version**: a consumer cloning an old version of the template might pin an old dpf.js version. Mitigation: keep the template's `package.json` unpinned (`"dpf.js": "*"` or `"latest"`) so it always pulls the most recent dpf.js. Document that major version bumps to dpf.js may require template edits (breaking ABI, API changes).
- **Miniz symbol collision moves to edge**: after extraction, the collision logic only matters when RetroPlug and dpf.js are both linked into the same binary (the plugin). Confirm the force-include on Mesen TUs still fires after moving dpf.js to node_modules (CMake's subdir scoping may hide it; test carefully).
- **First cmake configure now depends on Node**: users who don't have Node installed can't configure RetroPlug's build. Document this as a requirement; the devcontainer already includes Node, so CI is fine.
- **Derived artifacts stay derived**: do not commit `build/`, `node_modules/`, or any generated CMake config files from dpf.js (those are per-consumer). Verify `.gitignore` includes these in both repos.
