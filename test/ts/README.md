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
make -C build cli-ts-test          # transpile + run every test/ts/*.test.ts
```

Each `*.test.ts` runs as its **own** `retroplug-cli` process (one QuickJS
runtime per file = isolation). TAP goes to stdout; `console.log/warn/error`
goes to stderr as `[js:<level>] …`, so the TAP stream stays clean. The build
stops on the first failing file (nonzero exit).

Run one bundle by hand (paths are relative to the repo root):

```sh
node tools/build-test.js test/ts/smoke.test.ts build/test-js/smoke.js
build/bin/retroplug-cli --test build/test-js/smoke.js
```

> Adding a new `*.test.ts` requires a CMake reconfigure (the target globs the
> directory with `CONFIGURE_DEPENDS`) — just re-run `make`.

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
| `emu.readMemory(sys, Mem.Ram) → Uint8Array` | Copy of a memory region |
| `emu.getRegisters(sys) → {af,bc,de,hl,sp,pc}` | SM83 register file (SameBoy only) |
| `emu.setRegister(sys, CpuReg.PC, 0x150)` | Write one 16-bit register |
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
- **CPU state is SameBoy-only.** `getRegisters`/`setRegister`/`readCpu`/`step`/
  `runUntilPc` throw for NES/GBA systems (register files differ). The 9 memory
  regions (`Mem.*`) work on every backend.
- **`runUntilPc` always needs a `maxCycles` cap** — it returns `false` rather
  than spinning if the PC is never reached.
- **Reads are copies.** `readMemory`/`getFrame` hand back snapshots, never live
  emulator pointers.
- The harness shim is `test/harness/index.ts`; the native bridge is
  `cli/TestHarness.cpp`. The `Button`/`Mem` enum values are guarded by
  `static_assert`s in `TestHarness.cpp` against the C++ headers.
