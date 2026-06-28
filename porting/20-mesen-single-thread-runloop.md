# 20 — Remove Mesen's dead run-loop threading

**Status:** deferred follow-up. The debugger's two-thread *break* model has
already been removed (step 19 era — the headless break is now the only model;
see `Core/Debugger/Debugger.{h,cpp}`, `DebugBreakHelper.h`). What remains is the
broader run-loop threading in `Core/Shared/Emulator.{h,cpp}`, which is **entirely
unreachable** because the RetroPlug integration never calls `Emulator::Run()`.

## Why it's dead

The wrappers load with `stopRom=false` ([MesenNesSystem.cpp](../src/system/mesen/MesenNesSystem.cpp),
[MesenGbaSystem.cpp](../src/system/mesen/MesenGbaSystem.cpp)) and drive the core
by hand on the caller's thread (`cpu->Exec()` for NES, `console->RunFrame()` for
GBA). So:
- `Emulator::Run()` is never called → `_emuThread` is always null
  (`Emulator.cpp` ~110 / the spawn at ~539).
- `IsThreadPaused()` = `!_emuThread || _threadPaused` is permanently true
  (Emulator.cpp ~878) — already removed from the debugger's `IsExecutionStopped()`.

## What can be removed (all unreachable single-threaded)

- **Run loop:** `Emulator::Run()` (~110), the `_emuThread` spawn (~539) + join in
  `Stop()` (~284), `RunFrameWithRunAhead()` (~204).
- **Pause/lock waits (only called from `Run()`):** `WaitForLock()` (~891),
  `WaitForPauseEnd()` (~822), and the `_paused` / `_pauseOnNextFrame` /
  `_stopFlag` / `_threadPaused` run-loop bookkeeping (Emulator.cpp ~135-165, ~256).
- **Lock/suspend coordination:** `Lock()`/`Unlock()` (~864-876) and the
  `_lockCounter` / `_runLock` machinery, plus `Emulator::SuspendDebugger()` and
  the debugger-side `SuspendDebugger()` / `ResetSuspendCounter()` /
  `BreakRequest()` / `HasBreakRequest()` and the `_breakRequestCount` /
  `_suspendRequestCount` atomics (`Debugger.{h,cpp}`) — after this step they are
  reached **only** from the dead Run/Lock paths. (`ProcessBreakConditions` still
  reads `_breakRequestCount` but it is always 0; drop that term too.)
- `SystemActionManager.h` `SuspendDebugger` calls (~64/74) follow.

## What MUST be kept

- **`_debuggerLock`** around `InternalLoadRom` (Emulator.cpp ~398) — ROM-load
  ordering vs. the debugger; keep the `AcquireSafe()`.
- **`IsEmulationThread()` / `_emulationThreadId`** (Emulator.cpp ~1096) — gates
  the debug hooks, the APU `IsEmulationThread()` fast-path (NesApu.cpp ~118), and
  `GetConsoleUnsafe` safety asserts. Load-bearing. The wrappers bind it to
  **whoever drives the block**: `prepareForBlock` / `stepInstruction` call
  `SetEmulationThreadId(this_thread)` whenever `!emu_->IsEmulationThread()`, so it
  **rebinds when the driving thread changes** (e.g. boot on the main thread, then
  an offline parallel render on a worker thread, then back). It is still
  per-instance and the instance is still only ever driven by one thread at a time
  — the thread just isn't frozen at first block. (This replaced an earlier
  one-time `threadIdSet_` latch, which broke when a booted instance was handed to
  a render worker.)
- **`_threadPaused` write in `InternalLoadRom`** (Emulator.cpp ~392) — keep (it's
  a real "paused during load" signal, not run-loop coordination).

## Approach

Mechanical, in `Emulator.{h,cpp}` (+ `SystemActionManager.h`, the debugger
suspend/break methods). Compile after each removal; the linker/`-Wunused` will
flag anything still referenced. Beware: `LoadRom`'s `stopRom` parameter and the
`Pause()/Resume()/IsPaused()` API are part of the public surface — keep the
signatures, just gut the threaded bodies (e.g. `IsRunning()` should stay).

## Verify

`cmake --build build -j$(nproc)` clean (Emulator.h is plugin-wide), then
`make -C build cli-ts-test` (esp. `debug.test.ts` — breakpoints/stepping), and
the render regressions `cli-smoke` / `cli-nes-smoke` / `cli-gba-smoke`. A
regression here would surface as a hang (a removed wait that was load-bearing)
or a link error (a kept caller of a removed method).

## Live threads still spawned by `Initialize()` (found via ThreadSanitizer)

The above is about *dead* (unreachable) threading. Running the test binaries
under TSan (`tools/run-sanitizers.sh thread`) surfaced two *live* Mesen threads
that `Emulator::Initialize()` starts on **every** instance, independent of the
run loop:

- **`ShortcutKeyHandler`** (`Emulator.cpp:90`, only when `enableShortcuts=true`).
  It spawns a thread that polls the host keyboard every 50 ms
  (`ShortcutKeyHandler.cpp:28`) and reads `Emulator::IsPaused()` →
  `safe_ptr<Debugger>::lock()` — which **races `InternalLoadRom`'s
  `ResetDebugger()`** (`Emulator.cpp:412/1061`) during the same instance's
  `onActivate`. **Fixed:** the wrappers now call `Initialize(false)`
  ([MesenNesSystem.cpp](../src/system/mesen/MesenNesSystem.cpp),
  [MesenGbaSystem.cpp](../src/system/mesen/MesenGbaSystem.cpp)) — the plugin
  drives input itself and never uses Mesen's keyboard-shortcut layer, so this is
  pure overhead + a load-time race. This removed the only TSan finding in our
  own integration.
- **`VideoDecoder` / `VideoRenderer`** (`Emulator.cpp:94-95`, `StartThread()`,
  unconditional). Still live on every instance. TSan shows they do **not** race
  our `onProcess` path (the concurrent state-snapshot stress test is clean), but
  we render frames via `MesenVideoDevice` and don't consume Mesen's video
  pipeline — so these two threads look like further removable single-thread
  cleanup. Not yet done; no correctness issue, just overhead.

### Mesen process-global singleton races (out of scope, excluded from the gate)

TSan also flags genuine races in Mesen's process-global singletons —
`GameDatabase::InitDatabase` (`GameDatabase.cpp:82`), `SimpleLock::Acquire`
(`SimpleLock.cpp:26`), `FolderUtilities` — but **only** from the
`[MesenSingleton]` tests, which deliberately hammer those globals from many
threads to document the limitation (several are already `[!mayfail]`). A given
instance is only ever driven by **one thread at a time** — including offline
parallel render (`system/OfflineRender.cpp`), which farms each render unit onto
its own worker thread but never steps the same instance from two threads — and
the process-globals (`FolderUtilities` home folder, `MessageManager` options) are
written once at **activation**, single-threaded, before any render. So no
instance mutates these globals concurrently, and `tools/run-sanitizers.sh`
excludes `[MesenSingleton]` from the sanitizer gate rather than suppressing each.
Making those singletons thread-safe would be a separate effort.
