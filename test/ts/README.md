# TypeScript test harness

Tests written in TypeScript that drive the emulator directly — button input,
time advance, memory + CPU-state inspection, and audio/video capture — with
`test()`/`expect()` and TAP output. This is the read-check-then-act companion
to the JSON `--script` runner (`cli/Script.hpp`): a test can read live state and
branch on it, which the declarative JSON form can't.

It runs **in-process** inside the embedded txiki.js / QuickJS runtime that
ships in `retroplug-cli` (no external Node, no DAW, no LVGL). esbuild transpiles
each `*.test.ts` to a single ESM `.js`, and `retroplug-cli --test <bundle.js>`
evaluates it.

## Running

```sh
pnpm test:cli          # transpile + run every test/ts/*.test.ts
```

Each `*.test.ts` runs as its **own** `retroplug-cli` process (one QuickJS
runtime per file = isolation). TAP goes to stdout; `console.log/warn/error`
goes to stderr as `[js:<level>] …`, so the TAP stream stays clean. The build
stops on the first failing file (nonzero exit).

Run a single test by its target (slug = path under `test/ts/`, `/` → `-`):

```sh
pnpm test:cli gb-smoke        # test/ts/gb/smoke.test.ts
pnpm test:cli nes-cpu         # test/ts/nes/cpu.test.ts
pnpm test:cli gb-lsdj-sav     # test/ts/gb/lsdj/sav.test.ts
```

Or by hand (paths relative to the repo root):

```sh
node tools/build-test.js test/ts/gb/smoke.test.ts build/test-js/gb/smoke.js
build/bin/retroplug-cli --test build/test-js/gb/smoke.js
```

> Adding a new `*.test.ts` requires a CMake reconfigure (the target globs
> `test/ts/**` recursively with `CONFIGURE_DEPENDS`) — just re-run `make`.

## Layout

Tests are organised by emulated platform; LSDJ-specific tests nest under `gb/lsdj/`.
ROM paths inside tests are relative to the **repo root** (the run working dir),
not the test file, so files can move between folders freely.

```
test/ts/
  gb/                 Game Boy (SameBoy): cpu, smoke, capture
    lsdj/             LSDJ-specific: sav codec, sav-authored fixtures (arduinoboy metro, …)
  nes/                NES (Mesen): cpu, debug, observe, profile
  gba/                GBA (Mesen): cpu
  fixtures/           shared fixtures (e.g. n8-midi.dbg cc65 symbols)
  harness alias       "harness" -> test/harness/index.ts (works from any depth)
```

## Writing a test

```ts
import { test, expect, emu, Button, Mem, CpuReg } from "harness";

test("LSDJ boots and writes to WRAM", () => {
  const sys = emu.loadRom("../resources/roms/lsdj/lsdj9_4_2.gb");
  emu.runMs(2500);                       // advance past the boot logo
  const wram = emu.readMemory(sys, Mem.Ram);
  expect(wram.length).toBe(0x8000);      // CGB WRAM = 32 KiB
});
```

`test(name, fn)` registers a case; each runs with a **fresh emulator** (call
`emu.loadRom` inside the test). An `expect()` mismatch — or any thrown error,
including from a native `emu` call — fails that case as TAP `not ok`.

### `emu` API

| Call | Purpose |
| --- | --- |
| `emu.loadRom(path) → sys` | Load a Game Boy ROM; returns the system id |
| `emu.runMs(ms)` | Advance every system by `ms` of emulated time |
| `emu.press(sys, Button.X, down)` | Set one button's state |
| `emu.tap(sys, Button.X, holdMs?)` | A single tap |
| `emu.chord(sys, [Button.Select, Button.Up])` | LSDJ-timed chord (modifier leads) |
| `emu.sendMidi(sys, [0x90, note, vel])` | Deliver a 1–4 byte MIDI message (queued for the next `runMs`) |
| `emu.readMemory(sys, Mem.Ram) → Uint8Array` | Copy of a memory region |
| `emu.getRegisters(sys) → {pc, …}` | Name-keyed registers (GB af/bc/…, NES a/x/y/ps, GBA r0–r15/cpsr) |
| `emu.setRegister(sys, "pc", 0x150)` | Write one register by name |
| `emu.readCpu(sys, addr) → byte` | Banking-aware, side-effect-free byte read |
| `emu.step(sys) → cycles` | Advance one instruction boundary |
| `emu.runUntilPc(sys, pc, maxCycles) → bool` | Run until PC hit (or cap) |
| `emu.getFrame(sys) → {width,height,published,pixels}` | Framebuffer (XRGB8888) |
| `emu.screenshot(sys, path) → bool` | Write the framebuffer to a PNG |
| `emu.getAudio(ms) → Float32Array` | Advance and return mixed stereo (interleaved) |

`expect`: `toBe`, `toEqual` (deep, incl. typed arrays), `toBeGreaterThan`,
`toBeLessThan`, `toBeTruthy`, `toBeFalsy`.

## Caveats

- **Boot timing.** SameBoy plays ~1.5 s of boot logo, and a fresh LSDJ ROM runs
  a 12–15 s cartridge self-test. `runMs(>=2000)` before reading state; for the
  LSDJ song screen, advance ≥ 15000 ms or preload a savestate.
- **CPU state is generic** across SameBoy / NES / GBA — `getRegisters` returns a
  name-keyed object (the sets differ; every backend has `pc`), and `setRegister`
  / `readCpu` / `step` / `runUntilPc` work on all three. Only GBA can't yet
  single-step (`step` returns 0) and GBA `cpsr` is read-only. `getRegisters`
  throws on a system with no CPU-state support.
- **`runUntilPc` always needs a `maxCycles` cap** — it returns `false` rather
  than spinning if the PC is never reached.
- **Reads are copies.** `readMemory`/`getFrame` hand back snapshots, never live
  emulator pointers.

## Profiling & debugging (Mesen NES)

The NES backend exposes Mesen's debugger headlessly — the priority being
**performance profiling**. evermidi (`old/evermidi/rom`) builds to the committed
`resources/roms/n8-midi.nes`, so you can profile it directly.

```ts
import { test, expect, emu, printProfile } from "harness";

test("profile evermidi's MIDI handling", () => {
  const sys = emu.loadRom("resources/roms/n8-midi.nes");
  // Names need a cc65 .dbg: build with `make -C old/evermidi/rom` (the Makefile
  // emits -g + --dbgfile build/n8-midi.dbg) then point loadLabels at it.
  emu.loadLabels(sys, "old/evermidi/rom/build/n8-midi.dbg");
  emu.runMs(1500);
  emu.beginProfile(sys);
  emu.sendMidi(sys, [0x90, 0x3c, 0x7f]); // drive the code you care about
  emu.runMs(500);
  console.log(printProfile(emu.readProfile(sys), 15)); // hottest functions
  expect(emu.readProfile(sys).length).toBeGreaterThan(0);
});
```

| Call | Purpose |
| --- | --- |
| `emu.beginProfile(sys)` | Init the debugger + reset the profiler |
| `emu.readProfile(sys) → ProfiledFunction[]` | Per-function cycles + call counts, hottest first |
| `emu.loadLabels(sys, "*.dbg") → bool` | Load cc65 symbols so output is named |
| `emu.disassemble(sys, addr, n)` | Disassembled instructions (symbol-resolved) |
| `emu.getCallStack(sys)` | Current call stack (named) |
| `emu.setTrace(sys, on)` / `emu.readTrace(sys, n)` | Execution trace logger |
| `emu.setBreakpoints(sys, [{type, start, end?, condition?}])` | Execute/read/write breakpoints (optional Mesen condition expr) |
| `emu.runUntilBreak(sys, maxCycles) → {broke, pc, breakpointId}` | Run until a breakpoint fires |
| `emu.stepInto/stepOver/stepOut(sys) → BreakInfo` | Single-step |

- Mesen **NES only** (SameBoy / GBA have no `debugTarget`); the calls throw with
  a clear message otherwise.
- **Don't mix `runMs` with active breakpoints** — drive with `runUntilBreak`.
  `runUntilBreak` reports the *triggering* address; execution stops just **after**
  that instruction (single-threaded model), so the registers reflect its effect.
- `printProfile(fns, top?)` formats a hot-function table for `console.log`
  (which goes to stderr, keeping the TAP stream clean).
- The harness shim is `test/harness/index.ts`; the native bridge is
  `cli/TestHarness.cpp`. The `Button`/`Mem` enum values are guarded by
  `static_assert`s in `TestHarness.cpp` against the C++ headers.
