# Known bugs, batch 3: the settings rows, MIDI out and the session exit

Eight entries from [spec/07](../spec/07-remaining-work.md)'s known bugs, each checked against the code at 8a90f2c57
(the research ledgers are `.superpowers/kb3/ts-ledger.md` and `native-ledger.md`, gitignored):

| # | Bug | Where | Found |
|---|-----|-------|-------|
| 1 | The stripper bundle fails in a build directory outside the repository | `tools/build-ts-stripper.mjs` | B3a |
| 2 | A start-screen edit dirties the empty untitled project | `ProjectStore` | F5b final review |
| 3 | The audio draft and its sibling-backend cache outlive a visit | `audio` package | F5b final review |
| 4 | An Apply reaches the device past a config that refused the write | `UserConfigStore`, `audio` package | F5b final review |
| 5 | Settings > MIDI misses a claim made while it is open | `midi` package, SDL hooks | F5b final review |
| 6 | `MidiStream::send` blocks the SDL standalone's audio callback | `midi` package | F5b |
| 7 | The test host drops an uncaught error | `retroplug-host` | D3, sdk-n8 port |
| 8 | A normal CLI exit skips the teardown | txiki, `retroplug-cli`, `retroplug-host` | sdk-n8 port |

User decisions (2026-11-13): an Apply the config refuses leaves the device alone, keeps the draft and says why (section
4); the audio menu starts a fresh draft on every visit (section 3); the exit is fixed in txiki, with the submodule
pointer bumps that takes (section 8). The rest takes the ledgers' recommendations. Out of this batch, and left in
spec/07: the JACK standalone's control handoff, the cart-node handover's cut key, the N8 menu on a cart (hardware), and
the two decisions still open there (a tracker marker with no protocol node; bar and beat quantise).

## 1. The stripper bundle resolves from the repository

`build-ts-stripper.mjs` sets no esbuild `absWorkingDir`, so its `typescript-5` alias resolves from the directory CMake
runs it in (`packages/native/CMakeLists.txt:216`); a build directory outside the repository has no `node_modules` above
it. It also writes its stub and generated entry into the repository's `build/native` whatever the output path is.

**Fix.** `absWorkingDir: REPO`, as `tools/build-ui.js` sets; the stub and the entry are written beside the output file.

**Test.** The script run from a working directory outside the repository with an output path there (`env --chdir`)
builds, and writes nothing under the repository's `build/`. Red today with the esbuild resolve error.

## 2. An empty project takes settings edits without dirtying

`ProjectStore.setSection` (`packages/retroplug/src/projectStore.ts:302-339`) marks the project dirty unconditionally,
and so do `setEnumSetting` and `setZoom`. On the start screen, Settings > MIDI's picks and the Project submenu's
Layout, Zoom, MIDI Routing and Audio Routing all reach them, so the close guard and the Load guard ask to save a project
with no systems, in every product. Opening a ROM starts a new project anyway (`openRom` calls `newProject`), so the
prompt protects nothing.

**Fix.**
- A private `isPristine()` in `ProjectStore`: no systems, no path, no project name. A pathless project with systems
  still dirties.
- `setSection`, `setEnumSetting` and `setZoom` call one `markSettingsChanged()`: on a pristine project it fires the
  change callback without dirtying; otherwise `markDirty()`. Publish and notify are unchanged.
- `setSection` returns early when the merged section equals the current one (no dirty, no publish), closing the class
  of no-op patches that dirty (a cycler landing on its current value).
- `ProjectHost.setSection`'s contract doc (`packages/core/ts/host.ts`) says it marks a non-empty project dirty.

**Tests** (`pnpm test`): on an empty project a MIDI section pick, `setLayout` and `setZoom` leave `isDirty()` and
`hasUnsavedChanges` false and fire the change callback once; adding a system then dirties, and the save carries the
pick; a saved project still dirties on a section edit; an unchanged patch dirties nothing. `pnpm test:ui`:
`midi-settings.test.ts`'s start-screen pick lets the close through (`__rp_onCloseRequested` answers no veto).

## 3. A visit to the audio menu starts a fresh draft

`resetAudioDraft` (`packages/audio/ts/ui/audioDraft.ts`) has no production caller and the audio menu has no open hook,
so the sibling-backend cache lasts until the backend in force changes ("More drivers..." vanishes for the session, and a
sibling that answered nothing is never asked again), and an unapplied draft survives every later visit and project
load.

**Fix.** `audioMenu` gains `onOpen: () => resetAudioDraft()`, the seam the MIDI package uses
(`packages/midi/ts/ui/index.ts`). An unapplied draft is discarded when the container is collapsed and reopened (the
user's decision). `knownDrivers`' backend-in-force invalidation stays for an Apply to another backend. A re-fetch
re-asks the library, which cannot show a device PortAudio did not see at its initialisation; that limit is the
library's.

**Tests** (`pnpm test`, `audioDraft.test.ts`): a sibling that answered nothing is asked again after `onOpen`, and
"More drivers..." is back; a staged block size is gone after `onOpen` and Apply is disabled. The within-a-visit "asked
once" case stands.

## 4. A refused config write leaves the device alone

`applyAudioDraft` writes the section and then calls `audioDevice.apply` regardless; `ConfigHost.setSection` returns
`void` and the shell drops `UserConfigStore.setSection`'s answer (`packages/retroplug/ui/host/uiHost.ts:84`). Under a
read-only config (a newer RetroPlug's) or a failed write the device switches, then the rows snap back to the saved
values and nothing says why. The store's `false` cannot gate the device as it stands: it also means "nothing changed",
and an Apply with a clean draft is the muted device's retry.

**Fix.**
- `UserConfigStore` gains `writeSection(key, patch): "written" | "unchanged" | "refused"` (the read-only latch, a
  failed atomic write and a section stamped newer than its contributor are refused); `setSection` stays the boolean
  `=== "written"` for its existing callers.
- `ConfigHost.setSection` returns `boolean`: whether the file holds the patch (written or unchanged). The shell binding
  returns `writeSection(...) !== "refused"`.
- `applyAudioDraft` on a refusal: no device call, the draft kept (the rows still show what was asked, Apply stays live
  for a retry), and an info row "Not saved: config.json could not be written" in the audio rows, cleared by the next
  Apply that lands or a new visit (section 3).
- The Launchpad's config writes keep ignoring the answer: its `applyLink` re-reads the saved choice, so a refusal claims
  nothing new.

**Tests** (`pnpm test`): `writeSection`'s three answers (a fresh value, a repeat, the latch, a failed write, a
stamped-newer section); an Apply under a newer-stamped config and under a failing `writeFileAtomic` reaches no device,
keeps the staged value and Apply, shows the row and leaves the file's bytes alone; a muted device with a clean draft on
a writable config still re-applies (the unchanged write is not a refusal).

## 5. Settings > MIDI reads the stream's state every poll

The checklist renders from a cached `describe()` (which probes RtMidi, so it runs only on the service's edges). A
Launchpad claim or release changes the stream's opened lists and reserved pair but none of those edges, and on the start
screen the MIDI container never closes (the start menu's `onClose` is a no-op), so a claimed port stays an ordinary
"[x]" pick: tapping it turns All devices off, and with All devices off it saves the Launchpad's port into the project.

**Fix.**
- `MidiDeviceHost` gains an optional `state(): MidiState | null` (`packages/core/ts/host.ts`), `MidiState` being the
  description's `openedInputs`, `openedOutputs` and `reserved`: member reads only, no probe, the shape of
  `AudioDeviceHost.status()`.
- Native: a `__rp_midiState` hook beside `jsMidiDescribe` in `packages/native/sdl/hooks.cpp`, bound with it, declared
  in `nativeHooks.ts` and bound in `uiHost.ts`'s `midiDeviceHost()`; `FakeMidiDevice`
  (`packages/midi/ts/testSupport/fakeDevice.ts`) implements it.
- The midi service's poll merges `state()` into the cached listing while the menu is open (the gate the transport row
  uses); the port lists stay the last describe's, since a claim changes what is open, not what is listed. The rows key
  moves once and the menu repaints.

**Tests** (`pnpm test`, `midiService.test.ts`): a claim made with no edge repaints once, the claimed port's row reads
"(control surface)" and no pick row names it, the describe count does not move; a release mirrors it; the existing
"reads the device once, not once per frame" guard stands.

## 6. A writer thread per hardware MIDI output

Only the SDL standalone sends from its audio callback (`packages/native/sdl/audioLoop.cpp:197`); the plugin and the JACK
standalone never use `MidiStream`. RtMidi is the one real backend: WinMM's SysEx path allocates and sleeps until the
bytes leave the wire (a 538-byte Launchpad repaint is about 170 ms at DIN rate), ALSA's grows its encode buffer and
prints its warnings from the caller, CoreMIDI makes an OS call.

**Fix.**
- `rp::midi::PortWriter` in the midi package: it owns one hardware `MidiPort`, a preallocated SPSC byte ring of
  length-prefixed records (a push is all or nothing, so a SysEx is never torn; the header carries the frame and the
  block time for the later "SDL host MIDI out at block time" entry, sent as soon as possible for now), a thread, and a
  wake the audio thread gives without a lock or an allocation (a C++20 atomic `notify_one`, made only when the writer
  has said it is sleeping). The plan checks atomic wait against the macOS release build's deployment target and falls
  back to a bounded poll there if it is unavailable.
- `MidiStream` takes a mode: `Inline` (today's behaviour) or `Threaded`. The SDL standalone uses `Threaded`; the CLI and
  test-host facet stays `Inline` (its caller is the JS thread, and scripts rely on a returned `midiSend` being on the
  wire). The virtual output stays inline in both.
- In `Threaded` mode `send` keeps its epoch bracket and only copies into each writer's ring. A full ring drops the
  newest whole message and counts it per port, read on the control thread as `droppedCount()` is.
- A retire or close stops the writer with a flush: queued messages are sent (a dropped Note Off is a stuck note) up to a
  deadline (500 ms) after which the rest is discarded; the in-flight send cannot be interrupted. The swap protocol, the
  Reopen order and the exclusive-port retry keep their order, "closed" now meaning joined and closed.
- `RETROPLUG_MIDI_LOG`'s per-message string moves to the writer.

**Tests** (`retroplug-midi-test`, over `FakeMidiBackend` with `sendDelayMs`): a threaded `send` returns in about a
millisecond while the port takes 200 ms, and the port receives every byte in order; a slow port does not delay a fast
one; overflow drops and counts whole messages; a swap with a backlog flushes before the close and nothing is sent after
it; a wedged port's close returns within the deadline and one send; a counting `operator new` sees no allocation on
`send`; the existing cases pass in both modes. The TSan midi leg of `tools/run-sanitizer.sh` runs them.
`midi-facet.test.ts` passes unchanged (the facet stayed inline); `sdl:controller` exercises the threaded stream.

## 7. One session pump for both hosts

The test host (`packages/native/src/main.cpp`) pumps txiki without looking for a pending exception or a stop request,
so a module body's throw ends as "tests did not complete (tjs.exit never called)" with no reason, and a kept-alive
file's throw runs to the 30-minute deadline. Its own `tjs.exit` override fails and leaves a TypeError pending for the
whole run, so the fix must clear it or every file fails on its first pump.

**Fix.** A shared `packages/native/src/host/SessionPump.{hpp,cpp}`, built into both hosts: it installs the stop takeover
(txiki's stop `async_cb`), clears the failed override's exception (the override itself is deleted, since it never
takes), and offers `pumpOnce()` with the CLI's policy (a pending exception is printed and the session threw; a stop
request with none pending reports "a callback threw, and its error was cleared before it could be reported"). Each host
keeps its own loop: the CLI's SIGINT handling and unbounded keep-alive, the test host's deadline. The CLI moves onto
the helper unchanged in behaviour.

**Tests** (`pnpm test:native`, a self-respawning file as `midi-facet.test.ts` does): children that throw in the module
body, in a microtask, in a timer made before the first `test()`, and while kept alive each exit 1 with the error text,
the kept-alive one at once. Then the whole `pnpm test:native` and the sanitizer host legs; a file the fix turns red is
fixed in this task (expected: none to a handful). `pnpm test:cli-ts` passes unchanged.

## 8. `tjs.exit` returns to the launcher

txiki freezes `tjs` inside `TJS_NewRuntime`, before an embedder can wrap it, and `tjs_exit` (`src/mod_os.c`) calls C
`exit()` from inside the JS call, so main's stack never unwinds: the CLI's `Teardown` (`engine.clearGraph()`, then
`rp_stop_providers`) never runs on a normal exit. The loss a user sees: a Launchpad a CLI session claimed never gets its
close bytes and stays in Programmer mode with its front panel locked. An error thrown in the same loop turn as a
`tjs.exit(0)` is also lost (exit 0, nothing printed). Teardown is safe at an exit: it is the path a thrown session and
Ctrl-C already take, and every provider's stop joins its own threads.

**Fix.**
- In the txiki fork (`deps/dpf.js/deps/lv_binding_js/deps/txiki`, `tommitytom/txiki.js`): `TJS_SetExitHook(rt, hook,
  opaque)`. With a hook set, `tjs.exit(code)` calls it and throws an uncatchable error (`JS_SetUncatchableError`, the
  interrupt handler's mechanism), which unwinds try, catch, finally, async functions and promise jobs; txiki then stops
  its loop. With no hook (the plugin, the SDL standalone) `exit()` is unchanged. The hook is also told whether an
  exception was already pending when `exit` was called, and txiki exposes whether a stop was requested, so the
  same-turn error is seen.
- `SessionPump` (section 7) installs the hook: it records the code, and `pumpOnce` treats the recorded exit with its
  uncatchable exception as the clean end (cleared, the code returned). Both hosts' mains then unwind: the CLI's
  `Teardown`, the providers, the runtime. A same-turn error makes the exit 1 with the error printed.
- The pointer bumps, in order: txiki (a commit on the fork), lv_binding_js, dpf.js, RetroPlug, each a commit of its
  own. Nothing is pushed without the user's word; until the three submodule commits are pushed, CI cannot check out the
  RetroPlug commit. `feature/web` (in `/workspaces/Web`) bumps lv_binding_js and dpf.js too, so whichever lands second
  merges its pointers.

**New surface.** Every one of the test host's 176 files now runs the host's full teardown on exit for the first time
(the audio driver service, `clearGraph`, the three providers, the Engine's render pool). A latent teardown bug surfaces
here, so this task's acceptance is the whole `pnpm test:native`, its per-file wall time against the pre-batch run, and
the ASan and TSan host legs.

**Tests.** `tools/run-cli-ts-tests.sh` sections: a normal `tjs.exit(0)` runs the teardown (a trace line under
`RETROPLUG_TEARDOWN_TRACE=1`); `tjs.exit(3)` still exits 3; a timer that throws followed in the same turn by one that
calls `tjs.exit(0)` exits 1 with the text. `pnpm test:native`: a respawned child claims the scripted Launchpad
(`launchpad-devices.test.ts`'s fake rig), exits, and the parent sees the close bytes. All red today.

## 9. Plan, execution and acceptance

**Tasks**, each test-first, in this order: 1 stripper; 2 the pristine project; 3 the audio visit; 4 the refused write
(after 3, whose `onOpen` clears its row); 5 the MIDI state read; 6 the port writer; 7 the session pump; 8 the exit
(after 7); 9 docs: spec/07 loses the eight entries and their checklist lines, spec/13 (the MIDI stream's writer, the
CLI's exit), spec/14 (what a session's exit runs), spec/06 if a command's meaning moved, and this design's as-built and
execution sections. A plan written by Opus whose every block is applied on the tree red then green and replayed from
its text; a Sonnet implementer a task, the spec check as tree identity with the validated diff, an Opus code review a
task and follow-up passes; an Opus final review. Work files in `.superpowers/kb3`, `kb3plan`, `kb3exec`.

**Acceptance**, on the tip: the serial kit (strict parity with fixtures and `RP_FAIL_ON_SKIP=1`, no parity case moves;
TSan and ASan; BlipToaster and NESVJ against the rebuilt CLI; the guarded SDL legs, `sdl:controller` among them since
the stream and the hooks change), then the parallel kit. No run opens an audio, MIDI or serial device outside the guard
and the fakes.

## 10. As built

The eight fixes landed as sections 1 to 8 describe them. The differences, and what the reviews found:

- **Section 2: same-value writes are no edits either.** `setSection` snapshots the section before the contributor's
  merge (a merge that edits in place made an applied edit look unchanged), and `setZoom` and `setEnumSetting` return
  early on the value already set. A start-screen edit stays with its project when a system is added to it (Load mGB);
  opening a ROM still resets the settings, as before the batch, now without a prompt.
- **Section 3: a re-visit marks a saved device absent only on a list it fetched.** A saved sibling backend's device read
  "(not present)" on every re-visit until its drivers were fetched again; the menu now carries which lists it knows.
- **Section 4: the row says what is true for both refusals** (a newer build's read-only config and a failed write), and
  a draft edited back to the saved value drops it. The store's `setSection` boolean stays `=== "written"`, used by tests
  only; the host group binds `!== "refused"`.
- **Section 5: the state read runs only while the menu is open**, a gate a test now pins; the native hook answers
  `null` for a closed stream.
- **Section 6: the writer's wake took four rounds.** Inline mode is unchanged and has no size limit, and the stream
  reports a retiring writer's drops (`MIDI out (<port>): N message(s) dropped`), the flush's abandoned records counted
  with them. The plan's wake, reviewed twice, had two lost wakes, found in execution: the counter snapshot was taken
  after the ring check (a push between them was missed), and the producer cleared a sleep it had not seen published (a
  delayed clear spent its notify). The writer now waits on the flag word, the counter detects a push in the decide
  window, every operation is `seq_cst`, and `stop()` bumps the counter before it clears the flag (a stop missed on an
  RCpc-acquire target hung `join()`). The proof is in `PortWriter.hpp`. The idle-wake test catches the second bug about
  one run in six without the thread sanitizer and reliably under it. The record header carries no frame or block time:
  spec/07's "SDL host MIDI out at block time" records that. In the threaded stream `RETROPLUG_MIDI_LOG` has no line for
  the virtual output, whose send stays on the audio callback (docs/sdl-standalone.md).
- **Section 7: the overrides went in section 8**, not 7, so the pump first had something pending to clear; the stop
  check also asks txiki's `TJS_StopRequested`.
- **Section 8: the exit is terminal, and txiki's rejection dispatch was fixed twice.** Built as designed, the hook broke
  the CLI's own `--version`, `--help` and dispatcher exits (the module promise reported the marker): txiki keeps an
  `exit_taken` flag that its eval, load event, drain, callbacks and rejection dispatch honour, and dpf.js's
  `evalModuleBytecode` skips its load-event dump after an exit. The first exit wins and no JS runs after it, except what
  quickjs-ng runs synchronously after swallowing the marker in a promise executor (spec/14). A Ctrl-C hook's exit keeps
  its code. Two defects in the dispatch: the exit branch leaked the rejection's reason (an assertions build aborted at
  `JS_FreeRuntime`), and upstream txiki freed an entry under itself when a listener handled the promise (a crash). The
  CLI checks run against a Debug CLI by hand (`RETROPLUG_CLI=build-dbg/bin/retroplug-cli`, spec/06) because the release
  build cannot see a leak. Left as they are: txiki's posix-socket poll callback and `TJS_Run` do not look at a taken
  exit, and neither is reachable from RetroPlug's hosts.
- **Section 1: the stripper test cleans up after a failure** and reports the child's exit.
- **Docs beyond section 9's list:** spec/05 (what `setSection` answers), and the plan's links, written two directories
  deep and committed into docs/, made relative to docs/.

## 11. Execution

- Design 080c0c0ba; plan 2cce39949, written and validated on the tree by DeepSeek Flash (a first run was stopped when
  its own test respawned without limit; the second ran with depth guards and timeouts on every command), replayed from
  its text to the identical tree, three Opus review rounds (C1 and I1-I7, then R1-R2, then R3, the threaded close case's
  timer, which the controller fixed as step 7b).
- Tasks, each by a Qwen3.8 Flash-Next implementer at xhigh, each tree checked byte-identical to the validated diff by a
  script: 6c0902dbb, 31ea34739, 533b10806, b2ad0df1d, eab640989, 3fe4ba697, ab72bf25c, 4369fe158 (with txiki 38aef2d,
  lv_binding_js d16343b, dpf.js 989aa1e), 34498f153. Code reviews by DeepSeek (tasks 1-5, 9) and Opus (6-8): task 5
  and task 9 asked for changes (a missing gate test; a dead anchor and a teardown order stated backwards), task 6 for
  the first lost wake.
- Follow-ups: A (DeepSeek: the first lost wake, the drain, the minors) 5407f18b4, 22e6621ce, 22d8ee84e; B (DeepSeek: the
  TS findings) a32e9ed73..9cf59ce0e; A2 (DeepSeek, after A's Opus review found a leak) 2afb02965, e86c9ab8e, 07b79df35,
  453321c4c, which found the second lost wake; C (DeepSeek: docs and B's review) 6b65ed3ca..4f1c4559b; A3 (Opus, after
  A2's review found the stop hang: the second DeepSeek native round to fail its review) a46397b14..cdb68ae84. The
  submodule commits are on local `retroplug-exit-hook` branches in txiki (38aef2d, 4edae99, 4ae8453, 14b510d),
  lv_binding_js (d16343b, ea9d3e4, abc7fdf, 3b0fea7) and dpf.js (989aa1e, 20ac8ee, fd147b6, a1724ee); none is pushed,
  and CI cannot check RetroPlug out until they are. The Opus final review found the batch ready with minor fixes,
  settled in 0628313a9.
- Acceptance at cdb68ae84: the serial kit, 45 of 45 legs (strict parity with fixtures and `RP_FAIL_ON_SKIP=1`, 30
  files, no skips; TSan and ASan; BlipToaster 57/57 and NESVJ 22/22 against the rebuilt CLI; `sdl:smoke`,
  `sdl:controller` and SDL PipeWire 27/0/0 under the device guard, no card PCM opened).
