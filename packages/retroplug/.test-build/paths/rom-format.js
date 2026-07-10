// packages/retroplug/testing/harness.ts
var cases = [];
var scheduled = false;
function test(name, fn) {
  cases.push({ name, fn });
  if (!scheduled) {
    scheduled = true;
    Promise.resolve().then(runAll);
  }
}
function fmt(v) {
  if (v instanceof Uint8Array) {
    const head = Array.from(v.slice(0, 8)).join(",");
    return `Uint8Array(${v.length})[${head}${v.length > 8 ? ",\u2026" : ""}]`;
  }
  try {
    return typeof v === "string" ? JSON.stringify(v) : JSON.stringify(v) ?? String(v);
  } catch {
    return String(v);
  }
}
function expect(actual) {
  return {
    toBe(expected) {
      if (!Object.is(actual, expected)) {
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
      }
    },
    toEqual(expected) {
      const a = JSON.stringify(actual);
      const b = JSON.stringify(expected);
      if (a !== b) throw new Error(`expected ${b}, got ${a}`);
    },
    toBeTruthy() {
      if (!actual) throw new Error(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy() {
      if (actual) throw new Error(`expected falsy, got ${fmt(actual)}`);
    },
    toThrow(match) {
      if (typeof actual !== "function") throw new Error("toThrow expects a function");
      let threw;
      let didThrow = false;
      try {
        actual();
      } catch (e) {
        didThrow = true;
        threw = e;
      }
      if (!didThrow) throw new Error("expected function to throw, but it did not");
      if (match !== void 0) {
        const msg = threw instanceof Error ? threw.message : String(threw);
        const ok = typeof match === "string" ? msg.includes(match) : match.test(msg);
        if (!ok) throw new Error(`expected throw matching ${fmt(match)}, got ${fmt(msg)}`);
      }
    }
  };
}
function exit(code) {
  const g = globalThis;
  g.tjs?.exit(code);
}
async function runAll() {
  const out = ["TAP version 13", `1..${cases.length}`];
  let failed = 0;
  for (let i = 0; i < cases.length; i++) {
    const { name, fn } = cases[i];
    try {
      await fn();
      out.push(`ok ${i + 1} - ${name}`);
    } catch (e) {
      failed++;
      const detail = e instanceof Error ? `${e.message}
${e.stack ?? ""}` : String(e);
      out.push(`not ok ${i + 1} - ${name}`, "  ---");
      for (const line of detail.split("\n")) out.push(`  ${line}`);
      out.push("  ...");
    }
  }
  console.log(out.join("\n"));
  exit(failed ? 1 : 0);
}

// packages/retroplug/src/platform.ts
var GBA_LOGO = [
  36,
  255,
  174,
  81,
  105,
  154,
  162,
  33,
  61,
  132,
  130,
  10,
  132,
  228,
  9,
  173,
  17,
  36,
  139,
  152,
  192,
  129,
  127,
  33,
  163,
  82,
  190,
  25,
  147,
  9,
  206,
  32
];
var GBA_LOGO_OFFSET = 4;
var GB_LOGO = [
  206,
  237,
  102,
  102,
  204,
  13,
  0,
  11,
  3,
  115,
  0,
  131,
  0,
  12,
  0,
  13,
  0,
  8,
  17,
  31,
  136,
  137,
  0,
  14,
  220,
  204,
  110,
  230,
  221,
  221,
  217,
  153,
  187,
  187,
  103,
  99,
  110,
  14,
  236,
  204,
  221,
  220,
  153,
  159,
  187,
  185,
  51,
  62
];
var GB_LOGO_OFFSET = 260;
function matchesAt(bytes, offset, sig) {
  if (bytes.length < offset + sig.length) return false;
  for (let i = 0; i < sig.length; i++) {
    if (bytes[offset + i] !== sig[i]) return false;
  }
  return true;
}
function detectPlatform(bytes) {
  if (bytes.length >= 4 && bytes[0] === 78 && bytes[1] === 69 && bytes[2] === 83 && bytes[3] === 26) {
    return "nes";
  }
  if (matchesAt(bytes, GBA_LOGO_OFFSET, GBA_LOGO)) return "gba";
  if (matchesAt(bytes, GB_LOGO_OFFSET, GB_LOGO)) return "gb";
  return "unknown";
}

// packages/retroplug/test/paths/rom-format.test.ts
var NES_MAGIC = [78, 69, 83, 26];
var GBA_LOGO2 = [
  36,
  255,
  174,
  81,
  105,
  154,
  162,
  33,
  61,
  132,
  130,
  10,
  132,
  228,
  9,
  173,
  17,
  36,
  139,
  152,
  192,
  129,
  127,
  33,
  163,
  82,
  190,
  25,
  147,
  9,
  206,
  32
];
var GB_LOGO2 = [
  206,
  237,
  102,
  102,
  204,
  13,
  0,
  11,
  3,
  115,
  0,
  131,
  0,
  12,
  0,
  13,
  0,
  8,
  17,
  31,
  136,
  137,
  0,
  14,
  220,
  204,
  110,
  230,
  221,
  221,
  217,
  153,
  187,
  187,
  103,
  99,
  110,
  14,
  236,
  204,
  221,
  220,
  153,
  159,
  187,
  185,
  51,
  62
];
function buf(len, at, bytes) {
  const b = new Uint8Array(len);
  b.set(bytes, at);
  return b;
}
test("nes: iNES magic at offset 0", () => {
  expect(detectPlatform(buf(64, 0, NES_MAGIC))).toBe("nes");
  expect(detectPlatform(new Uint8Array(NES_MAGIC))).toBe("nes");
});
test("gba: Nintendo logo at 0x04", () => {
  expect(detectPlatform(buf(8192, 4, GBA_LOGO2))).toBe("gba");
  expect(detectPlatform(buf(4 + 32, 4, GBA_LOGO2))).toBe("gba");
});
test("gb: Game Boy logo at 0x104 (DMG + CGB carts share it)", () => {
  expect(detectPlatform(buf(32768, 260, GB_LOGO2))).toBe("gb");
  expect(detectPlatform(buf(260 + 48, 260, GB_LOGO2))).toBe("gb");
});
test("unknown: no recognizable magic", () => {
  expect(detectPlatform(new Uint8Array(32768))).toBe("unknown");
  expect(detectPlatform(new Uint8Array([1, 2, 3, 4, 5, 6]))).toBe("unknown");
});
test("unknown: short / empty buffers fall through the length guards", () => {
  expect(detectPlatform(new Uint8Array(0))).toBe("unknown");
  expect(detectPlatform(new Uint8Array([78, 69, 83]))).toBe("unknown");
  expect(detectPlatform(buf(260 + 47, 260, GB_LOGO2.slice(0, 47)))).toBe("unknown");
});
test("priority: NES beats a GB logo, GBA beats a GB logo", () => {
  const nesOverGb = buf(32768, 260, GB_LOGO2);
  nesOverGb.set(NES_MAGIC, 0);
  expect(detectPlatform(nesOverGb)).toBe("nes");
  const gbaOverGb = buf(32768, 260, GB_LOGO2);
  gbaOverGb.set(GBA_LOGO2, 4);
  expect(detectPlatform(gbaOverGb)).toBe("gba");
});
