# restructure-03 — CMake custom targets to package.json scripts

**Status:** Done (2026-06-13).

## As-built (what actually landed vs. the plan below)

- **18 workflow `add_custom_target`s removed** from [CMakeLists.txt](../CMakeLists.txt)
  (the `ui-ts-test`/`cli-ts-test` blocks incl. their per-slug subtargets, the
  `screenshot`/`validate`/`reaper-headless` convenience targets, `cli-smoke`, and
  all eight `reaper-*` render/author/analyze targets). **Kept in CMake:**
  `sav-regenerate`, `ui-regenerate` (the build-time codegen DAG), the Catch2 test
  build (`add_subdirectory(test)` under `BUILD_TESTING`), and `add_subdirectory(cli)`.
- **19 pnpm scripts** in the root [package.json](../package.json): `configure`,
  `build`, `test`, `test:cli`, `test:ui`, `smoke`, `screenshot`, `validate`,
  and `reaper:*` (headless / analyze-smoke / analyze-lsdj-sync / mgb-smoke /
  mgb-author / lsdj-arduinoboy-{author,metro} / lsdj-midi-{author,metro} /
  lsdj-midi-drift{,-author}).
- **Two Node helpers** in [scripts/](../scripts/): `cmake-build.js` (parallel
  `cmake --build` of given targets — replaces the CMake `DEPENDS` that built a
  binary before running a tool) and `run-ts-tests.js` (globs `test/ts` at
  **runtime** — no more `CONFIGURE_DEPENDS` reconfigure — bundles each via
  build-test.js and runs it in its own `retroplug-cli`/`retroplug-ui-test --test`
  process; accepts a slug filter in slash or dash form, exact or dir-prefix).
- **Build-vs-run split preserved:** each script first builds the exact target it
  needs (`screenshot` → `retroplug-jack` for `bin/retroplug`, fixing the latent
  AGENTS.md gotcha where the old target depended on the umbrella `retroplug`),
  then runs the tool. The codegen DAG stays in CMake.
- **Command references updated** to `pnpm <script>` across AGENTS.md, README.md,
  RELEASE_TESTING.md, test/ts/README.md, the test-file comments, the Dockerfile,
  and post-create.sh (which now also prints `pnpm configure`/`build`/`smoke`).
  `make -C build retroplug-tests` → `pnpm build retroplug-tests` (a real build
  target; `pnpm build <target>` forwards args to `cmake-build.js`).
- **No stub CMake targets** left behind — clean removal, per the "CMake should be
  clean" goal.
- **Verified:** `pnpm build` reconfigures + builds clean (codegen DAG intact);
  `pnpm smoke`, `pnpm test:ui` (11 files), `pnpm test:cli` (29 files),
  `pnpm validate` (clap 18/0 + pluginval SUCCESS), and `pnpm reaper:mgb-smoke`
  (real headless Reaper render → `build/reaper-mgb-smoke.wav`) all green. The
  other `reaper:*` scripts are faithful 1:1 ports of the same render/author
  patterns.

**Note:** `pnpm test:ui` needs `build/` configured with `-DBUILD_TESTING=ON`
(`pnpm configure` does this) — same gating as the old `make ui-ts-test` target.

The original plan follows for reference.

## Goal

Move ~18 workflow and test `add_custom_target` definitions from CMakeLists.txt into pnpm scripts, making `pnpm` the user entry point for testing, validation, and Reaper workflows. Preserve the build-time DAG (`ui-regenerate`, `sav-regenerate`) and parallel compilation in CMake; consolidate workflow tooling into package.json + a `scripts/` Node helper dir.

## Depends on

- [01 — pnpm workspace skeleton](./restructure-01-pnpm-workspace.md)

## Architecture introduced

**Two-tier entry points:**
1. **Build DAG (stays in CMake):** `ui-regenerate` (~line 321) and `sav-regenerate` (~line 271) are real codegen steps inside the plugin-build cycle (`native → OpenRPC schema → TS client → UI bundle → bytecode → plugin link`). These remain CMake targets because they are load-bearing dependencies of the C++ compilation, not workflows. pnpm scripts may *invoke* them with `cmake --build build --target sav-regenerate` if needed, but they own the rules.

2. **Workflows (move to pnpm):** The ~18 targets used by developers and agents for testing, rendering, and validation (`cli-ts-test`, `ui-ts-test`, `screenshot`, `validate`, `cli-smoke`, `reaper-*`, etc.) are not part of the core build. They orchestrate existing binaries + shell scripts. pnpm becomes the human/agent interface.

**Script organization:**
- Root `package.json` scripts reference a `scripts/` directory of Node helpers or inline shell commands.
- Per-package `package.json` scripts for `packages/{native,retroplug,cli,ui}` if tests/workflows are isolated.
- Each script first ensures its binary is built: `cmake --build build -j$(nproc) --target <binary>` then runs the tool.

**Dynamic test slug filtering:**
- The `cli-ts-test-<slug>` and `ui-ts-test-<slug>` convention is preserved: a runtime glob of `test/ts/**/*.test.ts` (and `test/ts/ui/**/*.test.ts`) feeds test file paths into the respective runners (`retroplug-cli --test` / `retroplug-ui-test --test`), with slug syntax (`pnpm test:cli <slug>` form) for filtering (e.g., `pnpm test:cli gb-mgb`).
- Removes the CMake `GLOB_RECURSE CONFIGURE_DEPENDS` reconfiguration trigger; tests added to `test/ts/` are picked up at runtime.

## Tasks

1. **Enumerate and categorize the 18 targets:** Document each current CMake target (lines 271, 321, 485–800), what binary/script it runs, and which category (build DAG, test, validation, Reaper workflow). Note dependencies (e.g., `reaper-mgb-author` depends on `cli-ts-test-gb-mgb`). Keep mapping: `sav-regenerate` / `ui-regenerate` (stay); `ui-ts-test` / `ui-test` / `cli-ts-test` / `cli-smoke` / `screenshot` / `validate` / `reaper-headless` / `reaper-analyze-smoke` / `reaper-analyze-lsdj-sync` / `reaper-mgb-smoke` / `reaper-mgb-author` / `reaper-lsdj-arduinoboy-author` / `reaper-lsdj-arduinoboy-metro` / `reaper-lsdj-midi-author` / `reaper-lsdj-midi-metro` / `reaper-lsdj-midi-drift-author` / `reaper-lsdj-midi-drift` (move).

2. **Design the pnpm script layout:** Root `package.json` with `test` / `test:cli` / `test:ui` / `test:smoke` / `screenshot` / `validate` / `reaper:headless` and namespace Reaper workflows as `reaper:<name>` (e.g. `pnpm reaper:mgb-smoke`). Create `scripts/` dir with Node helpers (`build-target.js` to wrap `cmake --build build -j$(nproc) --target`; `run-test-slug.js` for glob + slug filtering).

3. **Implement dynamic test runner:** Write `scripts/run-test-slug.js`: glob `test/ts/**/*.test.ts`, filter by slug (if provided as argv), transpile each with `node tools/build-test.js`, invoke `retroplug-cli --test` with result. Emit TAP; exit nonzero on failure. Usage: `pnpm test:cli` (all) or `pnpm test:cli gb-mgb` (filtered). Preserve slug format (path-under-test/ts with `/` → `-`).

4. **Rewrite CMakeLists.txt workflow blocks as pnpm scripts:**
   - `screenshot` (line 526) → `pnpm screenshot`
   - `validate` (line 536) → `pnpm validate`
   - `reaper-headless` (line 547) → `pnpm reaper:headless`
   - `cli-smoke` (line 558) / `reaper-analyze-smoke` (line 642) → `pnpm test:smoke` + `pnpm reaper:analyze-smoke`
   - `cli-ts-test` (line 577), `ui-ts-test` (line 485) → `pnpm test:cli` / `pnpm test:ui` + `run-test-slug.js`
   - Reaper author/render (`reaper-{lsdj-arduinoboy,lsdj-midi}-{author,metro,drift}`, lines 703–800) → `pnpm reaper:mgb-author` / `pnpm reaper:mgb-smoke` / `pnpm reaper:lsdj-arduinoboy-author` etc.

5. **Update CMakeLists.txt:** Remove lines ~485–800 (workflow targets). Keep lines 271 (`sav-regenerate`) and 321 (`ui-regenerate`). Add a comment: "Real build-time codegen; workflows moved to pnpm scripts (see restructure-03)." Ensure ui-regenerate still drives plugin variants via `add_dependencies` (unchanged).

6. **AGENTS.md follow-up (explicit, not blocking):** Document in Risks that all references to `make -C build <target>` (lines ~84–210 in AGENTS.md) must be rewritten as `pnpm <script>` equivalents in a follow-up PR. E.g., `make -C build cli-ts-test` → `pnpm test:cli`, `make -C build validate` → `pnpm validate`.

## Verification

- Every former `make -C build <target>` has a working `pnpm <equivalent>` that exits zero.
  - `make -C build cli-ts-test` → `pnpm test:cli` runs all; `pnpm test:cli gb-mgb` runs one slug.
  - `make -C build ui-ts-test` → `pnpm test:ui` runs all UI tests.
  - `make -C build screenshot` → `pnpm screenshot`.
  - `make -C build validate` → `pnpm validate`.
  - `make -C build cli-smoke` → `pnpm test:smoke`.
  - All `make -C build reaper-*` have `pnpm reaper:<name>` equivalents (exit zero with Reaper/Xvfb/jackd present).
- `tools/run-sanitizers.sh thread` and `tools/run-sanitizers.sh address` still work (separate `build-tsan/` / `build-asan/` dirs, untouched).
- Bare `cmake --build build -j$(nproc)` runs the full build (ui-regenerate + sav-regenerate are depended on by plugin link targets).
- Adding `test/ts/foo/bar.test.ts` is picked up by `pnpm test:cli` without `cmake .` reconfigure.

## Risks / open questions

- **AGENTS.md breaks immediately.** All references to `make -C build <target>` throughout "Verification loop" and "Headless workflows" (~lines 84–210) will fail. Mitigation: explicitly call out as Task 6 follow-up PR, rewrite all commands there.
- **Third-party tooling.** CI / user docs / external workflows calling `make -C build cli-ts-test` will fail. Mitigation: add stub CMake targets that error with "see pnpm test:cli" message, or handle in documentation update.
- **Reaper workflows depend on external setup** (Reaper + Xvfb + jackd + MCP server). Unchanged by this step; underlying scripts fail the same way if infra is missing.
- **Node path assumptions.** pnpm scripts resolve paths from workspace root. All `tools/*.sh` / `tools/*.py` must use `../tools/` or absolute resolution. Test early.
- **`-j$(nproc)` in pnpm.** Each `cmake --build` call in Node must inline nproc (e.g. `--target <target> -j$(nproc)` in shell, or `require('os').cpus().length` in JS). Document clearly in script helpers.
