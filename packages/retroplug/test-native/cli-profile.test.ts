// Profiler + disassembler + call stack against a REAL Mesen NES core, driven through the CLI session +
// Timeline. Proves the spec/09 profiling surface end-to-end: disassembling at the live PC yields an
// instruction line (address + mnemonic + hex bytes), beginProfile→run→readProfile captures hot functions
// (sorted hottest-first, inclusive >= exclusive), and getCallStack returns a sane frame list. Mirrors the
// legacy nes/observe.test.ts + nes/profile.test.ts assertions. (The mock counterpart is mockBackend.ts.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { CallFrame, DisasmLine, ProfiledFunction } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

test("disassemble + profiler + call stack observe a real NES core", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  let pc = 0;
  let lines: DisasmLine[] = [];
  let began = false;
  let fns: ProfiledFunction[] = [];
  let stack: CallFrame[] = [];

  // Warm the core (ensureDebugger), then disassemble at the live PC and start the profiler. Read the
  // accumulated profile + call stack at the end of the measured window.
  const tl = new Timeline()
    .at(20, (sess) => {
      pc = sess.backend.getCpuRegisters(id).find((r) => r.name === "pc")?.value ?? 0;
      lines = sess.backend.disassemble(id, pc, 4);
      began = sess.backend.beginProfile(id); // init debugger + reset profiler
    })
    .at(400, (sess) => {
      fns = sess.backend.readProfile(id);
      stack = sess.backend.getCallStack(id);
    });
  renderTimeline(s, tl, { durationMs: 500, warmupMs: 1000 });

  // disassemble: 4 instructions from the live PC — the first lands exactly at PC and carries a mnemonic +
  // hex bytes (mirrors legacy observe.test.ts).
  expect(lines.length).toBe(4);
  expect(lines[0].address).toBe(pc);
  expect(lines[0].text.length > 0).toBeTruthy(); // a mnemonic
  expect(lines[0].bytes.length > 0).toBeTruthy(); // hex byte(s)

  // profiler: begin succeeds on a live NES debug target, and the measured window burns cycles in some hot
  // functions, sorted hottest-first with inclusive >= exclusive (mirrors legacy profile.test.ts).
  expect(began).toBeTruthy();
  expect(fns.length > 0).toBeTruthy();
  expect(fns[0].exclusiveCycles > 0).toBeTruthy();
  for (let i = 1; i < fns.length; i++) {
    expect(fns[i - 1].exclusiveCycles >= fns[i].exclusiveCycles).toBeTruthy();
  }
  for (const f of fns) expect(f.inclusiveCycles >= f.exclusiveCycles).toBeTruthy();

  // call stack: a sane frame list (the idle loop is shallow, so it may be empty), each frame a numeric
  // address (mirrors legacy observe.test.ts).
  expect(Array.isArray(stack)).toBeTruthy();
  for (const f of stack) expect(typeof f.address).toBe("number");
});
