// The DSP-JS-runtime seam over the REAL native host (plans/03): the role KERNEL crosses as QuickJS
// bytecode (compiled on a scratch context, so the DSP side never re-parses source), the system
// structure crosses as a JSON string, and re-loading the bytecode swaps the kernel (hot-reload). A
// garbage source fails to compile cleanly. Behavioural coverage (roles produce the right output)
// lives in the pure-TS dsp tests + the audio tests (dsp-lsdj-midisync, dsp-serial); this file proves
// the byte seam itself, with no ROM or audio.
import { test, expect } from "../testing/harness";
import { createDspRuntime } from "../src/dspRuntime";

declare const __DSP_KERNEL_BUNDLE__: string;

test("compile → load the role kernel, then push structure (the bytecode + JSON seam)", () => {
  const dsp = createDspRuntime();
  const bc = dsp.compileScript(__DSP_KERNEL_BUNDLE__)!;
  expect(bc.length > 0).toBeTruthy(); // real bytecode bytes came back
  expect(dsp.loadKernel(bc)).toBeTruthy();
  // Structure crosses as JSON and parses into the kernel without throwing.
  expect(dsp.setSystems({ systems: [{ id: 1, pipeline: [{ kind: "mgb", config: {} }] }] })).toBeTruthy();
});

test("re-loading the kernel bytecode swaps it (hot-reload)", () => {
  const dsp = createDspRuntime();
  const bc = dsp.compileScript(__DSP_KERNEL_BUNDLE__)!;
  expect(dsp.loadKernel(bc)).toBeTruthy();
  expect(dsp.loadKernel(bc)).toBeTruthy(); // a second load re-instantiates the kernel cleanly
});

test("a garbage source fails to compile → null bytecode", () => {
  const dsp = createDspRuntime();
  expect(dsp.compileScript("function ( { this is not js")).toBe(null);
});
