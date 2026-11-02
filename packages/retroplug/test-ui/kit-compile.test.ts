// The plugin compiles a kit (docs/modular-graph-phase-d2-design.md section 6's "New"): the TypeScript kit compiler
// runs end to end over the facet set the PLUGIN binds, with no harness facet behind it, and over the settle path the
// plugin has and no other.
//
// The harness this suite runs on (packages/native/test/ui/UiHarness.cpp) mounts exactly what
// packages/native/plugin/PluginDSP.cpp does - registerHostRpc + registerEmulatorRpc, nothing else - so it is the
// narrowest honest host for this: the CLI and the native test host also mount the harness facet, and a Catch2 case
// composing the plugin's registration could not run TypeScript at all. The offline jobs the compile is made of ride
// the emulator facet (submitOfflineJob / drainOfflineJobs), which is why the plugin can compile a kit at all; the two
// harness RPCs that compile a kit on the harness hosts (compileKit / compileDmc) are checked to be absent, since a
// compile that needed them would pass here only by accident of some other host's registration.
//
// The compile is src/kit's entry point over the real backend, never an audio driver: making a driver turns the
// offline poll's native wait on (src/offline.ts), and that is the settle path of a host with NO UI tick. The plugin
// has a tick, and its only mover of these promises is useJobPump's backend.pumpJobs on a frame. So the promise below
// is settled by ui.pump + be.pumpJobs alone, and the microtask-only stretch before that loop is what proves no
// native wait is running underneath it.
import { test, expect, ui } from "ui-harness";
import { createRealBackend } from "../src/realBackend";
import { makeCall } from "../src/rpcClient";
import { compileKit, type KitCompileResult } from "../src/kit";
import { encodeWav } from "../cli/wav";
import { KitView } from "../src/lsdj/rom/kit";
import { BANK_SIZE, KIT_LOOKUP, ROM_SIZE } from "../src/lsdj/rom/constants";

test("the plugin's facet set compiles a kit, with no harness facet bound", async () => {
  expect(ui.boot()).toBeTruthy();
  const be = createRealBackend();
  const call = makeCall();
  const wav = ui.romDir() + "/rp-plugin-kit-src.wav";

  // A decaying 220 Hz tone: a real sample, so the slot's bytes cannot be an empty slot's.
  const frames = 6000;
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 220) * Math.exp(-i / 3000);
  expect(be.writeFile(wav, encodeWav(pcm, 44100, 1))).toBeTruthy();

  // The harness facet is NOT bound on this host: the RPCs the harness hosts compile a kit through are refused, and
  // the render harness beside them is too.
  expect(() => call("compileKit", { name: "", samples: [] })).toThrow();
  expect(() => call("compileDmc", { name: "", samples: [] })).toThrow();
  expect(() => call("renderAudio", 1)).toThrow();

  let result: KitCompileResult | null = null;
  let failure: unknown = null;
  let settled = false;
  void compileKit(be, {
    name: "PLUG",
    samples: [{ path: wav, name: "SIN", effects: [{ type: "dither", ditherType: "HighPassTPDF" }] }],
  }).then(
    (r) => {
      result = r;
      settled = true;
    },
    (e) => {
      failure = e;
      settled = true;
    },
  );

  // No pump yet: microtasks alone must settle nothing. A native wait in the poll would settle the compile here,
  // which is exactly the settle path the plugin does not have.
  for (let i = 0; i < 16; i++) await Promise.resolve();
  expect(settled).toBe(false);

  // The plugin's frame tick: pump the jobs, let the settled promises queue their next step, repeat. Bounded, so a
  // compile that cannot finish without a native wait fails here instead of hanging.
  let steps = 0;
  while (!settled && steps < 2000) {
    ui.pump();
    be.pumpJobs();
    await Promise.resolve();
    steps++;
  }
  expect(settled).toBe(true);
  if (failure !== null) throw failure;
  const { bytes, errors } = result!;

  // The bank is whole, the slot landed, and it decodes to a varied sample in both polarities (LSDj stores inverted
  // nibbles, so an empty data region would decode to a constant +1.0 and satisfy a peak alone).
  expect(bytes.length).toBe(BANK_SIZE);
  expect(errors[0]).toBe(null);
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bytes, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0);
  expect(kit.name()).toBe("PLUG");
  expect(kit.sampleName(0)).toBe("SIN");
  let lo = Infinity;
  let hi = -Infinity;
  for (const v of kit.sampleData(0)) {
    lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  expect(hi - lo).toBeGreaterThan(0.5);
  expect(lo).toBeLessThan(0);
  expect(hi).toBeGreaterThan(0);
  console.log(`[kit-compile] the plugin's facets compiled PLUG/SIN to a ${bytes.length}B bank in ${steps} frame pump(s)`);
});
