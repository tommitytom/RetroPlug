// The pure parts of `retroplug-cli n8-play`: the step grammar (incl. the raw / sysex / file steps that
// let a hardware check push a SysEx or a panic byte from the same command that plays the notes), the
// iNES mapper read, and the expansion-master-volume decision.
import { test, expect } from "../../testing/harness";
import { stepBytes, parseHexBytes, inesMapper, expVolPlan, EXPANSION_MAPPERS } from "../../cli/sessions/n8-play";

test("note / cc / wait steps are 1-based on channel and default velocity to 100", () => {
  expect(stepBytes("on:6:69")).toEqual({ bytes: [0x95, 69, 100], waitMs: 0 });
  expect(stepBytes("on:1:60:127")).toEqual({ bytes: [0x90, 60, 127], waitMs: 0 });
  expect(stepBytes("off:16:60")).toEqual({ bytes: [0x8f, 60, 0], waitMs: 0 });
  expect(stepBytes("cc:6:20:127")).toEqual({ bytes: [0xb5, 20, 127], waitMs: 0 });
  expect(stepBytes("wait:2000")).toEqual({ bytes: [], waitMs: 2000 });
  expect(() => stepBytes("on:17:60")).toThrow("channel");
  expect(() => stepBytes("bogus:1")).toThrow("unknown step");
});

test("raw: sends hex bytes verbatim; sysex: wraps a 7-bit payload in F0..F7", () => {
  expect(stepBytes("raw:ff")).toEqual({ bytes: [0xff], waitMs: 0 });
  expect(stepBytes("raw:f0,7d,42,02,f7").bytes).toEqual([0xf0, 0x7d, 0x42, 0x02, 0xf7]);
  expect(stepBytes("raw:0xF0, 0x7D").bytes).toEqual([0xf0, 0x7d]); // prefixes + spaces tolerated
  expect(stepBytes("sysex:7d,42,02").bytes).toEqual([0xf0, 0x7d, 0x42, 0x02, 0xf7]);
  expect(() => stepBytes("sysex:7d,90")).toThrow("payload byte 1 is 0x90");
  expect(() => stepBytes("raw:zz")).toThrow('bad hex byte "zz"');
  expect(() => stepBytes("raw:")).toThrow("expected hex bytes");
  expect(parseHexBytes("00,7f,80", "x")).toEqual([0, 0x7f, 0x80]);
});

test("file: reads the bytes up front through the supplied reader and refuses a missing or empty file", () => {
  const files: Record<string, Uint8Array> = { "flat.bin": new Uint8Array([0xf0, 0x7d, 0x42, 0x02, 0xf7]), "empty.bin": new Uint8Array(0) };
  const read = (p: string) => files[p] ?? null;
  expect(stepBytes("file:flat.bin", read).bytes).toEqual([0xf0, 0x7d, 0x42, 0x02, 0xf7]);
  expect(() => stepBytes("file:missing.bin", read)).toThrow("cannot read missing.bin");
  expect(() => stepBytes("file:empty.bin", read)).toThrow("is empty");
  expect(() => stepBytes("file:flat.bin")).toThrow("no file reader");
});

test("inesMapper reads the header's mapper number, null for a non-iNES image", () => {
  const ines = (mapper: number) => {
    const b = new Uint8Array(16);
    b.set([0x4e, 0x45, 0x53, 0x1a]);
    b[6] = (mapper & 0x0f) << 4;
    b[7] = mapper & 0xf0;
    return b;
  };
  expect(inesMapper(ines(0))).toBe(0);
  expect(inesMapper(ines(24))).toBe(24);
  expect(inesMapper(ines(85))).toBe(85);
  expect(inesMapper(new Uint8Array([1, 2, 3]))).toBe(null);
});

test("expVolPlan: explicit wins; --rom with an expansion mapper defaults to unity; nothing is a warning", () => {
  expect(expVolPlan("0", undefined)).toEqual({ value: 0, note: "expansion master volume <- 0 (MUTE)" });
  expect(expVolPlan("128", 24).value).toBe(128);
  expect(() => expVolPlan("300", undefined)).toThrow("--exp-vol");
  for (const m of Object.keys(EXPANSION_MAPPERS).map(Number)) expect(expVolPlan(undefined, m).value, `mapper ${m}`).toBe(128);
  expect(expVolPlan(undefined, 24).note).toBe("expansion master volume <- 128 (unity; --rom is mapper 24 = VRC6)");
  expect(expVolPlan(undefined, 0)).toEqual({ value: null, note: "mapper 0 has no expansion audio; expansion master volume left as it was" });
  expect(expVolPlan(undefined, null).value).toBe(null);
  const warn = expVolPlan(undefined, undefined);
  expect(warn.value).toBe(null);
  expect(warn.note.startsWith("warning: --exp-vol not set")).toBeTruthy();
});
