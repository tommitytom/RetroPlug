# 14 - Hosts and harnesses

The checks that run RetroPlug inside something else: a real DAW (Reaper on Linux and Windows, Renoise), the SDL
standalone against a real audio server, and `retroplug-cli` as a consumer repository's whole test runner. What they
have in common is that each proves something the headless tiers cannot, and that most of them can reach real hardware,
so on Linux they run under a guard that proves they did not.

What this doc does not repeat: the [`pnpm scripts`](06-build-test.md#pnpm-scripts) table gives the SDL harnesses and
`test:cli-ts` a row each (what it builds, whether CI runs it) and `reaper:all` one for the whole Reaper leg; [Running
the Reaper leg in parallel](06-build-test.md#running-the-reaper-leg-in-parallel) owns `pnpm reaper:all` and the
`RP_JOB_TAG` isolation that lets the legs share a machine; the parameter legs (`reaper:params`, `-clap`, `-vrc7`) are
[12-dynamic-parameters.md](12-dynamic-parameters.md) section 6's; the LSDj sync and DAW-timing renders and their
analyzer are [docs/lsdj.md](../docs/lsdj.md)'s; and what `retroplug-sdl` is and how it is built is
[13-subsystems.md](13-subsystems.md)'s.

## The Reaper leg

Every Reaper check boots its own headless stack through the sourced [tools/reaper-env.sh](../tools/reaper-env.sh) and
drives the built plugin in a real Reaper. Beside the renders (the LSDj and MIDI-timing ones in docs/lsdj.md) and the
parameter legs (spec/12) there are checks of the hosted editor and of a host holding several instances, which no
headless tier can reach:

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
editors and watches the host survive; `RP_TWO_FORMAT=vst3|clap` picks the format, `RP_TWO_COUNT` the count (default 2)
and `RP_TWO_AUTOLOAD` a `.rplg` autoloaded into every instance. The second
([tools/run-reaper-two-instances-load.sh](../tools/run-reaper-two-instances-load.sh)) drives the failure that was
reported: load a ROM through instance 1's editor, open instance 2, load through that one. It judges by a ReaScript defer
HEARTBEAT rather than by the process, because the bug froze the host rather than crashing it and a live process proves
nothing: Reaper runs defer on the same main thread both editors idle on, so a blocked UI thread stops the counter dead.
`RP_LOAD_MODE=dialog` (the "Load..." row) is the leg that reproduced the freeze; the default, `direct`, clicks the
built-in Load mGB row. The harness falls back to SIGKILL when SIGTERM goes unanswered, since a host spinning in LVGL
never answers it and the script would otherwise hang on the bug it just found. Neither is in CI, and neither is in
`reaper:all`: they stand up several instances and are run on their own.

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

**`sdl:smoke`** ([tools/run-sdl-smoke.sh](../tools/run-sdl-smoke.sh)) boots and shuts down the standalone on SDL's dummy
drivers and the null audio backend. CI runs it on the linux job (linux-x64), so CI proves the standalone both links AND
runs; it also fails the run if the null-pinned run touched a device path or loaded an audio library (its own `NOSND_LOG`
is not empty).

**`sdl:controller`** ([tools/run-sdl-controller.sh](../tools/run-sdl-controller.sh)) drives the control surface through
the real standalone against an ALSA fake, under the `RP_MIDI_PORT_ALLOW` port filter; its legs are in the `pnpm scripts`
table's row.

**`sdl:pipewire`** ([tools/run-sdl-pipewire.sh](../tools/run-sdl-pipewire.sh)) is the audio-device half the smoke cannot
cover, since the smoke runs on the null device: it stands up a private PipeWire server with three null sinks in its own
`XDG_RUNTIME_DIR` and asserts which output device the host opens, in both audio backends, PortAudio and SDL2. The
PortAudio legs hold that the session default sink wins (the `default.audio.sink` metadata key), that an explicit
Settings pick beats it, that a stale pick warns and falls back, that an explicit driver resolves the same way, and that
a run with no libpipewire degrades to ALSA, whose `default` the script points at a private `ALSA_CONFIG_PATH` config
whose default is the null PCM, so no card is touched. SDL2's legs run the same selection cases, then the 8-channel case,
which is the one place the two backends differ audibly (PortAudio opens 8 ports with pair 1 linked; SDL reports 8 at the
API but its PipeWire node has 2 output ports). SDL's live-menu leg drives the real menu to Settings > Audio while a
stream runs and holds the stream open across the menu build: the same node before and after, and exactly one open line.
A leg pins its backend with `--audio-backend`, which replaces only the backend, so the saved driver and device survive.

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

Every Linux harness that could reach real hardware runs under the device guard, whose source is tracked:
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
for the wrong ELF class would leave the run unguarded with nothing said. The guard is a Linux facility: on macOS
guard-env.sh prints a notice and the run is unguarded, and the PowerShell twins and the Renoise runner source nothing
like it. The three SDL harnesses and
[tools/reaper-env.sh](../tools/reaper-env.sh) source it themselves, so a bare `pnpm sdl:controller` or `pnpm reaper:*`
is guarded, not only a run of the verification kit (local tip-verify scripts, not in the repo): the kit prepends its own
build of the guard for the whole run, and guard-env.sh then adds no second one. A Reaper leg's teardown prints the
log's counts and fails on any refused card playback, video or serial node
([spec/06](06-build-test.md#running-the-reaper-leg-in-parallel)).

## retroplug-cli as a test harness

`retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `retroplug-cli run <session.ts>` does one
file, so a consumer repo (BlipToaster) needs no Node, npm, esbuild or node_modules: its kit is the binary plus tests,
and nothing else. [tools/sync-cli-to-bliptoaster.sh](../tools/sync-cli-to-bliptoaster.sh) is the sync step, a
binaries-only copy of two binaries, `retroplug-cli` and `retroplug-n8-hwtest` (the bare N8 device access the
nes-hardware-lab skill uses), into the consumer's `retroplug-cli/bin/`; no SDK file goes over. The harness's own check
is `pnpm test:cli-ts`, which CI runs on the linux job.

**The SDK is a module, never a file.** A test imports the SDK by the bare specifier `"retroplug-cli"`, which the binary
serves from QuickJS's loaded-module table: [packages/native/cli/main.cpp](../packages/native/cli/main.cpp)
`JS_ReadObject`s the embedded module bytecode (compiled with `tjsc -m -n retroplug-cli`) before a session evaluates, and
`js_host_resolve_imported_module` checks that table BEFORE the module loader, so nothing is left to sync or to go stale.
A relative specifier could not work this way: `tjs_module_normalizer` consults the import map only for bare names (`if
(name[0] != '.')`), so `"../sdk/x.js"` always hits the disk. Only the `.d.ts` is written out
([cli/sdkAssets.ts](../packages/retroplug/cli/sdkAssets.ts)), because editors and `tsc` read declarations from disk and
reach it through a tsconfig `paths` entry, and only when a source actually imports the SDK (`buildTsDir` reports
`needsSdk`).

**Stripping, not bundling.** The txiki runtime already resolves `import` off disk at runtime, so tests never needed
bundling, only stripping; and consumer tests already use explicit `.js` specifiers, so emitting `X.ts` -> `X.js` at the
same directory depth needs no specifier rewriting. That depth is why stripped output lands in the source dir's sibling
`.rp-test-build/` (`buildDirFor` in [cli/tsStrip.ts](../packages/retroplug/cli/tsStrip.ts)); anywhere else, every
`../sdk/...` import breaks. Where that sibling is not writable (a session straight under `/tmp`), `resolveBuildDir`
falls back to `<tmpDir>/rp-test-build/<slug>`. The whole directory is still stripped into one place, so imports between
its files still resolve, but a `../` import out of it misses; `--out` next to the sources covers that case.

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

**The real N8 and the event loop.** A session script reaches a real Everdrive N8 Pro through the SDK's `connectN8()`
([cli/sdk.ts](../packages/retroplug/cli/sdk.ts)): the n8 pack's device (`openN8Device`,
[packages/n8/ts/device.ts](../packages/n8/ts/device.ts), exported as `@retroplug/n8/device`) over the process's RPC
call, so a consumer uploads a file, boots a ROM at the menu, reads the live sniffer region or pushes raw FIFO bytes
without spawning `n8-load` and parsing what it printed. Every call is synchronous, over the N8 facet the CLI's own N8
tools use ([spec/13](13-subsystems.md#the-everdrive-n8-bridge)); a closed device refuses each call in its own words, and
a call that failed on the line leaves the next to resync the session in the host. `keepAlive()` is the session's own: a
session otherwise gets a bounded run of pumps, so a timer or an awaited `tjs.spawn(...).wait()` may not complete before
the pumps run out, and a script that waits on either calls it once, up front, and ends with `tjs.exit`. Both are
declared whole in [cli/sdk-types.d.ts](../packages/retroplug/cli/sdk-types.d.ts), which `cli/sdkSurfaceCheck.ts` holds
to the implementation by type identity, not mutual assignability (which passes a dropped `readonly`).

**An error nothing caught.** It ends a session, kept alive or not, with exit 1 and its text. txiki turns one (a throw in
an async module body, a rejection nothing cancelled, a throw in a timer) into a pending exception and a request to stop
its loop, and the launcher ([packages/native/cli/main.cpp](../packages/native/cli/main.cpp)) drives that loop itself, so
after every pump it looks for both: the exception, which it prints, and the stop request, which it takes over because it
survives a sibling callback's try/catch clearing the exception. In a test file the harness
([packages/testing/ts/harness.ts](../packages/testing/ts/harness.ts), the one `pnpm test`, `test:native` and `test:ui`
run too) takes over first, from its first `test()`: it records a rejection nothing handled, or a throw in a timer
callback, prints it to stderr at once, fails the running case at once (each case is raced against an abort, so one
awaiting work that died first does not wait for ever), blames a timer's throw on the case that created the timer
(failing the file, naming it, when that case has already reported), and fails the file for anything outside every case,
while the rest of the file still reports (a late one's reason names the case it came from; a case that skips or throws
still owns the rejection it left; the loop turn uses the timer captured at install, not a test's stub). A rejection
carries no trace of its creator, so it is blamed on the case running, whose failure says it may be one before; one
handled a loop turn late counts as unhandled, as in Node. A synchronous throw in a session's `onInterrupt` (Ctrl-C) hook
ends it with exit 1 too, as an async one that rejects does. A session holding a device releases it on that path:
`launchpad-probe` runs each timer body so that a throw reports, leaves Programmer mode and closes its ports before
exiting 1. [tools/run-cli-ts-tests.sh](../tools/run-cli-ts-tests.sh) checks each of these through the real binary.
