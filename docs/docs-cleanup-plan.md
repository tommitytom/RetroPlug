# Docs cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** AGENTS.md cut from 835 lines to rules and pointers (under 200 lines), what leaves it merged into the spec doc
that owns each topic (a new [spec/14](../spec/14-hosts-harnesses.md) for the host legs and the CLI harness), and
[spec/07](../spec/07-remaining-work.md) cut from 1,181 lines of checklist and phase history to a checklist of open work
and one detail entry per item, with the accepted limits it carried moved to the spec docs of the subsystems they limit,
and every reference to a moved AGENTS.md section or a gone spec/07 entry repointed.

**Architecture:** Docs and comments only; no code changes behaviour. The fact ledger
(`.superpowers/docsclean/ledger.jsonl`, 1,703 rows, one per claim of AGENTS.md and spec/07 plus the eight the design
adds) names, for every fact, where it goes and a probe (a short exact string) that must appear there;
`.superpowers/docsclean/check.mjs` is the test every task is written against: the probes at their destinations, every
`dup` quote still in the doc it names, every relative link and `#anchor` in a touched file resolving, AGENTS.md's
200-line cap and spec/07's shape once their tasks land, and no added line with an em dash or past 120 columns (a table
row aside). Tasks 1 to 4 merge into the spec docs (spec/14 and the index; spec/06; spec/00, 02 and 03; spec/04, 05, 11,
12 and 13), task 5 rewrites AGENTS.md and repoints what named it, task 6 rewrites spec/07 and repoints what named it.
Tasks 1-4 come first so the links tasks 5 and 6 write resolve.

**Tech Stack:** Markdown hard-wrapped at 120 columns; node scripts (`check.mjs`, `ledger-check.mjs`) reading the
ledger; the comment edits reach TypeScript (`pnpm check:boundaries`, `pnpm typecheck`), CMake (a reconfigure and a
`retroplug-cli` build) and C++ headers and sources (their targets rebuilt). No submodule pointer moves.

**Spec:** [docs-cleanup-design.md](docs-cleanup-design.md), sections 1 to 4, and section 5, which overrides them where
they differ. Its section 4 step 4 is how this plan is executed (an implementer per task, tree identity as the spec
check, an Opus review per task reading the old text against the new).

**Provenance:** every block below was applied to this worktree at `b65dbf98b` in this plan's order and checked before
the plan was written: linux-x64, GCC 13.3, Node 26. `check.mjs --task N` was run before each task (its red run, quoted)
and after it (its green run, quoted), each task's files were staged (nothing was committed), and each task's wider
checks were run on the tree it leaves. The tree was then restored and the plan's own text was applied to it a second
time, block by block through `git apply`, as an implementer would (`.superpowers/docsclean/execute-plan.mjs`): every
task red, then green, its staged diff identical to the first pass's; the red and green runs quoted below are that
pass's. The worktree was restored to its starting state (`git status --porcelain` printing ` M deps/sameboy` alone),
and the plan was replayed by text: each task's blocks applied in order to copies of the touched files at `b65dbf98b`,
the result after every task byte-identical to the validated tree (`node .superpowers/docsclean/replay.mjs`). Every
Expected line is quoted from a real run.

**How to apply a step's text.** A step gives either a `patch` block, a `git apply`-able unified diff against the tree
as the steps before it left it, or a `markdown file=<path>` block, the WHOLE of that file. The blocks are fenced with
four backticks, because the docs inside them carry three-backtick fences of their own. Apply a patch block by running
this with the block's text (not its fences) between the heredoc's first line and the closing `PATCH`:

```
git -C /workspaces/RetroPlug-main/cuttlefish apply - <<'PATCH'
<the step's patch block>
PATCH
```

and write a whole-file block with the editor, or with `cat > /workspaces/RetroPlug-main/cuttlefish/<path> <<'DOC'`, the
block's text, and `DOC` on its own line. The file ends with the block's last line and one newline. Every command below
is written out with absolute paths or `-C`, so no step needs to change directory.

**The check.** `node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task N` checks the rows
and files of tasks 1 to N (no flag: all six); `--max-failures 3` keeps a red run short. It reads the ledger beside it,
which the user saw (counts, every `done` and `stale` row) before this plan was written; `ledger-check.mjs` there
validates the ledger itself and passes. Both live in the gitignored `.superpowers/docsclean/`, which this plan does not
commit.

## Ground rules

These are the repo's rules this cleanup must keep. They are not optional and no step repeats them.

- **Never leave the worktree**: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish` and pnpm with
  `pnpm -C /workspaces/RetroPlug-main/cuttlefish`. Never `cd`, never switch branches, never `git stash`, never
  `git reset --hard`, never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset or stash it, and never touch any other submodule. `git status --porcelain` lists it; `git add`
  only the paths a step names.
- **The em dash (U+2014) never appears** in anything this plan adds: docs, comments or commit messages. Use " - ", ":"
  or ";". The check refuses an added line that carries one; the lines already there are left as they are.
- **A commit message is a subject line, a blank line, then a body saying why**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution of any kind.
- **Records stay records.** Never edit an existing `docs/*-plan.md`, or a design doc other than
  [docs-cleanup-design.md](docs-cleanup-design.md) (whose as-built section is the controller's, after the run). Design
  docs and plans are not repointed when a spec heading they name moves; nor are the other `docs/` records.
- **Never `rm -rf build`** (or any `build*/`). Build a target with `cmake --build <dir> --target <t> -j$(nproc)`.
- **Nothing in this cleanup opens an audio, MIDI, video or serial device.** Never run `retroplug-sdl`, an `sdl:*` or
  `reaper:*` script, or a verification kit. `check.mjs`, `pnpm check:boundaries`, `pnpm typecheck` and the builds open
  none.
- **Hard wrap at 120 columns**, as the docs do; a table row stays one line however long.

---

## Task 1: spec/14 for the hosts and harnesses run by hand, and its index row

AGENTS.md's last third (its lines 660 to 835) is how the checks that run RetroPlug inside something else work: the
Reaper leg's editor and multi-instance checks and their Windows twins, Renoise, the three SDL harnesses and the private
PipeWire server, the device guard and its self-test, and `retroplug-cli` as a consumer repo's whole test runner. None
of it is a rule, and no spec doc owns it, so it gets a doc of its own (design section 2's last row): 108 ledger rows,
all `merge` into spec/14. The new doc links what other docs already own rather than repeating it: spec/06's
`pnpm scripts` table (every script, what it builds, whether CI runs it), spec/06's "Running the Reaper leg in parallel"
(`RP_JOB_TAG`), spec/12 section 6 (the parameter legs) and docs/lsdj.md (the LSDj DAW-timing renders). Its six headings
are fixed, because tasks 5 and 6 link them: `## The Reaper leg`, `### Reaper on Windows`, `## Renoise`,
`## The SDL harnesses`, `### The device guard`, `## retroplug-cli as a test harness`.

spec/README.md's "The documents" table gains the row and "Reading order" a bullet. spec/00's "Where to go next" table
lists only 01 to 07, so it gains nothing.

**Files:**

- Create: `spec/14-hosts-harnesses.md` (190 lines).
- Modify: `spec/README.md` (one index row after 13's, one "Reading order" bullet).

**Ledger rows (task 1):** the 108 `merge` rows whose `dest` is `spec/14-hosts-harnesses.md` (ids A544 to A889); the 12
`dup` rows naming spec/README.md must still hold.

### Steps

- [ ] **Step 1: run the check and watch it fail**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 1 --max-failures 3
```

Expected: exit 1, every probe failing on the missing file:

```
check.mjs: tasks 1..1 (2 files)
FAIL probes at their destination (keep / merge / open / new): 0 passed, 108 failed
       A544: spec/14-hosts-harnesses.md does not exist (probe "linux-x64")
       A545: spec/14-hosts-harnesses.md does not exist (probe "links AND runs")
       A547: spec/14-hosts-harnesses.md does not exist (probe "touched a device path")
       ... and 105 more
ok   dup quotes in the doc they name: 12 passed, 0 failed
ok   relative links and anchors in touched markdown files: 38 passed, 0 failed
ok   no em dash in an added line: 0 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 0 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 0 passed, 0 failed
108 failure(s)
```

- [ ] **Step 2: write spec/14 and the index row**

The whole of `spec/14-hosts-harnesses.md`:

````markdown file=spec/14-hosts-harnesses.md
# 14 - Hosts and harnesses

The checks that run RetroPlug inside something else: a real DAW (Reaper on Linux and Windows, Renoise), the SDL
standalone against a real audio server, and `retroplug-cli` as a consumer repository's whole test runner. What they
have in common is that each proves something the headless tiers cannot, and that most of them can reach real hardware,
so they run under a guard that proves they did not.

What this doc does not repeat: the [`pnpm scripts`](06-build-test.md#pnpm-scripts) table lists every script, what it
builds and whether CI runs it; [Running the Reaper leg in parallel](06-build-test.md#running-the-reaper-leg-in-parallel)
owns `pnpm reaper:all` and the `RP_JOB_TAG` isolation that lets the legs share a machine; the parameter legs
(`reaper:params`, `-clap`, `-vrc7`) are [12-dynamic-parameters.md](12-dynamic-parameters.md) section 6's; the LSDj
sync and DAW-timing renders and their analyzer are [docs/lsdj.md](../docs/lsdj.md)'s; and what `retroplug-sdl` is and
how it is built is [13-subsystems.md](13-subsystems.md)'s.

## The Reaper leg

Every Reaper check boots its own headless stack through the sourced [tools/reaper-env.sh](../tools/reaper-env.sh) and
drives the built plugin in a real Reaper. Beside the renders (docs/lsdj.md, and the parameter legs in spec/12) there
are checks of the hosted editor and of a host holding several instances, which no headless tier can reach:

- **`reaper:editor`** ([tools/run-reaper-editor.sh](../tools/run-reaper-editor.sh)) floats the hosted plugin editor and
  asserts its LVGL snapshot rendered: the script dumps `RETROPLUG_SCREENSHOT_PATH`, and a uniform PNG of about 159
  bytes means the editor drew nothing. It is the only check of on-screen editor rendering.
- **`reaper:editor-reopen`** ([tools/run-reaper-editor-reopen.sh](../tools/run-reaper-editor-reopen.sh)) loads mGB
  through the UI, closes and reopens the editor, and asserts the project is still shown. It is mouse-driven (it clicks
  the Load mGB row with xdotool), since keys do not reach the plugin editor headlessly.
- **`reaper:editor-autoload`** ([tools/run-reaper-editor-autoload.sh](../tools/run-reaper-editor-autoload.sh)) floats
  the editor with a project already in the control plane and asserts the editor shows it: the session-restore /
  setState half. It is deterministic, with no mouse.

The last two guard the single-store graph the editor shares with the control plane: a close and reopen, or a
setState-restored project, that shows the start menu means the UI composed a store of its own again. The three are
not in CI: they need the whole DAW and X stack.

**More than one instance.** `reaper:two-instances` and `reaper:two-instances-load` are the only checks that run MORE
THAN ONE instance in a host, which is its own state: a control-plane runtime each, and an LVGL display each on one UI
thread. The first ([tools/run-reaper-two-instances.sh](../tools/run-reaper-two-instances.sh)) loads N instances + N
editors and watches the host survive; `RP_TWO_FORMAT=vst3|clap` picks the format, `RP_TWO_COUNT` the count (default
2) and `RP_TWO_AUTOLOAD` a `.rplg` autoloaded into every instance. The second
([tools/run-reaper-two-instances-load.sh](../tools/run-reaper-two-instances-load.sh)) drives the failure that was
reported: load a ROM through instance 1's editor, open instance 2, load through that one. It judges by a ReaScript
defer HEARTBEAT rather than by the process, because the bug froze the host rather than crashing it and a live process
proves nothing: Reaper runs defer on the same main thread both editors idle on, so a blocked UI thread stops the
counter dead. `RP_LOAD_MODE=dialog` (the "Load..." row) is the leg that reproduced the freeze; the default loads
directly. The harness SIGKILLs Reaper at the end, since a host spinning in LVGL never answers SIGTERM. Neither is in
CI, and neither is in `reaper:all`: they stand up several instances and are run on their own.

**risa's sync render.** `reaper:risa-sync` is the NES/risa twin of the LSDj drift render, judged by the same `--drift`
analyzer ([tools/reaper-timing-analyze.py](../tools/reaper-timing-analyze.py)), whose labels are tracker-neutral for
that reason. risa's host sync is driven by the DAW's transport alone, so its project carries no MIDI item at all
([tools/reaper-risa-author.lua](../tools/reaper-risa-author.lua)).

### Reaper on Windows

The same leg runs on Windows through PowerShell twins that reuse the `.rpp` fixtures, the `.lua` authors and the
Python analyzer unchanged: `pnpm reaper:win` ([tools/run-reaper-suite.ps1](../tools/run-reaper-suite.ps1)), plus
`reaper:win-editor` ([tools/run-reaper-editor.ps1](../tools/run-reaper-editor.ps1)) and `reaper:win-params`
([tools/run-reaper-params.ps1](../tools/run-reaper-params.ps1)). The Linux scaffolding has no Windows equivalent and
needs none; each piece maps to something simpler, and [tools/reaper-env.ps1](../tools/reaper-env.ps1)'s header
documents the mapping:

| Linux | Windows |
|---|---|
| Xvfb + openbox | nothing: there is a real desktop |
| the jackd dummy backend | Reaper's own Dummy Audio device, `[audioconfig] mode=4`, whose `dummy_blocksize` IS the render block size, so `REAPER_JACK_PERIOD` means the same on both platforms (the midi-timing scenarios need 8192) |
| the xdotool EULA dismisser | seeding the `[nag]` key from the user's own `%APPDATA%\REAPER\REAPER.ini`: without it a fresh config stops at the EULA before running the ReaScript, and the run hangs with nothing on stdout |
| `HOME=<cfg>` | `-cfgfile`, which on Windows relocates the whole resource tree (`Scripts/`, the plugin-scan cache), not just the ini |

The one real loss is concurrency: a single desktop and a process-global audio device mean the Windows scenarios run
sequentially, so budget the sum. A scenario whose ROM is absent (risa) reports SKIP, not FAIL. The analyzer needs a
real Python with numpy; the `python.exe` on `PATH` under `WindowsApps` is a Store stub, not an interpreter.

## Renoise

Renoise is the second host, driven by [tools/run-renoise-render.ps1](../tools/run-renoise-render.ps1) and the
`com.retroplug.harness.xrnx` tool ([tools/renoise](../tools/renoise)). It exists because every host-integration bug
this repo has hit (the class-id collision that blanked the hosted editor, the per-ROM parameter relabelling) was a bug
about how ONE host drives the plugin, and one host cannot catch those.

Renoise has no render CLI, so the tool IS the CLI: it runs at startup, reads a job spec, drives the Lua API and quits.
It has two modes, and the split is not cosmetic. `-Mode render` needs a registered Renoise: `song:render()` is
disabled in the demo, and Renoise says so with a modal dialog rather than a Lua return, so the runner looks for it in
`Log.txt` and reports SKIP instead of hanging. `-Mode probe` instantiates the plugin and dumps the parameter surface
the host sees; it renders nothing, so it works in the demo.

Renoise finds the plugin through its own configured VST path: point it at `build\bin` once in Preferences > Plug-ins.
The harness deliberately does not rewrite `Config.xml`, which Renoise owns and rewrites wholesale on exit.

## The SDL harnesses

Three scripts run the real `retroplug-sdl` binary: `sdl:smoke`, `sdl:controller` and `sdl:pipewire` (their legs are
in the [`pnpm scripts`](06-build-test.md#pnpm-scripts) table). Each puts the [device guard](#the-device-guard) in front
of the run itself.

**`sdl:smoke`** ([tools/run-sdl-smoke.sh](../tools/run-sdl-smoke.sh)) boots and shuts down the standalone on SDL's
dummy drivers and the null audio backend. CI runs it on the linux job (linux-x64), so CI proves the standalone both
links AND runs; it also fails the run if the null-pinned run touched a device path (its own `NOSND_LOG` is not empty).

**`sdl:pipewire`** ([tools/run-sdl-pipewire.sh](../tools/run-sdl-pipewire.sh)) is the audio-device half the smoke
cannot cover, since the smoke runs on the null device: it stands up a private PipeWire server and asserts which output
device the host opens, in both audio modes. The PortAudio legs start from the session default sink (the
`default.audio.sink` metadata key wins), then an explicit pick, a stale one and an explicit driver; SDL2's legs run the
same selection cases, then the 8-channel case, which is the one place the two backends differ audibly (PortAudio opens
8 ports with pair 1 linked; SDL reports 8 at the API but its PipeWire node has 2 output ports). The live-menu leg
drives the real menu to Settings > Audio while a stream runs and holds the stream open across the menu build: the same
node before and after, and exactly one open line. A leg pins its backend with `--audio-backend`, which replaces only
the backend, so the saved driver and device survive.

The private server is built the way it is for reasons a container forces:

- It is session-manager-free: the sinks AND the `default` metadata object are declared as `context.objects` in a
  daemon-only config. wireplumber needs logind and dbus and exits without them in a container, which once silently
  collapsed the script's expectations onto the priority fallback.
- A third sink, `rp-decoy`, always outranks the others on `priority.session` and is never the default, so "followed
  the default" cannot pass by accident.
- The SDL selection legs, the wide-output legs and the menu leg do need wireplumber, and dbus-daemon with it: SDL's
  PipeWire driver connects through the session graph, and ports only materialise once something links the stream.
  That part starts wireplumber with its bluetooth context dropped, because that context's logind module finds no
  `/run/systemd` in a container and takes the daemon down, and on a private `dbus-daemon`, without which wireplumber
  quits at startup whenever the shell has no session bus. Its config has the ALSA, v4l2 and libcamera device monitors
  disabled; they are wireplumber's `90-enable-all.lua`'s, which the script edits in its copy, and those monitors are
  what would open the capture card and mirror the MIDI interfaces.

The run reports its guard counts: the PortAudio legs' ALSA probe legitimately logs refused card control nodes, and
the leg that runs with no libpipewire holds only that no card's playback node is opened. That leg falls back to `null`
with a status, because a container's PortAudio cannot enumerate a private null PCM. No CI runner has PipeWire, so the
script is not in CI; the devcontainer ships one for it.

### The device guard

Every harness that could reach real hardware runs under the device guard, whose source is tracked:
[tools/sdl-audio-guard/nosnd.c](../tools/sdl-audio-guard/nosnd.c), built by
[build-guard.sh](../tools/sdl-audio-guard/build-guard.sh) into the harness's own scratch directory and loaded through
`LD_PRELOAD`. It refuses an open before the syscall: every `/dev/snd` node but `seq`/`timer`, every `/dev/video*`,
`/dev/media*` and `/dev/v4l-subdev*`, every `/dev/ttyACM*`, `/dev/ttyUSB*`, `/dev/ttyDbgProbe*` and `/dev/serial/*`,
and `/dev/hidraw*` and `/dev/bus/usb/*`. `NOSND_LOG` records each refusal, and each dlopen of an audio client library
that was not already loaded, so a run PROVES no real sound card, capture card or serial device was opened, which an
strace cannot: it never sees a refusal.

The guard is sourced through [guard-env.sh](../tools/sdl-audio-guard/guard-env.sh), which self-tests after preloading
it: a probe open of `/dev/snd/nosnd-selftest` must be refused AND logged, or the harness exits non-zero. The
self-test is not ceremony: ld.so silently ignores a preload it cannot load, so a guard built in a noexec directory or
for the wrong ELF class would leave the run unguarded with nothing said. The three SDL harnesses and
[tools/reaper-env.sh](../tools/reaper-env.sh) source it themselves, so a bare `pnpm sdl:controller` or `pnpm reaper:*`
is guarded, not only a run of the verification kit (the gitignored tip-verify scripts): the kit prepends its own copy
of the same source for the whole run, and guard-env.sh then adds no second one. A Reaper leg's teardown prints the
log's counts and fails on any refused card playback, video or serial node
([spec/06](06-build-test.md#running-the-reaper-leg-in-parallel)).

## retroplug-cli as a test harness

`retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `retroplug-cli run <session.ts>` does one
file, so a consumer repo (BlipToaster) needs no Node, npm, esbuild or node_modules: its kit is the binary plus tests,
and nothing else. [tools/sync-cli-to-bliptoaster.sh](../tools/sync-cli-to-bliptoaster.sh) is the sync step, a
binaries-only copy of two binaries, `retroplug-cli` and `retroplug-n8-hwtest` (the bare N8 device access the
nes-hardware-lab skill uses), into the consumer's `retroplug-cli/bin/`; no SDK file goes over. The harness's own check
is `pnpm test:cli-ts`.

**The SDK is a module, never a file.** A test imports the SDK by the bare specifier `"retroplug-cli"`, which the
binary serves from QuickJS's loaded-module table: `cli/main.cpp` `JS_ReadObject`s the embedded module bytecode
(compiled with `tjsc -n retroplug-cli`) before a session evaluates, and `js_host_resolve_imported_module` checks that
table BEFORE the module loader, so nothing is left to sync or to go stale. A relative specifier could not work this
way: `tjs_module_normalizer` consults the import map only for bare names (`if (name[0] != '.')`), so `"../sdk/x.js"`
always hits the disk. Only the `.d.ts` is written out ([cli/sdkAssets.ts](../packages/retroplug/cli/sdkAssets.ts)),
because editors and `tsc` read declarations from disk and reach it through a tsconfig `paths` entry, and only when a
source actually imports the SDK (`buildTsDir` reports `needsSdk`).

**Stripping, not bundling.** The txiki runtime already resolves `import` off disk at runtime, so tests never needed
bundling, only stripping; and consumer tests already use explicit `.js` specifiers, so emitting `X.ts` -> `X.js` at
the same directory depth needs no specifier rewriting. That depth is why stripped output lands in the source dir's
sibling `.rp-test-build/` (`buildDirFor` in [cli/tsStrip.ts](../packages/retroplug/cli/tsStrip.ts)); anywhere else,
every `../sdk/...` import breaks. Where that sibling is not writable (a session straight under `/tmp`),
`resolveBuildDir` falls back to a directory under the runtime's temp dir, which keeps sibling imports working.

**One process per file, in a pool.** Each test file runs in its own process (`tjs.spawn`), which is required, not
tidy: the TAP harness calls `tjs.exit` when a file ends, its case list is module-level, and the native `Engine` is
per-process. That isolation is also what lets the files run concurrently, in a bounded pool of half the logical cores
by default (`--jobs N` / `-j N` / `RP_TEST_JOBS`). Dispatch is longest-first from the durations recorded in
`<outDir>/.timings.json`, inside the derived build dir so a consumer gains no tracked file, and each child's stdout and
stderr are captured and flushed as one labelled block, since concurrent children sharing a terminal shred the TAP.
`-j1` keeps the serial behaviour exactly (listed order, stdio inherited, output streaming live), which is what you want
when debugging one file. The pipes are drained concurrently with the wait
([cli/childSession.ts](../packages/retroplug/cli/childSession.ts)): `await stdout.text()` then `await stderr.text()`
deadlocks on a child noisy enough to fill the other pipe's buffer.

**The stripper.** It is ts-blank-space plus the TypeScript parser, compiled in as global-code bytecode (+4 MB) and
loaded on demand through `__rp_loadTsStripper`, so no other command pays for it. Types are stripped, not compiled: only
erasable syntax works, and `enum`, `namespace` and constructor parameter properties are refused with `file:line:col`.
Wiring ts-blank-space's optional `onError` is what enforces that; omit it and an `enum` passes through as invalid
JavaScript ([tools/run-cli-ts-tests.sh](../tools/run-cli-ts-tests.sh) checks the refusal).
````

`spec/README.md`:

````patch
diff --git a/spec/README.md b/spec/README.md
index ce4e2223a..99bc3480c 100644
--- a/spec/README.md
+++ b/spec/README.md
@@ -46,6 +46,7 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 | [11-ui-rendering.md](11-ui-rendering.md) | **Built.** Rendering a system to WAV from the `System > Render` menu or `retroplug-cli render` as an engine job on the offline pool - a fresh instance built from a *copy* of the live SRAM/savestate (never the running core). A shared `src/render/` library run by a bare-QuickJS `RenderHost` (own `Engine`, no txiki, continuous blocks) on a pool worker; one `engineRun` promise, cancellable, surviving editor close; the `RenderJobs` list behind the tile elapsed-duration/cancel badge; and the render baseline that holds the bytes. |
 | [12-dynamic-parameters.md](12-dynamic-parameters.md) | **Built** (CLAP + VST3; other formats keep their construct-time names). Per-ROM named DAW automation lanes mapped to mGB / BlipToaster MIDI CCs: the fixed-pool / mutable-descriptor contract the two formats actually permit, the DPF fork's `requestParameterInfoUpdate()` + re-declare-and-diff behind an opt-in macro, the plugin's CC slot pool and its `__rp_parameterMapJson` seam, and the real-Reaper check that proves a host acts on the flag. |
 | [13-subsystems.md](13-subsystems.md) | **The index of everything else.** One short entry each for the subsystems the docs above do not cover: the common tracker interface (`packages/tracker/ts/`), risa, smsggdj, the LSDj HD player, the Everdrive N8 hardware bridge, Launchpad, the controller role, the pico firmware, `retroplug-sdl`, the Node addon, and the node graph the engine runs on. What each is, where it lives, and which command proves a change to it. |
+| [14-hosts-harnesses.md](14-hosts-harnesses.md) | The checks that run RetroPlug inside something else, and the guard they run under: the Reaper leg's editor and multi-instance checks and its Windows twins, Renoise, the SDL harnesses (`sdl:smoke`, `sdl:pipewire`) and the device guard that proves no real card, capture card or serial device was opened, and `retroplug-cli` as a consumer repo's whole test runner. |
 
 ## Reading order
 
@@ -55,6 +56,7 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 - **Working on the port itself?** [07-remaining-work.md](07-remaining-work.md) is the map of what's left.
 - **Looking for a subsystem you can't find above?** [13-subsystems.md](13-subsystems.md) - trackers,
   hardware bridges, and the non-plugin hosts.
+- **Running a DAW, the SDL standalone or a consumer's tests by hand?** [14-hosts-harnesses.md](14-hosts-harnesses.md).
 
 ## Provenance
 
````

- [ ] **Step 3: run the check and watch it pass**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 1
```

Expected: exit 0.

```
check.mjs: tasks 1..1 (2 files)
ok   probes at their destination (keep / merge / open / new): 108 passed, 0 failed
ok   dup quotes in the doc they name: 12 passed, 0 failed
ok   relative links and anchors in touched markdown files: 73 passed, 0 failed
ok   no em dash in an added line: 192 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 192 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 2 passed, 0 failed
all checks pass
```

- [ ] **Step 4: stage the task's files and check their whitespace**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- spec/14-hosts-harnesses.md spec/README.md
git -C /workspaces/RetroPlug-main/cuttlefish diff --cached --check
```

Expected: no output, exit 0.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(cleanup): spec/14 for the hosts and harnesses run by hand

AGENTS.md carried, as its last third, how the checks that run RetroPlug inside something else work: the Reaper leg's
editor and multi-instance checks, their Windows twins and the scaffolding each Linux piece maps to, Renoise and its
two modes, the SDL harnesses and the private PipeWire server they stand up, the device guard and why it self-tests,
and retroplug-cli as a consumer repo's whole test runner. None of it is a rule an agent must follow on every session,
and no spec doc owned it, so it moves to a doc of its own, which links spec/06's pnpm scripts table, spec/12's
parameter legs and docs/lsdj.md rather than repeating them. AGENTS.md drops it when it is rewritten; spec/README
indexes the new doc.
MSG
```

---

## Task 2: spec/06 takes AGENTS.md's build and test detail, and its own errors are fixed

spec/06 already holds most of AGENTS.md's test inventory, near word for word, in its `pnpm scripts` table (383 `dup`
rows quote spec/06, 150 of them that table); design section 5 keeps the table as it is but for its errors. What
AGENTS.md says that spec/06 does not, 116 `merge` rows, is merged into the section that owns each topic:

- **One configure:** `deps/efsw` and `deps/catch2` `EXCLUDE_FROM_ALL`; a bullet for the two build scripts' flags
  (incremental, `--clean`, the `-D` pass-through, the inert `--tests` / `BUILD_TESTING`, `build.bat`'s vcvars64 and tool
  overrides, `-j`).
- **CMake targets:** cells extended in place (the root setting Mesen's options before `add_subdirectory(deps/mesen)`,
  mquickjs upstream and unpatched, the `retroplug-<name>-script-pack` target, `processEffect` with no production
  caller, `retroplug_add_test_binary` and `Catch2::Catch2WithMain`).
- **The packages:** the `native` member's entry (and `system`'s target with none), `defineUi`'s five lists, the four
  embedded bundles, `desktop` as the union, the F1 design link, a paragraph for the bundle check
  (`bundle-ui-split.test.ts`), `exports` entries public deliberately.
- **Hosts → test tiers and the baselines:** `test.bat` and why; the test-speed traps after the pool paragraph (one
  slow file, `.test-timings/`, `renderAudio` in milliseconds, removing every system); the parity traps (`BOOT_MS`,
  `rig.rom(src, prefix)`); `[kit-baseline]` exiting 2; the one fixture-free per-channel render.
- **The test tiers:** a four-line index of the binaries that carry the engine's work (`retroplug-engine-graph-test`,
  `-graph-test`, `-dynparams-test`, `-device-mode-test`: what each holds, its tags, its design sections), pointing at
  the table's `test:plugin` row for the case detail; CI decodes no real LSDj MI.OUT.
- **The device build, Sanitizer specifics, Build gotchas:** the MicroQuickJS hash guard and `abort` wrapped; `/EHsc`;
  the three binaries the thread run adds; the `deps/sameboy` patches' reason; Mesen's `RetroPlug:` markers, compile-out
  guards and LTO; the Windows `/W4 /WX` policy (the three rules, `D9025`, VS 2026 on CI); `NativeFileWatcher`'s last
  member.

spec/06's own errors, each checked against the code: the `test:plugin` cell's `[script-pack]` has 51 cases (not 50),
there are four plan blob goldens (not three; `[mutation]` still flips three) and five refused-source checks (not three);
the CLI and the headless host name `PROVIDERS n8 launchpad midi` (only `retroplug-sdl` names `n8 launchpad`); `testing`
has a `native` member (a target, no entry); rule 9's `SHELL_IMPORTS` names no launchpad module and its tracker and dsp
lists are what `check-boundaries.mjs` holds; the `definePackage` list is the full set, with the package declaring each;
the ts tier's skip baseline records `lsdj/ui-views`; the N8 tests row names `retroplug-serial-test`; `mesen` declares
`n8-wire`, not `n8` (twice); and the tiers prose's `lsdj-arduinoboy` test is
`packages/lsdj/ts/test/lsdjArduinoboy.test.ts`.

spec/06's lines 20, 82, 142, 903 and 1049 name AGENTS.md or spec/07; this task leaves them byte-identical, and tasks 5
and 6 repoint them.

**Files:**

- Modify: `spec/06-build-test.md` (One configure, CMake targets, The packages, Hosts → test tiers and its subsections,
  The test tiers, `pnpm scripts`' `test:plugin` cell, Running the Reaper leg in parallel, The dpf.js seam, The device
  build, Sanitizer specifics, Build gotchas).

**Ledger rows (task 2):** the 116 `merge` rows whose `dest` is `spec/06-build-test.md`; the 383 `dup` rows quoting
spec/06 must still hold (none is in text this task corrects: A263 quotes the `[mutation]` half that stays, A409 and S582
the parts of the PROVIDERS sentence that stay).

### Steps

- [ ] **Step 1: run the check and watch it fail**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 2 --max-failures 3
```

Expected: exit 1, task 2's probes missing (task 1's all pass):

```
check.mjs: tasks 1..2 (3 files)
FAIL probes at their destination (keep / merge / open / new): 126 passed, 98 failed
       A040: probe "own README" not in spec/06-build-test.md
       A057: probe "unpatched" not in spec/06-build-test.md
       A062: probe "efsw::FileWatcher" not in spec/06-build-test.md
       ... and 95 more
ok   dup quotes in the doc they name: 395 passed, 0 failed
ok   relative links and anchors in touched markdown files: 256 passed, 0 failed
ok   no em dash in an added line: 192 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 192 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 2 passed, 0 failed
98 failure(s)
```

- [ ] **Step 2: merge into spec/06 and fix its errors**

`spec/06-build-test.md`:

````patch
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 488521f5d..da239ea4b 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -12,12 +12,21 @@ The root [CMakeLists.txt](../CMakeLists.txt) declares the project as
 `add_subdirectory(packages/native native)` ([CMakeLists.txt](../CMakeLists.txt)), after the
 `deps/dpf.js` submodule and the vendored cores (SameBoy, Mesen, efsw, catch2); r8brain
 supplies headers only, so it is not added as a subdirectory, and `deps/enkiTS` is vendored
-under `deps/` with nothing adding or linking it since the native kit compile retired. A full
-build produces the plugin (all formats) + the standalone.
+under `deps/` with nothing adding or linking it since the native kit compile retired. `deps/efsw` (the file watcher)
+and `deps/catch2` (the C++ unit-test framework) are added `EXCLUDE_FROM_ALL`, so only a target that links one builds
+it. A full build produces the plugin (all formats) + the standalone.
 
 - `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
   **zero build-specific special-casing** — a bare `./build.sh` builds every `ALL` target. See
   [AGENTS.md](../AGENTS.md) for their flags.
+- Both run the configure this project needs and build in parallel (`-j$(nproc)`, `sysctl` on macOS). A bare
+  `./build.sh` is incremental: it configures only when `build/CMakeCache.txt` is missing or a `-D` / `--tests` is
+  given, and `--clean` wipes `build/` first. Either script passes any `-D<var>=<value>` straight through to the
+  configure and forces one, so the entry lands on an existing tree. `--tests` / `BUILD_TESTING` is inert: the
+  Catch2 binaries are `EXCLUDE_FROM_ALL` and built by name from the `pnpm test*` scripts. `build.bat` enters
+  vcvars64 and the tool PATH and runs the Ninja + `cl` + vcpkg `x64-windows-static` configure, its tool locations
+  overridable through `VCPKG_ROOT` / `RGBDS_DIR` / `NODE_DIR`. For one target after a configure,
+  `cmake --build build --target <t> -j$(nproc)`: always pass `-j`, since the default is a single job.
 - `pnpm configure` runs `cmake -S . -B build`; `pnpm build` invokes
   [scripts/cmake-build.js](../scripts/cmake-build.js) (no target = a full build).
 - The per-package `.native-build/`, `.ui-build/`, `.test-build/` dirs under
@@ -37,14 +46,14 @@ CMakeLists of [node-sdk](../packages/node-sdk/native/CMakeLists.txt),
 
 | Target | Kind | Produces | In `ALL`? |
 |---|---|---|---|
-| `mesen` (the vendored emulator) | static lib | The consoles' emulator, [deps/mesen](../deps/mesen/CMakeLists.txt): 288 of its 426 translation units, because `MESEN_ENABLE_SNES` / `-PCE` / `-WS` are OFF in the root's cache defaults (ON as upstream builds them; `tools/build-mesen-all-consoles.sh`, `pnpm build:mesen-all`, proves that side). Each option drops a console's `Core/<Dir>/` units and its CPUs' evaluators through one table in that CMakeLists, and is a PUBLIC compile definition the guards around the call sites read. | yes (via `retroplug-mesen`) |
-| `retroplug-system` / `retroplug-sameboy` / `retroplug-mesen` / `retroplug-host-io` | static lib | The F2 packages (`docs/modular-graph-phase-f2-design.md` section 2), one row for the four: `system` is the console-neutral contract every console implements and the engine drives (`SystemBase` and the capabilities, the build request and build function, `HostSystems`, the `SystemNode` template); `sameboy` is the Game Boy core, its config, boot ROMs, button table, ROM gate and `rp.sameboy.gb`; `mesen` is the NES, GBA and SMS/GG cores over the vendored `mesen` library, their configs, the NES debug session, the emulated N8 FIFO and SMS sync roles and `rp.mesen.nes` / `.gba` / `.sms`; `host-io` is `rp.host.midi-in` / `-out` and the engine host-I/O record. Each declares its dependencies in its `package.json` (`mesen` declares `n8`) and each keeps its include paths; the transport primitives the cores and the host share are `system`'s (`packages/system/native/src/transport/`), and `retroplug-backend` keeps `SampleCache`, `FileSync` and lodepng. `src/lsdj/` is test-reference code only: its effect wrappers, compiled into the two test binaries that hold the dither and the buffer-reading nodes to them. | yes (via consumers) |
+| `mesen` (the vendored emulator) | static lib | The consoles' emulator, [deps/mesen](../deps/mesen/CMakeLists.txt): 288 of its 426 translation units, because `MESEN_ENABLE_SNES` / `-PCE` / `-WS` are OFF in the root's cache defaults (ON as upstream builds them; `tools/build-mesen-all-consoles.sh`, `pnpm build:mesen-all`, proves that side). The root sets them before `add_subdirectory(deps/mesen)`, and a `-D` overrides them. Each option drops a console's `Core/<Dir>/` units and its CPUs' evaluators through one table in that CMakeLists, and is a PUBLIC compile definition the guards around the call sites read. | yes (via `retroplug-mesen`) |
+| `retroplug-system` / `retroplug-sameboy` / `retroplug-mesen` / `retroplug-host-io` | static lib | The F2 packages (`docs/modular-graph-phase-f2-design.md` section 2), one row for the four: `system` is the console-neutral contract every console implements and the engine drives (`SystemBase` and the capabilities, the build request and build function, `HostSystems`, the `SystemNode` template); `sameboy` is the Game Boy core, its config, boot ROMs, button table, ROM gate and `rp.sameboy.gb`; `mesen` is the NES, GBA and SMS/GG cores over the vendored `mesen` library, their configs, the NES debug session, the emulated N8 FIFO and SMS sync roles and `rp.mesen.nes` / `.gba` / `.sms`; `host-io` is `rp.host.midi-in` / `-out` and the engine host-I/O record. Each declares its dependencies in its `package.json` (`mesen` declares `n8-wire`, not `n8`) and each keeps its include paths; the transport primitives the cores and the host share are `system`'s (`packages/system/native/src/transport/`), and `retroplug-backend` keeps `SampleCache`, `FileSync` and lodepng. `src/lsdj/` is test-reference code only: its effect wrappers, compiled into the two test binaries that hold the dither and the buffer-reading nodes to them. Nothing in production calls `rp::lsdj::processEffect`, so it is not the kit path to wire a caller back to. | yes (via consumers) |
 | `retroplug-node-sdk` / `retroplug-node-sdk-headers` / `retroplug-reflect-headers` | static lib / interface | The node SDK ([packages/node-sdk](../packages/node-sdk/native/CMakeLists.txt)): canonical JSON, descriptors, the graph document, the JSON to msgpack converter (the library); node-sdk's include root alone, which the executor and the launchpad and N8 tests take; reflect-cpp's headers alone, as `SYSTEM`, for the device-mode objects. | yes (via `retroplug-graph`) |
 | `retroplug-graph-exec` / `retroplug-graph` | static lib | The executor (no heap, no exceptions, builds for the RP2350) and, over it and the node SDK, the registry, planner, resources, `GraphHost` and the host nodes ([packages/graph-runtime](../packages/graph-runtime/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
 | `retroplug-core-nodes` | static lib | The `rp.core` node package ([packages/core](../packages/core/native/CMakeLists.txt)). | yes (via `retroplug-backend`) |
 | `retroplug-device-mode-objects` / `rp-device-check-cases` / `rp-device-check` | object lib / executable / firmware | The `RP_DEVICE` objects `retroplug-device-mode-test` links, built from every `<target>-device` check target; the desktop tool that writes the device check's cases (builds into `build/device-check/cases/`, not `build/bin`), beside `retroplug-export-plan` (`build/export-plan/`, `EXCLUDE_FROM_ALL`, which writes the device check's and the box's plan blobs and which `device:check`, `device:box` and the box's loop.sh need built first); and the RP2350 image `pnpm device:check` builds in its own CMake project (`build-device/`). The first two are `EXCLUDE_FROM_ALL`. | no |
-| `retroplug-device-io-nodes` / `retroplug-devtest-nodes` | static lib | The `rp.device.midi-in` node ([packages/device-io](../packages/device-io)) and the device check's test nodes ([packages/devtest](../packages/devtest), listed by no product); both `device: true`, so they have `-device` object checks too. [packages/bliptoaster](../packages/bliptoaster) and devtest also carry a `retroplug.script` member (E4): a TypeScript pack, no static library, built by `retroplug_device_script_pack` into `build/packages/<name>/script/`. | no |
-| `mqjs_stdlib` / `mqjs` / `retroplug-mqjs-engine` / `retroplug-mqjs-host` / `retroplug-mqjs-host-device` / `retroplug-mqjs-run` / `retroplug-mqjs-run-gc` / `retroplug-mqjs-engine-gc` / `retroplug-mqjs-tables-32` | executable / static lib / object lib / custom target | MicroQuickJS ([deps/mquickjs](../deps/mquickjs), vendored, [cmake/mquickjs.cmake](../cmake/mquickjs.cmake)): its stdlib table generator and the `mqjs` compiler (the only bytecode compiler), the engine wrapped by `rp_mqjs_engine.c` and renamed `rp_mqjs_*` (a post-build check refuses any other external name), the script host ([packages/graph-runtime/native/src/script](../packages/graph-runtime/native/src/script)) in the desktop's build and `RP_DEVICE` mode, the toolchain's runner over RetroPlug's own stdlib, and the same runner over an engine built to collect at every allocation (`retroplug-mqjs-engine-gc`, `EXCLUDE_FROM_ALL`, which `retroplug-device-mode-test` needs), and the 32-bit stdlib tables the box's bytecode is compiled against (`retroplug-mqjs-tables-32`, which `retroplug-export-plan-current` builds). `mqjs_stdlib`, `mqjs` and the runners are `EXCLUDE_FROM_ALL` and built on demand by the script packs' targets. | yes: `retroplug-mqjs-host-device` directly, the engine and host via every product; the tools and the `-gc` pair no |
+| `retroplug-device-io-nodes` / `retroplug-devtest-nodes` | static lib | The `rp.device.midi-in` node ([packages/device-io](../packages/device-io)) and the device check's test nodes ([packages/devtest](../packages/devtest), listed by no product); both `device: true`, so they have `-device` object checks too. [packages/bliptoaster](../packages/bliptoaster) and devtest also carry a `retroplug.script` member (E4): a TypeScript pack, no static library, built at build time by the custom target `retroplug-<name>-script-pack` (`retroplug_device_script_pack`) into `build/packages/<name>/script/`. | no |
+| `mqjs_stdlib` / `mqjs` / `retroplug-mqjs-engine` / `retroplug-mqjs-host` / `retroplug-mqjs-host-device` / `retroplug-mqjs-run` / `retroplug-mqjs-run-gc` / `retroplug-mqjs-engine-gc` / `retroplug-mqjs-tables-32` | executable / static lib / object lib / custom target | MicroQuickJS ([deps/mquickjs](../deps/mquickjs), vendored, [cmake/mquickjs.cmake](../cmake/mquickjs.cmake)), upstream's files unpatched with their LICENSE: its stdlib table generator and the `mqjs` compiler (the only bytecode compiler), the engine wrapped by `rp_mqjs_engine.c` and renamed `rp_mqjs_*` (a post-build check refuses any other external name), the script host ([packages/graph-runtime/native/src/script](../packages/graph-runtime/native/src/script)) in the desktop's build and `RP_DEVICE` mode, the toolchain's runner over RetroPlug's own stdlib, and the same runner over an engine built to collect at every allocation (`retroplug-mqjs-engine-gc`, `EXCLUDE_FROM_ALL`, which `retroplug-device-mode-test` needs), and the 32-bit stdlib tables the box's bytecode is compiled against (`retroplug-mqjs-tables-32`, which `retroplug-export-plan-current` builds). `mqjs_stdlib`, `mqjs` and the runners are `EXCLUDE_FROM_ALL` and built on demand by the script packs' targets. | yes: `retroplug-mqjs-host-device` directly, the engine and host via every product; the tools and the `-gc` pair no |
 | `retroplug-graph-exec-device` / `retroplug-box-usb-cdc` | object lib | The executor and the plan loader compiled with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` in the default build (a non-portable loader line fails there), and `UsbCdcTransport.cpp` over a fake TinyUSB for the desktop test (`EXCLUDE_FROM_ALL`). Both on the MSVC `/W4 /WX` list. | no |
 | `retroplug-dsp-kernels` | static lib | The core-free DSP kernels the `rp.dsp` nodes run (gain and peak, the eight RBJ biquads, dither and quantise, the DMC encode); the C++ standard library alone. | yes (via the node packages; the two DSP test binaries link it directly) |
 | `retroplug-dsp-resample` | static lib | The r8brain resampler kernel, with the heap, pthreads and RTTI r8brain brings; split out so a build with no resampler omits it. It does not link the vendored library, for which the tree declares no target. | yes (via the resample node package) |
@@ -65,8 +74,8 @@ CMakeLists of [node-sdk](../packages/node-sdk/native/CMakeLists.txt),
 | `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
 | `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
 | `retroplug-render-host-test` | executable | A manual smoke of engine jobs on `rp::host::OfflineJobs` ([11-ui-rendering.md](11-ui-rendering.md)): `<job-json>` runs one render (and is `cmp`-identical to `retroplug-cli render`), `--parallel <j>...` runs N concurrently on one pool, `--cancel <j>` cancels mid-render. Not a CI step (`[engine-jobs]` carries that coverage). `EXCLUDE_FROM_ALL`. | no |
-| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-sameboy-test` / `-mesen-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` / `-audio-host-test` | executable | The fourteen Catch2 binaries `pnpm test:plugin` builds and runs. `EXCLUDE_FROM_ALL`; the console packages', the midi package's and the launchpad pack's are declared in their own `native/CMakeLists.txt` (`retroplug-midi-test` holds `MidiStream` over `FakeMidiBackend`, the port filters and the ring; `retroplug-audio-host-test` holds `AudioHost` over `FakeAudioBackend` and the run-override rule). | no |
-| `retroplug-n8-test` / `-n8-hwtest` | executable | Everdrive N8 protocol checks (`packages/n8/native/test/`); `-hwtest` needs the physical console. `EXCLUDE_FROM_ALL`. | no |
+| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-sameboy-test` / `-mesen-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` / `-audio-host-test` | executable | The fourteen Catch2 binaries `pnpm test:plugin` builds and runs. Each is made by `retroplug_add_test_binary` ([cmake/test-binary.cmake](../cmake/test-binary.cmake)) and links `Catch2::Catch2WithMain`. `EXCLUDE_FROM_ALL`; the console packages', the midi package's and the launchpad pack's are declared in their own `native/CMakeLists.txt` (`retroplug-midi-test` holds `MidiStream` over `FakeMidiBackend`, the port filters and the ring; `retroplug-audio-host-test` holds `AudioHost` over `FakeAudioBackend` and the run-override rule). | no |
+| `retroplug-n8-test` / `-serial-test` / `-n8-hwtest` | executable | The Everdrive N8 pack's protocol and serial checks (`packages/n8/native/test/`), the first two `pnpm test:n8`'s; `-hwtest` needs the physical console and links no Catch2 (`NO_CATCH2`). `EXCLUDE_FROM_ALL`. | no |
 
 The plugin's identity is vendor-owned (not the DPF example namespace):
 `DISTRHO_PLUGIN_NAME "RetroPlug"`, URI `https://retroplug.io`, CLAP id
@@ -86,15 +95,21 @@ The build declares `clap`, `vst3`, `vst2`, `au`, and `jack` (the `dpf_add_plugin
 Every package under `packages/` is a workspace package: `@retroplug/<name>`, `private`, type `module`, a
 `package.json` whose `retroplug` member carries the contributions its language builds, and (where it has TypeScript) a
 `ts/` tree with a `tsconfig.json` that extends [tsconfig.base.json](../tsconfig.base.json). The members are `native`
-(a C++ target, [cmake/packages.cmake](../cmake/packages.cmake)'s), `script` (a device script pack),
+(a C++ target and the `extern "C"` entry a product registers it by, [cmake/packages.cmake](../cmake/packages.cmake)'s;
+`system`'s names a target and no entry: a contract, not a node set), `script` (a device script pack),
 `app` (`ts/app.ts`, the module default-exporting `definePackage({ roles, romProviders, trackerIntegrations,
-kitFormats, controllerApps })`) and `protocol` (`ts/protocol.ts`, the module default-exporting the package's
+kitFormats, embeds, projectSections, projectServices, configSections, controllerApps })`: core's contract declares
+`roles`, `romProviders`, `embeds`, `projectSections` and `configSections`, and the declaring packages' augmentations
+the rest, dsp's `kitFormats`, tracker's `trackerIntegrations` and `projectServices`, the launchpad kind's
+`controllerApps`) and `protocol` (`ts/protocol.ts`, the module default-exporting the package's
 `DefinedNode`s, beside each node's `type` and span `label` and the role kinds it places; a package with no node names
 no module and lists only `apps`, the runtime halves of the controller apps whose control-plane halves its `app` module
 contributes). A package may have any combination; a manifest naming no member at all is refused at configure for a
 package CMake registers through `retroplug_package` (the product list's, the SDK's, a device build's extras). `tracker`
-and `testing` carry no `retroplug` member and are added as libraries only: tracker is a module library no product
-lists, and testing is a devDependency of every package with tests.
+carries no `retroplug` member and `testing` only a `native` target with no entry (`retroplug-testing`, the test
+helpers' headers), so both are added as libraries only: tracker is a module library no product lists, and testing is a
+devDependency of every package with tests. The mechanism's design is the F1 design's
+([modular-graph-phase-f1-design.md](../docs/modular-graph-phase-f1-design.md)).
 
 **A product starts a pack's provider only when it names it.** A manifest entry with a `provider` (and, where the pack
 has a facet, an `rpc`) is a device service the pack's desktop target holds, and `retroplug_product(<target> PACKAGES ...
@@ -106,8 +121,9 @@ every product that has a control surface names `n8 launchpad`. The product calls
 that is the pump that writes what the audio thread queued, so a provider that paints is silent without it. The default is
 NONE, because a provider opens ports and runs threads, so the package list alone adds node types and links no port
 library; a duplicate is refused, and an item may not carry both `entry` and `provider` (a product that listed the package
-without naming it would silently lose its node type). Today the SDL standalone, the headless host and the CLI name
-`n8 launchpad`, the plugin names `n8` alone (its MIDI seam caps a message at 4 bytes, so no SysEx fits), and
+without naming it would silently lose its node type). Today `retroplug-sdl` names `n8 launchpad` (it uses
+`MidiStream` itself), the CLI and the headless host name `PROVIDERS n8 launchpad midi` (they mount the stream's facet
+as a provider), the plugin names `n8` alone (its MIDI seam caps a message at 4 bytes, so no SysEx fits), and
 `plugin.cmake` refuses, at configure, a plugin target linked to `retroplug-launchpad-desktop`. A test binary that is a
 product takes the same argument.
 
@@ -122,7 +138,10 @@ carts one of them can drive, kept apart from the app because the planner reads t
 (the menu contributions, imported only by the UI), `ui/generated/views.ts` each package's `retroplug.views` module (the
 views, imported only by the shell's view slots), and `cli/generated/tools.ts` is each package's `retroplug.cli` module
 in product order (the CLI tools, imported only by the dispatcher).
-**Every product but the desktop gets its own tree**, and its own bundles: `generate({ product })` (the CLI's
+`retroplug.ui` names a module default-exporting `defineUi({ rows, menus, trackers, services, actions })`: the menu
+contributions, the UI services and the app actions the key maps dispatch.
+**Every product but the desktop gets its own tree**, and its own bundles (the four embedded bundles: the control
+plane, the React UI, the script nodes and the render worker): `generate({ product })` (the CLI's
 `--product` with `--build-dir`, or `--products-dir` for the typecheck's tree) writes
 `build/products/<name>/packages/retroplug/{src,ui}/generated/`, in the shell's
 shape, with a `tsconfig.json` over it that `pnpm typecheck` runs for every non-desktop list (into
@@ -133,6 +152,7 @@ product but the desktop has a CLI tree. The shell's sources read their `../gener
 imports with the product's file. A product list is validated as the desktop's is, and it must be a SUBSEQUENCE of the
 desktop's, in its relative order; its script-node span labels are its own positions (the plugin numbers
 `rp.lsdj.serial-out` 22, where the desktop's Launchpad session holds 22 and the decoder 23).
+`desktop` is the union of the lists: its 21 packages hold the plugin's 15.
 The desktop's run also writes the typecheck paths: `tsconfig.host-modules.json` (each host module's declaration
 file, which a package with a `retroplug.host` declaration extends) and `tsconfig.paths.json` (the same entries plus the
 shell's own aliases, from `tsconfig.aliases.json`: a child's `paths` REPLACE its parent's, so the shell's program needs
@@ -192,10 +212,11 @@ re-export the ones they share, so a store or menu test keeps one import.
 `cli/` and `ui/`, each against an explicit list of the `@retroplug/*` specifiers it may import, each entry with the
 reason it is a contract rather than a cart. `src/` and `cli/` share `SHELL_IMPORTS`: core's contract modules
 (`systemRoles`, `consoles`, `graphDocument`, `midiRouting`, `romParameters` and the rest), the tracker contract
-(`songCatalog`, `assetCatalog`, `trackerIntegration`, `trackerRender`, `trackerPlayback`, `trackerSongImport`,
-`workingSongBlob`), dsp's kit contract and package-contributions shape, launchpad's registry and enums, and the test
-harness. `ui/` has a list of its own, `UI_IMPORTS`, because the kit and the view contract are the UI's alone (no `src/`
-or `cli/` file may name `@retroplug/ui-react`) and the control plane needs no menu model or contribution type. A cart's
+(`songCatalog`, `assetCatalog`, `trackerIntegration`, `projectService`, `trackerRender`, `workingSongBlob` and its
+package-contributions shape), dsp's kit contract, WAV codec and package-contributions shape, and the test harness; no
+controller module, since the shell names no controller. `ui/` has a list of its own, `UI_IMPORTS`, because the kit
+and the view contract are the UI's alone (no `src/` or `cli/` file may name `@retroplug/ui-react`) and the control
+plane needs no menu model or contribution type. A cart's
 or a console's own module is reached through the generated registries, which is why the four doors that used to
 re-export carts (`src/kit`, `src/tracker/index.ts`, `trackerRegistry`'s integration re-exports and `audioDriver`'s spec
 re-exports) are gone: an importer names the owning package's subpath (`@retroplug/lsdj/kitFormat`, `@retroplug/dsp`,
@@ -251,6 +272,13 @@ string with whitespace in it, and the SDK's declaration file, which it skips. Li
 than a determined author (a computed `["g", "b"].join("")`, a computed table key and a dotless pattern such as
 `/(gb|gbc)$/` are invisible to it).
 
+**A bundle check keeps the surfaces apart.**
+[test/script/bundle-ui-split.test.ts](../packages/retroplug/test/script/bundle-ui-split.test.ts) holds the control
+plane, the render worker, the script nodes, the SDK, the dispatcher and the planner to carrying no UI (no `ui` package
+module, no package's `ts/ui/`, no `ui/generated`), and the UI to carrying no CLI (no package's `ts/cli/`, no
+`core/cliTool.ts`, no `cli/generated`). The generated modules are split so that this can hold, the menus and views
+imported by the UI alone and the tools by the dispatcher alone, and the bundle check keeps that true.
+
 **A package's tests are its own.** A test whose subject is wholly in a package lives in its `ts/test/`, runs on the
 package's `tsconfig` and keeps its fixtures and goldens beside it
 ([packages/lsdj/ts/test/golden](../packages/lsdj/ts/test/golden) and the `.sav` corpus under `ts/test/savs`,
@@ -259,8 +287,9 @@ package's `tsconfig` and keeps its fixtures and goldens beside it
 move. Tests of the stores, the menus, `buildGraph`, the engine, `test-native` and `test-ui` stay with the shell, as do
 the ones that drive its registry (`test/system-settings/*`, `test/risa/detect`, `test/bliptoaster/romDetect`,
 `test/dsp/sms-sync`), whose subjects are the shell's provider dispatch and generated lists. The skip baseline
-([scripts/skip-baseline.json](../packages/retroplug/scripts/skip-baseline.json)) records nothing for the ts tier, so a
-move between it and the shell needs no entry.
+([scripts/skip-baseline.json](../packages/retroplug/scripts/skip-baseline.json)) records one ts-tier file,
+`lsdj/ui-views` (one of its five cases skips), so a test moved between a package and the shell takes its entry to the
+new slug.
 
 **The shared test helpers ([packages/testing/native](../packages/testing/native)).** A package's tests are its own, but
 some things two packages' tests must agree on are neither's: the TAP harness, the script-node rig and the byte helpers
@@ -281,7 +310,7 @@ the allocator library is `EXCLUDE_FROM_ALL` because only test binaries (built by
 1. The directory's name is the package's name: `packages/<name>` is `@retroplug/<name>`.
 2. `package.json`: `private`, `type: "module"`, the `retroplug` member(s) it contributes (`native`, `script`, `app`,
    `consoles`, `protocol`, `embed`, `ui`, `views`, `kinds`, `cli`, `examples`, `host`), an `exports` map of explicit
-   entries with no wildcard, and a `typecheck` script
+   entries with no wildcard (each entry is public deliberately), and a `typecheck` script
    (`tsc -p tsconfig.json`) when it has TypeScript. A **console package** adds a `consoles` module default-exporting its
    console descriptors (a platform id, a node type, the extensions it can be recognised by, its detectors in rank order,
    its setting/construct fields and params, its split modes, its link cable, its real-cart option) and any physical cart
@@ -503,10 +532,10 @@ the allocator library is `EXCLUDE_FROM_ALL` because only test binaries (built by
 three rules the two console packages (four node types between them) follow (F2,
 [modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 2):
 
-- It declares `@retroplug/system` and `@retroplug/node-sdk` beside whatever else it needs (`mesen` declares `n8`, whose
-  reliable-link header its emulated FIFO speaks). Its target is a `retroplug_package_library` linking `retroplug-system`,
-  `retroplug-node-sdk` and its third-party library (`sameboy`, `mesen`), and its include root is `src/`, so a moved
-  source keeps the include text it had.
+- It declares `@retroplug/system` and `@retroplug/node-sdk` beside whatever else it needs (`mesen` declares `n8-wire`,
+  whose reliable-link header its emulated FIFO speaks). Its target is a `retroplug_package_library` linking
+  `retroplug-system`, `retroplug-node-sdk` and its third-party library (`sameboy`, `mesen`), and its include root
+  is `src/`, so a moved source keeps the include text it had.
 - Its node type carries `static constexpr SystemBuildFn kBuild`, the package's own build function (section 3 of
   [modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md)), which the request hands to
   `rp.host.systems/2`; the engine knows no console. It declares `category = "system"`: that is what the engine
@@ -551,10 +580,14 @@ a nonzero exit on failure. Slugs accept slash or dash form, and a prefix runs ev
 end at any dash, so `parity-blocks-tiny` runs the five `parity/blocks-tiny-*` files and `parity/blocks-tiny`
 matches nothing.
 
+On Windows the suites run through `test.bat` (alone it runs all four; `test.bat native ui` a subset): it enters the
+MSVC env the `cmake-build` step needs and pins pnpm through corepack, because pnpm strips `PATH` for the children it
+spawns on Windows. Without it the scripts die on `spawnSync cmake ENOENT`, which looks like a broken tree but is not.
+
 ### Skips are reported, and ratcheted
 
 A case that cannot run — a fixture that is not checked out, a counter only a profile host exposes —
-calls `skip(reason)` from [testing/harness.ts](../packages/testing/ts/harness.ts) and is
+calls `skip(reason)` from [testing/harness.ts](../packages/testing/ts/harness.ts) (the CLI SDK exports it too) and is
 reported as TAP `ok N - name # SKIP reason`, counted separately from a pass. It **throws**, so it
 works from inside a helper or a loop (several files delegate a whole case to a shared helper), and
 `return skip(...)` type-checks anywhere. Call it only from code the test body runs or awaits: any
@@ -591,6 +624,16 @@ is [scripts/lib/testPool.mjs](../packages/retroplug/scripts/lib/testPool.mjs). W
 LONGEST-FIRST from durations the previous run recorded under `packages/retroplug/.test-timings/`, so
 the wall clock is bounded by the slowest FILE rather than by when the pool happened to reach it.
 
+So a slow suite is nearly always one slow file, not the pool: read `.test-timings/native.json` (the directory is
+gitignored), and if its top entry is near the whole wall clock, raising `--jobs` does nothing. Two traps put a file
+there, each of which has cost `test:native` a 10x factor. `audio.renderAudio(ms)` takes MILLISECONDS, not frames
+(`renderAudio(44100)` is 44 seconds of emulated audio). And a system a test builds and never removes is emulated by
+every later test in that file, since `renderAudio` advances every live system in the host process, so a file's cost
+grows with the square of its boots: remove each system once its state has been read,
+`s.project.systems.removeSystem(id)` for one the stores built, `graph.remove(id)` for one a `DirectGraph` placed, or
+`LsdjProbe.closeAll()`, which the probe-based files call at the TOP of each test so an early return still cleans up. A
+file whose realtime factor degrades case by case has this.
+
 The `reaper:*` tiers are parallel too, by a different mechanism — see
 [Running the Reaper leg in parallel](#running-the-reaper-leg-in-parallel).
 
@@ -613,6 +656,10 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   it builds its systems, their roles and the MIDI routing through a `DirectGraph` sharing the session's
   graph, with `setPipeline` and `setMidiRouting`, the document `setPipeline` publishes carrying an LSDj's
   serial-out capture as its Game Boy node's setting, as the stores' does).
+- **Two traps for a new case.** mGB ignores MIDI for its first 1.5 s, so a case sends nothing before `BOOT_MS`
+  ([parity/scenes.ts](../packages/retroplug/test-native/parity/scenes.ts)); and a second instance of one ROM path
+  gets a fresh `-2.sav` battery slot, so two systems that each need a battery each need their own copy
+  (`rig.rom(src, prefix)`).
 - **Rendering goes through `renderBlocks`**, a harness-facet driver call that repeats `PluginDSP::run`'s
   order for every host block: stage MIDI at its frame, drain the command ring, apply transport,
   `processBlock` at 2/4/6/8 lanes (the host shape `multi(outputs)`, which TS's audio driver sets around
@@ -742,7 +789,9 @@ through real offline jobs before any caller moved, and they are still the oracle
 - **Re-record** with `RP_KIT_GOLDEN_UPDATE=1` on either runner (`pnpm test:native kit-baseline`, `... kit-compile`).
   It refuses on any other host and refuses a case whose two compiles disagree (that case depends on something outside
   its input), and the file is written through a sibling temp file and renamed into place, so a failed write leaves the
-  committed golden as it was. Without the variable the files are only ever read.
+  committed golden as it was. Without the variable the files are only ever read. The C++ `[kit-baseline]` suite went
+  with native's compiler: no case carries that tag now, so `retroplug-audio-test "[kit-baseline]"` exits 2 (Catch2's
+  "no tests ran").
 - **A trap.** The baseline is a 4-bit view: a one-ulp change in a dither's arithmetic can move no code across every
   case (D1's Sierra Lite mutation), and `kit-edges` is the same view. That is why the kernels have `[dsp]`'s verbatim
   references at 16 bits and the buffered nodes `[dsp-buffer-nodes]`' own. Treat a kit golden failure as a real change
@@ -774,7 +823,8 @@ section 10) and these files hold what it writes.
   fixture-free cases (2 of the 5 test cases in the file; the rest skip, counted in the skip baseline), and a change to
   the render path is accepted on a run with fixtures and `RP_FAIL_ON_SKIP=1`. CI's coverage of the continuous path is
   smsggdj and the SGB jingle only: there is none of a NES, a split render or a Game Boy's stems until renders can be
-  fed MIDI.
+  fed MIDI. The only fixture-free per-channel render is C++: `[engine-jobs]` renders mGB's stems driven through
+  `rp.mgb.midi` (`EngineJobs.test.cpp`).
 - **`render-chunks`** holds the chunk independence (5 cases): the host-synced smsggdj render, a fixed smsggdj render,
   LSDj stems and risa pins each write the same files at pulls of 100, 37 and 250 ms (`RenderOpts.pullMs`), and an
   auto-detect render writes the same samples and ends within one pull. Forcing engine jobs back to exact blocks fails
@@ -807,7 +857,8 @@ tests that drove the DSP kernel moved when it was deleted; the controller sessio
 [test/script/nodes/](../packages/retroplug/test/script/nodes): `lsdjSerialOut.test.ts` holds MI.OUT's and
 master sync's messages each at its frame in its own block, and master sync's stop 200 ms after its byte across a
 change in the host's rate, read off each block's transport), and so are the pure functions they run (`test/dsp/`,
-where `lsdj-arduinoboy.test.ts` feeds the MI.OUT and master-sync decoders one byte stream cut into 1-frame, FL-style
+and the carts' own, where [lsdjArduinoboy.test.ts](../packages/lsdj/ts/test/lsdjArduinoboy.test.ts) feeds the MI.OUT
+and master-sync decoders one byte stream cut into 1-frame, FL-style
 ragged, FL's split 256-frame, longer ragged and 1024-frame blocks and holds every cut to the same messages at the
 same absolute frames, so the decoders add no dependence on the split, MI.OUT's stream spanning 10155 frames so that
 every cut lands at places of its own; holds master sync's stop 200 ms after its byte across a drop and a rise in the
@@ -828,7 +879,36 @@ of a tempo period, a run's first gap, which LSDj sends early, within 30%), which
 frame 0 fails in the ragged and 1024-frame splits; `lsdj-midiout` holds MI.OUT's messages to frames inside their
 blocks, most past frame 0. `dsp-bench`, in a profile host only, holds an mGB window and a playing LSDj's
 master-sync and MI.OUT windows to zero allocations a block (`RP_BENCH_LSDJ=0` leaves the LSDj windows out;
-[08-profiling.md](08-profiling.md)).
+[08-profiling.md](08-profiling.md)). CI decodes no real LSDj's MI.OUT: the fixture-free `HostIo.test.cpp` cases hold
+the capture's wiring, that mGB's idle line decodes to nothing and a real mGB capture through a master-sync decoder,
+while the real decode is the fixture-gated `lsdj-midiout` files' and parity's, and the clock path's other cases are
+the decoder's unit test, `lsdjArduinoboy.test.ts`.
+
+The C++ unit tier's fourteen binaries are the [`pnpm scripts`](#pnpm-scripts) table's `test:plugin` row, which says what
+each case holds; four carry most of the engine's work, and each has a line here:
+
+- `retroplug-engine-graph-test` (`packages/native/test/engine-graph/*.test.cpp`) holds the engine's graph to the
+  cores it wraps, bit for bit: `[alloc]`, `[video]`, `[input-drops]`, `[devices]`, `[capability]`, `[n8-node]`,
+  `[n8-adapters]` (`N8Adapters.test.cpp`), `[launchpad]`, `[stage]`, `[offline]` (`OfflineJobs.test.cpp`),
+  `[engine-jobs]`, `[script]`, `[packs]`, the hidden `[bench]` and `[offline-bench]`, and a profile build's
+  `[profile]`. The detail is the designs': C2 section 6, C3 7, C4 6, C5 10, F2 8, D2 3 and 11, D3 10, B3a 7.3, C1 9,
+  B3b 6 and 9, and the serial-out move's 8 and 12.
+- `retroplug-graph-test` holds the DSP library and its nodes (`[dsp]` with `[dsp][dpcm]`, `[dsp-nodes]`,
+  `[dsp-resample]`, `[dsp-buffer-nodes]`; the D1 design's sections 6 and 10) beside the graph's foundations.
+- `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`) with
+  its OWN `DistrhoPluginInfo.h` first on the include path, building no plugin format, to drive
+  `PluginExporter::reinitParameters` over a toy plugin.
+- `retroplug-device-mode-test` holds the node SDK built for the RP2350 to the desktop's (the E1 design's section 8,
+  E3's 8, E4's 7): `rp.core` and `rp.dsp`, and in the `[device-check]` cases `rp.n8.nes`,
+  `rp.launchpad.surface-in` / `-out`, `rp.device.midi-in` and `rp.devtest`, compiled with `RP_DEVICE`,
+  `-fno-exceptions` and `-fno-rtti` into an object library that also builds the box's link in device mode
+  (`BoxCartLink`). `[script-pack]` has 51 cases (its fixture packs in `test/device-mode/script-packs`),
+  `[bliptoaster]` holds the policy's heap over 64 full blocks, `[plan]` and `[mutation]` the loader against four
+  plan blob goldens (the last `device-check-script-plan.hex`, all recorded with `RP_PLAN_BLOB_UPDATE=1`),
+  `rp.device.midi-in` has four cases (`[device-io]`), and `[box]` has the `retroplug-export-plan` tool run as a
+  process, which is why the binary depends on it. The device check's digest literal is compared only on
+  `linux-x64-gnu13`. Its configure runs five refused-source checks (`test/device-mode/refused/`: `HeapType`,
+  `PointerSettings`, `UnguardedValidate`, `UnguardedLayout` and, under libstdc++ only, `HeapHeader`), none on MSVC.
 
 The native and UI runners each hand every test a fresh temp dir as `RETROPLUG_USER_CONFIG_DIR`
 (isolated real disk). The native runner also injects the resources dirs so tests can locate ROMs by
@@ -845,7 +925,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test:native` | `retroplug-host` | Real-host suite (C3's `test-native/app-construct-edit.test.ts`: a construct-time edit reaches the core with no Reset, and a project with one unreadable ROM loads the rest; C5's `test-native/capability.test.ts` and `test-native/stage-node-midi.test.ts`). |
 | `test:n8` | `retroplug-n8-test` + `retroplug-serial-test` | The Everdrive N8 pack's protocol and serial checks (built from `packages/n8/native/test/`, not part of `test:plugin`): the one Edio held to its byte-level golden (`edio-golden.json`, 28 cases), every status code, one write per frame, a cancel at each step of `bootRom`, the menu commands and operations over `FakeN8`, the facet's sessions and link methods (`[n8-rpc]`, the port guard both ways), the provider (`[n8-provider]`), and `[cart-jobs]` and `[cart-release]` since C5: the link's pause for a job, the SD card's battery file, an upload's stopped streaming, and the note-offs a leaving cart node's held notes get from the serial thread; the serial test also holds a POSIX port exclusive (`TIOCEXCL`: a second open fails busy). Not a CI step. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | fourteen Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-sameboy-test`, `-mesen-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`, `-device-mode-test`, `-audio-host-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split (the console packages' own cases are `-sameboy-test` / `-mesen-test`, which build their cores directly and link no shell target), the efsw file watcher, MIDI in/out, the control-surface pack's transport, provider and facet (`retroplug-launchpad-test`: a claim's generation and close bytes, the writer thread and its UI queue, `PortClaim` and `MidiPortPool`'s held edge, the port filter, the provider's `wake`, and the facet's twelve methods over a scripted device), the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`, the fixed-capacity settings types `[settings-types]`, the field walk `[fields]` and the enum scan `[enums]` among them), the node SDK built for the device (`retroplug-device-mode-test`, below), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly, and two of its lazily built process tables (the NES game database, the GBA CPU's dispatch tables) taken from two threads at once (`[mesen-statics]`, whose ThreadSanitizer run means something only alone, as each is built once per process); the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), the engine's build seam (F2: a request that names no build function refused, claiming nothing; a fixture console package the engine places, builds, slots, runs and frees with no console named; and a plan whose system slot would stay null refused before it is published, the running plan untouched); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). E1's `retroplug-device-mode-test` ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md) section 8) compiles `rp.core` and `rp.dsp` with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an object library reached through the C ABI and holds 96 cases in eight files: a device-mode node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, for every portable type over a table of settings, allocating nothing in create, process or destroy; the desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses what JSON refuses in the same words, naming the field (two inputs part by choice, pinned: design section 6); the JSON to msgpack converter; and nineteen `[device-check]` cases that hold the image's cases to the manifests' device entries, run the whole check in the image's arena with no allocation, stop it at each of its failure steps, judge its 10-node plan and run the devtest script pack's side (its types, a bench, a 4-node script plan held to the desktop's digest, the clock a node's deadline is read from, the deadline case's loops over a builtin in the box's pack's heap, the policy case's worst blocks); the script host (E4, [modular-graph-phase-e4-design.md](../docs/modular-graph-phase-e4-design.md), `[script-pack]`, 50 cases in `ScriptPack.test.cpp`, `BlipToaster.test.cpp` and `DeviceCheck.test.cpp`): a TypeScript node on MicroQuickJS in both builds of the host, its block API, every fault (a throw, a deadline, a loop over a builtin held to it, the bytes a comparison reads and a callback's returns counted toward the next poll, the calls `call`, `apply` and a bound function hand on polled, a heap running out silencing only its pack, the engine's dropped exceptions, a getter the host reads, the re-entry cap), a warmed block allocating nothing, a plan exported and loaded over the device-mode builds digest-identical to GraphHost and the loader's package memory, with `[bliptoaster]` holding `rp.bliptoaster.midi-policy` to `rp.core.midi-filter` message for message and `[debug-gc]` running the packs on an engine that collects at every allocation; the plan loader (`[plan]`: an exported document loaded into an arena equals GraphHost's run bit for bit, every refusal, every bit of the three blob goldens flipped, three blob goldens), `rp.device.midi-in` (`[device-io]`), the box's host pieces (`[box]`) and `UsbCdcTransport` over a fake TinyUSB (`[usb-cdc]`). Its configure also runs three refused-source checks (a heap type, a pointer in `Settings` and a heap header must each fail in device mode with their own reason; the heap header is skipped under libc++). The engine-graph binary's `[packages]` holds each product's generated registration to a golden list, and an engine's registry to that list plus the built-in script pack's types and nothing else. The configure holds the products too: `retroplug_check_products` (deferred to the root's end) refuses a binary that links a library carrying the engine, computed outward from `retroplug-backend`, and is no product; `retroplug_check_runtime_libraries` beside it refuses a target that is no host of the txiki runtime or the file watcher and reaches `tjs`, `uv_a` or `efsw-static` by any path, naming the chain. |
+| `test:plugin` | fourteen Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-sameboy-test`, `-mesen-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`, `-device-mode-test`, `-audio-host-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split (the console packages' own cases are `-sameboy-test` / `-mesen-test`, which build their cores directly and link no shell target), the efsw file watcher, MIDI in/out, the control-surface pack's transport, provider and facet (`retroplug-launchpad-test`: a claim's generation and close bytes, the writer thread and its UI queue, `PortClaim` and `MidiPortPool`'s held edge, the port filter, the provider's `wake`, and the facet's twelve methods over a scripted device), the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`, the fixed-capacity settings types `[settings-types]`, the field walk `[fields]` and the enum scan `[enums]` among them), the node SDK built for the device (`retroplug-device-mode-test`, below), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every setting reaching its core at the next block through its value guard and all of them at activation, a param at the node's first block, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); each system node builds its core at activation from the resources its settings name (C3, [modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): its ROM and seed bytes as resources, its construct-time fields, each console's bits held against a directly built core, a Master System's staged stem, and a refused plan unwinding the cores and pins it built (the resource pins themselves in `retroplug-graph-test`); a warmed core's whole block allocates and frees nothing, emulation included, and does so with a block's host input staged into it too (C4, [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md)): a Mesen audio ring's fill, drain, wrap, growth and overflow counter, the audio path draining without allocating at every ragged block size, the whole block for a NES (mixed, tapped, and through the Engine), an SMS, a Game Boy (a core each for the mix and the stems) and a GBA (its sections need a sibling fixture ROM, so they `SKIP`, counted, where the resources tree has none) with every counted window carrying a block's host input (the Engine's being what the host staged between blocks) and holding that none of it was refused (`inputDropped()` unchanged, and each non-press door's accepted count checked exactly), the node-level ones proved to have emulated by the frames they captured and the Engine one by its work RAM, a live arm sizing nothing, a re-activation growing the ring, and a node sizing its core to its plan rather than the engine's default (the suite's `CountingAllocator` counts the C library's blocks on request, glibc only and never under a sanitizer, so the arm's case skips where `countsCAllocations()` is false, and the whole-block cases prove the operator-new half where it cannot); a freshly built core's first block costing the boot savestate capture and nothing else (NES, SMS, its two rebuild knobs and a Game Boy built directly and through the backend; a GBA `SKIP`s without its fixture); the ingress queues a block's input lands in bounded and sized when the core is built (a `[input-drops]` case per queue kind, the bounds held to the four declarations `ports.ts` makes (a Game Boy's `serialOut` capacity is native), and `FixedQueue` / `FixedByteRing` / `SortScratch` unit cases in `retroplug-graph-test`, the sort's bound (a full reversed queue, about n * (ceil(log2 n) + 1) moves) counted rather than timed); each console's published frames held to recorded hashes (`[video]`, a frame's buffers being reused) and Mesen's cleared controller state, a notification's listener copy and a send's release of it held directly, and two of its lazily built process tables (the NES game database, the GBA CPU's dispatch tables) taken from two threads at once (`[mesen-statics]`, whose ThreadSanitizer run means something only alone, as each is built once per process); the tap a capability only a NES offers (the engine's tap and build check name no console: it arms through `SystemBase::channelTap()`, and since C5 it makes no console check at all), the engine's build seam (F2: a request that names no build function refused, claiming nothing; a fixture console package the engine places, builds, slots, runs and frees with no console named; and a plan whose system slot would stay null refused before it is published, the running plan untouched); the engine placing, re-arming and freeing what the documents TS builds say, the idle seam both host loops call (`HostServices::reclaimOnIdle`) freeing a replaced plan with blocks running, and `host/IdleReclaimProbe` (which the two host smokes arm) driven to its pass over an audio thread; a core joining and leaving beside a plan that publishes its state every block, and a snapshot claim that shares no memory with a state publish on another thread (the registry driven from two threads, which the thread sanitizer holds); the host MIDI I/O nodes moving what the hosts stage and drain (the staging sorted and taken with no allocation); C5's hardware nodes ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md)): device links over fake devices, a host that owns no device and the state its handles answer, the expansion volume through the interface, acquire and release across a reactivate and a teardown, a handover that never leaves the link with no holder, a warmed block touching both links allocating and freeing nothing, the capability jobs on each emulated console against the system-id RPCs and the ROM file (the ROM title stopping at its first non-printable byte), a node that presents no capability, a battery dump holding what `readSram` answers, a write completing at the block that publishes it (or in the call while no audio thread runs), a node removed or a plan replaced under a job, the engine's 64-job bound, a real device thread's progress, a warmed block with writes in flight, one guard holding the engine's audio-thread ownership bit per case, `rp.n8.nes` receiving from one source what its emulated twin receives, in order and at their frames, its expansion volume, its owed note-offs, the held notes it hands its link when it leaves the plan (an idle reclaim with blocks running freeing the replaced plan with no further publish, and allocating nothing when nothing came back), and its job refusals and abandonment, its being `unique`, the node behind a real Engine driving the production adapters (`N8CartLink`, `N8Host`, `N8Link`) down to a fake serial port, the jobs and a host's teardown with a note held each in a section of its own (`N8Adapters.test.cpp`), the surface's two nodes (the drain at frame 0, the queue's bound, the first-block discard, uniqueness, the link taken once each), a staged message reaching the node the running plan places under its id (in both orders relative to the swap that changes which plan runs), and every golden document planned and run with the device nodes in place, the driven role runs comparing each cart node's pushes byte for byte with what the system it drives fed its own N8 FIFO (one cart per document, the matrix's quiet block comparing empty to empty); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input, while a second device node is the planner's `unique` flag; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. The DSP library and its nodes (D1, [modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md)): `retroplug-graph-test`'s `[dsp]` holds each kernel to the same input split any way (the resampler's chunking, every block split, 1- to 16-bit dither), a `process` allocating and freeing nothing once warmed with the resampler counted from its first chunk, the verbatim references of every dither algorithm and of both padded error diffusions, the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), and the dither seed's golden; `[dsp-nodes]` holds each node's output to its kernel through a real plan (ragged splits, the gain ramp and snap, the biquad's sweep, type change and its state guard when a sweep passes Nyquist, the dither's seed, per-channel noise, non-finite-input restart and per-channel sizing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream proved different, and the three types planning on the Pico); `[dsp-resample]` holds a 22.05 kHz buffer played into a 48 kHz plan and a 48 kHz one into the kit rate, at six splits, to its kernel's stream, a loop committed after the buffer ran out, a rate change, a re-activation past the buffer's end that wraps with `loop` and clamps to silence without it, a block counted from the plan's first block allocating nothing and the Pico `capability` refusal, the node itself also absent at link time; and `retroplug-audio-test`'s `[dither]` holds the TPDF seed (the compiled banks are `test:native`'s, see [the kit baselines](#the-kit-baselines-kit-baseline-kit-compile)); `[dsp-buffer-nodes]` holds the three buffer-reading nodes to the kit arithmetic (normalize whole and split, its peak and a committed gain, a dither's `seedFrom` and a committed seed, the DPCM's bits and its rebuild, a block counted from the plan's first block, the refusals, and the Pico plan), D3's engine jobs are `[engine-jobs]` in the same binary (`test/engine-graph/EngineJobs.test.cpp`: a render as a job against the same render run directly, the worker budget and first-in-first-out order, cancel at boot, mid-render and after the body, an engine destroyed with renders in flight, a flush-to-zero submitter, continuous blocks and the carry's keys, per-channel stems; `[engine-jobs-hff]` is the fixture-gated auto-detect case), and D2's offline pool is `retroplug-engine-graph-test`'s `[offline]` (a document run to completion against the same nodes run directly, the pool's workers and bound, a cancel, a teardown's abandonment, the RPC's samples, both windows and the cap arithmetic, the file input against `SampleCache`, the live plan unchanged beside a job, the guard count and non-finite flag, the FP environment, and a hidden `[offline-bench]`). A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). E1's `retroplug-device-mode-test` ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md) section 8) compiles `rp.core` and `rp.dsp` with `RP_DEVICE`, `-fno-exceptions` and `-fno-rtti` into an object library reached through the C ABI and holds 96 cases in eight files: a device-mode node created from msgpack renders bit for bit what a desktop-mode node renders from the same settings as JSON, for every portable type over a table of settings, allocating nothing in create, process or destroy; the desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses what JSON refuses in the same words, naming the field (two inputs part by choice, pinned: design section 6); the JSON to msgpack converter; and nineteen `[device-check]` cases that hold the image's cases to the manifests' device entries, run the whole check in the image's arena with no allocation, stop it at each of its failure steps, judge its 10-node plan and run the devtest script pack's side (its types, a bench, a 4-node script plan held to the desktop's digest, the clock a node's deadline is read from, the deadline case's loops over a builtin in the box's pack's heap, the policy case's worst blocks); the script host (E4, [modular-graph-phase-e4-design.md](../docs/modular-graph-phase-e4-design.md), `[script-pack]`, 51 cases in `ScriptPack.test.cpp`, `BlipToaster.test.cpp` and `DeviceCheck.test.cpp`): a TypeScript node on MicroQuickJS in both builds of the host, its block API, every fault (a throw, a deadline, a loop over a builtin held to it, the bytes a comparison reads and a callback's returns counted toward the next poll, the calls `call`, `apply` and a bound function hand on polled, a heap running out silencing only its pack, the engine's dropped exceptions, a getter the host reads, the re-entry cap), a warmed block allocating nothing, a plan exported and loaded over the device-mode builds digest-identical to GraphHost and the loader's package memory, with `[bliptoaster]` holding `rp.bliptoaster.midi-policy` to `rp.core.midi-filter` message for message and `[debug-gc]` running the packs on an engine that collects at every allocation; the plan loader (`[plan]`: an exported document loaded into an arena equals GraphHost's run bit for bit, every refusal, every bit of the three blob goldens flipped, four blob goldens), `rp.device.midi-in` (`[device-io]`), the box's host pieces (`[box]`) and `UsbCdcTransport` over a fake TinyUSB (`[usb-cdc]`). Its configure also runs five refused-source checks (a heap type, a pointer in `Settings`, an unguarded `validate`, an unguarded `adjust_layout` and a heap header must each fail in device mode with their own reason; the heap header is skipped under libc++, and none runs under MSVC). The engine-graph binary's `[packages]` holds each product's generated registration to a golden list, and an engine's registry to that list plus the built-in script pack's types and nothing else. The configure holds the products too: `retroplug_check_products` (deferred to the root's end) refuses a binary that links a library carrying the engine, computed outward from `retroplug-backend`, and is no product; `retroplug_check_runtime_libraries` beside it refuses a target that is no host of the txiki runtime or the file watcher and reaches `tjs`, `uv_a` or `efsw-static` by any path, naming the chain. |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`, and fails unless the plugin editor's idle reclaimed the plan the audio thread handed back (`host/IdleReclaimProbe`, the only check that the plugin's frame hook still calls `HostServices::reclaimOnIdle`). Needs Xvfb + jackd; not in CI. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown, the frame loop's idle reclaim among the checks (the same probe); needs no display server or audio server. It runs under a `mktemp -d` config dir of its own and the test-only port filter set to a name no port has (`RP_MIDI_PORT_ALLOW=RP_NO_PORT`), so a saved `controlSurface` is never claimed and no real MIDI port is listed or opened (see `sdl:controller`). It pins the `null` audio backend and puts the tracked `/dev/snd` guard in front of `LD_PRELOAD` itself. In CI. |
 | `sdl:controller` | `retroplug-sdl` | [tools/run-sdl-controller.sh](../tools/run-sdl-controller.sh): the control surface through the REAL standalone against an ALSA fake Pro MK3 ([tools/fixtures/fake-launchpad.c](../tools/fixtures/fake-launchpad.c)), five legs that each fail if the thing they name is broken (nothing selected and the fake's notes reach the shared stream; a restored pair claimed before the audio device opens and before the stream opens a hardware port (and the notes not reaching it); the device lifecycle's order, Programmer mode first and the release last; a real frame-polled scan persisted and claimed mid-session beside a second device; a second process refused the claim). Linux only (an ALSA sequencer), not in CI, needs `libasound2-dev`. It runs under the TEST-ONLY port filter `RP_MIDI_PORT_ALLOW` (a comma-separated list of port-name substrings, honoured by the pack's lister, opener, pool and link and by `MidiStream`, so a filtered process can neither list nor open a port outside it), which it sets to its two fakes' WHOLE port names - `RPFakeLPProMK3:LPProMK3 MIDI 128:0` and `RPTestKeys:RPTestKeys 129:0`, the fake's client name being one no real device has - itself, refusing to run if a caller set the variable at all. Its listing pass, before anything is claimed or probed, fails if any other device appears AND if a listed port that is not a fake is named like a Launchpad, so a real Pro MK3 stops the run even if the list is loosened later. It pins `null` as its audio backend and puts the tracked `/dev/snd` guard ([tools/sdl-audio-guard](../tools/sdl-audio-guard)) in front of `LD_PRELOAD` itself. |
@@ -883,7 +963,8 @@ rather than only a kit's copy: jackd's ALSA backend opens every card's control n
 ALSA default is a real card. Each leg's teardown prints the guard log's counts and fails the run on any refused
 card playback, video or serial node (the control-node refusals are the ALSA probe and are only reported). A bare
 `reaper:*` run
-defaults the tag to the scenario, so single runs are unchanged. The renders are
+defaults the tag to the scenario, so single runs are unchanged, and the same keying is what makes two individual
+`reaper:*` scripts safe to run in parallel. The renders are
 NOT bit-repeatable run to run, even with cores powered on in a fixed state: measured over two
 runs of two re-authored passes each, four and then five of the nine legs rendered different PCM in
 consecutive passes, and which legs changed from run to run (why is not yet known). So the analyzers judge timing and level with
@@ -897,7 +978,7 @@ its Reaper waits forever. Skip the rebuild + fixture regen with `RP_SUITE_NO_BUI
 ## The dpf.js seam
 
 The generic framework (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c, dpf-widgets) lives in the
-nested `deps/dpf.js` submodule, consumed through this seam:
+nested `deps/dpf.js` submodule (documented in its own README), consumed through this seam:
 
 1. **Submodule** — `.gitmodules` registers `deps/dpf.js` (clone with `--recursive`).
 2. **pnpm link** — `"dpf.js": "link:./deps/dpf.js"` in the root [package.json](../package.json) `devDependencies`; `pnpm install` wires it before the first configure. lv_binding_js has its own pnpm workspace (react/react-reconciler/lvgljs-ui) that needs its own `pnpm install` — see [AGENTS.md](../AGENTS.md).
@@ -975,10 +1056,11 @@ Release sizes: core, dsp and the executor were about 42 KB of flash and no stati
 (MicroQuickJS 100 KB, the script host 20.5 KB, the executor and the loader about 25 KB, the devtest script pack 22.5 KB,
 the N8 pack's nodes and Edio about 14 KB, the devtest nodes 7.5 KB, the bliptoaster script pack 7 KB) and about 424 KB
 RAM (data and bss), almost all of it the arena. The image takes a script pack's heap from
-`RP_SCRIPT_HEAP_BYTES_devtest` (45056) in its registration, links the bliptoaster pack's bytecode for its policy case
-(which registers it at 20480, the box's 24576 less 4096), and
+`RP_SCRIPT_HEAP_BYTES_devtest` (45056) in its registration (`RP_SCRIPT_HEAP_BYTES_<package>` sets a pack's heap in
+one image, and is refused unless the build names `RP_DESKTOP_BUILD_DIR`), links the bliptoaster pack's bytecode for
+its policy case (which registers it at 20480, the box's 24576 less 4096), and
 `RP_DESKTOP_BUILD_DIR` (derived from `RP_EXPORT_PLAN_TOOL`) names the desktop build whose generated pack sources it
-embeds.
+embeds. Building the image needs no hardware: CI builds it and never runs it.
 
 `pnpm device:box` ([tools/build-box.sh](../tools/build-box.sh)) builds the Pico box's firmware under the same trap.
 The firmware is `retroplug_product(n8-host DEVICE PACKAGES core device-io n8 bliptoaster TARGETS n8_host)`: C++ on the
@@ -1004,6 +1086,19 @@ declaration with the load's and the first block's times, the proof that the plan
 installs TinyUSB and clones Pico-PIO-USB at 5a37a66 for it. The hardware gate (`loop.sh`, `stress.sh`, `faults.sh`) is
 run by hand on the rig with BlipToaster; it is not CI.
 
+**The MicroQuickJS pin is guarded.** The engine's sources in `deps/mquickjs` are upstream's, unpatched; its fixes are
+wrappers in [rp_mqjs_engine.c](../packages/graph-runtime/native/src/script/rp_mqjs_engine.c), and a configure-time hash
+in [cmake/mquickjs.cmake](../cmake/mquickjs.cmake) guards each upstream span a wrapper replaces or rests on (with
+counted mentions of the names it relies on). A bump of the pin that changes one fails the configure, naming the
+wrapper: the fix is regenerating the wrapper (re-reviewing it against the new text, then recording the new hash), never
+loosening the guard. The wrapper's first fix (the name table's growth under a collection) is guarded by that hash alone,
+since no test reaches it. The engine is compiled at -Os and passes the device images' link trap with `abort` wrapped
+(`LINKER:--wrap=abort` on `retroplug-mqjs-engine`, whose wrapper calls the SDK's panic).
+
+**What the desktop's device objects prove.** The `<target>-device` object libraries and the device-mode test's
+objects are compiled with `-fno-exceptions` on GCC and Clang, but MSVC keeps its `/EHsc` (a second `/EH` flag is
+`D9025`), so only the GCC and Clang builds prove the no-exceptions build.
+
 ### Sanitizer specifics
 
 [run-sanitizer.sh](../tools/run-sanitizer.sh) is **not** a pnpm script — run it
@@ -1018,7 +1113,10 @@ proof that the cores and the built-in script pack's audio runtime are touched on
 blocks. It loads no other pack: a loaded pack's runtimes are made, loaded and freed on the control thread
 while an audio thread runs blocks in the others, which those slugs never do. The Catch2 cases that do,
 `retroplug-engine-graph-test "[packs]"`, are built by name in `build-tsan/` and `build-asan/` and run with the
-same options (the C1 design's section 9). Any finding aborts the host → nonzero exit.
+same options (the C1 design's section 9). Any finding aborts the host → nonzero exit. The thread run also builds
+and runs `retroplug-launchpad-test`, `retroplug-midi-test` and `retroplug-audio-host-test` under ThreadSanitizer after
+the native slugs. `retroplug-device-mode-test`'s `[mutation]` bounds checks bite only under `build-asan`, which is
+run by hand: no CI or `test:plugin` leg builds that binary there.
 
 ## Build gotchas
 
@@ -1027,8 +1125,47 @@ same options (the C1 design's section 9). Any finding aborts the host → nonzer
   per-channel audio tap, `Core/apu.{c,h}`) and then `cmake/patches/sameboy-process-statics.patch` (the power-on random
   seed and its enable flag as relaxed atomics in `Core/random.c`, the camera's noise seed in `Core/camera.c`, and the
   SGB's `fm_sin` table built by a load-time constructor in `Core/sgb.c`, so that parallel Game Boy cores in engine jobs
-  do not race). After any configure `git status` shows five modified files in `deps/sameboy`: expected. A bump that
-  invalidates either patch fails loudly at configure.
+  do not race; it uses GNU `__atomic` builtins, which GCC and Clang have, clang-cl included). After any configure
+  `git status` shows five modified files in `deps/sameboy`: expected. The parent repo tracks only the pinned
+  submodule pointer, so the patched files are committed nowhere: never reset, stash or commit them, or bump the pointer
+  to absorb them. A bump that invalidates either patch fails loudly at configure. The fix is to regenerate the patch.
+- **`deps/mesen` is vendored, and its fixes are marked.** The tree is SourMesen/Mesen2 at `b9fa69d` with no `.git`
+  of its own, so a fix is a plain source change carrying a `RetroPlug:` comment (`//`, or `/* */` where the file is
+  C-style): 46 files carry one (`grep -rl RetroPlug: deps/mesen`). An upstream merge re-applies them over the new
+  snapshot and then runs both modes of `tools/build-mesen-all-consoles.sh`; `--off` is the one that catches a guard
+  the merge dropped, since a Release link folds the arm away.
+- **The consoles RetroPlug never runs are compiled out** (the `mesen` row of [the targets](#cmake-targets)). The call
+  sites into a dropped console are guarded `#if MESEN_ENABLE_*` with a `// RetroPlug:` marker. A `case` whose console
+  is off is gone from its switch, unreachable because a dropped console cannot be constructed; a switch that had a
+  default keeps it, a void one without becomes a no-op for that CPU, and a value one falls to the code after it
+  (`nullptr`, `false`, its own `return` or its throw).
+- **Mesen LTO is opt-in (`-DRETROPLUG_MESEN_LTO=ON`).** It buys about 10% on the NES core (Cortex-A53: xRT 0.83 ->
+  0.92), but it makes every mesen object LTO bitcode, so each binary linking the static library re-runs codegen over
+  its 288 objects (426 with `MESEN_ENABLE_SNES` / `-PCE` / `-WS` all on) at every link: fourteen on Linux with the
+  Node addon (the plugin's three formats, the JACK standalone, the host, the CLI, the Node addon, the SDL binary,
+  `rp-golden-dump` and five test binaries, the render host one of them; macOS adds the AU). So it is OFF by default
+  and every `release.yml` job passes it ON: shipped builds keep the speedup, and the dev loop does not pay for it.
+  Toggling it recompiles mesen (~45 s). A build with it ON that seems to stall on every link is that link-time
+  codegen, not a stale-dependency bug (`[NN%] Built target mesen` prints whether or not it did work); the per-TU `-w`
+  cannot reach link-time codegen, so `deps/mesen/CMakeLists.txt` puts a `-w` on the consumers' link line under LTO.
+- **The Windows build is `/W4 /WX` on RetroPlug's OWN targets.**
+  [packages/native/cmake/warnings.cmake](../packages/native/cmake/warnings.cmake), included at the end of
+  [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt), puts it on the `_rp_werror_targets` list; the
+  vendored subtrees keep their own levels, and `RETROPLUG_WERROR=OFF` opts out of `/WX`. Windows is the one platform
+  whose compiler sees the `#if defined(_WIN32)` half of a platform guard, so it is where a rotted guard shows up. When
+  a warning fires, fix our code; suppress only what we do not own, by number, each `/wd` pinned to the one header that
+  raises it, so a new warning class still fails. A level cannot be lowered with `/W0` or `/w`: a second `/W`-family
+  flag trips command-line warning `D9025`, which cannot itself be suppressed. That is why GNU/Clang's lever,
+  `retroplug_silence_dep_warnings()` in the root [CMakeLists.txt](../CMakeLists.txt) putting `-w` on a dependency's
+  own target, is not Windows': that form trips `D9025` on MSVC. A vendored `.cpp` compiled straight into one of our
+  targets (lodepng, dpf-widgets' `LVGL.cpp`) takes a per-source `/wd` list through `set_source_files_properties`,
+  appended after the target's options so it wins over `/W4`; a whole vendored translation unit with no reason to sit
+  under our level gets a target of its own, kept off `_rp_werror_targets` and given the deps' `-w` on GNU/Clang
+  (`retroplug-r8brain-base`). Both Windows CI jobs run `windows-2025-vs2026` (VS 2026, MSVC 14.51) to match the dev
+  box: plain `windows-2025` ships the same VS 2022 17.14 / MSVC 14.44 as `windows-2022`, and 14.44 and 14.51 disagree
+  about which warnings exist, so a CI-only warning reproduces locally and is never fixed locally-only.
+- **`NativeFileWatcher`'s `efsw::FileWatcher` member is its LAST**, so the efsw thread is joined before the state its
+  callback touches goes; a member declared after it would be destroyed while the thread still runs.
 - **The embedded bundles are derived — never commit them.** `cp-bundle.js`/`cp-bundle_data.c` and
   `ui-bundle.js`/`ui-bundle_data.c` are CMake `BYPRODUCTS` under `build/native/`
   (CMake `BYPRODUCTS` of the `retroplug-cp-bundle` / `-ui-bundle` custom targets), regenerated from
````

- [ ] **Step 3: run the check and watch it pass**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 2
```

Expected: exit 0.

```
check.mjs: tasks 1..2 (3 files)
ok   probes at their destination (keep / merge / open / new): 224 passed, 0 failed
ok   dup quotes in the doc they name: 395 passed, 0 failed
ok   relative links and anchors in touched markdown files: 268 passed, 0 failed
ok   no em dash in an added line: 369 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 369 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 3 passed, 0 failed
all checks pass
```

- [ ] **Step 4: stage the task's file and check its whitespace**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- spec/06-build-test.md
git -C /workspaces/RetroPlug-main/cuttlefish diff --cached --check
```

Expected: no output, exit 0.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(cleanup): spec/06 takes AGENTS.md's build and test detail, and its stale counts are fixed

AGENTS.md is being cut to rules and pointers, and most of its build and test detail already sat in spec/06's pnpm
scripts table. What spec/06 did not say moves into the section that owns each topic: the build scripts' flags, the
targets' cells, the package mechanics AGENTS.md alone stated, the test-speed traps and test.bat, a one-line index of
the four C++ binaries that carry the engine's work, the MicroQuickJS hash guard, and the Build gotchas for the SameBoy
patches, Mesen's markers, compile-out and LTO, the Windows /W4 /WX policy and the file watcher's member order.

The merge corrects what spec/06 repeated wrongly, each checked against the code: [script-pack] has 51 cases, there
are four plan blob goldens and five refused-source checks; the CLI and the headless host name n8 launchpad midi;
testing has a native target; SHELL_IMPORTS names no launchpad module; the definePackage list is the full set; the ts
skip baseline records lsdj/ui-views; the N8 tests include retroplug-serial-test; and mesen declares n8-wire.
MSG
```

---

## Task 3: the package list, device packs, providers and accepted limits into spec/00 and 02, UI gotchas into 03

Three destinations design section 5 assigns:

- **spec/00 "Package layout" is the package list** (design section 5; spec/06 has none), and it lacked six packages:
  one row for the four carts (`packages/lsdj/`, `mgb/`, `risa/`, `smsggdj/`), one for `packages/tracker/` and one for
  `packages/testing/`, each saying what it holds and which products list it; BlipToaster's row gains its app half. Its
  N8 row said only the desktop products and the box list the pack: the `plugin` product lists it too (root
  `package.json`), and BlipToaster likewise.
- **spec/02's "Packages, generated registration and device mode" takes the device packs and the provider mechanics**
  (design section 5, not spec/13): `heapBytes` and its two factors, a script pack registered beside the package's native
  entries, what `defineNode`, the toolchain's scan and the host refuse, the deadline's residual (at most 156 units of
  work on the box), the reserved pair refused at every door, `suspend_hardware_inputs` a real lever on Windows alone,
  and the portable settings' limits (`rp::choice` for `rfl::Literal`; a custom `rfl::Reflector<E>` and an
  `enum_range<E>` override unseen by the device decode). "The DSP targets" gains the r8brain `unref` use-after-free,
  the biquad's rail-to-rail bursts and two dither nodes drawing the same noise (each channel's state 2,560 bytes: the
  ledger's "about 5 KB" was std::mt19937's size, and the kernel has its own, so the row was corrected); "The offline job
  pool" a graph job's fixed transport and no MIDI.
- **The accepted limits spec/07 carried as work** (design section 5) become sentences in the spec/02 sections of the
  subsystem each limits: the release ring back nothing has filled, the system nodes' unknown settings field and two
  enum spellings, a node's silent first block, a stale tap record, the console-only casts the tests keep, the
  `system-<id>` stem, the frame copy the compiled-out consoles keep, a thread's first nested notification, the
  harness's controller seam, and a user graph of script nodes needing a job-side script host. Three were already
  stated there (a closed editor's retired generation, the first block's boot capture, a model switch's scratch growth)
  and gain no text.
- **spec/03 gains `### Framework gotchas`** (design section 2), after "The shell's LVGL primitives": lv_binding_js's
  ignored `insertChildBefore` and the two workarounds (linking the grid's `StableSlot` and the menu's re-key rather than
  repeating them), and the font symbol substitution (the style pipe, `font.hpp`, `lv_conf.h`'s zeroed sizes and
  `LV_FONT_CUSTOM_DECLARE`, the seven files built from DejaVu Sans Mono by `tools/gen-mono-fonts.sh`, the include
  guard, the duplicate-symbol trap, the FontAwesome pass, the `lvgl` target).

Every line of these three files that names `07-remaining-work.md`, spec/07 or AGENTS.md is left byte-identical for
task 6.

**Files:**

- Modify: `spec/00-overview.md` (Package layout), `spec/02-native-host.md` (Device nodes, the system nodes' settings and
  params, `QueuedInvoker`, the Mesen consoles' builds, the script host, the DSP targets, Packages and device mode, the
  offline job pool), `spec/03-ts-layer.md` (a new `### Framework gotchas`).

**Ledger rows (task 3):** the 61 `merge` rows naming these three files (7, 34 and 20), the accepted limits among them
(S152, S154, S172, S173, S180, S181, S182, S213, S217, S222, S228, S232, S251, S323: their `section5` field); the 116
`dup` rows naming them must still hold.

### Steps

- [ ] **Step 1: run the check and watch it fail**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 3 --max-failures 3
```

Expected: exit 1, task 3's probes missing:

```
check.mjs: tasks 1..3 (6 files)
FAIL probes at their destination (keep / merge / open / new): 232 passed, 53 failed
       A015: probe "packages/lsdj/" not in spec/00-overview.md
       A016: probe "packages/mgb/" not in spec/00-overview.md
       A017: probe "packages/risa/" not in spec/00-overview.md
       ... and 50 more
ok   dup quotes in the doc they name: 511 passed, 0 failed
ok   relative links and anchors in touched markdown files: 652 passed, 0 failed
ok   no em dash in an added line: 369 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 369 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 3 passed, 0 failed
53 failure(s)
```

- [ ] **Step 2: merge into spec/00, spec/02 and spec/03**

`spec/00-overview.md`:

````patch
diff --git a/spec/00-overview.md b/spec/00-overview.md
index a17b06bd7..dbf18d32f 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -102,8 +102,11 @@ system routed to one pair by its audio routing.
 | [`packages/graph-runtime/`](../packages/graph-runtime) | The executor (`retroplug-graph-exec`, no heap, no exceptions), the planner, the registry, resources and `GraphHost` (`retroplug-graph`), the plan blob's `exportPlan`, loader and `retroplug-export-plan` tool (E3), and `device-check/`, the RP2350 image that proves the portable nodes link under a heap-and-exception trap and, when flashed, that a plan loads and runs. | Shared by every host. |
 | [`packages/launchpad/`](../packages/launchpad) / [`packages/lsdj-launchpad/`](../packages/lsdj-launchpad) | The Launchpad pack: the protocol, the session and its `rp.controller.session` node, the port scan and profiles, the provider and the facet, the app registry, the `controllerApps` contribution kind, the project's `controller` section, its service and the graph placer that places them - and MI.MAP, the app that drives LSDj through it (the bridge beside the pack, which imports it and `lsdj`, never the reverse). | The desktop products alone: the plugin's `plugin` list carries neither, so a DAW host is handed no control surface. |
 | [`packages/n8-wire/`](../packages/n8-wire) | The header-only wire between a console and the N8: `N8ReliableLink.hpp` and the expansion-volume range, device-ready, so the emulated NES FIFO role and the cart's link share one definition instead of `mesen` depending on `n8`. | `n8` and `mesen`. |
+| [`packages/lsdj/`](../packages/lsdj) / [`packages/mgb/`](../packages/mgb) / [`packages/risa/`](../packages/risa) / [`packages/smsggdj/`](../packages/smsggdj) | The carts, each the cart's half of the app in TypeScript: its roles and ROM provider, its protocol node (`rp.lsdj.sync` and `rp.lsdj.serial-out`, `rp.mgb.midi`, `rp.risa.sync`, `rp.sms.sync`), and, for the trackers, the save and song codecs, the ROM asset layer, the RAM layout reader, the tracker integration, the kit bank format and the menus, views and CLI tools they contribute. `mgb` also bakes its ROM in (`retroplug.embed`). | Every product: the `desktop` and `plugin` lists carry all four. |
+| [`packages/tracker/`](../packages/tracker) | The cart-neutral half of the tracker layer: the song and asset catalog contracts, a cart's `TrackerIntegration`, the playback-position model a control surface codes against, a working song as bytes, the shared asset-override contract and its patcher, and the NES asset formats every tracker cart reads the same way. It has no `retroplug` member: a module library, no node and no contribution of its own. | Imported by the carts, `n8`, `ui`, the two launchpad packages and the shell; listed by no product. |
+| [`packages/testing/`](../packages/testing) | The test harness the packages share: `@retroplug/testing`'s TAP harness, the script-node rig and the byte and deep-equal helpers in TypeScript, and a native half of two targets (`retroplug-testing`, the headers such as `CountingAllocator.hpp` and `LinkRomModel.hpp`; `retroplug-testing-allocator`, the object library that replaces the global `operator new`). | A devDependency of every package with tests; listed by no product. |
 | [`packages/core/`](../packages/core) / [`packages/dsp/`](../packages/dsp) | The `rp.core` mixing nodes; the DSP kernels and the `rp.dsp` nodes (`rp.dsp.resample` beside them, desktop only). Both build for the device. | Shared by every host. |
-| [`packages/n8/`](../packages/n8) / [`packages/device-io/`](../packages/device-io) / [`packages/bliptoaster/`](../packages/bliptoaster) / [`packages/devtest/`](../packages/devtest) | The N8 pack (`rp.n8.nes`, the one Edio, its provider and facet); the box's `rp.device.midi-in`; BlipToaster's MIDI policy, a TypeScript node on MicroQuickJS (E4); the device check's test nodes (listed by no product). | The N8 pack by the desktop products and the box; the policy by the box and the desktop products; the others by the device images and the exporter. |
+| [`packages/n8/`](../packages/n8) / [`packages/device-io/`](../packages/device-io) / [`packages/bliptoaster/`](../packages/bliptoaster) / [`packages/devtest/`](../packages/devtest) | The N8 pack (`rp.n8.nes`, the one Edio, its provider and facet); the box's `rp.device.midi-in`; BlipToaster's MIDI policy, a TypeScript node on MicroQuickJS (E4), and the cart's app half (its ROM detection, asset view and patcher, roles, ROM provider, menus and CLI tool); the device check's test nodes (listed by no product). | The N8 pack by every product and the box; BlipToaster by the box (the policy) and by every desktop product and the plugin; the others by the device images and the exporter. |
 
 The LSDj-persistence path is TS-owned. `retroplug-backend` is the shell's one library: the
 `Engine` (which runs the systems on `retroplug-graph`, through the node types the console and
````

`spec/02-native-host.md`:

````patch
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 8457763b4..db676f8b1 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -469,12 +469,13 @@ into a fixed table of 8 entries (a name up to 31 characters, an interface id up
 the `N8CartLink` in every product that starts providers (the plugin, the SDL standalone, the CLI, where it is never
 connected, and the test host, over a fake cart), the Launchpad pack's provider installs its `ControllerLink` in the
 products that name it and install links (the SDL standalone and the CLI, over real ports; the test host starts it for
-its facet but hands the graph a `HarnessSurfaceLink`, so a test stages a press where no port could); and the Node addon
-and the render host install neither. A name it has no device for answers
-null, and the node runs on a stub of its own that is never connected and that drops and counts everything, so a
-document plans and runs in every host. A node keeps its handle across a reactivate and gives it back once in destroy,
-which runs at reclaim on the control thread while the links are still installed: a host tears its plans down first
-(`Engine::clearGraph`), then uninstalls its links.
+its facet but hands the graph a `HarnessSurfaceLink`, so a test stages a press where no port could, and the harness's
+controller seam, `stageControllerIn` and `renderBlocks`' controller field, works only there: a harness-facet host that
+installs no `HarnessSurfaceLink` refuses it); and the Node addon and the render host install neither. A name it has no
+device for answers null, and the node runs on a stub of its own that is never connected and that drops and counts
+everything, so a document plans and runs in every host. A node keeps its handle across a reactivate and gives it back
+once in destroy, which runs at reclaim on the control thread while the links are still installed: a host tears its plans
+down first (`Engine::clearGraph`), then uninstalls its links.
 
 A capability job ([CapabilityJobs.hpp](../packages/native/src/host/engine/CapabilityJobs.hpp)) is what `submitJob`
 takes: a request for a node of the last published plan, answered with an id or a refusal. A system node's job goes to
@@ -526,20 +527,23 @@ wire could drive; everything else, display knobs, accuracy switches and latencie
   [lsdj](../packages/lsdj/ts/enums.ts), and the app's own [settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)
   for routing and layout), which is its C++ enumerator's name with the first
   letter lowercased (`SameBoyModel::CgbC` is `"cgbC"`, `SameBoyHighpass::RemoveDcOffset` is `"removeDcOffset"`):
-  `knobs::Label<E>`, an `rfl::Literal` generated at compile time from reflect-cpp's enumerator list, reads exactly
-  those strings, lists them in the descriptor's schema and maps one back to its enumerator by position, so native has
-  one spelling of each label and a document and a saved `.rplg` carry the same word. An ordinal, another spelling, or
-  a number out of its range (`lightTemperature` -1 to 1, `apuLatencyMs` 0.25 to 6, NaN included) refuses the settings,
-  and with them the document, never clamped. A field the type does not know is ignored (the settings reader takes
-  extra fields), which is why `BuildGraphGoldens.test.cpp` holds every golden document's system nodes to the fields
-  their types' schemas list. `serialOutCapture` is `off`, `listen` (master sync) or `drive` (MI.OUT). `gain` is dB
-  from -90 to 12, default 0; `expansionVolume` an int param, whole percent of unity from 0 to 200, default 100, as the
-  core takes it. The construct-time fields are the ones the core reads as it boots and has no setter for: NES `sdRoot`
-  (a string), `fifo`, `n8Link` and `channelExportMode` (labels), `fifoDepth`, `fifoBytesPerSecond` and `n8LinkRttUs`
-  (ints in the range the store clamps them to), SMS `enableFm` and `gameGear` (flags). `buildGraph` writes every field
-  the core-config role carries, the node's default where the stored config holds no value the node takes; `gameGear`
-  is the platform's, written `true` for a Game Gear and left out otherwise, and a platform never changes under a live
-  id. So a document never flips a field between absent and present under a system id (the key tells the two apart).
+  `knobs::Label<E>`, an `rfl::Literal` generated at compile time from reflect-cpp's enumerator list, reads exactly those
+  strings, lists them in the descriptor's schema and maps one back to its enumerator by position, so native has one
+  spelling of each label and a document and a saved `.rplg` carry the same word. node.hpp's `enum_param` labels are
+  kebab-case (`"cgb-c"`), though, so the C++ side spells an enum two ways, and a knob moved from a setting to an enum
+  param would change the word a document carries; no enum knob is a param. An ordinal, another spelling, or a number out
+  of its range (`lightTemperature` -1 to 1, `apuLatencyMs` 0.25 to 6, NaN included) refuses the settings, and with them
+  the document, never clamped. A field the type does not know is ignored (the settings reader takes extra fields, as
+  every C++ node's does), so a misspelt knob plans and does nothing (a strict reader would refuse it at plan time); that
+  is why `BuildGraphGoldens.test.cpp` holds every golden document's system nodes to the fields their types' schemas
+  list. `serialOutCapture` is `off`, `listen` (master sync) or `drive` (MI.OUT). `gain` is dB from -90 to 12, default 0;
+  `expansionVolume` an int param, whole percent of unity from 0 to 200, default 100, as the core takes it. The
+  construct-time fields are the ones the core reads as it boots and has no setter for: NES `sdRoot` (a string), `fifo`,
+  `n8Link` and `channelExportMode` (labels), `fifoDepth`, `fifoBytesPerSecond` and `n8LinkRttUs` (ints in the range the
+  store clamps them to), SMS `enableFm` and `gameGear` (flags). `buildGraph` writes every field the core-config role
+  carries, the node's default where the stored config holds no value the node takes; `gameGear` is the platform's,
+  written `true` for a Game Gear and left out otherwise, and a platform never changes under a live id. So a document
+  never flips a field between absent and present under a system id (the key tells the two apart).
 - **Only a new core rebuilds.** The node keys its rebuild on `system`, `romName` and its construct-time fields
   (node.hpp's `rebuild_key`: prepare compares the new settings' key with the key the instance was created with, which
   no commit writes), declares `stateless_prepare`, so every other settings change is a commit that never makes
@@ -573,20 +577,24 @@ wire could drive; everything else, display knobs, accuracy switches and latencie
   block lands at its start. A node reads its params only in a block (the host binds its control streams there,
   landing the document's values in them as it adopts the plan), so they reach the core at the node's first block,
   before its first `prepareForBlock`, not in `activate`: a core its node has taken runs nothing before that. That
-  first value is snapped into the core (whose gain smoother starts at 0 dB), so its first sample plays at the
-  document's level; every later gain change is smoothed, as it always is. A document without a param leaves its
+  first value is snapped into the core (whose gain smoother starts at 0 dB), so its first sample plays at the document's
+  level (a node's first block is silent, so that snap is observable only through a savestate seed taken mid-note, which
+  is the case that pins it); every later gain change is smoothed, as it always is. A document without a param leaves its
   stream where it was: at its default, on a new node.
 - **Allocation.** A warmed core's whole block allocates and frees nothing, emulation included: its audio ring and its
   per-block scratches are sized at activation (the block size arrives in the build request, `maxBlockFrames`, and a
   later host growth re-activates the graph quiescently, where `reserveAudio` moves the sizes with no block in flight),
-  and Mesen's per-frame plumbing reuses its buffers. That holds with a block's host input staged into it too: every
-  ingress queue a press, a MIDI run, a serial byte or a raw message lands in is bounded and sized when the core is
-  built, and a push past its bound is dropped and counted (above), so the counted window may carry what a node applies
-  before the core's `prepareForBlock`. The cases that prove it `SKIP` the GBA, counted, where its sibling fixture ROM
-  is not present, as on CI, and cover it where the resources tree is. What still allocates on the block thread is work
-  that makes something: a Game Boy's model change restarts the core (`GB_alloc` / `GB_init`), which allocates, as it
-  did on the command ring; a NES's region change resets it, which allocates too; the 0.5 s state-snapshot capture
-  allocates inside Mesen's streaming serializer; and the N8 firmware's SD path allocates in its command handlers, in
+  and Mesen's per-frame plumbing reuses its buffers: its notification dispatch iterates a `thread_local` copy, so a
+  console's first frame allocates nothing, and only a thread's first nested notification send allocates (the pool entry
+  for a listener that sends one of its own, `NotificationManager.cpp`). That holds with a block's host input staged into
+  it too: every ingress queue a press, a MIDI run, a serial byte or a raw message lands in is bounded and sized when the
+  core is built, and a push past its bound is dropped and counted (above), so the counted window may carry what a node
+  applies before the core's `prepareForBlock`. The cases that prove it `SKIP` the GBA, counted, where its sibling
+  fixture ROM is not present, as on CI, and cover it where the resources tree is. What still allocates on the block
+  thread is work that makes something: a Game Boy's model change restarts the core (`GB_alloc` / `GB_init`), which
+  allocates, as it did on the command ring; a NES's region change resets it, which allocates too; the 0.5 s
+  state-snapshot capture allocates inside Mesen's streaming serializer; and the N8 firmware's SD path allocates in its
+  command handlers, in
   a bigger directory listing and in an unbounded TX queue ([07-remaining-work.md](07-remaining-work.md)). A Game
   Boy's knobs, the ones it applies in place, a NES's setters but its region, and every param allocate nothing at all.
 - **A freshly built core's first block is the warmed block with one exception.** A Mesen core's controller snapshot, the
@@ -752,9 +760,12 @@ then, when the request names an arm, set that core's tap to its export mode (eit
 core and on every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
 it rides the plan's swap, so no block runs between the two. `armedTap_` holds the tap's address as well as the id, so a
 duplicate id does not restore onto another core; a NES's tap is the core object, so the address differs from the core
-pointer's by a per-class constant and, within one class, tap equality is core equality. A core allocated where the
-armed tap was freed still matches, which TS cannot reach (design section 12). A param's value from `setParam`
-rides the same ring as a command of the GraphHost's own, not a `DspCommand`, into its param's control stream.
+pointer's by a per-class constant and, within one class, tap equality is core equality. A core allocated where the armed
+tap was freed still matches, since the record survives a drop and a reclaim; TS cannot reach that (its ids only go up,
+and every edit that drops a core re-arms), so it is left as it is and the suite pins only the new-id replacement (design
+section 12). No tap read casts to a console class any more; the tests keep their console-only casts to read a config
+field, `linkPeers_` or a ring counter, which are the core's own business. A param's value from `setParam` rides the same
+ring as a command of the GraphHost's own, not a `DspCommand`, into its param's control stream.
 
 **Ownership handoff.** A displaced or removed core leaves with the plan that last ran it, and is
 freed on the control thread when that plan is reclaimed (`reclaimReleased` → `GraphHost::reclaim`), which a
@@ -888,10 +899,12 @@ per console it runs, each reading its own config out of the request: `rp::mesen:
 `MesenGbaSystem` (empty `biosPath` → HLE boot ROM), and `rp::mesen::buildSmsCore` refuses a ROM that is positively
 another console's (the NES magic, the GBA logo or the Game Boy logo) but accepts a headerless one (TS admits homebrew by
 its extension). Each gate checks its own console's signature alone, so a ROM carrying two consoles' magic is accepted by
-whichever node the document names; the TS side picks the node. The NES and GBA cores take their seeds through the config;
-the SMS takes the `romName` it stages its file under. All three run the same tail, `reserveAudio` then `onActivate` then
-`enableStateSnapshot`, and refuse a boot Mesen's `LoadRom` rejected after the magic gate ("Mesen refused the
-ROM"), so NES/GBA/SMS cores publish a tear-free savestate for the registry / Duplicate just like SameBoy.
+whichever node the document names; the TS side picks the node. The NES and GBA cores take their seeds through the
+config; the SMS takes the `romName` it stages its file under, and a node that sends none stages under `system-<id>`,
+which is unique within a Project but not across the several Projects one process holds, so two projects' unnamed system
+1 would share one battery (TS always sends a real `romName`). All three run the same tail, `reserveAudio` then
+`onActivate` then `enableStateSnapshot`, and refuse a boot Mesen's `LoadRom` rejected after the magic gate ("Mesen
+refused the ROM"), so NES/GBA/SMS cores publish a tear-free savestate for the registry / Duplicate just like SameBoy.
 
 Their audio arrives through [`MesenAudioDevice`](../packages/mesen/native/src/system/mesen/MesenAudioDevice.hpp), a
 fixed-capacity ring of interleaved stereo `int16` sized `maxBlockFrames + kMaxPlayBufferFrames` (32768: the most one
@@ -913,7 +926,9 @@ lock-free). The other batch is the per-console compile-out (`deps/mesen/CMakeLis
 [docs/modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md) section 4): `MESEN_ENABLE_SNES` (the
 Super Nintendo and the Game Boy, which Super Game Boy couples), `-PCE` and `-WS` are ON as upstream builds them and OFF
 in the root's cache defaults, each dropping its `Core/<Dir>/` units and its CPUs' evaluators and guarding the call sites
-into them; NES, GBA and SMS are always built, and 288 of the 426 translation units remain.
+into them; NES, GBA and SMS are always built, and 288 of the 426 translation units remain. Built back in, the consoles
+RetroPlug does not run (PCE, SNES, WS) and Mesen's own Game Boy (`GbPpu`) still copy a frame's controller snapshot once
+more per emulated frame, through `RenderedFrame`'s by-value constructor; the NES, GBA and SMS refill a member instead.
 
 ## `ScriptHost` + `ScriptCompiler`: the script-node runtime
 
@@ -980,7 +995,8 @@ out a node's ports, and an audio-side one that holds every instance of the pack'
   within a few frames of the replan; a host with no tick while audio runs (a plugin whose editor is closed, the harness
   hosts) frees it only at those calls, a stop or `drainReleased`
   ([07-remaining-work.md](07-remaining-work.md) section 5). A record the release ring back could not take,
-  leaked rather than freed on the audio thread, keeps its pack for the host's life.
+  leaked rather than freed on the audio thread, keeps its pack for the host's life. It is left as it is, since nothing
+  has filled that ring.
 - **Settings and layout** (control side, the planning thread). `layout_for` and `prepare_settings` run
   the definition's `parseSettings` and its `layout` on the settings alone, so the planner can check a
   whole document before it creates anything. Every script type declares `RP_TYPE_STATELESS_PREPARE`,
@@ -1186,27 +1202,34 @@ the same way, and the dither's `seedFrom` names one too, so a kit-shaped documen
 player's port and the settings ref). A rate change resumes where the output had reached, to within one frame, so a
 fresh r8brain run-in is heard from there rather than a gap. `rp.dsp.normalize` measures its peak over the channels it
 plays (the loudest of each played channel's peak), and `rp.dsp.dpcm` refuses a multi-channel buffer rather than mixing
-one.
+one. `retroplug-r8brain-base`'s statics are why no resampler may outlive its engine: a `rp::dsp::Resampler` destroyed
+after `R8bBase.cpp`'s statics is a use-after-free in r8brain's `unref` (it locks a static mutex and writes into
+cache-owned objects), so a host tears its engines down before exit and the offline pool joins its workers in its own
+teardown (below).
 
 Two of the nodes guard their state, because nothing between a plan and the host output rejects a non-finite sample:
 `rp.dsp.biquad` resets a channel's filter and writes that block as silence when the filter comes out of it non-finite
 (a frequency above the plan rate's Nyquist makes the RBJ poles unstable, and the kernel's output clamp passes a NaN
-straight through), and `rp.dsp.dither` restarts a channel whose state came out non-finite (a non-finite input sample
-would latch its error terms for the rest of the stream). The guards are on state, not on a document's values: a node
-hands its kernel the values it reads, because a clamp would be a different kit byte (the planner and `setParam` clamp
-a document's value into a param's range with no diagnostic, and a wired value is not clamped at all). The param ranges
-cover any sane kit value (gain -100..100, frequency 1 Hz to 96 kHz, Q 0.001 to 1000, -96..96 dB), so a kit's values
-reach the kernels through them whole; the planner cannot see the plan's rate, so a kit compile refuses a frequency at
-or above its source rate's Nyquist itself. Each guard reports into `rp.host.guards/1` when the host offers it: the
-offline job pool does, and counts the reports into its job's completion. Both guards differ from the kernel run
-directly in a way a kit could see, so the compiler refuses the regimes rather than choosing between them: a filter at
-or above its source's Nyquist, a source with a non-finite sample, a pass that records a non-finite sample and a pass a
-guard stepped into each refuse that slot ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md)
-section 11). A dither node's channels each draw their own noise, channel `c` from the setting's seed plus `c` times a
-golden-ratio step, so a stereo patch's dither is decorrelated and channel 0 is the setting's own seed (a mono patch is
-unchanged), unless `seedFrom` names a buffer, whose first channel's hash seeds the stream instead; a kit compile is
-byte-identical because it refuses rather than recovers. No kit byte moves either way: the nodes are held to those
-kernels by their own tests.
+straight through; until the guard fires, the unstable filter writes rail-to-rail output, so a held above-Nyquist setting
+is full-scale bursts between the guard's silent blocks, not silence), and `rp.dsp.dither` restarts a channel whose state
+came out non-finite (a non-finite input sample would latch its error terms for the rest of the stream). The guards are
+on state, not on a document's values: a node hands its kernel the values it reads, because a clamp would be a different
+kit byte (the planner and `setParam` clamp a document's value into a param's range with no diagnostic, and a wired value
+is not clamped at all). The param ranges cover any sane kit value (gain -100..100, frequency 1 Hz to 96 kHz, Q 0.001 to
+1000, -96..96 dB), so a kit's values reach the kernels through them whole; the planner cannot see the plan's rate, so a
+kit compile refuses a frequency at or above its source rate's Nyquist itself. Each guard reports into `rp.host.guards/1`
+when the host offers it: the offline job pool does, and counts the reports into its job's completion. Both guards differ
+from the kernel run directly in a way a kit could see, so the compiler refuses the regimes rather than choosing between
+them: a filter at or above its source's Nyquist, a source with a non-finite sample, a pass that records a non-finite
+sample and a pass a guard stepped into each refuse that slot
+([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md) section 11). A dither node's channels each
+draw their own noise, channel `c` from the setting's seed plus `c` times a golden-ratio step, so a stereo patch's dither
+is decorrelated and channel 0 is the setting's own seed (a mono patch is unchanged), unless `seedFrom` names a buffer,
+whose first channel's hash seeds the stream instead. The step separates one node's channels, not two nodes, so two
+`rp.dsp.dither` nodes with the same settings draw the same noise; each channel's state is 2,560 bytes
+(`sizeof(rp::dsp::Dither)` on a 64-bit build: its own `Mt19937`'s 624 32-bit words and a position, inline). A kit
+compile is byte-identical because it refuses rather than recovers. No kit byte moves either way: the nodes are held to
+those kernels by their own tests.
 
 ### Packages, generated registration and device mode
 
@@ -1232,31 +1255,41 @@ instance owns its `Settings` in the bytes `query_memory` counted, with the resou
 missing one and skips an unknown one; the desktop reads the same blob for a type whose settings are portable, and
 `settingsToMsgpack` converts a node's JSON settings. Settings use `rp::text<N>`, `rp::list<T, N>`, `rp::choice<...>`,
 numbers, bools, numeric `rp::resource_ref`s and enums with a fixed underlying type, no `operator|` and enumerators
-within -256 to 256 (the same limits hold for `enum_param`). A device-capable node source wraps `validate`,
-`adjust_layout` and the helpers only they use in one `#ifndef RP_DEVICE`; the desktop build compiles a
-`<target>-device` object library from every `device: true` target (the `RP_DEVICE` flags, nothing linked) so a missed
-guard, a pointer or non-portable `Settings`, or a `heap` node fails the desktop build. `rp.sdk.settings-json/1` is an
-SDK interface every `node.hpp` type answers, writing an instance's settings as JSON. The device image and the link
-trap are in [06-build-test.md](06-build-test.md).
+within -256 to 256 (the same limits hold for `enum_param`). `rp::choice` stands in for `rfl::Literal`, which a portable
+node does not use, and the device decode reads an enum by its own scan alone, so it does not see a custom
+`rfl::Reflector<E>` and does not honour an `rfl::config::enum_range<E>` override (no node has either). A device-capable
+node source wraps `validate`, `adjust_layout` and the helpers only they use in one `#ifndef RP_DEVICE`; the desktop
+build compiles a `<target>-device` object library from every `device: true` target (the `RP_DEVICE` flags, nothing
+linked) so a missed guard, a pointer or non-portable `Settings`, or a `heap` node fails the desktop build.
+`rp.sdk.settings-json/1` is an SDK interface every `node.hpp` type answers, writing an instance's settings as JSON. The
+device image and the link trap are in [06-build-test.md](06-build-test.md).
 
 **Script packs and package memory (E4).** A package with a `retroplug.script` member is a TypeScript pack whose nodes
 run on MicroQuickJS, in the device-mode script host (`MqjsHost`, `packages/graph-runtime/native/src/script/`), on the
 desktop and on the box alike ([modular-graph-phase-e4-design.md](../docs/modular-graph-phase-e4-design.md));
-desktop-only packs stay on QuickJS (the script host above). The engine is `deps/mquickjs`, vendored, behind
-`rp_mqjs_engine.c`, which wraps the fixes (`rp_mqjs_names.h`, force-included, renames its symbols).
+desktop-only packs stay on QuickJS (the script host above). `heapBytes` is the box's 32-bit context: a 64-bit build
+doubles it (`kWidthFactor`) and a desktop host gives the pack twice that again (`kDesktopHeapFactor`), and
+`retroplug_product` registers the pack beside the package's native entries. The engine is `deps/mquickjs`, vendored,
+behind `rp_mqjs_engine.c`, which wraps the fixes (`rp_mqjs_names.h`, force-included, renames its symbols).
 `build-device-pack.mjs` bundles the entry, lowers it to ES5, scans it for library calls the engine lacks, compiles the
-bytecode the generated `pack64.c` / `pack32.c` carry, and runs that bytecode on `retroplug-mqjs-run` (a transcript and
-a heap check), with the type ids a device registers from without running any JavaScript. A pack's memory is one block
-`[host state][a private copy of its bytecode][the context, heapBytes]`, declared by `rp_package`'s `memory` hook
-(`query`, `query_target`, `setup`, `teardown`, with `ctx`), which the loader sizes once at the target's width, places
-before the nodes and sets up before the first is created; a node asks for its package's block through
-`rp.host.packages/1`. Each node's context loads the pack's device runtime bundle (no settings parse, no descriptor),
-the desktop's control context the whole pack. A node's `process` runs under a deadline read from `rp.host.clock/1` (10
-ms and 250 us a message on the box, 500 ms of thread CPU time on the desktop with no clock), a throw, an overrun or a
-dead heap faults the node (`rp.script.fault/1`) and a dead heap every node of its pack, and a definition's `idle:
-"skip"` keeps a block with nothing to do off the interpreter. The block API gains `forward(port, message)`.
-`rp.host.packages/1`, `rp.script.fault/1`, `rp.script.heap/1`, `rp.host.clock/1` and `rp.host.script-log/1` are
-declared in [rp_script.h](../packages/node-sdk/native/src/graph/rp_script.h) and `rp_node.h`.
+bytecode the generated `pack64.c` / `pack32.c` carry, and runs that bytecode on `retroplug-mqjs-run` (a transcript and a
+heap check), with the type ids a device registers from without running any JavaScript. A pack's nodes use the same
+`defineNode`, which refuses an audio port in a device pack (audio stays C++); the scan refuses `async` and `Promise`,
+and the host a promise returned from a hook. A pack's memory is one block `[host state][a private copy of its
+bytecode][the context, heapBytes]`, declared by `rp_package`'s `memory` hook (`query`, `query_target`, `setup`,
+`teardown`, with `ctx`), which the loader sizes once at the target's width, places before the nodes and sets up before
+the first is created; a node asks for its package's block through `rp.host.packages/1`. Each node's context loads the
+pack's device runtime bundle (no settings parse, no descriptor), the desktop's control context the whole pack. A node's
+`process` runs under a deadline read from `rp.host.clock/1` (10 ms and 250 us a message on the box, 500 ms of thread CPU
+time on the desktop with no clock), a throw, an overrun or a dead heap faults the node (`rp.script.fault/1`) and a dead
+heap every node of its pack, and a definition's `idle: "skip"` keeps a block with nothing to do off the interpreter. The
+deadline is polled finer as it nears, and inside the builtins whose one call can run long, so a node overruns it by at
+most 156 units of work on the box (a call, a branch or an allocation each), plus the rest of the one builtin call or
+straight-line run the count ran out in; a collection inside a unit and an `indexOf` at the heap's size are unmeasured,
+the watchdog the backstop ([MqjsHost.hpp](../packages/graph-runtime/native/src/script/MqjsHost.hpp) states the bound).
+The block API gains `forward(port, message)`. `rp.host.packages/1`, `rp.script.fault/1`, `rp.script.heap/1`,
+`rp.host.clock/1` and `rp.host.script-log/1` are declared in
+[rp_script.h](../packages/node-sdk/native/src/graph/rp_script.h) and `rp_node.h`.
 
 **Providers.** A pack's desktop entry may name a `provider` (a function returning an
 [`rp_provider`](../packages/node-sdk/native/src/graph/rp_provider.h): `create`, `restore`, `rpc_service`, `destroy` and,
@@ -1275,8 +1308,10 @@ both an `entry` and a `provider`), so listing a pack is not starting it, and a p
 where it is named: the render host, the Node addon and most test binaries carry none of it. A provider installs its
 links into the engine's device registry through `rp_device_host` (`host/devices/ProviderHost.hpp`), and the ABI at /2
 carries the calls a provider needs from the product that owns the shared MIDI stream (`reserve_midi`,
-`suspend_hardware_inputs`) and the product's callback for the startup rendezvous (`restored`). The configure check
-refuses a provider or an `rpc` on a `device: true` entry.
+`suspend_hardware_inputs`) and the product's callback for the startup rendezvous (`restored`). `reserve_midi` keeps a
+claimed pair out of the shared `MidiStream`, so a reserved pair is refused at every door and never opened, and
+`suspend_hardware_inputs` is the scan's held edge, a real lever on Windows alone, where a MIDI input is exclusive (every
+platform makes the call). The configure check refuses a provider or an `rpc` on a `device: true` entry.
 
 **A pack's facet is mounted whole, in every product that lists it (decided).** `rp_register_package_rpc` makes one
 `registerRpc` call per provider and the manifest's `rpc` is one header and one function, so the N8 facet's 34 methods
@@ -1351,20 +1386,22 @@ frame count is not finite or is past `size_t`'s range fails the job, naming the
 A job's node registry is the pool's own, filled in its constructor with rp.core, rp.dsp and rp.dsp.resample before any
 worker exists and never changed, so the workers share it read-only; a script, system or host I/O node in a document is
 `unknown-type`, a node fault fails the job, and a guard report does not: the completion carries `guardsFired` and the
-first `guardReason`, each buffer carries `nonFinite`, and a caller decides (a kit compile refuses the pass). The whole
-run is held in the default floating-point environment, with the caller's environment saved before the default is
-pinned and put back after (rounding mode and the flush bits included), so a worker that inherited its submitter's
-flush-to-zero mode still computes what the kernels do. Cancelling completes a queued job unrun and stops a running one
-at its next block boundary, or as it finishes when it is past its last (a job decoding a file finishes that decode
-and stops before its next input; one planning finishes the plan and stops before its first block); tearing the engine
-down abandons every job in flight, joins the workers and discards undrained completions, so no resampler outlives the
-engine (D1's r8brain teardown rule). The TypeScript side
-([`src/offline.ts`](../packages/retroplug/src/offline.ts)) turns submit-and-drain into a promise: the queue joins the
-capability jobs' live set so one pump settles both, the native wait is turned on by `createAudioDriver` for hosts with
-no UI tick, and runs past the 64-job bound wait in a FIFO rather than being refused busy. A port call that throws fails
-the runs it belongs to, never the queue: a submit throw rejects that run, a drain throw rejects every run the queue was
-holding, and the poll always clears its flag. The jobs native still holds after a drain threw are no longer counted by
-the queue, so a fan-out past the bound submitted before they are drained is refused busy rather than held.
+first `guardReason`, each buffer carries `nonFinite`, and a caller decides (a kit compile refuses the pass). A graph job
+takes no MIDI (the pool registers no `rp.host.midi-in`) and its transport stays fixed at 120 bpm, ppq 0, not playing; a
+user graph of script nodes run as a job would need a job-side script host. The whole run is held in the default
+floating-point environment, with the caller's environment saved before the default is pinned and put back after
+(rounding mode and the flush bits included), so a worker that inherited its submitter's flush-to-zero mode still
+computes what the kernels do. Cancelling completes a queued job unrun and stops a running one at its next block
+boundary, or as it finishes when it is past its last (a job decoding a file finishes that decode and stops before its
+next input; one planning finishes the plan and stops before its first block); tearing the engine down abandons every job
+in flight, joins the workers and discards undrained completions, so no resampler outlives the engine (D1's r8brain
+teardown rule). The TypeScript side ([`src/offline.ts`](../packages/retroplug/src/offline.ts)) turns submit-and-drain
+into a promise: the queue joins the capability jobs' live set so one pump settles both, the native wait is turned on by
+`createAudioDriver` for hosts with no UI tick, and runs past the 64-job bound wait in a FIFO rather than being refused
+busy. A port call that throws fails the runs it belongs to, never the queue: a submit throw rejects that run, a drain
+throw rejects every run the queue was holding, and the poll always clears its flag. The jobs native still holds after a
+drain threw are no longer counted by the queue, so a fan-out past the bound submitted before they are drained is refused
+busy rather than held.
 
 The txiki host `TjsHostRuntime` comes from
 [`deps/dpf.js`](../deps/dpf.js), not the shell's own sources, and the shell compiles it into a library of its own
````

`spec/03-ts-layer.md`:

````patch
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index eaa781de6..40c66d6e2 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -604,6 +604,45 @@ app's own flows - the full-window overlays (`overlay.ts` + the close guard, proj
 hooks), the unsaved-changes rows, the interactive save, and the background watchers (song, SRAM auto-save, N8 SD job,
 Launchpad scan).
 
+### Framework gotchas
+
+Two behaviours of lv_binding_js (in the `deps/dpf.js` submodule) shape the UI code, and neither is visible from React.
+
+**`insertChildBefore` always appends.** React reorders children through `insertChildBefore`, and the binding's
+[comp.cpp](../deps/dpf.js/deps/lv_binding_js/src/render/native/core/basic/comp.cpp) ignores `beforeChild`, so a child
+swapped at a stable React position (a tile becoming a menu, a type swap) or a mid-list insert lands at the END of the
+LVGL child list, not at its source position. `appendChild` lands correctly only where the parent has ≤1 child, which is
+what both workarounds use: a stable per-id wrapper whose position never changes, with a single swappable child
+(`StableSlot`, used by the grid and the tile views, [above](#the-grid---screensgrid)), or re-keying the parent on the
+visible set so every child mounts fresh in JSX order ([the menu](#the-menu---screensmenu)). A tile, row or menu that
+renders in a confusingly wrong slot is almost certainly this.
+
+**There is no font-family: the UI face is monospaced by SYMBOL SUBSTITUTION.** A component picks a font by pixel size
+and nothing else: the TS style pipe turns `font-size` into an index
+([text.ts](../deps/dpf.js/deps/lv_binding_js/src/render/react/core/style/pipe/text.ts)), and native maps that index onto
+an `lv_font_montserrat_*` pointer
+([font.hpp](../deps/dpf.js/deps/lv_binding_js/src/render/native/core/style/font/font.hpp)). That table is in a submodule
+and names the symbols literally, so the face is changed by changing what the names POINT AT, not by asking for another
+one. Every `LV_FONT_MONTSERRAT_*` in [lv_conf.h](../packages/native/src/lv_conf.h) is therefore 0, so LVGL compiles none
+of its own; `LV_FONT_CUSTOM_DECLARE` declares the seven sizes the table uses; and
+[`packages/native/src/fonts/lv_font_montserrat_{12,14,16,18,22,24,32}.c`](../packages/native/src/fonts) supply those
+symbols, built from DejaVu Sans Mono. Regenerate them with [tools/gen-mono-fonts.sh](../tools/gen-mono-fonts.sh), which
+also renames each file's uppercase include guard: lv_font_conv derives it from the filename, so it would otherwise be
+the very `LV_FONT_MONTSERRAT_*` macro now set to 0, and the file would preprocess away to a missing symbol. So:
+
+- Turning an `LV_FONT_MONTSERRAT_*` back to 1 is a duplicate-symbol link error, not a font change. Switch faces by
+  regenerating, never in `lv_conf.h`.
+- The generation keeps its FontAwesome pass: the `LV_SYMBOL_*` glyphs live in these same font files (the binding's
+  [dropdownlist.cpp](../deps/dpf.js/deps/lv_binding_js/src/render/native/components/dropdownlist/dropdownlist.cpp) draws
+  them), and without that `--font` they become placeholder boxes.
+- The `.c` files are committed, as LVGL's own are, so a build needs no npm/network. They are added to the `lvgl`
+  target in the root [CMakeLists.txt](../CMakeLists.txt), the archive the objects they replace came from, so nothing
+  depends on static-link order.
+
+The face is monospaced, but layout must not depend on that: the action-cycler row aligns its arrows with real widget
+geometry rather than space-padding ([the menu](#the-menu---screensmenu)), and
+[menu-action-cycler.test.ts](../packages/retroplug/test-ui/menu-action-cycler.test.ts) pins it.
+
 ### Game input - [`ui/input/useGameInput.ts`](../packages/retroplug/ui/input/useGameInput.ts)
 
 Resolves each `"key"` bus code to a Game Boy button via `buildKeyToButton(bindings.keyboard)`
````

- [ ] **Step 3: run the check and watch it pass**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 3
```

Expected: exit 0.

```
check.mjs: tasks 1..3 (6 files)
ok   probes at their destination (keep / merge / open / new): 285 passed, 0 failed
ok   dup quotes in the doc they name: 511 passed, 0 failed
ok   relative links and anchors in touched markdown files: 671 passed, 0 failed
ok   no em dash in an added line: 541 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 541 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 6 passed, 0 failed
all checks pass
```

- [ ] **Step 4: stage the task's files and check their whitespace**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- spec/00-overview.md spec/02-native-host.md spec/03-ts-layer.md
git -C /workspaces/RetroPlug-main/cuttlefish diff --cached --check
```

Expected: no output, exit 0.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(cleanup): the package list, device packs, providers, accepted limits and UI gotchas into spec/00, 02 and 03

spec/00's Package layout is the package list, and it lacked the four carts, tracker and testing; they get rows, and
the N8 and BlipToaster rows now say the plugin product lists them too. spec/02's packages section takes what AGENTS.md
said of device packs on MicroQuickJS and of the provider ABI, and the DSP and offline-pool sections what spec/07's
phase history said of their kernels. The accepted limits spec/07 carried as work (the release ring back, the unknown
settings field, the stale tap record and the rest) are behaviour we accept, so each is now a sentence in the spec/02
section of the subsystem it limits. spec/03 gains a Framework gotchas subsection for lv_binding_js's ignored
insertChildBefore and the UI fonts' symbol substitution, which AGENTS.md will keep as one line each.

The dither's per-channel state is 2,560 bytes (its own Mt19937), not the "about 5 KB" of std::mt19937.
MSG
```

---

## Task 4: the N8, Launchpad and SDL facts and the remaining accepted limits into spec/04, 05, 11, 12 and 13

The rest of the merges, each into the entry that already describes its subsystem:

- **spec/13**, the subsystem index (35 rows). The N8 entry gains the pack's two targets and what each holds, the Edio
  written once with no exceptions, the operations (`bootRom`, `readFile`, `writeFile`), the box's checks booting
  BlipToaster and never `n8-midi.nes`, the E2 design link, and the accepted limits of a real cart (its battery only at
  the menu, a dump's minute, a running game refusing a job, `rom.header` after a connect, a cart job pausing streaming,
  the CLI's raw serial path, the desktop's N8 graphs not using the policy node). The Launchpad entry gains the provider,
  the facet's client, the writer thread and UI queue, the bridge's dependencies, the scenarios as the proof, the
  `controlSurface` fields, and the two limits (one controller link per process; the plugin has no control surface,
  since its host MIDI OUT caps a message at 4 bytes: MIDI in takes any size). The controller role gains the
  `controllerApps` contribution's control-plane half. The `retroplug-sdl` entry gains its release and CI facts, the
  corrected startup rendezvous (500 ms once the audio settings have applied, 2000 ms, `kAudioBoundMs`, until they have:
  AGENTS.md said 500 ms alone), the run flags, and the F5b design link. The N8 entry's emulated FIFO path is fixed
  (`NesEverdriveFifo.hpp` is the mesen package's).
- **spec/05**: the migrations map is not "in migrate.ts": each root's map sits beside its schema constant
  (`PROJECT_MIGRATIONS` in `projectConfig.ts`, `USER_CONFIG_MIGRATIONS` in `userConfigSerialization.ts`,
  `BINDINGS_MIGRATIONS` / `RECENT_MIGRATIONS` beside theirs), `migrate.ts` holding the machinery. A pre-F5b plugin's
  loss of a newer standalone's `audio` key joins the config-section rules. Its `PluginDSP.cpp:240` links are `:250`
  (`initState` moved).
- **spec/04**: `routeBlock` as the routing node's reference twin; SameBoy's serial byte timing moving with the block
  split (an accepted limit); a stored value the role schema would repair taking the node's default (an accepted limit).
- **spec/11**: the snapshot prefix's two halves that must move together, who sets `inputsMustExist`, the `render`
  prefix run, and the render-host smoke's build commands.
- **spec/12**: `tools/run-reaper-params.sh` and running it per format (design section 5 keeps the params checks here,
  not in spec/14), and the sync-latency tripwire (a control surface joining the plugin's list must make the report ask
  the placed mode). Section 5's "one raw step in migrate.ts" is the same error as spec/05's, and its `PluginDSP.cpp`
  line links had drifted (`updateParameterMap` is 495-524, `updateLatency` 483-487, the CC emission 336-356, the
  constructor 135).

Every line of these files that names `07-remaining-work.md` is left byte-identical for task 6.

**Files:**

- Modify: `spec/04-roles-protocol-nodes.md`, `spec/05-data-persistence.md`, `spec/11-ui-rendering.md`,
  `spec/12-dynamic-parameters.md`, `spec/13-subsystems.md`.

**Ledger rows (task 4):** the 48 `merge` rows naming these five files (35 of them spec/13's), the accepted limits among
them (S183 and S504 to spec/04; S243, S244, S245, S246, S248, S252, S253, S255, S498, S603 and S610 to spec/13); the
92 `dup` rows naming them must still hold (A618, A619, A622 and A623 quote the spec/05 paragraph this task corrects,
in the parts it keeps).

### Steps

- [ ] **Step 1: run the check and watch it fail**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 4 --max-failures 3
```

Expected: exit 1, task 4's probes missing:

```
check.mjs: tasks 1..4 (11 files)
FAIL probes at their destination (keep / merge / open / new): 289 passed, 44 failed
       A375: probe "`retroplug-n8`" not in spec/13-subsystems.md
       A377: probe "with no exceptions" not in spec/13-subsystems.md
       A378: probe "`bootRom`" not in spec/13-subsystems.md
       ... and 41 more
ok   dup quotes in the doc they name: 603 passed, 0 failed
ok   relative links and anchors in touched markdown files: 914 passed, 0 failed
ok   no em dash in an added line: 541 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 541 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 6 passed, 0 failed
44 failure(s)
```

- [ ] **Step 2: merge into spec/04, 05, 11, 12 and 13**

`spec/04-roles-protocol-nodes.md`:

````patch
diff --git a/spec/04-roles-protocol-nodes.md b/spec/04-roles-protocol-nodes.md
index b8605e933..712fd5832 100644
--- a/spec/04-roles-protocol-nodes.md
+++ b/spec/04-roles-protocol-nodes.md
@@ -295,7 +295,8 @@ routing node's outputs follow the systems); a system removed and added again get
   and `midiChannelToInstance`: the channel, each modulo the system count), by `routeTarget`'s arithmetic.
   `midiChannelToInstance` hands its target a copy with the channel rewritten to 0, from a buffer made once.
   System order is authoritative: the positional modes route by a system's index in `systems`. Its layout
-  refuses two systems under one id.
+  refuses two systems under one id. `routeBlock`, the pure twin in `midiRouting.ts`, stays as the reference the
+  node's test holds every mode to ([midi-route.test.ts](../packages/core/ts/test/midi-route.test.ts)).
 - **`rp.mgb.midi`** sends each routed message's bytes as one `serialIn` message at its frame, which is all
   mGB listens for (the system node applies a message's bytes in order at its frame).
 - **`rp.n8.midi-to-fifo`** sends each routed message whole to the NES core's `midi` input, which feeds the
@@ -334,7 +335,10 @@ routing node's outputs follow the systems); a system removed and added again get
   from one of three arrays made with the instance, one a message length. MI.OUT's bit accumulator and pending
   command are kept on the instance across mode changes; master sync's state is dropped outside that mode, so coming
   back opens a fresh run. Its settings are the role's config through the schema `rp.lsdj.sync` takes too
-  ([nodes/lsdjSyncSettings.ts](../packages/lsdj/ts/lsdjSyncSettings.ts)); it reads only `mode`.
+  ([nodes/lsdjSyncSettings.ts](../packages/lsdj/ts/lsdjSyncSettings.ts)); it reads only `mode`. A message's frame is
+  the one SameBoy completes its byte at, and that serial byte timing moves with the block split (it predates the
+  decoder): a SameBoy matter, its own change if wanted
+  ([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) section 11).
 - **`rp.controller.session`** runs a `ControllerSession` ([session.ts](../packages/launchpad/ts/session.ts))
   for the app its settings name, which the generated script bundle registers from the manifests'
   `protocol.kinds.controllerApps` entries ([apps/index.ts](../packages/launchpad/ts/apps/index.ts), the apps' runtime,
@@ -457,7 +461,10 @@ so a ROM swapped back brings its settings back.
 (so the node's settings, the capture its Game Boy's node runs, the menus and a save all see the parsed one), and a
 repair is logged once with the ROM and each repaired value. A config the schema refuses outright falls back to
 what the ROM's providers suggest for the role, then to the schema's defaults. The parse drops a key the schema does
-not read, as an edit already did, and names it in the log.
+not read, as an edit already did, and names it in the log. A system's core config is the exception: it stays as
+written, and a stored value its node would refuse reaches no document, even one the role schema would repair:
+`systemKnobs` leaves it out, so the node's default applies rather than the repaired value (there is no blob to carry
+it), and `adopt` names it in the log (`droppedKnobs`).
 
 The rest of the document follows from those nodes:
 
````

`spec/05-data-persistence.md`:

````patch
diff --git a/spec/05-data-persistence.md b/spec/05-data-persistence.md
index da6556f7c..15d47384b 100644
--- a/spec/05-data-persistence.md
+++ b/spec/05-data-persistence.md
@@ -102,7 +102,7 @@ framing happen entirely in TypeScript.
 | `RETROPLUG_AUTOLOAD_PROJECT=path` | `__rp_loadProjectPath(path)` | headless seed (reaper `-renderproject`): load a project FILE from disk via `load()` — a thin `.rplg` or an export `.rplg.zip` |
 
 State is a single `"project"` key, host-readable + host-writable
-([PluginDSP.cpp:240](../packages/native/plugin/PluginDSP.cpp#L240)).
+([PluginDSP.cpp:250](../packages/native/plugin/PluginDSP.cpp#L250)).
 The C++ boundary stays **string-only**: base64 is done in JS
 ([pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts))
 because a DPF state chunk is NUL-terminated UTF-8 while the export archive it
@@ -196,7 +196,12 @@ JSON**, backed by two mechanisms:
 2. **Migrations (raw-JSON, latest-schema-only).** Keep only the LATEST zod schema per
    root — never a per-version copy. On a breaking (non-additive) change, bump the root's
    version constant and add ONE raw `(obj) => obj` step to that root's migrations map,
-   keyed by from-version, in [migrate.ts](../packages/retroplug/src/migrate.ts). On load
+   keyed by from-version, which sits beside the root's schema constant: `PROJECT_MIGRATIONS` in
+   [projectConfig.ts](../packages/retroplug/src/projectConfig.ts), `USER_CONFIG_MIGRATIONS` in
+   [userConfigSerialization.ts](../packages/retroplug/src/userConfigSerialization.ts), `BINDINGS_MIGRATIONS` and
+   `RECENT_MIGRATIONS` in [bindingSerialization.ts](../packages/retroplug/src/bindingSerialization.ts) and
+   [recentSerialization.ts](../packages/retroplug/src/recentSerialization.ts)
+   ([migrate.ts](../packages/retroplug/src/migrate.ts) holds the machinery they share). On load
    `migrateRaw` applies the ordered chain from the file's stamp up to current, on the RAW
    object, **before** the (latest) zod schema validates. Steps must be idempotent-safe (an
    unstamped file floors to current). The project **v1→v2** (`PROJECT_MIGRATIONS[1]`)
@@ -219,7 +224,9 @@ them. (1) **The shell's keys are the shell's**: `CORE_CONFIG_KEYS` is derived fr
 `schemaVersion`, and the generated registry refuses a contributor of one. (2) **An absent key is not invented**: a
 section this product contributes but the file has never carried reads its contributor's defaults, and a write of the
 file leaves it absent until `setSection` writes it - which is what lets a pre-section file round-trip byte for byte and
-the plugin (which lists no audio package) write a standalone's `audio` block back instead of erasing it. (3) **A block
+the plugin (which lists no audio package) write a standalone's `audio` block back instead of erasing it (a
+pre-F5b PLUGIN build, whose root schema is strict, still drops a newer standalone's `audio` key at its next write of
+the file: unavoidable, and the cost of releasing that plugin beside a newer standalone). (3) **A block
 this session did not set is written back VERBATIM**: the store keeps each block as the file carried it beside its parsed
 value, and only a key `setSection` set this session is serialized from its parsed form, so an additive field a newer
 build wrote survives this one. A section write is computed inside the store's apply callback over the file AS IT IS ON
@@ -386,7 +393,8 @@ plane; the file-watch side ("watcher = C++, policy = TS") is covered in
 - [projectStore.ts](../packages/retroplug/src/projectStore.ts) — save/export/load, the missing-scan + relink lifecycle, `exportBytes`/`loadBytes`.
 - [projectBinaries.ts](../packages/retroplug/src/projectBinaries.ts) / [projectPaths.ts](../packages/retroplug/src/projectPaths.ts) — blob-key contract; path rebasing.
 - [pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts) — the `__rp_saveProjectB64`/`__rp_loadProjectB64`/`__rp_loadProjectPath` surface + base64.
-- [PluginDSP.cpp:240](../packages/native/plugin/PluginDSP.cpp#L240) - DPF `initState`/`getState`/`setState` + `RETROPLUG_AUTOLOAD_PROJECT`.
+- [PluginDSP.cpp:250](../packages/native/plugin/PluginDSP.cpp#L250) - DPF `initState`/`getState`/`setState` +
+  `RETROPLUG_AUTOLOAD_PROJECT`.
 - [userConfig.ts](../packages/retroplug/src/userConfig.ts) / [userConfigSerialization.ts](../packages/retroplug/src/userConfigSerialization.ts), [recentSerialization.ts](../packages/retroplug/src/recentSerialization.ts), [bindingSerialization.ts](../packages/retroplug/src/bindingSerialization.ts) — the config models + on-disk codecs.
 - [configSchema.ts](../packages/retroplug/src/configSchema.ts) — the clamp/default zod builders (TS forward-tolerance).
 - [migrate.ts](../packages/retroplug/src/migrate.ts) + [projectConfig.ts](../packages/retroplug/src/projectConfig.ts) — the raw-JSON migration framework + the version constants / `checkVersion` / `parseProjectVersion` (TS is the single source of truth; native does no version check).
````

`spec/11-ui-rendering.md`:

````patch
diff --git a/spec/11-ui-rendering.md b/spec/11-ui-rendering.md
index b67bb4732..6d82599f6 100644
--- a/spec/11-ui-rendering.md
+++ b/spec/11-ui-rendering.md
@@ -109,21 +109,23 @@ SameBoy's `Core/random.c`, `camera.c` and `sgb.c` statics are patched
   plane's, so it outlives the editor and a completion settles through the same queue that submitted the job (nothing
   drains with the editor closed; the WAV is on disk either way, and the badge catches up at the next pump).
 - **`ui/host/render.ts`** - `startSystemRender` snapshots the live `readSram`/`readState` to files of the job's own
-  under `configDir` (`.render-src-<key>.sav` / `.ss0`, `RENDER_SNAPSHOT_PREFIX`) and calls `renders.start` with their
-  list as the submission's own (`EngineRunOptions.discardFiles`): the engine's pool deletes them however the job ends
-  (on the job's worker before the completion is queued, or with the job itself when it is dropped unrun), and
-  `onSettled` deletes them again on whichever road it took: a job the queue held past the 64-job bound and cancelled
-  before it was submitted, a submission the RPC threw on, a spec function that threw, and every job a drain that threw
-  failed, which native may still hold (a second removal is a no-op). Where that last road meets a job native still has
-  queued, the boot refuses (`RenderOpts.inputsMustExist`); a job already past that check can lose the file before its
-  boot reads it, a window of a handful of calls, and then boots the cart's blank start. What is left is an instance
-  that ends with a render held in TS, and a crash, which the startup sweep takes (`src/renderSnapshotSweep.ts`: a
-  snapshot more than a day old). The worker reads the snapshots only when it boots the render, which can be minutes
-  later behind the worker budget, so a shared name would boot a later edit's (or another instance's) state; and the
-  spec says they must exist when it boots (`RenderOpts.inputsMustExist`), so one the sweep took from under a live job
-  refuses rather than booting blank, except in the window above. A snapshot that cannot be written refuses the render,
-  listed as an error, rather than boot the sibling `.sav` on disk. `pickActiveRenderJob` selects a tile's badge job and
-  `renderBadgeLabel` writes its text.
+  under `configDir` (`.render-src-<key>.sav` / `.ss0`, `RENDER_SNAPSHOT_PREFIX`, the twin of native's
+  `kRenderSnapshotPrefix` in `OfflineJobs.hpp`, which keeps a listed path only under that prefix, so the two move
+  together) and calls `renders.start` with their list as the submission's own (`EngineRunOptions.discardFiles`): the
+  engine's pool deletes them however the job ends (on the job's worker before the completion is queued, or with the job
+  itself when it is dropped unrun), and `onSettled` deletes them again on whichever road it took: a job the queue held
+  past the 64-job bound and cancelled before it was submitted, a submission the RPC threw on, a spec function that
+  threw, and every job a drain that threw failed, which native may still hold (a second removal is a no-op). Where that
+  last road meets a job native still has queued, the boot refuses (`RenderOpts.inputsMustExist`); a job already past
+  that check can lose the file before its boot reads it, a window of a handful of calls, and then boots the cart's blank
+  start. What is left is an instance that ends with a render held in TS, and a crash, which the startup sweep takes
+  (`src/renderSnapshotSweep.ts`: a snapshot more than a day old). The worker reads the snapshots only when it boots the
+  render, which can be minutes later behind the worker budget, so a shared name would boot a later edit's (or another
+  instance's) state; and the spec says they must exist when it boots (`RenderOpts.inputsMustExist`, which the UI sets
+  and the CLI does not: its `--sav` is its own to name), so one the sweep took from under a live job refuses rather than
+  booting blank, except in the window above. A snapshot that cannot be written refuses the render, listed as an error,
+  rather than boot the sibling `.sav` on disk. `pickActiveRenderJob` selects a tile's badge job and `renderBadgeLabel`
+  writes its text.
 - **The instance's CONFIGURATION rides along** — `spec.roles` (the live system's roles, configs included)
   and `spec.gainDb`. The job boots a fresh core from the ROM on disk, so without them it would take each
   role's schema DEFAULTS and render a PAL project at NTSC, a Game Boy as the wrong model, a replaced DMC kit
@@ -161,10 +163,12 @@ SameBoy's `Core/random.c`, `camera.c` and `sgb.c` statics are patched
 - **The render baseline** (`pnpm test:native render-baseline`): 13 renders through engine jobs hashed against
   `test-native/golden/render-baseline.json`, exact on `linux-x64-gnu13` (`RP_RENDER_GOLDEN_REQUIRE_TARGET=1` in CI),
   with `render-chunks` holding the chunk independence and `render-cli` the CLI's tool against the same golden
-  ([06-build-test.md](06-build-test.md)).
+  ([06-build-test.md](06-build-test.md)); `pnpm test:native render` runs every `render-*` file at once.
 - **A manual smoke:** `retroplug-render-host-test` (built by name) - `<job-json>` runs one engine job and is
   `cmp`-identical to `retroplug-cli render` (the same code); `--parallel <j>...` runs jobs concurrently on one pool;
-  `--cancel <j>` cancels mid-render. The coverage CI runs is `[engine-jobs]`; the tool is not a CI step.
+  `--cancel <j>` cancels mid-render. The coverage CI runs is `[engine-jobs]`; the tool is not a CI step. Build it with
+  `cmake --build build --target retroplug-render-host-test -j$(nproc)`, or in `build-tsan/` for a ThreadSanitizer
+  pass.
 - **UI-seam logic:** `pnpm test render` - `startSystemRender` spec/snapshot (two queued renders of one system each
   boot their own snapshot, gone once settled; a render held past the queue's bound and a submission the RPC threw on
   delete theirs; a snapshot that cannot be written refuses the render; a render whose snapshot went refuses to boot),
````

`spec/12-dynamic-parameters.md`:

````patch
diff --git a/spec/12-dynamic-parameters.md b/spec/12-dynamic-parameters.md
index a6f7ec2ee..803872892 100644
--- a/spec/12-dynamic-parameters.md
+++ b/spec/12-dynamic-parameters.md
@@ -21,7 +21,7 @@ stands in the fork now:
 
 1. **Count is frozen at construction.** [DistrhoPlugin.cpp:43-59](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPlugin.cpp#L43-L59)
    does `new Parameter[parameterCount]` once. RetroPlug passed `1` (its constructor,
-   [PluginDSP.cpp:137](../packages/native/plugin/PluginDSP.cpp#L137), now declares the fixed pool,
+   [PluginDSP.cpp:135](../packages/native/plugin/PluginDSP.cpp#L135), now declares the fixed pool,
    `kParameterCount`).
 2. **`initParameter` is called exactly once per index**, in the `PluginExporter` constructor
    ([DistrhoPluginInternal.hpp:406](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginInternal.hpp#L406)).
@@ -260,23 +260,25 @@ this is a BlipToaster ROM with no tag. Sega still always takes its deep read (sm
 `$3640`); no other NES cart pays one. One read at construct, not on the classify path.
 
 **The seam.** `PluginDSP` polls a new `__rp_parameterMapJson` global in `updateParameterMap()`
-([PluginDSP.cpp:484-513](../packages/native/plugin/PluginDSP.cpp#L484-L513)), called right after
+([PluginDSP.cpp:495-524](../packages/native/plugin/PluginDSP.cpp#L495-L524)), called right after
 `updateLatency()` polls `__rp_syncLatencyMs`
-([PluginDSP.cpp:472-476](../packages/native/plugin/PluginDSP.cpp#L472-L476)) - that is, after
+([PluginDSP.cpp:483-487](../packages/native/plugin/PluginDSP.cpp#L483-L487)) - that is, after
 `setState` and on `activate` - and from the editor's idle (section 9). If the JSON differs from the cached copy it stores the new descriptors
 and calls `requestParameterInfoUpdate()`. `initParameter` then reads from that cached copy, which is
-what makes the DPF re-declare/diff loop work.
+what makes the DPF re-declare/diff loop work. The latency report asks the STORED `lsdj-sync` mode, not one a
+controller app forces: no plugin binary's product list carries `launchpad` or `lsdj-launchpad`, so nothing can force
+one there, and a control surface joining the plugin's list must make the report ask the placed mode.
 
 **Writing values.** A DAW parameter write in `setParameterValue` is stored, and `run()` turns each
 claimed slot whose value moved into a synthesised CC at frame 0, staged into the engine after the
-real MIDI ([PluginDSP.cpp:330-351](../packages/native/plugin/PluginDSP.cpp#L330-L351)). Values
+real MIDI ([PluginDSP.cpp:336-356](../packages/native/plugin/PluginDSP.cpp#L336-L356)). Values
 arrive on the audio thread already, so this is the existing path with a different source.
 
 **Persistence.** Parameter *values* are host-owned and symbol-keyed, so they need nothing from us.
 The map is derived from the project (role + ROM), so it also needs nothing. A **user-editable**
 override would live in the project JSON, and per the config-migration rule that means bumping
-`K_PROJECT` and adding one raw `(obj) => obj` step in
-[migrate.ts](../packages/retroplug/src/migrate.ts). Treat the override as a follow-up.
+`K_PROJECT` and adding one raw `(obj) => obj` step to `PROJECT_MIGRATIONS` in
+[projectConfig.ts](../packages/retroplug/src/projectConfig.ts). Treat the override as a follow-up.
 
 ## 6. Verification
 
@@ -293,16 +295,17 @@ override would live in the project JSON, and per the config-migration rule that
   lane, and a chip-global custom-patch lane, so a build that silently fell back to the 2A03 table
   fails. Deterministic, no mouse. It is the only coverage of the per-chip tables, which the mGB path
   below never reaches. Point `RP_PARAMS_ROM` at any build to check another.
-- **Real hosts, re-labelling.** `pnpm reaper:params` (VST3) and `pnpm reaper:params-clap` (CLAP) insert RetroPlug
-  with no project, read every parameter name through ReaScript, click-load mGB **through the UI**,
+- **Real hosts, re-labelling.** `pnpm reaper:params` (VST3) and `pnpm reaper:params-clap` (CLAP), both
+  [tools/run-reaper-params.sh](../tools/run-reaper-params.sh) (the CLAP leg sets `RP_PARAMS_FORMAT=clap`), insert
+  RetroPlug with no project, read every parameter name through ReaScript, click-load mGB **through the UI**,
   and re-read: the names must become mGB's map, `"1: CC 1"` must be gone, and the count must not have
   moved. This is the only check that proves a host acts on the flag the plugin raises, and loading
   through the UI also covers the editor idle poll. All three are in
   [run-reaper-suite.sh](../tools/run-reaper-suite.sh) as `params-vst3` / `params-clap` / `params-vrc7`;
   none is in CI (they need a full DAW + X stack).
-- The format is requested by its **prefixed** name (`"CLAPi: RetroPlug"`) and then re-checked against
-  what Reaper actually loaded. A bare `"RetroPlug"` let Reaper pick VST3 for both legs, so the CLAP
-  run was silently a second VST3 run that still reported PASS.
+- Run it per format: the two take different paths in DPF. The format is requested by its **prefixed** name
+  (`"CLAPi: RetroPlug"`) and then re-checked against what Reaper actually loaded. A bare `"RetroPlug"` let Reaper
+  pick VST3 for both legs, so the CLAP run was silently a second VST3 run that still reported PASS.
 - **`tools/run-sanitizer.sh thread`** passes, but note what it does *not* cover: it builds and runs
   `retroplug-host`, which does not link DPF, so it never exercises this code. The guard for the
   `hints` invariant in 3.3 is the Catch2 case above, not TSAN.
````

`spec/13-subsystems.md`:

````patch
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 4be9aef51..0f84e4df9 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -89,19 +89,34 @@ copying 128 KiB and decoding a song every frame is too expensive (see the live-m
 Drives a **real** NES over USB: MIDI in (from a DAW or a controller) is forwarded to a physical
 Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart.
 
-- Code: the workspace package `packages/n8`: natively `native/src/n8/` (the one `Edio`, written once in C++ for the
-  desktop and the Pico box over an abstract transport, with `Menu`, `Ops`, `rp.n8.nes` and `CartLink`) and
-  `native/src/n8/desktop/` (`N8Link`, `N8Host`, the serial transport, the RPC facet and the provider); in TS
-  `packages/n8/ts` (`createN8Client`, the sniffer and the other decoders, the load helpers). The emulated twin is
-  `src/system/mesen/NesEverdriveFifo.hpp`, which every NES ROM gets at `$40F0`/`$40F1`.
+- Code: the workspace package `packages/n8`: natively `native/src/n8/`, the portable target `retroplug-n8`
+  (`device: true`): the one `Edio`, written once in C++ for the desktop and the Pico box over an abstract transport,
+  with no exceptions, beside the menu commands (`Menu`), the operations (`Ops`: `bootRom`, `readFile`,
+  `writeFile`), `rp.n8.nes` and the `CartLink` it drives; and `native/src/n8/desktop/`, the desktop target
+  `retroplug-n8-desktop` (the serial transport, `N8Link`, `N8Host`, the `N8CartLink`, the RPC facet `N8Rpc` and
+  the provider `rp_n8_provider`); in TS `packages/n8/ts` (`createN8Client`, the sniffer and the other decoders, the
+  load helpers). The emulated twin is the mesen package's `native/src/system/mesen/NesEverdriveFifo.hpp`, which every
+  NES ROM gets at `$40F0`/`$40F1`.
 - Every product that lists the pack and includes its generated provider header starts it (the plugin, the SDL
   standalone, the headless host and the CLI, whose sessions use the facet's session half; a product's `PROVIDERS` list
-  says which packs it starts). The Pico box (`pico/n8-host`) is the same Edio on TinyUSB.
+  says which packs it starts). The Pico box (`pico/n8-host`) is the same Edio on TinyUSB. BlipToaster's MIDI policy
+  (`rp.bliptoaster.midi-policy`) is a node of the box's plan; the desktop's N8 graphs do not use the policy node.
+- What the cart allows, and the pack accepts: a cart's battery is reachable only at the N8 menu, through the SD card's
+  save file `EDN8/gamedata/<rom>/bram.srm`, and a dump takes about a minute for 64 KB; a running game refuses the job
+  (a direct cart-memory read wedges it and leaves the menu deaf), and a restore goes before a `rom.upload` so the game
+  loads it at its next boot. `rom.header` / `rom.title` answer for what this host last booted, forgotten at a connect
+  when the running mapper differs; a game of the same mapper, or one started between connects, goes unnoticed, because
+  the cart has no query for the image it runs. A cart job pauses streaming (pushes during it are dropped and counted),
+  and a `rom.upload` leaves streaming stopped until the host reconnects. The CLI's n8 tools keep their own raw serial
+  path (the battery dump among them, the one cart-memory read left); the capabilities deliberately offer no peek,
+  poke, sniffer or FIFO reads.
 - **Prove a change:** `pnpm test:n8` (protocol, no hardware; `packages/n8/native/test`), the `NesN8FifoTiming` and
   `N8Host` Catch2 cases, `pnpm test n8` and `pnpm test:native n8-session`; for the box, `pnpm device:box` and the rig's
-  `loop.sh`, `stress.sh` and `faults.sh`. `retroplug-n8-hwtest` needs the physical console.
+  `loop.sh`, `stress.sh` and `faults.sh`, which boot BlipToaster, never `n8-midi.nes`. `retroplug-n8-hwtest` needs
+  the physical console.
 - Deeper: [docs/n8-bridge-plan.md](../docs/n8-bridge-plan.md),
-  [docs/n8-usb-capabilities.md](../docs/n8-usb-capabilities.md).
+  [docs/n8-usb-capabilities.md](../docs/n8-usb-capabilities.md), and the pack's design,
+  [docs/modular-graph-phase-e2-design.md](../docs/modular-graph-phase-e2-design.md).
 
 ### Launchpad
 
@@ -110,13 +125,24 @@ from the 8x8 grid. Since F3 the whole thing is a pack: the transport and the sca
 [packages/launchpad](../packages/launchpad)'s native halves, the protocol, the port scan and the session are its
 TypeScript, and the behaviour is an app (the controller settings select it) that runs on the audio thread, in the
 controller session node (below). The selection is the user's: `config.json`'s `controlSurface`, which TS owns and
-writes with a re-read, so two processes can share the file.
+writes with a re-read, so two processes can share the file. Its fields (`input`, `output`, `device`, `enabled`)
+are additive, so no migration; and the SDL standalone holds MIDI and audio until TS's `restored()` lands (500 ms at
+most once its audio settings have applied: `retroplug-sdl` below), so a restored pair is never opened by the shared
+stream first. One controller link per process is all the registry holds, one per `rp.host.devices/2` name; more than
+one is a registry change. The plugin has no control surface: its host MIDI out caps a message at 4 bytes, so no SysEx
+reaches a surface, and it names no launchpad provider.
 
 - Code: [packages/launchpad](../packages/launchpad) - the portable target (`retroplug-launchpad`, `device: true`) with
   the two surface nodes, and `retroplug-launchpad-desktop` with the transport (`ControllerLink`, `PortClaim`,
   `MidiPortPool`, `MidiPort`), the provider and the facet (`LaunchpadRpc`) - beside
   [ui/launchpadDevices.ts](../packages/launchpad/ts/ui/launchpadDevices.ts), which drives the
-  facet, and the MI.MAP app in [packages/lsdj-launchpad](../packages/lsdj-launchpad).
+  facet, and the MI.MAP app in [packages/lsdj-launchpad](../packages/lsdj-launchpad), a bridge that depends on
+  `launchpad` and `lsdj` and which `launchpad` never imports. The provider is `rp_launchpad_provider`, and the facet
+  is TS's only door to a port, through `createLaunchpadClient(call)` in `@retroplug/launchpad/client`.
+  `ControllerLink`'s writer thread drains the send ring and a UI queue of its own, so `send()` waits for no write in
+  flight: a push takes a lock the writer holds only for a swap. The pack's portable core is held to MicroQuickJS by the
+  device-pack toolchain's scenario mode (byte-identical transcripts under Node and `retroplug-mqjs-run`), but it
+  declares no `script` member, so the scenarios are the proof and no box image carries it.
 - **Prove a change:** `retroplug-launchpad-test` (one of the fourteen `pnpm test:plugin` binaries),
   `pnpm test:native launchpad` (the facet's cases over the headless host's scripted device, and
   `launchpadDevices.ts`'s over it), `pnpm test launchpad`, `pnpm test:native lsdj-launchpad`,
@@ -136,7 +162,9 @@ MI.MAP cart's sync mode forced), so the chosen app runs on the audio thread as p
 than as UI-thread glue. `appConfig` is deliberately opaque at this level: the chosen app's own schema validates it on
 the control plane (`parseAppConfig`), and the node takes it parsed.
 The apps themselves are contributions (`controllerApps`, in two halves joined by id, F3): MI.MAP is
-[packages/lsdj-launchpad](../packages/lsdj-launchpad)'s, and a tracker with another app adds its own bridge.
+[packages/lsdj-launchpad](../packages/lsdj-launchpad)'s, and a tracker with another app adds its own bridge. The
+control plane's half is `definePackage({ controllerApps })` (`id`, `label`, the app's config schema), which the
+kind's `accept` files in the pack's own `ts/registry.ts` table, the one `parseAppConfig` reads.
 
 - **Prove a change:** `pnpm test controller`, `pnpm test script/nodes/controller-session`,
   `pnpm test graph/build-graph`, and `pnpm test:plugin` (`retroplug-launchpad-test` for the transport the
@@ -166,11 +194,22 @@ runtime and named by the product's `AUDIO_BACKENDS`; RtMidi behind `packages/mid
 
 It is in the default `all` target, so every CI platform compiles and links it and it rides in the
 build artifact, but it is deliberately **not** in any release: `release.yml`'s packaging steps are
-explicit allowlists and it is on none of them.
+explicit allowlists and it is on none of them, and each carries a comment saying so. CI builds `all` on all four
+platforms, and the standalone needs nothing extra installed: SDL2 is already REQUIRED at root scope for every plugin
+variant, and rtmidi and PortAudio are in-tree submodules. On macOS the artifact binary is build-machine-only: the
+`SDL2.framework` embed covers the plugin and app bundles, and `retroplug-sdl` is a bare executable, so shipping it
+would need a framework copy and an `@executable_path` rpath.
+
+At startup it holds MIDI and audio until the UI has reported: the control surface's `restored()` (only when it
+started the launchpad provider) and the `audio` section's apply. The wait is bounded in time, at 2000 ms
+(`kAudioBoundMs`) while the audio apply has not landed and 500 ms once it has, after which it starts on the defaults
+and says so. Render-ahead is a run flag, not a setting: `--audio-backend` / `--block-size` / `--render-ahead`
+override the saved section for that run.
 
 - **Prove a change:** `pnpm sdl:smoke` (headless, no display or audio server — in CI) and
   `pnpm sdl:pipewire` (which output device PortAudio actually opens; not in CI, needs PipeWire).
-- Deeper: [docs/sdl-standalone.md](../docs/sdl-standalone.md).
+- Deeper: [docs/sdl-standalone.md](../docs/sdl-standalone.md), and the audio and MIDI packages' design,
+  [docs/modular-graph-phase-f5b-design.md](../docs/modular-graph-phase-f5b-design.md).
 
 ### `retroplug-node` — the N-API addon
 
````

- [ ] **Step 3: run the check and watch it pass**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 4
```

Expected: exit 0.

```
check.mjs: tasks 1..4 (11 files)
ok   probes at their destination (keep / merge / open / new): 333 passed, 0 failed
ok   dup quotes in the doc they name: 603 passed, 0 failed
ok   relative links and anchors in touched markdown files: 923 passed, 0 failed
ok   no em dash in an added line: 651 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 651 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 0 passed, 0 failed
ok   no changed file outside the plan's list: 11 passed, 0 failed
all checks pass
```

- [ ] **Step 4: stage the task's files and check their whitespace**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- spec/04-roles-protocol-nodes.md spec/05-data-persistence.md \
  spec/11-ui-rendering.md spec/12-dynamic-parameters.md spec/13-subsystems.md
git -C /workspaces/RetroPlug-main/cuttlefish diff --cached --check
```

Expected: no output, exit 0.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(cleanup): the N8, Launchpad and SDL facts and the remaining accepted limits into spec/04, 05, 11, 12 and 13

What AGENTS.md said of the N8 pack, the Launchpad pack, the controller apps and the SDL standalone moves into their
spec/13 entries, and what it said of renders and the params legs into spec/11 and spec/12. The accepted limits spec/07
carried as work (a real cart's battery and streaming rules, one controller link per process, the plugin's missing
control surface, SameBoy's serial timing, a repaired stored value) become sentences in the entry of the subsystem each
limits.

Errors fixed on the way, each checked against the code: the SDL standalone's startup hold waits up to 2000 ms for its
audio settings (kAudioBoundMs), not 500 ms alone; a root's migrations map sits beside its schema constant, not in
migrate.ts (spec/05 and spec/12 both said so); the plugin's 4-byte cap is MIDI out's; the emulated FIFO's header is the
mesen package's; and spec/05's and spec/12's PluginDSP.cpp line links had drifted.
MSG
```

---

## Task 5: AGENTS.md becomes rules and pointers, and what named its sections is repointed

With tasks 1 to 4 landed, everything AGENTS.md said that is not a rule lives in the spec doc that owns it, so AGENTS.md
is rewritten whole to design section 1's shape: the header paragraph (rules and pointers only; how something works
goes in the spec doc that owns it; under 200 lines, and a new rule cuts or moves one), Start here, the Workflow rules
one or two lines each with its link, Verification, and the two Gotchas. It carries the 46 `keep` rows' probes and the
four rows the design adds with no source (N001 to N004: no `Co-Authored-By` and no attribution; a commit that finishes
work listed in spec/07 deletes it there; the header's two sentences). It is 83 lines. CLAUDE.md's `@AGENTS.md` import
is unchanged.

Two departures from design section 1, both forced by what the ledger found: the package map's one line links spec/00's
"Package layout" (the per-package list; spec/06's "The packages" has none) as well as spec/06's "The packages" (the
mechanism); and the Mesen-merge rule rides the `RetroPlug:` marker line rather than a line of its own.

What named an AGENTS.md section is repointed to the content's new home (design section 1's list, confirmed by `git grep
AGENTS`): `TileViews.tsx` (spec/03's Framework gotchas), `cmake/packages.cmake` (the `D9025` warning, spec/06's Build
gotchas), `scripts/cmake-build.js` (spec/06's Build gotchas, and its stale "names all eight" is fourteen), `build.yml`
(spec/06's dpf.js seam), `.devcontainer/post-create.sh` (it named an "Agent workflows" section that never existed), the
cli-debug skill's SameBoy patch pointer, spec/06's two mentions (line 20's flags now follow in the next bullet, which
task 2 wrote; line 1049's "also noted in AGENTS.md" is no longer true), and `tools/lsdj-manual.py`'s docstring
(docs/lsdj.md documents the Read+grep path). Four mentions stay, because they name AGENTS.md as the repo guide or a rule
it still holds: the cli-debug skill's line 20, the feature-dev skill's line 36, `packages/retroplug/README.md` line 9
(the package boundary rule), and spec/06's line 903 (the nested `pnpm install`). docs/ records are not edited.

The three stale code comments design section 5 names are corrected to what the code says now:
`cmake/test-binary.cmake` (seventeen binaries use the helper: fourteen `test:plugin`, two `test:n8`, the hardware-only
`retroplug-n8-hwtest`; it said fourteen and eleven), the root `CMakeLists.txt`'s Mesen LTO comment (288 objects, 426
with every console on; fourteen consuming binaries on Linux, counted from `build/`'s `link.txt` files, the AU a
fifteenth on macOS; its measurement labelled as taken before the compile-out), and `cli/sessions/test.ts`'s header (the
kit is the binary and the tests; the SDK is served as a module, only its `.d.ts` written; its
`"../sdk/retroplug-cli.js"` example is gone).

**Files:**

- Rewrite: `AGENTS.md` (835 lines to 83).
- Modify: `.claude/skills/cli-debug/SKILL.md`, `.devcontainer/post-create.sh`, `.github/workflows/build.yml`,
  `CMakeLists.txt` (the LTO comment), `cmake/packages.cmake`, `cmake/test-binary.cmake`,
  `packages/retroplug/cli/sessions/test.ts`, `packages/retroplug/ui/views/TileViews.tsx`, `scripts/cmake-build.js`,
  `spec/06-build-test.md` (lines 19-20 and 1049), `tools/lsdj-manual.py` (the docstring). Comments only.

**Ledger rows (task 5):** the 46 `keep` rows and N001 to N004. Once this task lands the check also holds AGENTS.md to
200 lines and every `keep` / `new` probe of its own.

### Steps

- [ ] **Step 1: run the check and watch it fail**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 5 --max-failures 3
```

Expected: exit 1. The `keep` probes come from AGENTS.md itself, so they pass; the new rows and the line count fail:

```
check.mjs: tasks 1..5 (24 files)
FAIL probes at their destination (keep / merge / open / new): 379 passed, 4 failed
       N001: probe "Co-Authored-By" not in AGENTS.md
       N002: probe "finishes work listed in spec/07" not in AGENTS.md
       N003: probe "rules and pointers only" not in AGENTS.md
       ... and 1 more
ok   dup quotes in the doc they name: 603 passed, 0 failed
ok   relative links and anchors in touched markdown files: 1008 passed, 0 failed
FAIL AGENTS.md is 200 lines or fewer and carries every keep / new probe of its own: 1 passed, 2 failed
       AGENTS.md is 835 lines
       AGENTS.md lacks N001, N002, N003, N004
ok   no em dash in an added line: 651 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 651 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 1 passed, 0 failed
ok   no changed file outside the plan's list: 11 passed, 0 failed
6 failure(s)
```

- [ ] **Step 2: rewrite AGENTS.md and repoint its references**

The whole of `AGENTS.md`:

````markdown file=AGENTS.md
# Agent guide

This file holds rules and pointers only: how something works goes in the [spec/](spec/README.md) doc that owns the
topic, and a line here links to it. Keep it under 200 lines; a new rule cuts or moves one.

## Start here

- [README.md](README.md): what RetroPlug is, how to build, the project layout.
- [spec/README.md](spec/README.md): the architecture, one doc per concern. The shell is `packages/native/` (C++) and
  `packages/retroplug/` (TS and the UI); every other package is listed in spec/00's
  [Package layout](spec/00-overview.md#package-layout), and how packages work is spec/06's
  [The packages](spec/06-build-test.md#the-packages).
- [docs/lsdj.md](docs/lsdj.md): the LSDj domain reference, and how to test LSDj sync, DAW timing and audio quality
  headlessly.
- What is left to do is [spec/07-remaining-work.md](spec/07-remaining-work.md); the hosts and harnesses run by hand
  (Reaper, Renoise, the SDL legs, `retroplug-cli test`) are [spec/14](spec/14-hosts-harnesses.md).

## Workflow rules

- **Don't push to remotes or open PRs without an explicit ask.** The user pushes their own work.
- **Commits carry no `Co-Authored-By` trailer and no attribution of any kind.**
- **When a commit finishes work listed in spec/07, it deletes that item there**: its checklist line and its detail
  entry, never a tick left in place ([spec/07](spec/07-remaining-work.md)).
- **Clone `--recursive`** (`deps/dpf.js` is a nested submodule), and after a fresh checkout also run `pnpm install` in
  `deps/dpf.js/deps/lv_binding_js`, or the UI bundle cannot resolve `react`
  ([the dpf.js seam](spec/06-build-test.md#the-dpfjs-seam)). Don't bump a submodule pointer (in either repo) without
  checking: they are managed deliberately.
- **`deps/sameboy` is dirty after a configure, and that is EXPECTED**: the configure applies RetroPlug's tracked
  patches. Never reset / stash / commit it, and never bump its pointer to absorb it
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **Every edit to the vendored `deps/mesen` carries a `RetroPlug:` marker comment**, and a Mesen merge runs both modes
  of `tools/build-mesen-all-consoles.sh` (`--off` catches a dropped guard)
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **A mquickjs pin bump that fails the configure's hash guard means regenerating the wrapper, not loosening the guard**
  ([The device build](spec/06-build-test.md#the-device-build)).
- **Never `rm -rf build` to fix CMake**: investigate first. Build with `build.sh` / `build.bat`; a single target with
  `cmake --build build --target <t> -j$(nproc)` (always `-j`: the default is single-threaded)
  ([One configure](spec/06-build-test.md#one-configure)).
- **Windows is `/W4 /WX` on our own targets**: fix our code; suppress by NUMBER, and only what we do not own. CI's
  compiler (`windows-2025-vs2026`) matches the dev box's, so a CI-only warning is fixed, not explained away
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **Mesen LTO (`RETROPLUG_MESEN_LTO`) stays off in the dev loop and on in releases**
  ([Build gotchas](spec/06-build-test.md#build-gotchas)).
- **`retroplug-sdl` is not in releases until the standalone is release-ready** (`release.yml`'s packaging steps are
  allowlists; [spec/13](spec/13-subsystems.md)).
- **Never commit derived artifacts**: the embedded bundle C arrays (`build/native/*bundle_data.c`).
- **Config migrations**: every config root is versioned raw JSON; a newer stamp is refused and latched read-only, an
  older one migrated up, and only the latest schema is kept
  ([spec/05-data-persistence.md](spec/05-data-persistence.md#persistence-policy-the-precise-statement)).
- **A `device: true` node keeps to the portable settings types and wraps its desktop hooks in `#ifndef RP_DEVICE`**
  ([spec/02](spec/02-native-host.md#packages-generated-registration-and-device-mode)).
- **A package imports only its declared dependencies and exports no wildcard**; adding one follows spec/06's
  "Adding a package" checklist ([The packages](spec/06-build-test.md#the-packages)).
- **A harness that can reach an audio, video or serial device runs under the self-tested device guard**, and a MIDI
  harness lists only the fakes (`RP_MIDI_PORT_ALLOW`) ([the device guard](spec/14-hosts-harnesses.md#the-device-guard)).

## Verification

- **Verify headlessly before claiming done: a "tests pass" claim needs an actual exit-zero.** Which command proves
  which change is
  [spec/06-build-test.md](spec/06-build-test.md#the-headless-verification-loop--which-command-proves-which-change); the
  hosts and harnesses run by hand are [spec/14](spec/14-hosts-harnesses.md).
- **On Windows, run the suites through `test.bat`**, not bare `pnpm test*`
  ([Hosts → test tiers](spec/06-build-test.md#hosts--test-tiers)).
- **A case that cannot run skips with `skip(reason)`, and the skip is counted**: it THROWS, so no `catch` may stand
  between it and the harness, and `packages/retroplug/scripts/skip-baseline.json` pins which files may skip
  ([Skips](spec/06-build-test.md#skips-are-reported-and-ratcheted)).
- **The test-speed traps**: a slow suite is nearly always one slow file, not the pool; `renderAudio` takes
  milliseconds, not frames; and every system a test builds is removed once its state is read
  (`s.project.systems.removeSystem(id)` for a store-built one)
  ([Hosts → test tiers](spec/06-build-test.md#hosts--test-tiers)).
- **A parity failure is a real change to the audio, never noise**; an intentional one re-records the groups it moves
  in the same commit ([the parity baseline](spec/06-build-test.md#the-parity-baseline-test-nativeparity)).
- **The kit and render goldens compare bytes on `linux-x64-gnu13` only**; elsewhere a case must agree with itself
  ([kit](spec/06-build-test.md#the-kit-baselines-kit-baseline-kit-compile),
  [render](spec/06-build-test.md#the-render-baseline-render-baseline-render-chunks-render-cli)).

## Gotchas

- **lv_binding_js ignores `insertChildBefore` (it always appends)**: wrap a swappable child in a stable per-id
  `StableSlot`, or Re-key the parent on the visible set ([Framework gotchas](spec/03-ts-layer.md#framework-gotchas)).
- **The UI face is monospaced by symbol substitution**: switch faces by regenerating the font files, never in
  `lv_conf.h` ([Framework gotchas](spec/03-ts-layer.md#framework-gotchas)).
````

`.claude/skills/cli-debug/SKILL.md`:

````patch
diff --git a/.claude/skills/cli-debug/SKILL.md b/.claude/skills/cli-debug/SKILL.md
index f333b0337..bb0ba049c 100644
--- a/.claude/skills/cli-debug/SKILL.md
+++ b/.claude/skills/cli-debug/SKILL.md
@@ -62,7 +62,8 @@ current source: `tools/sync-cli-to-bliptoaster.sh [dest]` (rebuilds `retroplug-c
   `.../host/rpc/DebugRpcService.{hpp,cpp}` + `.../host/rpc/BackendRpcRegistration.hpp` (registration).
 - **TS backend + CLI SDK**: `packages/retroplug/src/{backend.ts,realBackend.ts}`,
   `packages/retroplug/testing/mockBackend.ts`, `packages/retroplug/cli/{sdk-types.d.ts,sdk.ts}`.
-- **Cores**: `deps/mesen` (NES, vendored — edit directly), `deps/sameboy` (GB, tracked patch — see AGENTS.md).
+- **Cores**: `deps/mesen` (NES, vendored: edit directly), `deps/sameboy` (GB, tracked patches: see
+  [spec/06's Build gotchas](../../../spec/06-build-test.md#build-gotchas)).
 
 ## Fix + verify (an exit-zero, not a claim)
 
````

`.devcontainer/post-create.sh`:

````patch
diff --git a/.devcontainer/post-create.sh b/.devcontainer/post-create.sh
index 7c14e6056..83cdb84fc 100755
--- a/.devcontainer/post-create.sh
+++ b/.devcontainer/post-create.sh
@@ -130,5 +130,5 @@ Next steps:
   pnpm smoke                              # smoke-test retroplug-cli end-to-end
   pnpm screenshot                         # capture standalone UI -> /tmp/retroplug.png
 
-Agent tooling docs: see AGENTS.md ("Agent workflows" section).
+Agent docs: AGENTS.md (the rules, and where each topic's detail lives) and spec/README.md.
 EOF
````

`.github/workflows/build.yml`:

````patch
diff --git a/.github/workflows/build.yml b/.github/workflows/build.yml
index cdedcd63b..2a6811287 100644
--- a/.github/workflows/build.yml
+++ b/.github/workflows/build.yml
@@ -58,7 +58,7 @@ jobs:
           pnpm install --frozen-lockfile
           # The UI bundle's react/react-reconciler/lvgljs-ui live in
           # lv_binding_js's own workspace (deps/ is excluded from the root
-          # workspace), so it needs its own install (see AGENTS.md).
+          # workspace), so it needs its own install (spec/06-build-test.md, "The dpf.js seam").
           pnpm install --frozen-lockfile -C deps/dpf.js/deps/lv_binding_js
 
       # Platform-independent, so the x86 linux job alone carries it, and it runs BEFORE the build for
````

`CMakeLists.txt`:

````patch
diff --git a/CMakeLists.txt b/CMakeLists.txt
index 18b05d0d6..ddc71f20f 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -71,12 +71,14 @@ endif()
 # --- LTO on the Mesen NES core: ~10% faster on the Cortex-A53 handheld, but it costs the dev loop
 # dearly, so it's opt-in and the release workflow turns it on (see deps/mesen/CMakeLists.txt).
 #
-# Mesen's ~430 objects become LTO bitcode, and EVERY binary that links the static lib re-runs codegen
-# over all of them: retroplug{,-cli,-host,-ui-test,-sdl} + the four plugin variants. Changing a single
-# .cpp measured 1m05s wall / 26m51s CPU with it on, and 2s wall / ~9s CPU with it off (32 threads) —
-# the compile is one file either way, the rest is eight full LTO links. Off by default so `./build.sh`
-# stays quick; release.yml
-# passes -DRETROPLUG_MESEN_LTO=ON so shipped binaries keep the speedup.
+# Mesen's objects (288 with the consoles RetroPlug does not run compiled out, 426 with MESEN_ENABLE_SNES /
+# -PCE / -WS all on) become LTO bitcode, and EVERY binary that links the static lib re-runs codegen over
+# all of them: fourteen on Linux (the clap, vst3 and vst2 plugins, the JACK standalone, retroplug-host,
+# -cli, -sdl, the Node addon, rp-golden-dump and five test binaries), the AU a fifteenth on macOS. Changing
+# a single .cpp measured 1m05s wall / 26m51s CPU with it on, and 2s wall / ~9s CPU with it off (32
+# threads), before the compile-out, at nine links over 426 objects: the compile is one file either way,
+# the rest is full LTO links. Off by default so `./build.sh` stays quick; release.yml passes
+# -DRETROPLUG_MESEN_LTO=ON so shipped binaries keep the speedup.
 option(RETROPLUG_MESEN_LTO "LTO the Mesen NES core (~10% faster, much slower links; on for releases)" OFF)
 
 # tell LVGL where to find its config file. Set before add_subdirectory(dpf.js)
````

`cmake/packages.cmake`:

````patch
diff --git a/cmake/packages.cmake b/cmake/packages.cmake
index 633b820b3..317f3d01a 100644
--- a/cmake/packages.cmake
+++ b/cmake/packages.cmake
@@ -1983,8 +1983,9 @@ endfunction()
 # which lists the renamed entries in the order PACKAGES does, as a device product's rp_register_packages() lists its
 # own. The binary keeps exceptions in its own translation units (Catch2 is built with them) and reaches the device-mode
 # code through the C ABI alone, as a device's host would. MSVC keeps its /EHsc: a second /EH option on one command line
-# is the D9025 warning AGENTS.md describes, and the GCC and Clang builds are where -fno-exceptions is proved. OWNERS
-# names a variable set to the desktop targets the objects twin, which the binary links for the desktop's build.
+# is the D9025 warning spec/06's Build gotchas describes, and the GCC and Clang builds are where -fno-exceptions is
+# proved. OWNERS names a variable set to the desktop targets the objects twin, which the binary links for the desktop's
+# build.
 #
 # A listed package's script pack (_retroplug_script_targets), and each of SCRIPTS (a test's own, made by
 # retroplug_device_script_pack), is compiled the same way: its registration's source and the MicroQuickJS host it links,
````

`cmake/test-binary.cmake`:

````patch
diff --git a/cmake/test-binary.cmake b/cmake/test-binary.cmake
index 803c8a42f..0a7347bcd 100644
--- a/cmake/test-binary.cmake
+++ b/cmake/test-binary.cmake
@@ -1,6 +1,6 @@
 # retroplug_add_test_binary() — the shape every native test binary shares.
 #
-# There are fourteen of them (eleven run by `pnpm test:plugin`, two by `pnpm test:n8` and the
+# There are seventeen of them (fourteen run by `pnpm test:plugin`, two by `pnpm test:n8` and the
 # hardware-only `retroplug-n8-hwtest`), and each was spelled out in full: the same
 # EXCLUDE_FROM_ALL, the same C++20 pair, the same output directory, the same Catch2 link.
 # Nine tenths of each block was boilerplate, which buried the one or two lines that were
````

`packages/retroplug/cli/sessions/test.ts`:

````patch
diff --git a/packages/retroplug/cli/sessions/test.ts b/packages/retroplug/cli/sessions/test.ts
index e5a344adc..4088d5ac4 100644
--- a/packages/retroplug/cli/sessions/test.ts
+++ b/packages/retroplug/cli/sessions/test.ts
@@ -1,13 +1,14 @@
 // `retroplug-cli test <dir>` - strip and run a directory of TypeScript test files.
 //
 // This is what lets a consumer repo (BlipToaster) use the CLI as its whole test harness with nothing
-// installed: no Node, no npm, no esbuild, no node_modules. The kit is the binary, the SDK `.js`, the
-// `.d.ts`, and the tests.
+// installed: no Node, no npm, no esbuild, no node_modules. The kit is the binary and the tests. The SDK
+// is no file: the binary registers it as the module `retroplug-cli`, which a test imports by that bare
+// specifier, and only its `.d.ts` is written beside the tests, for an editor and tsc (../sdkAssets).
 //
 // Two things make it work, both pre-existing:
 //   * The CLI's txiki runtime resolves `import` off disk at runtime, so tests need no bundling; they
 //     just have to be JavaScript.
-//   * Consumer tests already use explicit `.js` specifiers ("./helper.js", "../sdk/retroplug-cli.js"),
+//   * Consumer tests already use explicit `.js` specifiers ("./helper.js"),
 //     so stripping `X.ts` -> `X.js` at the same directory DEPTH needs no specifier rewriting. See
 //     buildDirFor in ../tsStrip.
 //
````

`packages/retroplug/ui/views/TileViews.tsx`:

````patch
diff --git a/packages/retroplug/ui/views/TileViews.tsx b/packages/retroplug/ui/views/TileViews.tsx
index 17f47cd85..9616e8a0e 100644
--- a/packages/retroplug/ui/views/TileViews.tsx
+++ b/packages/retroplug/ui/views/TileViews.tsx
@@ -1,7 +1,7 @@
 // The tile slots (docs/modular-graph-phase-f4c-design.md section 5.2): every REGISTERED tile view, in product order,
 // each in a StableSlot of its own, always. The slot is why a view that appears late (a flag flipping, a session opening
 // on the next frame) does not land at the END of the tile's child list: lv_binding_js's insertChildBefore always
-// appends (AGENTS.md "Framework gotchas"), so a fixed wrapper with one swappable child is the one way React can place
+// appends (spec/03's "Framework gotchas"), so a fixed wrapper with one swappable child is the one way React can place
 // it. The list is the registry's, which is a module constant, so the slot list never changes length; a `when` that
 // flips under a live tile swaps the slot's child rather than appending a slot over the border and the badge, and a slot
 // with nothing in it is an empty transparent Box (the same shape the dim layer proves passes a click to the tile).
````

`scripts/cmake-build.js`:

````patch
diff --git a/scripts/cmake-build.js b/scripts/cmake-build.js
index 62ea0725a..bb010e89d 100644
--- a/scripts/cmake-build.js
+++ b/scripts/cmake-build.js
@@ -3,10 +3,11 @@
 //
 //   node scripts/cmake-build.js [target...]
 //
-// No targets => a full build (bare `cmake --build`, what AGENTS.md recommends
-// for UI changes). Assumes `build/` is already configured (run `pnpm configure`
-// once). The Catch2 test binaries are EXCLUDE_FROM_ALL and built by NAME - `pnpm
-// test:plugin` names all eight - so they need no BUILD_TESTING configure.
+// No targets => a full build (bare `cmake --build`, what spec/06's Build gotchas
+// recommends to verify a UI change in the standalone). Assumes `build/` is already
+// configured (run `pnpm configure` once). The Catch2 test binaries are EXCLUDE_FROM_ALL
+// and built by NAME - `pnpm test:plugin` names all fourteen - so they need no
+// BUILD_TESTING configure.
 
 const os = require("os");
 const path = require("path");
````

`spec/06-build-test.md`:

````patch
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index da239ea4b..ad62b650c 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -17,8 +17,8 @@ and `deps/catch2` (the C++ unit-test framework) are added `EXCLUDE_FROM_ALL`, so
 it. A full build produces the plugin (all formats) + the standalone.
 
 - `build.sh` (Linux/macOS) and `build.bat` (Windows) are the canonical entry points and have
-  **zero build-specific special-casing** — a bare `./build.sh` builds every `ALL` target. See
-  [AGENTS.md](../AGENTS.md) for their flags.
+  **zero build-specific special-casing**: a bare `./build.sh` builds every `ALL` target. Their flags
+  follow.
 - Both run the configure this project needs and build in parallel (`-j$(nproc)`, `sysctl` on macOS). A bare
   `./build.sh` is incremental: it configures only when `build/CMakeCache.txt` is missing or a `-D` / `--tests` is
   given, and `--clean` wipes `build/` first. Either script passes any `-D<var>=<value>` straight through to the
@@ -1183,7 +1183,7 @@ run by hand: no CI or `test:plugin` leg builds that binary there.
   binary `build/bin/retroplug` is produced by `retroplug-jack`. To verify a UI
   change in the standalone, build with a bare `cmake --build build -j$(nproc)` (no `--target`) or
   `--target retroplug-jack` — otherwise the screenshot shows old behaviour against a fresh
-  bundle. (This standalone-vs-umbrella gotcha is also noted in [AGENTS.md](../AGENTS.md).)
+  bundle.
 - **Don't `rm -rf build` to "fix" CMake.** The configured `build/` is load-bearing for the dev loop;
   the sanitizer dirs (`build-tsan/`/`build-asan/`) are the only ones that get reconfigured freely.
 
````

`tools/lsdj-manual.py`:

````patch
diff --git a/tools/lsdj-manual.py b/tools/lsdj-manual.py
index 5b85d755a..b091b91d3 100755
--- a/tools/lsdj-manual.py
+++ b/tools/lsdj-manual.py
@@ -240,7 +240,7 @@ def write_latest_markdown(latest: ManualSource,
                           blocks: list[tuple[int, str]],
                           images: dict[int, list[str]]) -> None:
     """Regenerate lsdj_manual.md from the highest-version manual. The fallback
-    Read+grep path documented in AGENTS.md only needs the canonical version."""
+    Read+grep path docs/lsdj.md documents only needs the canonical version."""
     lines = [
         f"# LSDj {latest.version_str} — extracted manual\n",
         "",
````

- [ ] **Step 3: run the check and watch it pass**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 5
```

Expected: exit 0 (`scripts/cmake-build.js` is the touched `.js` file `node --check` parses).

```
check.mjs: tasks 1..5 (24 files)
ok   probes at their destination (keep / merge / open / new): 383 passed, 0 failed
ok   dup quotes in the doc they name: 603 passed, 0 failed
ok   relative links and anchors in touched markdown files: 975 passed, 0 failed
ok   AGENTS.md is 200 lines or fewer and carries every keep / new probe of its own: 3 passed, 0 failed
ok   no em dash in an added line: 757 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 757 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 1 passed, 0 failed
ok   no changed file outside the plan's list: 22 passed, 0 failed
all checks pass
```

- [ ] **Step 4: the wider checks the comments reach**

The TypeScript comments (`test.ts`, `TileViews.tsx`):

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
```

Expected: both exit 0; `check:boundaries` ends with

```
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))
```

and `typecheck` (30 of 31 workspace projects) with

```
packages/retroplug typecheck: typecheck: the plugin product's tree (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.products/plugin/tsconfig.json)
packages/retroplug typecheck: Done
```

The CMake comments (`CMakeLists.txt`, `cmake/packages.cmake`, `cmake/test-binary.cmake`) reconfigure on the next build:

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-cli -j$(nproc)
```

Expected: exit 0, with

```
-- Configuring done (2.4s)
-- Generating done (0.2s)
-- Build files have been written to: /workspaces/RetroPlug-main/cuttlefish/build
...
[100%] Built target retroplug-cli
```

The shell script's comment:

```bash
bash -n /workspaces/RetroPlug-main/cuttlefish/.devcontainer/post-create.sh
```

Expected: no output, exit 0. (`build.yml`'s and `lsdj-manual.py`'s edits are inside a comment and a docstring; this
plan runs no Python and no workflow.)

- [ ] **Step 5: stage the task's files and check their whitespace**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- AGENTS.md .claude/skills/cli-debug/SKILL.md \
  .devcontainer/post-create.sh .github/workflows/build.yml CMakeLists.txt cmake/packages.cmake \
  cmake/test-binary.cmake packages/retroplug/cli/sessions/test.ts packages/retroplug/ui/views/TileViews.tsx \
  scripts/cmake-build.js spec/06-build-test.md tools/lsdj-manual.py
git -C /workspaces/RetroPlug-main/cuttlefish diff --cached --check
```

Expected: no output, exit 0.

- [ ] **Step 6: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(cleanup): AGENTS.md becomes rules and pointers

AGENTS.md is loaded into every session through CLAUDE.md, and at 835 lines most of it described how things work
rather than what an agent must or must not do. Everything of that kind now lives in the spec doc that owns it
(spec/06, spec/14, spec/02, spec/03, spec/13 and the rest), so the file is rewritten to the rules, each one or two
lines with a link to its detail, under a header that says how to keep it that way: rules and pointers only, under
200 lines, and a new rule cuts or moves one. Two rules are new: commits carry no attribution, and a commit that
finishes work listed in spec/07 deletes it there.

The comments and docs that named an AGENTS.md section now name the section that holds the content. Three code
comments the move touched were stale and say what the code does now: seventeen binaries use
retroplug_add_test_binary, the Mesen LTO comment counts 288 objects and fourteen consuming binaries on Linux, and the
CLI test harness's kit is the binary and the tests, with the SDK served as a module.
MSG
```

---

## Task 6: spec/07 lists open work only, and what named a gone entry is repointed

spec/07 is rewritten whole to design section 3 with section 5's changes: the top note (open work only; the commit that
finishes an item deletes it, its checklist line and its detail entry; never tick an item and leave it; the design docs
and git log are the history), a checklist of 85 lines in the seven groups (Before merge, Package split, Known bugs,
Engine, Renders and kits, Pico, Feature gaps and SDL), each line linking its detail entry, one section per group with
an entry per item (what is missing and why it matters, present tense, and a link to the design section that found or
deferred it), and Deferred last (intent, not work: About panel, LV2, the extension model, savestate slots, a web port,
`MemoryType` staying closed, one Mesen library, and the standing note that `deferredProject` is a domain term; the
`kind: "deferred"` half is gone from the code). The ticked lines, the phase narratives and every finished "What it
leaves" bullet go (the ledger's 132 `done` rows cite what finished each); the accepted limits went to the spec docs in
tasks 3 and 4. Before merge gains the line the ledger found owed: a green CI run of the tip (run 43131974297 built
ea2517dc9, and nothing after it has run on a runner).

Where no design doc records an item's analysis, its entry carries what spec/07's section 5 said, checked against the
code: the JACK standalone's control handoff, the dead `routeBlockInto` and `OffsetLookup.hpp`, the test names still
saying "the kernel", `retroplug-host` swallowing a top-level throw, the render residuals, and the script node `Block`'s
missing resource accessor. (`CountingAllocator`'s Windows hazard, on the ledger agent's list, is a `done` row: CI's
windows job ran the counting binaries green, so it has no entry.)

Three departures from design section 3, all noted in the report: an entry is a `###` heading rather than a bold title,
because only a heading gives the checklist line an anchor to link; Deferred has no checklist lines, as the design's
group list for the checklist does not name it; and the file is 813 lines, not 250 to 350, because the ledger found 245
open rows (each with a probe the file must carry) and the entries the design wanted kept whole.

Every reference to spec/07 is repointed or cut (`git grep -n -E 'spec/07|07-remaining|remaining-work'` outside docs/):
the comments naming done work now name the doc that describes it (the file watcher: `CMakeLists.txt`, `FileWatcher.hpp`,
`HostRpcService.hpp`, `NativeFileWatcher.hpp`, `HostRpcWatching.test.cpp`, `NativeFileWatcher.test.cpp` to spec/02 or
spec/03; the render-snapshot sweep, `HostRpcService.hpp`, `HostRpcWatching.test.cpp` and spec/02, to spec/11); two said
something no longer true (`lsdjSerialOutNode.ts`'s header had a cart the MI.MAP controller forces keep capturing, which
it has not since C2's `serialOutCaptureFor`; spec/02's press-queue line had a flood drop a release, which kb1 fixed);
the still-open ones link their entry (`PluginDSP.cpp`'s control-handoff comment, `EngineGraph.test.cpp`'s threaded-case
comment, `launchpad/ts/registry.ts`, spec/01, 02, 05, 06, 08, 09, 10 and 13); spec/02's and spec/13's links to "section
5" point at the entry and the group that replaced it; spec/README's and spec/00's descriptions of spec/07 say what it
is now, and README's dead anchor into section 4 goes; spec/05's "rich per-system domains are tracked" names no entry,
so it is cut. spec/04's line 503 (the whole file as the inventory) and spec/README's line 55 stay as they are.

**Files:**

- Rewrite: `spec/07-remaining-work.md` (1,181 lines to 813).
- Modify (comments only in code): `CMakeLists.txt`, `packages/launchpad/ts/registry.ts`,
  `packages/lsdj/ts/lsdjSerialOutNode.ts`, `packages/native/plugin/PluginDSP.cpp`,
  `packages/native/src/host/rpc/FileWatcher.hpp`, `packages/native/src/host/rpc/HostRpcService.hpp`,
  `packages/native/src/host/rpc/NativeFileWatcher.hpp`, `packages/native/test/engine-graph/EngineGraph.test.cpp`,
  `packages/native/test/host/HostRpcWatching.test.cpp`, `packages/native/test/watcher/NativeFileWatcher.test.cpp`,
  `spec/00-overview.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/05-data-persistence.md`,
  `spec/06-build-test.md`, `spec/08-profiling.md`, `spec/09-cli-debugging.md`, `spec/10-multichannel-audio-out.md`,
  `spec/13-subsystems.md`, `spec/README.md`.

**Ledger rows (task 6):** the 245 `open` rows and N005 to N008. Once this task lands the check also holds spec/07 to
no ticked line and every checklist line linking an entry that exists.

### Steps

- [ ] **Step 1: run the check and watch it fail**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 6 --max-failures 3
```

Expected: exit 1. Most `open` probes come from spec/07 itself and pass; the corrected and new ones fail, and so does
the shape (the old file's 13 ticked lines, and checklist lines that link nothing):

```
check.mjs: tasks 1..6 (38 files)
  (spec/07: 39 checklist lines, 1181 lines)
FAIL probes at their destination (keep / merge / open / new): 624 passed, 8 failed
       S009: probe "ea2517dc9" not in spec/07-remaining-work.md
       S086: probe "steps 1–7" not in spec/07-remaining-work.md
       S097: probe "PluginDSP.cpp:331" not in spec/07-remaining-work.md
       ... and 5 more
ok   dup quotes in the doc they name: 603 passed, 0 failed
ok   relative links and anchors in touched markdown files: 1245 passed, 0 failed
ok   AGENTS.md is 200 lines or fewer and carries every keep / new probe of its own: 3 passed, 0 failed
FAIL spec/07: no ticked line, every checklist line links to its detail entry: 0 passed, 52 failed
       spec/07-remaining-work.md:30: a ticked line
       spec/07-remaining-work.md:33: a ticked line
       spec/07-remaining-work.md:34: a ticked line
       ... and 49 more
ok   no em dash in an added line: 757 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 757 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 1 passed, 0 failed
ok   no changed file outside the plan's list: 22 passed, 0 failed
60 failure(s)
```

- [ ] **Step 2: rewrite spec/07 and repoint its references**

The whole of `spec/07-remaining-work.md`:

````markdown file=spec/07-remaining-work.md
# 07 - Remaining work

This file lists open work only. The commit that finishes an item deletes it, its checklist line and its detail entry
together: never tick an item and leave it. The design docs and the git log are the history, and nothing here records
what was done. Each entry says what is missing and why it matters, and links the design section that found or
deferred it; another spec doc names a gap in a line and links its entry here. Deferred, last, records intent rather
than work.

## Checklist

**Before merge**
- [ ] [A green CI run of the tip](#a-green-ci-run-of-the-tip)
- [ ] [`pnpm test:n8` in CI and in test.bat](#pnpm-testn8-in-ci-and-in-testbat)
- [ ] [The Mesen compile-out proof in CI](#the-mesen-compile-out-proof-in-ci)
- [ ] [The Launchpad real-device checklist](#the-launchpad-real-device-checklist)
- [ ] [The Launchpad's Windows items on a real device](#the-launchpads-windows-items-on-a-real-device)
- [ ] [The overlay strip placement on a real host](#the-overlay-strip-placement-on-a-real-host)
- [ ] [The `RP_NATIVE_USB` box image on hardware](#the-rp_native_usb-box-image-on-hardware)
- [ ] [The format-truncation warning at PluginDSP.cpp:177](#the-format-truncation-warning-at-plugindspcpp177)
- [ ] [The console test binaries onto `/W4 /WX`](#the-console-test-binaries-onto-w4-wx)
- [ ] [A `release.yml` run](#a-releaseyml-run)

**Package split**
- [ ] [A real component view on a host](#a-real-component-view-on-a-host)
- [ ] [A UI kit compile](#a-ui-kit-compile)
- [ ] [Gamepad axis tokens as action bindings](#gamepad-axis-tokens-as-action-bindings)
- [ ] [Downloadable packs](#downloadable-packs)
- [ ] [The app loads no script pack](#the-app-loads-no-script-pack)
- [ ] [The template gaps a second pack meets](#the-template-gaps-a-second-pack-meets)
- [ ] [Pack-boundary exceptions 2 and 3](#pack-boundary-exceptions-2-and-3)
- [ ] [The view reopen key's width](#the-view-reopen-keys-width)
- [ ] [One owner for the `rp.midi` family](#one-owner-for-the-rpmidi-family)
- [ ] [A config section paired with its UI service](#a-config-section-paired-with-its-ui-service)
- [ ] [F5a's other leftovers](#f5as-other-leftovers)

**Known bugs**
- [ ] [A tracker marker with no protocol node](#a-tracker-marker-with-no-protocol-node)
- [ ] [The JACK standalone's control handoff](#the-jack-standalones-control-handoff)
- [ ] [A cart-node handover can cut a key](#a-cart-node-handover-can-cut-a-key)
- [ ] [The N8 menu, unverified on a cart](#the-n8-menu-unverified-on-a-cart)
- [ ] [The stripper bundle outside the repository](#the-stripper-bundle-outside-the-repository)
- [ ] [A bar or beat quantise counts host bars](#a-bar-or-beat-quantise-counts-host-bars)
- [ ] [MidiStream::send blocks the audio callback](#midistreamsend-blocks-the-audio-callback)
- [ ] [A MIDI pick dirties the untitled project](#a-midi-pick-dirties-the-untitled-project)
- [ ] [Settings > MIDI misses a claim made while open](#settings--midi-misses-a-claim-made-while-open)
- [ ] [The audio draft's sibling-backend cache](#the-audio-drafts-sibling-backend-cache)
- [ ] [An Apply past a read-only config](#an-apply-past-a-read-only-config)

**Engine**
- [ ] [DAW automation of node params](#daw-automation-of-node-params)
- [ ] [P3: cores borrow their pinned ROM](#p3-cores-borrow-their-pinned-rom)
- [ ] [P4: the snapshot registry's double copy](#p4-the-snapshot-registrys-double-copy)
- [ ] [P7: the rest of the capability split](#p7-the-rest-of-the-capability-split)
- [ ] [Allocations left on the block thread](#allocations-left-on-the-block-thread)
- [ ] [Input-drop counters beside the graph's](#input-drop-counters-beside-the-graphs)
- [ ] [The host MIDI-in staging](#the-host-midi-in-staging)
- [ ] [Script packs: isolation, reloads, refusals, memory](#script-packs-isolation-reloads-refusals-memory)
- [ ] [Dead code and the kernel test names](#dead-code-and-the-kernel-test-names)
- [ ] [The tap's mode bound](#the-taps-mode-bound)
- [ ] [A narrow host still arms the tap](#a-narrow-host-still-arms-the-tap)
- [ ] [Threaded engine cases are smoke runs](#threaded-engine-cases-are-smoke-runs)
- [ ] [A plugin with its editor closed has no idle reclaim](#a-plugin-with-its-editor-closed-has-no-idle-reclaim)
- [ ] [The script node Block has no resource accessor](#the-script-node-block-has-no-resource-accessor)
- [ ] [The command ring and the adoption wait](#the-command-ring-and-the-adoption-wait)
- [ ] [Serial capacities past large blocks](#serial-capacities-past-large-blocks)
- [ ] [A feedback source's capacity change](#a-feedback-sources-capacity-change)
- [ ] [A custom reflector in the device decode](#a-custom-reflector-in-the-device-decode)

**Renders and kits**
- [ ] [A plugin kit row](#a-plugin-kit-row)
- [ ] [The kit patcher's latency budget](#the-kit-patchers-latency-budget)
- [ ] [CI sees 5 of 13 render cases](#ci-sees-5-of-13-render-cases)
- [ ] [A GBA render case](#a-gba-render-case)
- [ ] [A sanitizer leg for renders](#a-sanitizer-leg-for-renders)
- [ ] [retroplug-host swallows a top-level throw](#retroplug-host-swallows-a-top-level-throw)
- [ ] [Render residuals](#render-residuals)
- [ ] [The behavioural render tests run in-process](#the-behavioural-render-tests-run-in-process)
- [ ] [The statics patch's camera half has no test](#the-statics-patchs-camera-half-has-no-test)

**Pico**
- [ ] [Audio on the box](#audio-on-the-box)
- [ ] [Live plan upload](#live-plan-upload)
- [ ] [A controller path on the box](#a-controller-path-on-the-box)
- [ ] [A 32-bit runner in the build](#a-32-bit-runner-in-the-build)
- [ ] [The box's stats print can stall a message](#the-boxs-stats-print-can-stall-a-message)
- [ ] [A SysEx past 3 bytes allocates](#a-sysex-past-3-bytes-allocates)

**Feature gaps and SDL**
- [ ] [Raw LSDj Keyboard sync mode](#raw-lsdj-keyboard-sync-mode)
- [ ] [NES per-mapper expansion sub-channels](#nes-per-mapper-expansion-sub-channels)
- [ ] [The standalone reopens the last project](#the-standalone-reopens-the-last-project)
- [ ] [Live memory-region subscription](#live-memory-region-subscription)
- [ ] [MIDI out past 4 bytes](#midi-out-past-4-bytes)
- [ ] [Mesen .mlb labels in the CLI debugger](#mesen-mlb-labels-in-the-cli-debugger)
- [ ] [SDL host MIDI out at block time](#sdl-host-midi-out-at-block-time)
- [ ] [SDL MIDI-in at frame 0](#sdl-midi-in-at-frame-0)
- [ ] [Per-device MIDI routing](#per-device-midi-routing)
- [ ] [The handheld-perf rebase and the muOS run](#the-handheld-perf-rebase-and-the-muos-run)
- [ ] [The n8-bridge sessions on a real N8](#the-n8-bridge-sessions-on-a-real-n8)
- [ ] [SDL on a real Windows machine](#sdl-on-a-real-windows-machine)
- [ ] [macOS on a real machine](#macos-on-a-real-machine)
- [ ] [Device hotplug and MIDI clock phase lock](#device-hotplug-and-midi-clock-phase-lock)
- [ ] [A really removed audio device](#a-really-removed-audio-device)
- [ ] [SDL's PipeWire node for a wide request](#sdls-pipewire-node-for-a-wide-request)
- [ ] [The PipeWire data loop's RT pin](#the-pipewire-data-loops-rt-pin)
- [ ] [No DAW check records LSDj's MIDI out](#no-daw-check-records-lsdjs-midi-out)
- [ ] [A plain-ROM controller target](#a-plain-rom-controller-target)
- [ ] [DAW MIDI to a cart with no NES system](#daw-midi-to-a-cart-with-no-nes-system)

## Before merge

What `feature/modularize` owes before it merges to main.

### A green CI run of the tip

The last green run of `build.yml` (run 43131974297, all four jobs: windows through `build.bat` at `/W4 /WX` and
`test.bat`, macos, linux with `device:check` and `device:box`, linux-arm64) built ea2517dc9; nothing after it (the
known-bugs batches and the docs) has run on a runner. CI runs by hand (`workflow_dispatch`), so the user starts it. ([F2
design](../docs/modular-graph-phase-f2-design.md) section 12.)

### `pnpm test:n8` in CI and in test.bat

`pnpm test:n8` is no CI step and not in `test.bat`: the N8 pack's Edio golden, the facet's sessions and the serial
port's exclusivity case (`retroplug-n8-test`, `retroplug-serial-test`) run only by hand. ([E2
design](../docs/modular-graph-phase-e2-design.md) section 13.8.)

### The Mesen compile-out proof in CI

No CI job runs `tools/build-mesen-all-consoles.sh` in either mode. A missed compile-out guard links in Release and fails
only at `-O0`, which `tools/build-mesen-all-consoles.sh --off` builds, so both modes are owed by hand on every Mesen
change until a job runs `--off`. ([F2 design](../docs/modular-graph-phase-f2-design.md) sections 11 and 12.)

### The Launchpad real-device checklist

Design section 8's ten steps, about ten minutes on the desk with the Pro MK3 and LSDj in a ROM, have never run; they
cover the claim and the scan on a real device, which the ALSA fakes stand in for. ([F3
design](../docs/modular-graph-phase-f3-design.md) section 8.)

### The Launchpad's Windows items on a real device

CI's windows job builds and unit-tests the pack but touches no device, so the Windows items are owed on hardware: an
input's exclusivity (a scan's held edge suspends the shared stream's inputs), the unshared `CreateFile` claim (a second
process refused), and the writer thread under WinMM (a bulk repaint does not freeze the UI frame). The held edge is
untested in place: `suspend_hardware_inputs` is a real lever under `_WIN32` alone (`scanSuspendsInputs` in
[PortFilter.hpp](../packages/midi/native/src/midi/PortFilter.hpp); an ALSA sequencer input and a CoreMIDI endpoint are
shared), and the ALSA legs assert only that it does nothing there. `ControllerLink::disconnect()` under WinMM belongs to
the same run: a claim, a release or the provider's stop runs it on the UI thread, which joins a writer that may be
mid-drain of up to 32 messages from the send ring plus the 16 the UI queue holds (about 170 ms each under WinMM), then
flushes the ring and writes the close bytes itself. A swap cannot dodge the wait, since the old pair's ports must be
closed before the new pair opens; measure it with a full repaint queued and, if it is too long, hand the farewell to the
writer. ([F3 design](../docs/modular-graph-phase-f3-design.md) sections 8 and 12.)

### The overlay strip placement on a real host

F4c's overlay strip (edge E4: its z-order against the focus border and the render badge) has been seen only in the
headless UI host. ([F4c design](../docs/modular-graph-phase-f4c-design.md) section 13.)

### The `RP_NATIVE_USB` box image on hardware

`RP_NATIVE_USB` has never run on hardware: the image (af36f223 at E4) is still unflashed, built under the link trap
only. The Pico-PIO-USB image is the one the rig runs. ([E3 design](../docs/modular-graph-phase-e3-design.md) section
12.8, [E4 design](../docs/modular-graph-phase-e4-design.md) sections 11.7 and 11.8.)

### The format-truncation warning at PluginDSP.cpp:177

A cold GCC build warns `-Wformat-truncation` at `packages/native/plugin/PluginDSP.cpp:177`, the audio-port naming code
(`snprintf` of `"out_%u_%c"` into a 16-byte `symBuf`, whose worst case is 17). It predates F5a; fix it as its own
change. ([F5a design](../docs/modular-graph-phase-f5a-design.md) section 14, finding F1; [F5b
design](../docs/modular-graph-phase-f5b-design.md) section 14.)

### The console test binaries onto `/W4 /WX`

`retroplug-sameboy-test` and `retroplug-mesen-test` are still left off the `/W4 /WX` list
([warnings.cmake](../packages/native/cmake/warnings.cmake): the first Windows run of the two was to decide). Windows now
runs them in `test:plugin`, and nobody has tried adding them. ([F2 design](../docs/modular-graph-phase-f2-design.md)
section 11.)

### A `release.yml` run

`release.yml` last ran on 2026-08-07 (v0.7.4), before F2, so its Windows job has never built `RETROPLUG_MESEN_LTO=ON`
with the three Mesen console options off, and no release job has built the branch. ([F2
design](../docs/modular-graph-phase-f2-design.md) section 11.)

## Package split

### A real component view on a host

The `component` view kind and its fixture ship, but no real `component` view runs on a host: only `packages/ui-fixture`,
listed by no product, calls `reactView`. A real `component` view comes with the first package that needs React for one,
proved on a host with the `test:ui` case that lands with it. ([F4c design](../docs/modular-graph-phase-f4c-design.md)
section 11.)

### A UI kit compile

The UI host offers no `kits` group ([uiHost.ts](../packages/retroplug/ui/host/uiHost.ts): the UI composes no audio
driver). A kit compile (`host.kits` in the UI host) is owed when a UI feature needs one. ([F4b
design](../docs/modular-graph-phase-f4b-design.md) section 10, [F4c](../docs/modular-graph-phase-f4c-design.md) section
11.)

### Gamepad axis tokens as action bindings

The gamepad-axis bus dispatches no app action, and the generator refuses a binding name that is not a button
(`GAMEPAD_BUTTON_NAMES`), so gamepad axis tokens as action bindings wait for a package that wants a stick-driven action.
([F4c design](../docs/modular-graph-phase-f4c-design.md) section 11.)

### Downloadable packs

Every pack is built in. The authoring loop, the pack format and downloads, a pack's UI half and unloading a pack (the
`Engine` has `loadScriptPack` and no unload) are what downloadable packs need (the C1 design's section 8). With them
come menus on extensions (a downloaded pack contributing menu rows, which packages in the tree do through `defineUi`)
and third-party controller apps: `controllerApps` registers from the product's package list, so an app from outside the
tree waits for this road. And a provider cannot be downloaded either, since it is reached only through the generated
static table. Whether a downloaded pack's bytecode is stripped is the format's to say: a pack compiled from source keeps
its debug information, about a quarter more memory than stripped code. ([C1
design](../docs/modular-graph-phase-c1-design.md) sections 8, 12 and 13, [E1](../docs/modular-graph-phase-e1-design.md)
section 10, [E2](../docs/modular-graph-phase-e2-design.md) section 13.8, [F3](../docs/modular-graph-phase-f3-design.md)
section 11, [F5a](../docs/modular-graph-phase-f5a-design.md) section 12.)

### The app loads no script pack

Nothing in the app loads a pack yet: `backend.loadScriptPack` exists on every host, but no non-test caller uses it, and
a render job's Engine has the built-in pack only (`RenderHost` loads none), so loading a pack into more than one Engine
is unbuilt too. ([C1 design](../docs/modular-graph-phase-c1-design.md) section 12.)

### The template gaps a second pack meets

The E2 template gaps remain: a pack cannot add a capability beyond `rp.system.memory/sram/rom` (the engine refuses an
unknown capability), and the test host has no seam for a pack's test provider
([main.cpp](../packages/native/src/main.cpp) calls the N8's and the Launchpad's factories by name). ([E2
design](../docs/modular-graph-phase-e2-design.md) section 13.8.)

### Pack-boundary exceptions 2 and 3

`plugin.cmake` links `retroplug-n8-desktop` by name, because the DSP library's own sources include the product's
providers header; and the test host's `main.cpp` names the provider (it includes `FakeN8.hpp` and calls
`rp::n8::createProvider` and `rp_n8_provider`), the same root as the missing test-provider seam. ([E2
design](../docs/modular-graph-phase-e2-design.md) section 13.6.)

### The view reopen key's width

F4c's shape question about the view reopen key is still open: whether the reopen key stays as wide as a role write (a
role write reopens a system's views, one visible case a blink;
[viewSession.ts](../packages/retroplug/ui/views/viewSession.ts)). ([F4c
design](../docs/modular-graph-phase-f4c-design.md) sections 10.4 and 11.)

### One owner for the `rp.midi` family

`core` owns `rp.midi.route` alone (exempt in the literal scanner through `--node-exempt`), the `midi` package every
other `rp.midi.*` type. Decide one owner before the per-device MIDI routing adds node types. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### A config section paired with its UI service

No check pairs a config section's key with the UI service that reads it, so a typo is silent: the generator pairs a
placer with a project section but checks config sections only against the shell's keys and duplicates (F5a's
pairing-check item, one more key). ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### F5a's other leftovers

The F5a design's section 12 lists what no other entry carries: a `ui` module may still import React (no rule); a
per-header device check for header-only packages, and a header-only package's device manifest form; the plugin list
carrying `n8-wire` is held by hand (`order()` cannot hold a native-only package); comment edits move the desktop's
bundle bytes; `Placement.wires` reads `PlaceSpec.launches` only; `retroplug-launchpad-test`'s mid-drain swap case is
load-sensitive; contribution checks run at load (a malformed pack takes the control plane down, and the controller
registry has no unregister); and a project service is tied to `@retroplug/tracker` and the editor's song watch. ([F5a
design](../docs/modular-graph-phase-f5a-design.md) sections 12 and 14.)

## Known bugs

### A tracker marker with no protocol node

`risa` and `bliptoaster` have no protocol node, so they count as placed whenever stored: a NES project storing both (a
risa project whose ROM is now BlipToaster) carries both markers, and `resolveTracker`
([trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts)) takes the first in product order. ([Known bugs
batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)

### The JACK standalone's control handoff

The DPF JACK standalone's control handoff can run on JACK's thread. A JACK buffer-size or sample-rate change runs
`PluginDSP::deactivate`, the change and then `activate` from DPF's notification callback (`DistrhoPluginJACK.cpp`'s
`jackBufferSizeCallback` / `jackSampleRateCallback` -> `setBufferSize(n, true)`), not on the main thread every other
control path of the plugin runs on, while the editor's `uiIdle` reclaims on that main thread every frame. So
`deactivate` hands the engine back on JACK's thread: `setAudioThreadOwns(false)` clears the ownership bit (every push
then flushes inline on whichever thread makes it), `drainInto` drains the command ring there, `reclaimReleased` takes
the release ring there, and the script host's reclaim runs a pack's GC and free there - a second consumer of the ring,
and two threads in `JS_RunGC` / `freePack` is undefined behaviour. `activate` can likewise grow the plan on that thread
when the new buffer exceeds the plan's maximum (`setMaxBlockFrames` -> `GraphHost::reactivate`, which replans and
publishes) and re-declare the DAW parameters there. The fix: marshal that whole handoff (the flush, the ownership flip,
the drain, the release-ring reclaim, the script GC and free, and activate's replan and parameter re-declaration) to the
control thread, or make a buffer-size or rate change a message that thread applies, so the quiescent engine has one
owner. Rare and manual: a JACK buffer-size change in the standalone; CLAP, VST3 and AU call both on the host's main
thread, and VST2 runs `setBufferSize(n, true)` from its block-size opcode on whichever thread its host calls that from
([PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp), above `activate`). Only `activate`'s `setMaxBlockFrames` is a
no-op for a block within the plan's maximum, which is why the path went unnoticed; C5's per-frame reclaim widened its
window from an edit to any frame. ([C5 design](../docs/modular-graph-phase-c5-design.md) section 10.)

### A cart-node handover can cut a key

A handover can cut a key the new cart node strikes before the old one is reclaimed: the old node's held notes are
released behind every byte pushed when its destroy runs, the new node's first blocks among them, so a note the new node
starts on a key the old one still held, between the swap and the reclaim, is released with them. Where an idle tick
reclaims that is a frame or so; in a plugin whose editor is closed it runs to the next publish. ([C5
design](../docs/modular-graph-phase-c5-design.md) section 11.)

### The N8 menu, unverified on a cart

Three behaviours of a cart at its menu are reasoned, not seen. MIDI streamed to a cart at its menu reaches the menu's
own command parser, which takes commands over the same FIFO, so a data pair such as `0x2A 0x73` (`*s`) can ask it to
start an app; it holds for raw MIDI after a connect at the menu and for framed MIDI after a console reset, where the
stale window is re-sent every 100 ms until it re-syncs. The quiet rule assumes the menu writes nothing unprompted on the
back-channel: unsolicited bytes would keep `lastHeardNs` fresh and a menu with stale frames would read as never quiet.
And a heavy stream into a menu can starve the probe: the window's 48-frame re-sync re-stamps the oldest frame's first
send, so past about 190 messages a second (48 per 250 ms) no turn is quiet and a submission is refused. ([C5
design](../docs/modular-graph-phase-c5-design.md) section 11.)

### The stripper bundle outside the repository

`retroplug-ts-stripper-bundle` fails in a build directory outside the repository:
[tools/build-ts-stripper.mjs](../tools/build-ts-stripper.mjs) runs in the binary directory and sets no esbuild
`absWorkingDir`, so its `typescript-5` alias resolves from there (unlike [tools/build-ui.js](../tools/build-ui.js),
which sets it to the repo root). ([B3a design](../docs/modular-graph-phase-b3a-design.md) section 10.)

### A bar or beat quantise counts host bars

A quantise of `bar` or `beat` still counts the host's bars: at a tempo divisor of 2 a cart's phrase lasts two of them.
It may be the behaviour wanted; decide, then fix it or state it in [spec/13](13-subsystems.md#the-controller-role).
([Known bugs batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)

### MidiStream::send blocks the audio callback

`MidiStream::send` runs every selected hardware output's send inline on the audio callback, so a WinMM bulk SysEx of
about 170 ms (a controller app's LED traffic) blocks the audio thread. The fix is a writer thread per hardware output,
as the Launchpad link has. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### A MIDI pick dirties the untitled project

A MIDI pick on the start screen dirties the empty untitled project (`ProjectStore.setSection` marks it dirty
unconditionally), so the close guard asks to save a project holding only the pick; exempt a section-only change to an
empty, pathless project. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 14.)

### Settings > MIDI misses a claim made while open

The row's reserved pair is a `describe()` snapshot, so Settings > MIDI's checklist does not notice a claim made while
its container is open; a cheap, no-probe `state()` read on `MidiDeviceHost` would fix it. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 14.)

### The audio draft's sibling-backend cache

The audio draft's sibling-backend cache is per backend change, not per visit
([audioDraft.ts](../packages/audio/ts/ui/audioDraft.ts) caches by the described backend). ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 14.)

### An Apply past a read-only config

`applyAudioDraft` ignores `setSection`'s answer, so an Apply still reaches the device when `setSection` refused a
read-only config. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 14.)

## Engine

### DAW automation of node params

The plugin maps DAW automation to its MIDI CC slots only: its parameters are not mapped onto `setParam`, though params
make DAW automation of node params possible. Its own work. ([C2 design](../docs/modular-graph-phase-c2-design.md)
section 9.)

### P3: cores borrow their pinned ROM

Each system holds one more full copy of its ROM: the `rom` resource a node pins for the system's life is on top of the
vector the build hands the core and Mesen's own copies, so a 32 MB GBA ROM is +32 MB per system, per plugin instance.
Phase C's P3, cores borrowing their node's pinned ROM bytes, would remove it; it needs a regenerated SameBoy patch, and
a Master System re-reads its staged file on reset. ([C3 design](../docs/modular-graph-phase-c3-design.md) section 11,
[C4](../docs/modular-graph-phase-c4-design.md) section 12.)

### P4: the snapshot registry's double copy

The read door copies from each core's own tear-free triple into a registry-owned buffer, because the shared `SystemBase`
cannot yet publish straight into the registry
([SnapshotRegistry.hpp:23](../packages/native/src/host/engine/SnapshotRegistry.hpp#L23)). P4 removes the snapshot
registry's double copy: a documented redundancy, not a bug, and a `SystemBase` refactor, not urgent. ([C4
design](../docs/modular-graph-phase-c4-design.md) section 12.)

### P7: the rest of the capability split

C5 built three capabilities (`rp.system.memory/1`, `.sram/1`, `.rom/1`). P7's remainder, the rest of the `SystemBase`
capability split: video, buttons and debug are still `SystemBase` virtuals (`framebuffer()`, `pressButton`,
`getCpuRegisters` / `readCpuByte` / `debugTarget`), and `state/1` is still the system-id RPC (`readState(id)`). F2 gave
each package one node file, not one node file per console: `MesenNode.cpp` holds all three Mesen consoles' nodes; after
F2 the split's remainder is section 10's list in its design. ([C4 design](../docs/modular-graph-phase-c4-design.md)
section 12, [C5](../docs/modular-graph-phase-c5-design.md) section 11, [F2](../docs/modular-graph-phase-f2-design.md)
section 10.)

### Allocations left on the block thread

A warmed block allocates nothing, but for work that makes something:
- A Mesen core's 0.5 s state publish allocates inside its streaming serializer, every 24 k frames at 48 kHz, so every
  `[alloc]` case on an Engine-built Mesen core charges a whole interval uncounted first.
- The N8 firmware's SD path still allocates, in the emulated cart (`std::string` and `std::filesystem` in its command
  handlers, a bigger directory listing), and its `_txQueue`'s unbounded growth when a ROM writes `CMD_USB_WR` and no
  host drains it is the other half; bounding it would change the "never drop a byte the device would deliver" contract.
  No block test drives an SD command, so the SD path's allocations are ROM-dependent. The same queue is a `std::vector`
  the debug `drainCoreBytes` RPC swaps empty, so a non-link ROM's next write after a drain grows it.
- A Game Boy's `serialOutLog_` is cleared each block but grows to the largest block's capture while MI.OUT capture is
  armed (off the steady-state path the `[alloc]` cases hold).
- `rp.host.midi-out` appends a `std::vector` per message to the engine's host output (`Engine::midiOut_`, the
  `rp.engine.host-io/3` service): an allocation for every message out; a flat arena, as the staging has, would remove
  the vector per message.
- A retained generation of a script pack is freed only at a reclaim (see the closed-editor entry below).

([B3a design](../docs/modular-graph-phase-b3a-design.md) section 10, [C4](../docs/modular-graph-phase-c4-design.md)
section 12, [C5](../docs/modular-graph-phase-c5-design.md) section 11.)

### Input-drop counters beside the graph's

The cores' input-drop counters are not surfaced beside the graph's: each core's `inputAccepted()` / `inputDropped()` and
the FIFO's `coreTransportStats().droppedBytes` have no host caller, and `rp_process` carries no instance-drop pointer,
so an input-drop service on the node seam is its own change. ([C4 design](../docs/modular-graph-phase-c4-design.md)
section 12.)

### The host MIDI-in staging

`rp.host.midi-in`'s sort merges two frame runs in linear time (what real plugin hosts stage: the DAW's events, then the
automation CCs at frame 0); three or more runs fall back to an in-place insertion sort, quadratic, on the block thread.
The staging has no count cap either (past its reserved room it grows), so a host staging many messages out of order pays
for every one, though the input node's queue keeps 2048. A cap at what the input node carries, or a k-run merge, would
bound it ([Engine.cpp](../packages/native/src/host/engine/Engine.cpp) `Staging::takeSortedInto`). ([B3a
design](../docs/modular-graph-phase-b3a-design.md) sections 3 and 3.4.)

### Script packs: isolation, reloads, refusals, memory

What the script packs leave, from isolation inside a pack to a re-plan's two instances:

- **Isolation inside a pack.** Nodes of one pack share its realm, so one node's code can change what another's reads
  through (an accessor on `Object.prototype` or `Array.prototype`, a global, a definition in `__rp_scriptNodes`).
  Freezing a pack's intrinsics after its load (an SES-style lockdown) would close it, at the price of code that writes
  over an inherited property.
- **A node's state across a reload.** A reloaded pack's nodes restart clean; a `snapshot` / `restore` pair would carry
  it.
- **A failing node refusing the whole document.** A node whose settings check fails refuses every document placing it,
  so no other edit lands while it is placed. A per-node "invalid" state, the document planning without the node, is a
  choice for the authoring work.
- **A pack's memory limit counts garbage.** An allocation past the limit is refused with no collection first, so a pack
  whose reachable heap is above about two thirds of its limit can be faulted for garbage: the limit ends up counting
  garbage as well as reachable bytes.
- **A re-plan holds two instances.** On the desktop's MicroQuickJS host a re-plan holds two instances of a pack at once
  (contexts are 4x `heapBytes` with a 1 MiB floor, and a dead pack is revived), so a re-plan behind a large source can
  still run a pack out until the replaced instance is reclaimed.

([C1 design](../docs/modular-graph-phase-c1-design.md) section 12, [E4](../docs/modular-graph-phase-e4-design.md)
section 11.8.)

### Dead code and the kernel test names

- `routeBlockInto` in [midiRouting.ts](../packages/core/ts/midiRouting.ts) has had no caller since B3b: the routing node
  routes each message with `routeTarget`, the same arithmetic, and `routeBlock`, its pure twin, stays as the reference
  the routing node's test holds every mode to. It can be deleted.
- `packages/native/src/lsdj/OffsetLookup.hpp` is dead (nothing includes it); `src/lsdj/` is otherwise test-reference
  code.
- The vendored `deps/enkiTS` is tracked but neither built nor linked; a cleanup decides whether it goes.
- Some test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`, `dsp-sms-sync` and
  `plugin-controlplane`) where they mean the protocol nodes; B3b kept every test identity it did not have to change.

([B3b design](../docs/modular-graph-phase-b3b-design.md) sections 1 and 7,
[D2](../docs/modular-graph-phase-d2-design.md) sections 9 and 11.)

### The tap's mode bound

The tap's 1-to-3 mode bound is the engine's, not the capability's: `Engine::setGraph` still validates 1 to 3 (the NES
numbering), a number TS owns. `maxMode()` on `ChannelTap`, which declares only `exportMode` / `setExportMode`, is the
later move. ([C4 design](../docs/modular-graph-phase-c4-design.md) section 12.)

### A narrow host still arms the tap

A split routing on a host too narrow for it still arms the tap, paying for the capture, its resamplers and the per-block
discard. Not arming it is a CPU-only change `buildGraph` could make from the lane budget (`tapArmOf` checks one system,
a split and a tappable core). ([C4 design](../docs/modular-graph-phase-c4-design.md) section 10.)

### Threaded engine cases are smoke runs

The engine-level threaded cases are smoke runs, not race detectors: `EngineGraph.test.cpp`'s case of a system joining
and leaving beside an audio thread, and the GBA beside a publishing Game Boy, passed against a registry whose claim
freed a buffer the publish wrote, while the registry-level cases caught it. Why the GBA case stays silent under the
sanitizers is unproven: "an Engine hides the race" (a happens-before edge in `setGraph`) is a reading, not a finding.
They are weak race detectors until that is understood. ([D2 design](../docs/modular-graph-phase-d2-design.md) section
11.)

### A plugin with its editor closed has no idle reclaim

A plugin whose editor is closed has no idle reclaim (DPF gives a plugin no idle callback), so a plan a DAW's `setState`
replaced while the transport plays keeps its nodes, a cart node's held notes, a core and their pinned resources until
the next publish, the editor opening or a deactivate. ([C5 design](../docs/modular-graph-phase-c5-design.md) section
11.)

### The script node Block has no resource accessor

The script node `Block` ([defineNode.ts](../packages/core/ts/defineNode.ts)) has no resource accessor and no link peers,
though a `PortSpec` can declare resource and link ports. ([B3a design](../docs/modular-graph-phase-b3a-design.md)
section 2.3.)

### The command ring and the adoption wait

Commits to script nodes never make `setGraph` wait (their prepare is stateless), so the GraphHost command ring (256
slots, 255 usable, shared with params and the engine's commands) bounds edits while no block runs: each edit queues a
plan holding its buffers, and a full ring answers busy; the publisher keeps its model, so the next publish lands the
latest. Coalescing queued, unadopted plans is the alternative, should it bite. The adoption wait is global: one commit
to a type without `RP_TYPE_STATELESS_PREPARE` holds back an edit touching only stateless nodes (a gate per instance is
possible), and every `node.hpp` node without `needs_rebuild` already prepares statelessly, so for C++ nodes the flag
could be derived. ([B3a design](../docs/modular-graph-phase-b3a-design.md) sections 2.4 and 10.)

### Serial capacities past large blocks

A Game Boy clocking its own serial port (a CGB on its fast clock) can send more in a block past about 12000 frames (6000
in double speed) than its fixed `serialOut` holds; the rest is dropped and counted (`kSerialOutCapacity` in
[SameBoyNode.cpp](../packages/sameboy/native/src/system/sameboy/SameBoyNode.cpp)). `rp.lsdj.serial-out`'s `midiOut`
(`MIDI_OUT` in [ports.ts](../packages/core/ts/ports.ts), 1024 messages and 8192 bytes) is sized for a decoded MI.OUT
stream at its densest up to 8192-frame blocks, while `serialOut` is sized for 65536; in a larger block the rest is
dropped and counted. ([B3a design](../docs/modular-graph-phase-b3a-design.md) sections 3.2 and 4.1.)

### A feedback source's capacity change

A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front: the block in flight
is lost and `droppedMessages` never counts it, since the carry and its count run only while quiescent
([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). Nothing triggers it today. ([B3a
design](../docs/modular-graph-phase-b3a-design.md) section 2.6.)

### A custom reflector in the device decode

A custom `rfl::Reflector<E>` is not seen by the device decode; `rfl::internal::has_read_reflector<E>` would refuse it in
one line. No node has one. ([E1 design](../docs/modular-graph-phase-e1-design.md) section 12.3.)

## Renders and kits

### A plugin kit row

A plugin kit row is possible and not built: no UI row compiles kits in the plugin. It would call the cart's `kitFormat`,
found through the shell's `kitFormat(id)` registry, with the stores' backend as its job runner, not
`AudioDriver.compileKitFormat`, whose native wait would block the UI frame. It should also lower the ceiling of the 8
offline workers each Engine parks for life once it has compiled anything (up to 8 normal-priority offline workers, so N
instances in a DAW hold 8N threads), or retire idle ones. ([D2 design](../docs/modular-graph-phase-d2-design.md) section
9.)

### The kit patcher's latency budget

Interactive kit patching misses the kit patcher's latency budget, a knob change heard almost at once: the TypeScript
compile costs about 23 ms typical and 83 ms worst for a 15-slot kit (native was 1.7 and 4.9 ms), nearly all of it
buffers crossing the JS thread between passes. When a patcher UI lands, chain a slot's passes inside one job or keep the
intermediates native behind handles, and recompile only the slot a knob touched. ([D2
design](../docs/modular-graph-phase-d2-design.md) section 9.)

### CI sees 5 of 13 render cases

CI sees 5 of 13 render-baseline cases (mGB as a Super Game Boy and four smsggdj songs); the LSDj and risa cases need
fixtures, and nothing of a NES, a split render or a Game Boy's stems runs there. MIDI-driven renders (an mGB or
BlipToaster render fed a MIDI file) would close it and are not built. ([D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### A GBA render case

A GBA render case is missing: without a BIOS the repo's GBA ROM renders exact silence. ([D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### A sanitizer leg for renders

A sanitizer leg for renders is missing: `tools/run-sanitizer.sh` has no render leg, so the pool's thread and address
checks are `[engine-jobs]` built in `build-tsan` / `build-asan` by hand. ([D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### retroplug-host swallows a top-level throw

`retroplug-host` swallows a top-level throw, reporting only "tests did not complete (tjs.exit never called)"
([main.cpp](../packages/native/src/main.cpp)), which hides a fixture script's real error; it should print a top-level
throw. Not render-specific, but the renders' fixtures meet it. ([D3 design](../docs/modular-graph-phase-d3-design.md)
section 10.)

### Render residuals

The D3 design's residual list, extended after its final review:
- An auto-detect render's length is quantised to its 100 ms poll, and acts between the library's calls land at the next
  block boundary; block-granular probes or an autonomous recipe would remove both (D3 section 8).
- An interrupt that lands in a pending promise job is also printed as a script error before the loop re-checks
  `interrupted_` (`RenderHost.cpp`, the job loop's `r < 0` branch); only a script working in promise jobs reaches it.
- An interrupt after the script reported `done` keeps its `outputs` under `cancelled` (only `message` is cleared).
- A failed `.part` sync reads as a failed move in `render.ts`'s error ("could not replace X"), pointing at a locked
  target rather than a disk error.
- `foldsCase` trusts a single probe (the first entry with a cased letter), so a broken symlink or a non-simple case swap
  reads a folding filesystem as case-sensitive.
- A battery cart whose live SRAM reads null still boots the sibling `.sav` silently in `ui/host/render.ts` (it sets the
  spec's `sav` only when `readSram` answers); refusing is open.
- Wedged jobs: a script missing its cancel is interrupted after `RenderHost::kCancelGrace` (2 s), but a core wedged in
  one native call hangs the CLI and an instance's teardown.
- Cores print straight to the process: Mesen's load log goes to fd 1 while the job's console lines arrive through the
  drain, so in the CLI the two can interleave differently.

([D3 design](../docs/modular-graph-phase-d3-design.md) sections 8 and 10.)

### The behavioural render tests run in-process

Some behavioural render tests (`risa-render`, `sms-render`, and the pure tier's `start-truncation` and
`risa-autodetect`) call `runRenderJob` on an exact-blocks engine, and `audio-render` and `render-guard` drive the audio
driver's renders in-process: paths no production caller uses. Their assertions reach engine jobs only through the render
baseline and `engine-run`. ([D3 design](../docs/modular-graph-phase-d3-design.md) section 10.)

### The statics patch's camera half has no test

The SameBoy camera half of `cmake/patches/sameboy-process-statics.patch` has no red-green test: there is no Game Boy
Camera ROM in the repo or the fixtures. ([D3 design](../docs/modular-graph-phase-d3-design.md) section 10.)

## Pico

### Audio on the box

The box has no audio: a codec and its DMA driver, a fixed-cadence block driver and the float tolerance against the
desktop are unbuilt (`pico/host` has only `ElapsedDriver`), and a time-reading plan on the elapsed driver loses the time
its carry bound skips under load. A fixed cadence cannot run from the box's main loop, which blocks for every N8 call (5
ms a FIFO write, up to about 200 ms draining a full ring, 2.5 ms a sniff or health read, 13.7 ms a `[stats]` line), so
blocks move off it; then the staging and the cart link's ring become single-producer / single-consumer (or the N8 calls
non-blocking), and `frameOf` moves onto `BlockDriver`. ([E3 design](../docs/modular-graph-phase-e3-design.md) sections
10 and 12.8.)

### Live plan upload

The loader trusts the blob, so a live plan upload needs a per-node port signature carried in the blob and checked at
load; the same check would replace `loop.sh`'s guard against a stale `retroplug-export-plan` (a blob from an exporter
built before a port changed loads and reads the wrong views). A pack in a plan that is not built in also needs a trust
model for bytecode MicroQuickJS does not verify. No plan file on the SD card and no resources on the device exist yet.
([E3 design](../docs/modular-graph-phase-e3-design.md) sections 10 and 12.8,
[E4](../docs/modular-graph-phase-e4-design.md) section 9.)

### A controller path on the box

The Pico has no controller path: the box's one root port is the N8's, `UartMidi` is input only, and there is no Game Boy
on the box for MI.MAP to drive. The launchpad core alone needs at least 61440 bytes (budget 76800), past the box's 64
KiB arena, and the bridge with MI.MAP at least 144960 (budget 184320). The pack declares no `script` member: its
scenarios prove the core runs on MicroQuickJS, and making it a device pack (bytecode at both widths, a `heapBytes`) is
the Pico controller's work. ([F3 design](../docs/modular-graph-phase-f3-design.md) sections 6, 11 and 12.)

### A 32-bit runner in the build

No 32-bit runner is in the build: the 32-bit bytecode runs nowhere in CI, so the 3/2 sizing of a 64-bit device build is
a heuristic, and the box's boot line and the device check's policy case are the authority. ([E4
design](../docs/modular-graph-phase-e4-design.md) sections 11.6 and 11.8.)

### The box's stats print can stall a message

The box's `[stats]` print can still stall a message for ~13.7 ms, as E2's did; a ring console would cost about 100 us at
every message's worst, so it is recorded, not fixed. ([E3 design](../docs/modular-graph-phase-e3-design.md) section
12.8.)

### A SysEx past 3 bytes allocates

A SysEx past 3 bytes allocates a view per message (dying with its block), and at 32 bits a non-whole number read
allocates, so a SysEx stream makes the box collect in the MIDI path; the policy's worst block fits with 4 KB spare. ([E4
design](../docs/modular-graph-phase-e4-design.md) section 11.8.)

## Feature gaps and SDL

Things a user could reasonably expect that are not wired. None block shipping.

### Raw LSDj Keyboard sync mode

`lsdj-sync` runs every mode but raw Keyboard (`keyboard`, LsdjSyncMode 4): MidiSync, MidiSyncArduinoboy, MidiMap,
KeyboardMidi and MidiPassthrough in [lsdjSyncNode.ts](../packages/lsdj/ts/lsdjSyncNode.ts), MidiOut (ArduinoboyMaster)
and MasterSync in [lsdjSerialOutNode.ts](../packages/lsdj/ts/lsdjSerialOutNode.ts), the role's second node, after its
Game Boy. Raw Keyboard sends nothing because it needs a per-block **`keys` feed**: `rp.lsdj.sync` has no `keys` input
and no host stages any. A `keys` input on the node and a host I/O node that feeds it unblock Raw LSDj Keyboard sync.
([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md#not-yet-built--deferred).)

### NES per-mapper expansion sub-channels

Per-console channel output ships end to end ([10-multichannel-audio-out.md](10-multichannel-audio-out.md), steps 1–7).
The open piece is the per-mapper expansion voices (VRC6 pulse and saw, VRC7's six FM channels, N163, MMC5, FDS, S5B),
which live inside each mapper's audio class before they sum into the chip's `AudioChannel` delta, so they need a
per-mapper audio tap deeper than the `NesSoundMixer` edit; VRC7's emu2413 core is the one large tap. Test material is
not the obstacle: `resources/roms/bliptoaster-{vrc6,vrc7,n163,mmc5,s5b}.nes` are committed, and `reaper:params-vrc7`
drives one. ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 3b.)

### The standalone reopens the last project

In a DAW the host chunk is authoritative. The standalone starts empty unless `RETROPLUG_AUTOLOAD_PROJECT` seeds it;
reopening the last project on launch from disk is not built.
([05-data-persistence.md](05-data-persistence.md#not-yet-built--deferred).)

### Live memory-region subscription

There is no live "watch RAM" streaming path: reads are one-shot through the `SnapshotRegistry` read door, and the
consumers that want live memory poll it, the LSDj overlay per frame and the HD player
([hdView.ts](../packages/lsdj/ts/ui/hdView.ts)) on a frame divider, because a 128 KiB WRAM copy plus a song decode is
too costly every frame. A cost question, not a capability one: an arming seam would let those two read a few hundred
bytes instead of copying the whole image. ([02-native-host.md](02-native-host.md#not-yet-built--deferred).)

### MIDI out past 4 bytes

MIDI in takes every size (`PluginDSP.cpp:331` reads `dataExt` when a message outgrows the inline `data[4]`), so a DAW
can upload an N163 wave the way the CLI does. The out path drops anything past 4 bytes: `PluginDSP.cpp:369` skips events
over `MidiEvent::kDataSize`. Lifting it is not one line: the plugin never sets `dataExt`, and DPF's VST3 writer reads it
for a size over `kDataSize`, so removing the guard is a null dereference. It also buys little: VST2 drops over 4 bytes,
CLAP drops over 3, VST3 drops SysEx regardless; only the JACK standalone gains. ([F3
design](../docs/modular-graph-phase-f3-design.md) section 11.)

### Mesen .mlb labels in the CLI debugger

The debug facet is built out ([09-cli-debugging.md](09-cli-debugging.md)); the one remaining item is Mesen's native
`.mlb` label files, which need a new parser (Mesen's C# one isn't vendored). A CLI-only nicety.

### SDL host MIDI out at block time

The SDL standalone sends host MIDI at block time: its RtMidi out takes no timestamp, so the audio callback sends each
block's `Engine::midiOut` as the block renders, its frames dropped, where the plugin hands the DAW each message at its
frame. Scheduling it is the standalone's own work. ([LSDj serial-out design](../docs/lsdj-serial-out-design.md) section
11.)

### SDL MIDI-in at frame 0

MIDI-in frame offsets: every SDL hardware MIDI message still stages at frame 0, up to a block of jitter. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### Per-device MIDI routing

Per-device MIDI routing, the user's next want: each port its own source and destination, `rp.host.midi-in` / `-out` per
port (so `rp.engine.host-io` takes a new version), `buildGraph`'s routing keyed by saved name, and the menus.
`MidiStream` merges every input into one ring today, so a staged message carries no port, and a per-port node needs a
node target in the `midi` package, whose only manifest item is a provider. The `midi` section is ready: each entry is an
object with room for a routing field, so adding one needs no version bump. The `rp.midi.*` node family and `MidiStream`
are the layer that follow-up extends. ([F5b design](../docs/modular-graph-phase-f5b-design.md) sections 12 and 14.)

### The handheld-perf rebase and the muOS run

The `feature/handheld-perf` rebase onto F5b: its render-ahead (4b9e39ea8) is `AudioHost`'s now, so its `main.cpp` hunk
reduces to the muOS launcher flags. Then the muOS device run that proves render-ahead (xRT, underruns, fps) at the
quantum the server locks, confirming the PortAudio [PipeWire] line. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) sections 12 and 14.)

### The n8-bridge sessions on a real N8

The n8-bridge sessions through the CLI's real RtMidi facet have not run on a real N8; only the headless host's
fake-backed facet is tested. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### SDL on a real Windows machine

CI builds and unit-tests the SDL packages on Windows but lists no real device: WinMM's port names for `portMatch`'s
table (rows unverified; two identical devices resolve to nothing; a refused exclusive input stays refused until a real
apply), where WASAPI Auto lands for PortAudio now that AutoConvert is set, and the send wait the output-set swap bounds
(a `MidiPort` destructor cannot wait for an in-flight WinMM callback). ([F5b
design](../docs/modular-graph-phase-f5b-design.md) sections 8, 12 and 14.)

### macOS on a real machine

CI compiles both audio backends on macOS and runs the unit tests; CoreMIDI and CoreAudio on a real machine, the
framework build and `sdl:smoke`'s legs are owed. ([F5b design](../docs/modular-graph-phase-f5b-design.md) sections 12
and 14.)

### Device hotplug and MIDI clock phase lock

There is no hotplug: a PortAudio device list is fixed at `Pa_Initialize`, and MIDI ports are read when asked. MIDI clock
phase lock is unbuilt too. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### A really removed audio device

`lost()` on a really removed device is unproved for both backends (the leg would be a `pw-cli destroy` of the open sink
mid-run, then the muted line), and a watchdog trip stays muted with no backoff retry. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12, R10.)

### SDL's PipeWire node for a wide request

SDL's PipeWire node has 2 output ports for a wide request while the host renders the requested width (R11, the cause not
established). ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### The PipeWire data loop's RT pin

The PipeWire data loop is still RT-pinned through rtkit's overrides: `AudioHost` applies `SCHED_FIFO` and a core pin to
any non-null device's callback thread; tune it only on ALSA, per the release list. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### No DAW check records LSDj's MIDI out

No DAW check records LSDj's host MIDI out (the plugin's `writeMidiEvent`); a Reaper leg that records the plugin's MIDI
out would close it. ([LSDj serial-out design](../docs/lsdj-serial-out-design.md) section 11.)

### A plain-ROM controller target

No row says a plain-ROM target has nothing to drive: a controller aimed at a system whose graph places no cart sync node
shows no `Cart: <app>` row at all; an info row would say why. ([Known bugs batch 2
design](../docs/known-bugs-batch-2-design.md) section 5.)

### DAW MIDI to a cart with no NES system

No project-level fallback forwards DAW MIDI to the real cart without a NES system: a card job addresses a node, and a
project with no system places none, so driving a MIDI cart with no NES in the project has no UI path. ([C5
design](../docs/modular-graph-phase-c5-design.md) section 11.)

## Deferred

Intentionally not planned; listed so the intent isn't lost. These are decisions, not work, so they have no checklist
line.

- **About panel**: dropped (it offered little); the one menu item still marked deferred in
  [menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts).
- **LV2**: deferred indefinitely; its out-of-process DSP/UI split does not fit RetroPlug.
- **The `ui?` render descriptor and a third-party extension model**: a role's own settings UI (the field is still on
  `RoleType`, unconsumed), and registering role types, ROM providers and behaviours from outside the tree, which
  [Downloadable packs](#downloadable-packs) would carry.
- **Savestate slots and a sav inspector**: never built; the state-snapshot machinery and the pure-TS sav codec exist, a
  multi-slot feature and a React sav view do not.
- **A web / Emscripten port**: design only ([modular-graph-design.md](../docs/modular-graph-design.md) section 11).
- **`MemoryType` stays a closed wire enum** of nine regions
  ([MemoryType.hpp](../packages/system/native/src/system/MemoryType.hpp)): a tenth changes the wire, the debug RPCs and
  the TS mirror together, and it is not a build seam (the closed wire enum, F2's item; [F2
  design](../docs/modular-graph-phase-f2-design.md) section 10).
- **One Mesen library for every product**, built with the root's options: a product that wants fewer consoles, or an
  embedder who wants SNES back, flips a cache variable (`tools/build-mesen-all-consoles.sh` proves the ON build); a
  library per console set is a later question the compile-out filter's table is written to make cheap ([F2
  design](../docs/modular-graph-phase-f2-design.md) section 10).
- **A standing note**: `deferredProject` in [systemsStore.ts](../packages/retroplug/src/systemsStore.ts) is the
  sibling-`.rplg` load-handoff concept, an intentional domain term, not incomplete work.
````

`CMakeLists.txt`:

````patch
diff --git a/CMakeLists.txt b/CMakeLists.txt
index ddc71f20f..7f7adf717 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -263,7 +263,7 @@ add_library(miniaudio INTERFACE)
 target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniaudio)
 
 # --- efsw: cross-platform filesystem watcher backing the native file watcher
-# (NativeFileWatcher → HostRpcService::drainChangedPaths, spec/07). EXCLUDE_FROM_ALL so only the
+# (NativeFileWatcher → HostRpcService::drainChangedPaths, spec/02). EXCLUDE_FROM_ALL so only the
 # static lib the plugin links (efsw-static) compiles — a build that never links it pays nothing.
 # efsw's own CMake links Threads::Threads to the shared lib only, so the one consumer of efsw-static adds it
 # itself (retroplug-file-watcher, which links both). BUILD_TEST_APP / EFSW_INSTALL default OFF as a subproject.
````

`packages/launchpad/ts/registry.ts`:

````patch
diff --git a/packages/launchpad/ts/registry.ts b/packages/launchpad/ts/registry.ts
index ef47fa7c7..a00dcb477 100644
--- a/packages/launchpad/ts/registry.ts
+++ b/packages/launchpad/ts/registry.ts
@@ -9,7 +9,7 @@
 // `protocol.kinds.controllerApps` entry (the node type, the mode and the polled value that counts as listening),
 // applied by this package's graph placer (control/placer.ts), so a new app needs no code in the shell.
 //
-// Loading user scripts at runtime stays the deferred extension model (spec/07).
+// Loading user scripts at runtime stays the deferred extension model (spec/07, "Downloadable packs").
 
 import type { RoleConfigSchema } from "@retroplug/core/systemRoles";
 import type { ConfigRow } from "@retroplug/ui/contribution";
````

`packages/lsdj/ts/lsdjSerialOutNode.ts`:

````patch
diff --git a/packages/lsdj/ts/lsdjSerialOutNode.ts b/packages/lsdj/ts/lsdjSerialOutNode.ts
index 3b905cb92..d154f94ca 100644
--- a/packages/lsdj/ts/lsdjSerialOutNode.ts
+++ b/packages/lsdj/ts/lsdjSerialOutNode.ts
@@ -7,13 +7,13 @@
 // byte, a silence the stop). Each message goes on `midiOut` at the frame the decoder gives it, a frame of this block,
 // so the host hears it where LSDj sent it.
 //
-// The other modes send nothing and read nothing of their input. It is usually empty, the core capturing for the two
-// MIDI-out modes alone, but not always: the capture follows the role's stored mode, so an LSDj stored in MI.OUT whose
-// cart the MI.MAP controller forces (to `midiMap`, or to `off`) keeps capturing in `drive` (spec/07). Left unread,
-// those bytes send nothing, which is right: the node decodes for the mode it runs, and a cart so forced is as silent on
-// host MIDI as one set to that mode. The node is placed whatever the mode, so a mode switch is a change of settings, as
-// it is for rp.lsdj.sync, and never of the document. Its settings are the role's config through the schema rp.lsdj.sync
-// takes too (lsdjSyncSettings.ts), the MI.MAP controller's forced mode included; it reads only `mode`.
+// The other modes send nothing and read nothing of their input, which is empty there: the core captures for the two
+// MIDI-out modes alone, and the capture follows the mode this node runs, the MI.MAP controller's forced one included
+// (the shell's serialOutCaptureFor, spec/04), so a cart the controller forces (to `midiMap`, or to `off`) captures
+// nothing and is as silent on host MIDI as one set to that mode. The node is placed whatever the mode, so a mode
+// switch is a change of settings, as it is for rp.lsdj.sync, and never of the document. Its settings are the role's
+// config through the schema rp.lsdj.sync takes too (lsdjSyncSettings.ts), the MI.MAP controller's forced mode
+// included; it reads only `mode`.
 //
 // MI.OUT's bit accumulator and pending command are kept on the instance across mode changes; master sync's state is
 // dropped whenever the mode is not master sync, so coming back opens a fresh run. Nothing is allocated in a warm
````

`packages/native/plugin/PluginDSP.cpp`:

````patch
diff --git a/packages/native/plugin/PluginDSP.cpp b/packages/native/plugin/PluginDSP.cpp
index a0b716dd0..ef8348ae2 100644
--- a/packages/native/plugin/PluginDSP.cpp
+++ b/packages/native/plugin/PluginDSP.cpp
@@ -281,8 +281,8 @@ protected:
     // jackSampleRateCallback -> setBufferSize(n, true)), and VST2 runs the same setBufferSize(n, true) from its own
     // block-size opcode, on whichever thread its host calls that from. On a thread beside the main one, this is not a
     // no-op to wave through: deactivate hands the engine back there (the ring's flush, the ownership flip, the drain,
-    // the release ring) with the editor's idle reclaiming and editing on this thread, which is spec/07's "control
-    // handoff on JACK's thread" residual, and only activate's setMaxBlockFrames is a no-op for a block within the
+    // the release ring) with the editor's idle reclaiming and editing on this thread, which is spec/07's "The JACK
+    // standalone's control handoff" known bug, and only activate's setMaxBlockFrames is a no-op for a block within the
     // plan's maximum - which is why that is the only part ever noticed. The hazard predates the graph engine. (The
     // output shape is no longer set here: the control plane builds the graph for the plugin's outputs at boot.)
     void activate() override {
````

`packages/native/src/host/rpc/FileWatcher.hpp`:

````patch
diff --git a/packages/native/src/host/rpc/FileWatcher.hpp b/packages/native/src/host/rpc/FileWatcher.hpp
index b5d92a227..ca041eaf0 100644
--- a/packages/native/src/host/rpc/FileWatcher.hpp
+++ b/packages/native/src/host/rpc/FileWatcher.hpp
@@ -3,7 +3,7 @@
 #include <string>
 #include <vector>
 
-// The file watcher a host hands HostRpcService (spec/07): the two calls the service forwards and nothing else, so the
+// The file watcher a host hands HostRpcService (spec/02): the two calls the service forwards and nothing else, so the
 // service, and every includer of its header, is clear of a watcher library's headers and of its link. The native one
 // (retroplug-file-watcher's NativeFileWatcher, over efsw) is what the plugin builds; a host that watches nothing passes
 // nothing, and drainChangedPaths then answers empty while setWatchedRoms answers false, as watching off always did.
````

`packages/native/src/host/rpc/HostRpcService.hpp`:

````patch
diff --git a/packages/native/src/host/rpc/HostRpcService.hpp b/packages/native/src/host/rpc/HostRpcService.hpp
index 25d7bb284..0dd86ef67 100644
--- a/packages/native/src/host/rpc/HostRpcService.hpp
+++ b/packages/native/src/host/rpc/HostRpcService.hpp
@@ -42,9 +42,9 @@ public:
     // How long ago `path` was written, in milliseconds, or nothing when it is missing or cannot be read. Measured on
     // the file clock itself (file_time_type::clock::now() minus last_write_time): all a caller wants is an age, and
     // C++20's clock_cast between the file clock and the system clock is not implemented the same way by libc++ and
-    // MSVC. The UI's startup sweep ages a crashed render's snapshots with it (spec/07).
+    // MSVC. The UI's startup sweep ages a crashed render's snapshots with it (spec/11).
     std::optional<double> fileAgeMs(std::string path);
-    // The native side of the file watcher (spec/07). drainChangedPaths pulls the paths the efsw watcher
+    // The native side of the file watcher (spec/02). drainChangedPaths pulls the paths the efsw watcher
     // saw since the last call (empty when watching isn't enabled); setWatchedRoms tells it which ROM
     // files to watch (their parent dirs), recomputed by TS whenever the systems list changes.
     std::vector<std::string> drainChangedPaths();
````

`packages/native/src/host/rpc/NativeFileWatcher.hpp`:

````patch
diff --git a/packages/native/src/host/rpc/NativeFileWatcher.hpp b/packages/native/src/host/rpc/NativeFileWatcher.hpp
index e32b279ef..2e6f3af0d 100644
--- a/packages/native/src/host/rpc/NativeFileWatcher.hpp
+++ b/packages/native/src/host/rpc/NativeFileWatcher.hpp
@@ -1,6 +1,6 @@
 #pragma once
 
-// NativeFileWatcher — the C++ half of "watcher = C++, policy = TS" (doc-03 / spec/07).
+// NativeFileWatcher: the C++ half of "watcher = C++, policy = TS" (spec/03).
 //
 // It owns an efsw file watcher and reports changed paths that the TS FileWatcher.pump() drains via
 // HostRpcService::drainChangedPaths(): the user config, bindings profiles, and any watched ROM. It
````

`packages/native/test/engine-graph/EngineGraph.test.cpp`:

````patch
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index f3e215df4..f79243b12 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -1610,9 +1610,9 @@ TEST_CASE("a larger core joins and leaves beside a running Game Boy while the au
     // This is the whole path, the join and the leave beside a publishing plan and the running core's state reading
     // whole after both: a smoke run of it, not the case that catches a claim touching the publish's memory. Against a
     // registry that shared one buffer across slots it reported nothing in three runs under the thread sanitizer and
-    // three under the address sanitizer (why is open: spec/07 records it as unproven), while ad hoc runs at 256-frame
-    // blocks did catch the block thread writing through a stale pointer. The next case drives the registry alone and
-    // fails on it.
+    // three under the address sanitizer (why is open: spec/07's "Threaded engine cases are smoke runs"), while ad hoc
+    // runs at 256-frame blocks did catch the block thread writing through a stale pointer. The next case drives the
+    // registry alone and fails on it.
     const char* const kGba = "rp.mesen.gba";
     {
         Engine quiet(kRate);
````

`packages/native/test/host/HostRpcWatching.test.cpp`:

````patch
diff --git a/packages/native/test/host/HostRpcWatching.test.cpp b/packages/native/test/host/HostRpcWatching.test.cpp
index 5f3b3b62d..281685d42 100644
--- a/packages/native/test/host/HostRpcWatching.test.cpp
+++ b/packages/native/test/host/HostRpcWatching.test.cpp
@@ -1,4 +1,4 @@
-// HostRpcService's watcher gate (spec/07): the service forwards its two watcher calls to the watcher a host hands it,
+// HostRpcService's watcher gate (spec/02): the service forwards its two watcher calls to the watcher a host hands it,
 // and is inert until one is handed over, which is what the test host and the CLI (which watch nothing) rely on. The
 // watcher here is a fake, so the case pins the gate itself; the real one is efsw's, and NativeFileWatcher's own cases
 // in retroplug-watcher-test hold that, in a binary that links the watcher library alone.
@@ -96,7 +96,7 @@ TEST_CASE("HostRpcService forwards to the one watcher it was handed", "[watcher]
 }
 
 TEST_CASE("fileAgeMs measures a file's age on the file clock, and has none where there is no file", "[host]") {
-    // What a UI render's snapshot is aged by when a crash leaves it: the sweep TS runs at startup (spec/07) deletes
+    // What a UI render's snapshot is aged by when a crash leaves it: the sweep TS runs at startup (spec/11) deletes
     // the ones a day old, and asks here rather than comparing a system clock, which would need C++20's clock_cast
     // between the file clock and the system clock, an operation libc++ and MSVC do not agree on.
     // The directory carries a random suffix: build/ and build-tsan/'s copies of this binary can run at once, and a
````

`packages/native/test/watcher/NativeFileWatcher.test.cpp`:

````patch
diff --git a/packages/native/test/watcher/NativeFileWatcher.test.cpp b/packages/native/test/watcher/NativeFileWatcher.test.cpp
index f96d90c32..414e7b0e6 100644
--- a/packages/native/test/watcher/NativeFileWatcher.test.cpp
+++ b/packages/native/test/watcher/NativeFileWatcher.test.cpp
@@ -1,4 +1,4 @@
-// NativeFileWatcher: the efsw mechanism behind HostRpcService::drainChangedPaths (spec/07). Verifies it reports
+// NativeFileWatcher: the efsw mechanism behind HostRpcService::drainChangedPaths (spec/02). Verifies it reports
 // config.json + bindings/*.json (recursive config-dir watch) and registered ROMs (parent-dir watch, filtered by
 // path), and that unregistered files under the config dir are ignored.
 //
````

`spec/00-overview.md`:

````patch
diff --git a/spec/00-overview.md b/spec/00-overview.md
index dbf18d32f..ecbfaaec4 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -13,7 +13,7 @@ exist in explicit "Not yet built" subsections.
 > build (the twin `Plugin*.cpp` DSP/UI and the old generated RPC client) has been
 > deleted; this is the sole build, with the canonical `RetroPlug` plugin identity.
 > `packages/ui/` is the F4b menu-contribution package, not the legacy UI. The
-> remaining feature gaps live in [07-remaining-work.md](07-remaining-work.md).
+> remaining open work, the feature gaps among it, lives in [07-remaining-work.md](07-remaining-work.md).
 
 ## The thesis: native owns bytes and cores; TypeScript owns meaning
 
@@ -163,7 +163,7 @@ this is specified in [01-architecture.md](01-architecture.md).
 | [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md) | The role model: system-role vs feature-role, the role registry, and the protocol nodes: a script node per role, the MIDI routing and the controller session, their settings and ports, and how `buildGraph` places them. |
 | [05-data-persistence.md](05-data-persistence.md) | The project model + `.rplg`, DPF get/setState + autoload, config models, forward-tolerant reads + version stamps, the LSDj sav codec, SRAM auto-save. |
 | [06-build-test.md](06-build-test.md) | How the project builds, the pnpm scripts, the dpf.js seam, and the headless verification loop. |
-| [07-remaining-work.md](07-remaining-work.md) | The remaining feature gaps now the port is complete: the raw LSDj Keyboard mode, NES per-mapper expansion sub-channels, the standalone disk-wins reopen, the live memory-region subscription, the 4-byte MIDI-out cap, Mesen `.mlb` labels in the CLI debugger, the deferred items, the pending refactor, the code-comment cleanup backlog, and the node-graph engine's next steps after B3b. |
+| [07-remaining-work.md](07-remaining-work.md) | Open work only: a checklist (before merge, the package split, known bugs, the engine, renders and kits, the Pico, feature gaps and the SDL standalone) whose lines link each item's detail, then what is deliberately deferred. |
 
 ## Key files
 
````

`spec/01-architecture.md`:

````patch
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index be975e4a8..7064b7f68 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -303,7 +303,8 @@ of the running plan's `SystemTable`, which is exactly the window before each sys
 > owned buffer. That second copy exists only because the shared `SystemBase` cannot yet publish
 > straight into the registry; collapsing it is a pending refactor
 > (the header comment of [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp)). It is
-> a documented redundancy, not a bug — see [07-remaining-work.md](07-remaining-work.md).
+> a documented redundancy, not a bug: P4 in
+> [07-remaining-work.md](07-remaining-work.md#p4-the-snapshot-registrys-double-copy).
 
 ---
 
@@ -580,7 +581,8 @@ lost mutation.
 ### Not yet built / deferred
 
 - **The registry double-copy** (§4) stands until the shared `SystemBase` can publish directly into
-  the registry — a pending refactor, see [07-remaining-work.md](07-remaining-work.md). (State
+  the registry: a pending refactor, P4 in
+  [07-remaining-work.md](07-remaining-work.md#p4-the-snapshot-registrys-double-copy). (State
   snapshots are now armed for **both** cores — SameBoy in
   [`SameBoyBackend.cpp`](../packages/sameboy/native/src/system/sameboy/SameBoyBackend.cpp) and Mesen in
   [`MesenBackend`](../packages/mesen/native/src/system/mesen/MesenBackend.cpp) - so `readSram`/state-based
````

`spec/02-native-host.md`:

````patch
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index db676f8b1..10476dd77 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -339,7 +339,7 @@ it:
 | Node | Ports | Does |
 |---|---|---|
 | `rp.host.midi-in` | out `midi` (events: 2048 messages, 64 KiB) | takes the staged host MIDI, stable-sorted by frame without allocating: the plugin stages two runs in frame order (a block's DAW events, then each moved automation slot's CC at frame 0), which it merges onto its queue, a tie to the earlier run; more runs are sorted in place first, by insertion |
-| `rp.host.midi-out` | in `in1` to `inN` (events), an input per entry of its settings `{"systems": [<id>, ...]}`, at most 128 | each input's messages land in `Engine::midiOut` tagged with that entry's system, the inputs merged by frame: each step hands out the head with the earliest frame, the lower input on a tie, so with every frame 0 it is the inputs' concatenation, input by input, each in queue order; each message goes at the later of its frame and the last handed out in the block, and at the block's last frame at most, a guard that keeps the list frame-sorted and inside the block whatever an input's queue holds (a node on the C ABI writes its own, and a script node's `send` carries a frame past the block as given), since the plugin hands each frame to the DAW unchecked. The merge allocates nothing, a head index per input, and costs messages times inputs; the sink it hands each message to, `Engine::midiOut`, still takes a `std::vector` per message ([07-remaining-work.md](07-remaining-work.md)). Two entries may name one system, and an empty list is a node with no inputs, which sends nothing. A settings change that keeps the inputs is a swap, one that moves them a rebuild |
+| `rp.host.midi-out` | in `in1` to `inN` (events), an input per entry of its settings `{"systems": [<id>, ...]}`, at most 128 | each input's messages land in `Engine::midiOut` tagged with that entry's system, the inputs merged by frame: each step hands out the head with the earliest frame, the lower input on a tie, so with every frame 0 it is the inputs' concatenation, input by input, each in queue order; each message goes at the later of its frame and the last handed out in the block, and at the block's last frame at most, a guard that keeps the list frame-sorted and inside the block whatever an input's queue holds (a node on the C ABI writes its own, and a script node's `send` carries a frame past the block as given), since the plugin hands each frame to the DAW unchecked. The merge allocates nothing, a head index per input, and costs messages times inputs; the sink it hands each message to, `Engine::midiOut`, still takes a `std::vector` per message ([07-remaining-work.md](07-remaining-work.md#allocations-left-on-the-block-thread)). Two entries may name one system, and an empty list is a node with no inputs, which sends nothing. A settings change that keeps the inputs is a swap, one that moves them a rebuild |
 | `rp.launchpad.surface-in` | out `in` (events: 1024 messages, 16 KiB), out `connected` (control), out `drops` (control) | drains the surface link's receive ring at the top of its block (16-message batches until the link answers short), every message at frame 0; `connected` carries the claim's `generation` (0 while nothing is attached, a new number on every claim, so a pair swapped inside one block still reads as a new device) and `drops` its lost-message count, each from frame 0 of the block, both wrapped at 2^24 (`connected` into 1..2^24 - 1, so a wrap never reads as nothing attached, and a reader compares each with the previous block, so a wrap only reads as a change). Its first block discards what the link held when it bound it, so a press from before the plan is not replayed; every drop the link sees (an oversized message, a full receive ring, a paint sent with nothing attached) is in that one count, and one the node's queue cannot hold is dropped and counted by the queue. `unique`, category `device` |
 | `rp.launchpad.surface-out` | in `out` (events) | hands each message to the surface link's send ring in queue order; a detached link drops and counts them. `unique`, category `device` |
 | `rp.n8.nes` | in `midi` (events), in `bytes`, param `expansionVolume`, out `connected` (control) | pushes each message at the cart link at its frame and the block's rate, the `bytes` port's flush flag carried; hands the link the expansion volume at its first block, on a change and on every reconnect; `connected` is the link's state from frame 0; leaving the plan, hands the link the notes the cart still holds for their note-offs. No audio output. `unique`, category `device` (no `threads`, below) |
@@ -407,8 +407,8 @@ console's map names at most one button per byte, and a byte the console has no b
 System's Select, a byte past any console's table - drives nothing), a Game
 Boy's presses (1024, sixteen blocks of a saturated buttons input, because presses are scheduled ahead; a Mesen
 console's presses coalesce, but these cannot, since each is scheduled and meant to be seen, so a quiescent session that
-pushes more than 1024 transitions between two blocks drops the tail, release included, which
-[07-remaining-work.md](07-remaining-work.md) records) and its serial-in (a run a message over a byte ring of two
+pushes more than 1024 transitions between two blocks drops the tail, though never a held button's release, for
+which the queue keeps its last slots) and its serial-in (a run a message over a byte ring of two
 blocks' `serialIn` or one `serialOut`), a NES's N8
 runs (4352) and their bytes (74752), and a Master System's levels (74752, in bytes from both doors). A push
 past a bound is dropped rather than grown, and counted (a Mesen press queue has none to reach), and the
@@ -595,7 +595,8 @@ wire could drive; everything else, display knobs, accuracy switches and latencie
   allocates, as it did on the command ring; a NES's region change resets it, which allocates too; the 0.5 s
   state-snapshot capture allocates inside Mesen's streaming serializer; and the N8 firmware's SD path allocates in its
   command handlers, in
-  a bigger directory listing and in an unbounded TX queue ([07-remaining-work.md](07-remaining-work.md)). A Game
+  a bigger directory listing and in an unbounded TX queue
+  ([07-remaining-work.md](07-remaining-work.md#allocations-left-on-the-block-thread)). A Game
   Boy's knobs, the ones it applies in place, a NES's setters but its region, and every param allocate nothing at all.
 - **A freshly built core's first block is the warmed block with one exception.** A Mesen core's controller snapshot, the
   four persistent frame input copies and the video filter's output buffer are sized at activation, off the block thread,
@@ -834,7 +835,8 @@ fix for the historical live-read bug class where reads were guarded off in the r
 The registry deliberately **double-copies** from each core's own tear-free triple, because the
 shared `SystemBase` can't yet publish straight into the registry (the header comment of
 [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp));
-collapsing that second copy is a pending refactor ([07-remaining-work.md](07-remaining-work.md)).
+collapsing that second copy is a pending refactor, P4 in
+[07-remaining-work.md](07-remaining-work.md#p4-the-snapshot-registrys-double-copy).
 
 ## The build seam: `SystemBuildRequest` and each console's build function
 
@@ -994,7 +996,8 @@ out a node's ports, and an audio-side one that holds every instance of the pack'
   editor is open) and the SDL standalone reclaim at every frame of their idle tick too, so there the generation goes
   within a few frames of the replan; a host with no tick while audio runs (a plugin whose editor is closed, the harness
   hosts) frees it only at those calls, a stop or `drainReleased`
-  ([07-remaining-work.md](07-remaining-work.md) section 5). A record the release ring back could not take,
+  ([07-remaining-work.md](07-remaining-work.md#a-plugin-with-its-editor-closed-has-no-idle-reclaim)).
+  A record the release ring back could not take,
   leaked rather than freed on the audio thread, keeps its pack for the host's life. It is left as it is, since nothing
   has filled that ring.
 - **Settings and layout** (control side, the planning thread). `layout_for` and `prepare_settings` run
@@ -1364,7 +1367,7 @@ however the job ends: on its worker once the body has returned and before the co
 the time a drain reports the job, and with the job itself when it is dropped unrun (a refusal, a cancel while queued,
 the pool's teardown). Only a listed name whose filename carries `kRenderSnapshotPrefix` is ever deleted, so a caller's
 own `--sav` cannot be named by a bug; a removal that fails is passed over without a throw, and an unknown kind's
-list is deleted too, as any refusal's is. The UI's startup sweep takes what a crash leaves (`spec/07`). The pool is the
+list is deleted too, as any refusal's is. The UI's startup sweep takes what a crash leaves (`spec/11`). The pool is the
 `Engine`'s, made at the first submission and reset before the engine's other members go. A graph job runs on one
 worker thread from start to end, which is the job host's control thread and block thread at once (quiescent: no audio
 thread, so `setGraph` flushes inline and `GraphHost::processBlock` is the block driver, draining then running), in
@@ -1445,9 +1448,9 @@ is one automatable parameter applied post-render across all 8 channels
   it outright, so nothing is left to enable.
 - **Per-block host buttons/keys.** No node has an input for them (the DSP kernel carried empty `buttons`
   and `keys` arrays until B3b) and no host feeds them yet: this blocks the raw LSDj Keyboard mode, which
-  needs `keys` on `rp.lsdj.sync` ([07-remaining-work.md](07-remaining-work.md)).
+  needs `keys` on `rp.lsdj.sync` ([07-remaining-work.md](07-remaining-work.md#raw-lsdj-keyboard-sync-mode)).
 - **The snapshot double-copy collapse** (§`SnapshotRegistry`) is a documented redundancy, not a
-  bug — a pending refactor, see [07-remaining-work.md](07-remaining-work.md).
+  bug: a pending refactor, P4 in [07-remaining-work.md](07-remaining-work.md#p4-the-snapshot-registrys-double-copy).
 
 ## Key files
 
````

`spec/05-data-persistence.md`:

````patch
diff --git a/spec/05-data-persistence.md b/spec/05-data-persistence.md
index 15d47384b..a7ce5c0d8 100644
--- a/spec/05-data-persistence.md
+++ b/spec/05-data-persistence.md
@@ -381,11 +381,10 @@ plane; the file-watch side ("watcher = C++, policy = TS") is covered in
 
 - **Standalone disk-wins reopen.** The standalone starts empty (or from
   `RETROPLUG_AUTOLOAD_PROJECT`); reopening the last-saved project from disk on
-  launch is not implemented. See [07-remaining-work.md](07-remaining-work.md).
+  launch is not implemented. See [07-remaining-work.md](07-remaining-work.md#the-standalone-reopens-the-last-project).
 - **Kit-patch persistence.** The thin config persists platform, paths, sav
   suffix/override, embedded-ROM marker, non-default universal settings, and
-  roles; kit (sample-patch) state is not yet serialized. Rich per-system domains
-  are tracked in [07-remaining-work.md](07-remaining-work.md).
+  roles; kit (sample-patch) state is not yet serialized.
 
 ## Key files
 
````

`spec/06-build-test.md`:

````patch
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index ad62b650c..52a1338eb 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -88,7 +88,7 @@ stereo pairs `out_1..4` ([10-multichannel-audio-out.md](10-multichannel-audio-ou
 The build declares `clap`, `vst3`, `vst2`, `au`, and `jack` (the `dpf_add_plugin` call in
 [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt)); AU is gated by DPF to macOS. Only
 **LV2** is not built — its out-of-process DSP/UI split doesn't fit RetroPlug
-([07-remaining-work.md](07-remaining-work.md)).
+([07-remaining-work.md](07-remaining-work.md#deferred)).
 
 ## The packages
 
@@ -159,7 +159,7 @@ shell's own aliases, from `tsconfig.aliases.json`: a child's `paths` REPLACE its
 one file carrying both).
 The product list is the **dependency order and the registration order both** (a package before the ones that use it, the
 shell last), and only a ROM provider's place in it is behaviour, and a tracker integration's for a system storing two
-node-less markers (spec/07's open entry): the providers run in that order, and the first to match a ROM fixes a system's
+node-less markers (a known bug, spec/07): the providers run in that order, and the first to match a ROM fixes a system's
 role-list order. A role's kind, a kit format's id and a tracker integration's marker role are otherwise looked up rather
 than sequenced (one package's roles interleave another's without consequence), so for those a duplicate is the hazard:
 the generated module refuses a role kind, a kit format id or a marker role declared twice, each with its own message.
````

`spec/08-profiling.md`:

````patch
diff --git a/spec/08-profiling.md b/spec/08-profiling.md
index da3533c9d..daa41a6c7 100644
--- a/spec/08-profiling.md
+++ b/spec/08-profiling.md
@@ -67,7 +67,8 @@ suite holds that promise directly (`retroplug-engine-graph-test`'s `[alloc]` cas
 the profile benches measure the JS side, so their 0 is the script host's, not the core's. What still allocates inside a
 long run is Mesen's 0.5 s savestate capture (its streaming serializer), a NES's region reset and a Game Boy's model
 restart, the N8 firmware's SD-command path (`std::string` / `std::filesystem`, a bigger directory listing, an unbounded
-TX queue) and a Game Boy's serial-out capture log; [07-remaining-work.md](07-remaining-work.md) lists them.
+TX queue) and a Game Boy's serial-out capture log;
+[07-remaining-work.md](07-remaining-work.md#allocations-left-on-the-block-thread) lists them.
 
 **How it got here.** Under the DSP kernel (one TS kernel running every role, until B3b) the first
 measure was **~171 allocs/block** on Profile A: the kernel built a `Map` of routed inboxes, per-system
````

`spec/09-cli-debugging.md`:

````patch
diff --git a/spec/09-cli-debugging.md b/spec/09-cli-debugging.md
index 4f7de42d4..bdcf31927 100644
--- a/spec/09-cli-debugging.md
+++ b/spec/09-cli-debugging.md
@@ -17,7 +17,8 @@ TAP-emitting `cli/sessions/rom-test.ts` is the agent-facing example. Proven on a
 ch1 note drives pulse1 at ~261 Hz (C4) with pulse2 silent.
 
 **The one remaining item** is Mesen native `.mlb` label files (need a new parser — Mesen's C# one
-isn't vendored; cc65 `.dbg` labels ship). See [07-remaining-work.md](07-remaining-work.md).
+isn't vendored; cc65 `.dbg` labels ship). See
+[07-remaining-work.md](07-remaining-work.md#mesen-mlb-labels-in-the-cli-debugger).
 
 This doc builds on [02-native-host.md](02-native-host.md) (the Backend RPC surface, of which `debug`
 is one facet) and [06-build-test.md](06-build-test.md) (how the CLI builds and is verified). The
````

`spec/10-multichannel-audio-out.md`:

````patch
diff --git a/spec/10-multichannel-audio-out.md b/spec/10-multichannel-audio-out.md
index 31ff08f9d..deb22c98d 100644
--- a/spec/10-multichannel-audio-out.md
+++ b/spec/10-multichannel-audio-out.md
@@ -3,7 +3,7 @@
 **Status: built (§10 steps 1–7) — the host seam, the SameBoy GB tap, the CLI GB export, the plugin GB
 option, the NES stereo-mod pins, the NES 5-individual-mono core stems, and NES-in-plugin (mono-lane
 packing) are all in. The one open piece is the per-mapper expansion sub-channels (§3b /
-[07-remaining-work.md](07-remaining-work.md)).**
+[07-remaining-work.md](07-remaining-work.md#nes-per-mapper-expansion-sub-channels)).**
 This doc describes outputting the *individual console sound channels* of a single emulator instance
 instead of its mixed stereo — 8 outputs for a Game Boy (a stereo pair per channel), and 5 mono
 channels / the hardware "stereo-mod" pins for an NES. It builds on
````

`spec/13-subsystems.md`:

````patch
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 0f84e4df9..2cb0dda04 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -74,7 +74,7 @@ shape one layer down — it reads its EXT parallel port (`$01`/PC4-PC6) instead.
 A full-resolution LSDj view: a pure-TS reader over live WRAM, tile-diffed into an LVGL canvas. No
 new native code — it polls `readRam` through the snapshot registry on a frame divider, because
 copying 128 KiB and decoding a song every frame is too expensive (see the live-memory entry in
-[07-remaining-work.md](07-remaining-work.md)).
+[07-remaining-work.md](07-remaining-work.md#live-memory-region-subscription)).
 
 - Code: `packages/lsdj/ts/hd/` (tiles, render, canvas) and
   `packages/lsdj/ts/ui/hdView.ts` (the "lsdj-hd" window view the shell renders).
@@ -259,7 +259,7 @@ every Engine embeds, each with its settings in the published graph, its inputs a
 host I/O nodes and to its system node's inputs, and each LSDj's Game Boy's `serialOut` going forward into
 its serial-out decoder, `rp.lsdj.serial-out`, placed after the systems
 ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)). What is left is
-[07-remaining-work.md](07-remaining-work.md) section 5.
+the Engine group of [07-remaining-work.md](07-remaining-work.md#engine).
 
 - Code: `packages/node-sdk/`, `packages/graph-runtime/`, `packages/core/` and `packages/dsp/` (C++: the node SDK, the
   executor, planner and plan loader, the `rp.core` and `rp.dsp` nodes; `packages/n8/` holds `rp.n8.nes`,
````

`spec/README.md`:

````patch
diff --git a/spec/README.md b/spec/README.md
index 99bc3480c..6e596d7b6 100644
--- a/spec/README.md
+++ b/spec/README.md
@@ -25,7 +25,7 @@ The port is complete. RetroPlug is a single build: the older **legacy** build (t
 deleted, the transitional target suffix dropped, and the plugin identity reverted to the canonical
 `RetroPlug` strings. These docs describe that build in the present tense. What remains is
 **feature work, not migration** — a handful of gaps (the raw LSDj Keyboard mode, NES per-mapper
-expansion sub-channels, the 4-byte MIDI-out cap) tracked in
+expansion sub-channels, the 4-byte MIDI-out cap) tracked, with the rest of the open work, in
 [07-remaining-work.md](07-remaining-work.md).
 
 ## The documents
@@ -39,7 +39,7 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 | [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md) | The role model (system-role vs feature-role) and the protocol nodes: a script node per role, the MIDI routing and the controller session, their ports and settings, the drift-exact PPQ clock in JS, the zod-free settings schema, and how `buildGraph` places them. |
 | [05-data-persistence.md](05-data-persistence.md) | The project model and `.rplg` (thin vs export), DPF get/setState, the config schemas, the forward-tolerant-read + version-stamp policy, the LSDj sav codec, and SRAM auto-save. |
 | [06-build-test.md](06-build-test.md) | How the build works, the pnpm scripts, and the headless verification loop — which command proves which kind of change. The practical "how do I verify a change" doc. |
-| [07-remaining-work.md](07-remaining-work.md) | The remaining feature gaps now the port is complete: the raw LSDj Keyboard mode, NES per-mapper expansion sub-channels, the standalone disk-wins reopen, the live memory-region subscription, the 4-byte MIDI-out cap, Mesen `.mlb` labels in the CLI debugger, the deferred items, the pending refactor, the code-comment cleanup backlog, and the node-graph engine's next steps after B3b. |
+| [07-remaining-work.md](07-remaining-work.md) | Open work only: a checklist (before merge, the package split, known bugs, the engine, renders and kits, the Pico, feature gaps and the SDL standalone) whose lines link each item's detail, then what is deliberately deferred. The commit that finishes an item deletes it. |
 | [08-profiling.md](08-profiling.md) | **Built** (behind `RETROPLUG_PROFILE`, `pnpm profile`). A benchmark harness that profiles the DSP-thread JS runtime — allocations/GC — under an mGB + heavy-MIDI workload: the refcount-churn reframe, the off-RT `renderAudio` harness, in-process QuickJS allocator instrumentation, the agent-friendly tool tier, and CI gates. |
 | [09-cli-debugging.md](09-cli-debugging.md) | **Built** (the plumbing tier). The CLI as a scriptable ROM-test harness: the session runner + `Timeline` + WAV/screenshot output, the compiled-in `render` subcommand, and Mesen's compiled-in debugger (decoded APU/PPU state, CPU/memory peeks + poke, breakpoints, trace, profiler, cc65 labels) surfaced through the debug RPC facet so an agent can drive a real NES and assert on its state. |
 | [10-multichannel-audio-out.md](10-multichannel-audio-out.md) | **Built** (steps 1–7; per-mapper NES expansion sub-channels remain). Outputting the individual console sound channels of one instance: Game Boy → 8 outputs (a stereo pair per channel, via a tracked SameBoy tap), NES → 5 mono stems or the hardware "stereo-mod" pins (via a `NesSoundMixer` edit). One mode-driven host seam (`channelLayout()` + a lane-counted `finishBlock`, with the graph TS builds wiring each stream to its lanes), the `channelSplit` / `pinSplit` / `stereoPinSplit` plugin options (menu rows `Channels` / `Pins` / `Stereo Pins`; a stereo stream takes a lane pair, a mono one a single lane, so the NES fits 8 outputs - except `Stereo Pins`, which spends a pair per pin so each is its own DAW track), and a `renderAudioPerChannel` CLI RPC feeding multichannel / per-stereo / per-mono WAV export. |
@@ -64,5 +64,4 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 `packages/retroplug/plans/`) whose content had drifted out of sync with the code and with
 each other. This set describes the architecture **as it is today**; forward-looking work lives under
 a clearly-labelled "Not yet built / deferred" heading in each doc, and is inventoried in
-[07-remaining-work.md](07-remaining-work.md). Stale in-code comments that still describe the old design are
-tracked as a separate cleanup task (note, don't fix) in [07-remaining-work.md](07-remaining-work.md#4-code-comment-cleanup-backlog-note-dont-fix).
+[07-remaining-work.md](07-remaining-work.md), whose Deferred section records what is intentionally not planned.
````

- [ ] **Step 3: run the check and watch it pass**

```bash
node /workspaces/RetroPlug-main/cuttlefish/.superpowers/docsclean/check.mjs --task 6
```

Expected: exit 0. With every task landed this is the whole check (`check.mjs` with no flag prints the same):

```
check.mjs: tasks 1..6 (38 files)
  (spec/07: 85 checklist lines, 813 lines)
ok   probes at their destination (keep / merge / open / new): 632 passed, 0 failed
ok   dup quotes in the doc they name: 603 passed, 0 failed
ok   relative links and anchors in touched markdown files: 1387 passed, 0 failed
ok   AGENTS.md is 200 lines or fewer and carries every keep / new probe of its own: 3 passed, 0 failed
ok   spec/07: no ticked line, every checklist line links to its detail entry: 85 passed, 0 failed
ok   no em dash in an added line: 1597 passed, 0 failed
ok   no added line past 120 columns (a table row aside): 1597 passed, 0 failed
ok   touched .js / .mjs files parse (node --check): 1 passed, 0 failed
ok   no changed file outside the plan's list: 36 passed, 0 failed
all checks pass
```

- [ ] **Step 4: the wider checks the comments reach**

The TypeScript comments (`registry.ts`, `lsdjSerialOutNode.ts`):

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
```

Expected: both exit 0, ending as in task 5 (`check-boundaries: the packages hold their boundaries (0 allowed by
EXCEPTIONS, 0 warning(s))`; `packages/retroplug typecheck: Done`).

The C++ and CMake comments: `CMakeLists.txt` reconfigures, and each edited source is compiled by one of these targets
(`HostRpcWatching.test.cpp` is `retroplug-audio-test`'s):

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-cli retroplug-clap \
  retroplug-watcher-test retroplug-engine-graph-test retroplug-audio-test -j$(nproc)
```

Expected: exit 0, with

```
-- Configuring done (2.4s)
-- Generating done (0.2s)
-- Build files have been written to: /workspaces/RetroPlug-main/cuttlefish/build
...
[100%] Built target retroplug-cli
[100%] Built target retroplug-clap
[100%] Built target retroplug-watcher-test
[100%] Built target retroplug-engine-graph-test
[100%] Built target retroplug-audio-test
```

The compile of `PluginDSP.cpp` prints GCC's `-Wformat-truncation` warning at its line 177; that warning predates this
cleanup and is spec/07's "The format-truncation warning at PluginDSP.cpp:177" entry.

- [ ] **Step 5: stage the task's files and check their whitespace**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- spec/07-remaining-work.md CMakeLists.txt \
  packages/launchpad/ts/registry.ts packages/lsdj/ts/lsdjSerialOutNode.ts packages/native/plugin/PluginDSP.cpp \
  packages/native/src/host/rpc/FileWatcher.hpp packages/native/src/host/rpc/HostRpcService.hpp \
  packages/native/src/host/rpc/NativeFileWatcher.hpp packages/native/test/engine-graph/EngineGraph.test.cpp \
  packages/native/test/host/HostRpcWatching.test.cpp packages/native/test/watcher/NativeFileWatcher.test.cpp \
  spec/00-overview.md spec/01-architecture.md spec/02-native-host.md spec/05-data-persistence.md \
  spec/06-build-test.md spec/08-profiling.md spec/09-cli-debugging.md spec/10-multichannel-audio-out.md \
  spec/13-subsystems.md spec/README.md
git -C /workspaces/RetroPlug-main/cuttlefish diff --cached --check
```

Expected: no output, exit 0.

- [ ] **Step 6: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(cleanup): spec/07 lists open work only

spec/07 had grown to 1,181 lines: ticked checklist lines left in place, and about 970 lines of per-phase history whose
"What it leaves" lists the checklist indexed a second time, many of their items done. It is rewritten to open work
only: a top note saying the commit that finishes an item deletes it, a checklist whose every line links its entry, one
entry per item saying what is missing and why it matters with a link to the design section that found it, and
Deferred last. Each finished item is cited in the docs cleanup's ledger and dropped; where no design doc records an
open item's analysis (the JACK control handoff, the dead routing code, the render residuals and the like) its entry
carries it. Before merge gains a green CI run of the tip, owed since run 43131974297 built ea2517dc9.

The comments and spec lines that pointed at a spec/07 entry now point at the entry that replaced it or, where the work
is done, at the doc that describes it; two said something no longer true and now say what the code does (a cart the
MI.MAP controller forces captures nothing, and a Game Boy's press queue never drops a held button's release).
MSG
```
