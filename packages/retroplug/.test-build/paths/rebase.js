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

// packages/retroplug/src/pathUtil.ts
function isAbsolute(p) {
  return /^([a-zA-Z]:[\\/]|[\\/])/.test(p);
}

// packages/retroplug/src/projectPaths.ts
function rebaseToAbsolute(field, baseDir) {
  if (!field || isAbsolute(field) || !baseDir) return field;
  const combined = baseDir.replace(/[\\/]+$/, "") + "/" + field;
  const out = [];
  for (const s of combined.split(/[\\/]+/)) {
    if (s === "" || s === ".") continue;
    if (s === "..") {
      if (out.length && out[out.length - 1] !== "..") out.pop();
      else out.push("..");
    } else {
      out.push(s);
    }
  }
  const body = out.join("/");
  return combined.startsWith("/") ? "/" + body : body;
}
function components(p) {
  let root = "";
  let rest = p;
  const drive = /^[a-zA-Z]:/.exec(rest);
  if (drive) {
    root = drive[0];
    rest = rest.slice(2);
  }
  if (/^[\\/]/.test(rest)) {
    root += "/";
    rest = rest.replace(/^[\\/]+/, "");
  }
  const parts = rest.split(/[\\/]+/).filter((s) => s !== "" && s !== ".");
  return { root, parts };
}
function lexicallyRelative(path, base) {
  const P = components(path);
  const B = components(base);
  if (P.root !== B.root) return "";
  let i = 0;
  while (i < P.parts.length && i < B.parts.length && P.parts[i] === B.parts[i]) i++;
  const up = [];
  for (let k = i; k < B.parts.length; k++) up.push("..");
  const rel = [...up, ...P.parts.slice(i)];
  return rel.length === 0 ? "." : rel.join("/");
}
function rebaseToRelative(field, baseDir, canonicalize) {
  if (!field || !isAbsolute(field)) return field;
  const base = canonicalize(baseDir);
  const p = canonicalize(field);
  if (!base || !p) return field;
  const rel = lexicallyRelative(p, base);
  if (rel === "" || rel.startsWith("..")) return field;
  return rel;
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

// packages/retroplug/testing/mockBackend.ts
var enc = new TextEncoder();
var dec = new TextDecoder();
var PK_MAGIC = [80, 75, 3, 4];
function pushU32(out, n) {
  out.push(n & 255, n >>> 8 & 255, n >>> 16 & 255, n >>> 24 & 255);
}
function readU32(b, off) {
  return (b[off] | b[off + 1] << 8 | b[off + 2] << 16 | b[off + 3] << 24) >>> 0;
}
function stateBytesFor(id) {
  return Uint8Array.of(83, 84, id & 255, id >>> 8 & 255);
}
function sramBytesFor(id) {
  return Uint8Array.of(83, 82, id & 255, id >>> 8 & 255);
}
var MockBackend = class {
  constructor(configDir = "/config") {
    this.files = /* @__PURE__ */ new Map();
    /** Names of Backend methods called, in order — lets tests assert side-effects
     *  (e.g. that a write went through writeFileAtomic, not writeFile). */
    this.log = [];
    // --- emulator-lifecycle bookkeeping (mock-only) -------------------------
    // (no id counter — TS owns ids and passes them to constructSystem.)
    this.systems = /* @__PURE__ */ new Map();
    /** Every ConstructSpec passed to constructSystem, in order — lets tests assert
     *  the CONCRETE paths TS resolved (savPath/statePath/replaceId). */
    this.constructCalls = [];
    // NOTE: duplicate/reload no longer hit the backend — they are TS orchestration over constructSystem,
    // so their effect shows up in constructCalls (with stateBytes/sramBytes + replaceId), not a dedicated log.
    // --- file-dialog bookkeeping (mock-only) --------------------------------
    /** Opts passed to each openFileBrowser call, in order — lets tests assert which
     *  dialog (ROM-or-sav vs ROM-only) was opened. */
    this.fileBrowserCalls = [];
    /** One response per dialog the flow will open, consumed FIFO. `null` = cancel. */
    this.browseQueue = [];
    /** Live-config applies recorded for assertions. */
    this.applySettingCalls = [];
    this.applyRoleCalls = [];
    this.serialOutCaptureCalls = [];
    this.audioRoutingCalls = [];
    this.pressButtonCalls = [];
    /** Ids passed to the pump reads, in order. */
    this.readStateCalls = [];
    this.readSramCalls = [];
    /** Test-driven SRAM content per system (setSram), overriding the deterministic
     *  default — lets a test model SRAM changing over time (dedup vs write). */
    this.sramOverrides = /* @__PURE__ */ new Map();
    /** Paths queued by emitFileChange, drained by drainChangedPaths — simulates the
     *  native watcher (efsw + ROM mtime poll) firing. */
    this.changedPaths = [];
    /** Entry lists passed to zip / archives passed to unzip, in order. */
    this.zipCalls = [];
    this.unzipCalls = [];
    this.dir = configDir;
  }
  /** Seed the responses openFileBrowser will resolve to, in dialog order. */
  queueBrowse(...responses) {
    this.browseQueue.push(...responses);
  }
  // --- test helpers (not part of Backend) ---------------------------------
  /** Put a file on the fake disk (string is UTF-8 encoded). */
  seed(path, contents) {
    const bytes = typeof contents === "string" ? enc.encode(contents) : new Uint8Array(contents);
    this.files.set(this.canonicalize(path), bytes);
  }
  /** Drive a system's live SRAM content (what readSram returns), overriding the
   *  deterministic default — lets a test model SRAM changing between flushes. */
  setSram(id, bytes) {
    this.sramOverrides.set(id, new Uint8Array(bytes));
  }
  /** Simulate the native watcher firing for `path` (config.json / a bindings profile /
   *  a ROM) — the next drainChangedPaths returns it. */
  emitFileChange(path) {
    this.changedPaths.push(path);
  }
  /** Read a file back as text, or null if absent. */
  readText(path) {
    const b = this.readFile(path);
    return b ? dec.decode(b) : null;
  }
  /** Every path currently on the fake disk, sorted. */
  paths() {
    return [...this.files.keys()].sort();
  }
  // --- Backend ------------------------------------------------------------
  readFile(path) {
    this.log.push("readFile");
    const b = this.files.get(this.canonicalize(path));
    return b ? new Uint8Array(b) : null;
  }
  writeFile(path, bytes) {
    this.log.push("writeFile");
    this.files.set(this.canonicalize(path), new Uint8Array(bytes));
    return true;
  }
  writeFileAtomic(path, bytes) {
    this.log.push("writeFileAtomic");
    this.files.set(this.canonicalize(path), new Uint8Array(bytes));
    return true;
  }
  fileExists(path) {
    this.log.push("fileExists");
    return this.files.has(this.canonicalize(path));
  }
  rename(from, to) {
    this.log.push("rename");
    const cf = this.canonicalize(from);
    const bytes = this.files.get(cf);
    if (!bytes) return false;
    this.files.delete(cf);
    this.files.set(this.canonicalize(to), bytes);
    return true;
  }
  listDir(dir) {
    this.log.push("listDir");
    const parent = this.canonicalize(dir);
    const out = [];
    for (const key of this.files.keys()) {
      const slash = key.lastIndexOf("/");
      const keyParent = slash <= 0 ? "/" : key.slice(0, slash);
      if (keyParent === parent) out.push(key.slice(slash + 1));
    }
    return out.sort();
  }
  deleteFile(path) {
    this.log.push("deleteFile");
    return this.files.delete(this.canonicalize(path));
  }
  drainChangedPaths() {
    this.log.push("drainChangedPaths");
    const out = this.changedPaths;
    this.changedPaths = [];
    return out;
  }
  readFilePrefix(path, length) {
    this.log.push("readFilePrefix");
    const b = this.files.get(this.canonicalize(path));
    return b ? new Uint8Array(b.slice(0, length)) : null;
  }
  canonicalize(path) {
    const abs = path.startsWith("/") ? path : `${this.dir}/${path}`;
    const out = [];
    for (const seg of abs.split(/[\\/]+/)) {
      if (seg === "" || seg === ".") continue;
      if (seg === "..") {
        if (out.length) out.pop();
      } else {
        out.push(seg);
      }
    }
    return "/" + out.join("/");
  }
  configDir() {
    return this.dir;
  }
  version() {
    return "0.0.0";
  }
  // --- Emulator lifecycle -------------------------------------------------
  constructSystem(spec, id) {
    this.log.push("constructSystem");
    this.constructCalls.push(spec);
    if (!spec.embeddedRom) {
      const bytes = this.files.get(this.canonicalize(spec.romPath));
      if (!bytes || detectPlatform(bytes) === "unknown") return false;
    }
    if (spec.replaceId !== void 0) this.systems.delete(spec.replaceId);
    this.systems.set(id, {
      // TS owns the id counter; the mock just records under the given id
      romPath: spec.romPath,
      embeddedRom: spec.embeddedRom,
      savPath: spec.savPath,
      statePath: spec.statePath,
      restoredFromBytes: spec.sramBytes !== void 0 || spec.stateBytes !== void 0
    });
    return true;
  }
  // duplicate + reload are TS orchestration (SystemsStore) over readState/readSram + constructSystem —
  // the mock has no bespoke method; those store paths land as constructCalls (restoredFromBytes).
  readState(id) {
    this.log.push("readState");
    this.readStateCalls.push(id);
    return this.systems.has(id) ? stateBytesFor(id) : null;
  }
  readSram(id) {
    this.log.push("readSram");
    this.readSramCalls.push(id);
    const override = this.sramOverrides.get(id);
    if (override) return new Uint8Array(override);
    return this.systems.has(id) ? sramBytesFor(id) : null;
  }
  getFrame(id) {
    this.log.push("getFrame");
    if (!this.systems.has(id)) return null;
    return { width: 160, height: 144, published: false, pixels: new Uint8Array(0) };
  }
  // Live-core debug reads: the mock has no real core, so these are deterministic stand-ins that keep
  // `implements Backend` satisfied (the real behaviour is proven against the native backend).
  getApuState(_id) {
    this.log.push("getApuState");
    const square = () => ({
      enabled: false,
      period: 0,
      timer: 0,
      duty: 0,
      outputVolume: 0,
      frequency: 0,
      lengthCounter: 0,
      constantVolume: false,
      envelopeVolume: 0,
      sweepEnabled: false,
      sweepNegate: false,
      sweepPeriod: 0,
      sweepShift: 0
    });
    return {
      pulse1: square(),
      pulse2: square(),
      triangle: { enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, linearCounter: 0 },
      noise: { enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, modeFlag: false, constantVolume: false, envelopeVolume: 0 },
      dmc: { enabled: false, sampleAddr: 0, sampleLength: 0, bytesRemaining: 0, period: 0, outputVolume: 0, loop: false, irqEnabled: false, sampleRate: 0 }
    };
  }
  getPpuState(_id) {
    this.log.push("getPpuState");
    return {
      scanline: 0,
      cycle: 0,
      frameCount: 0,
      control: 0,
      mask: 0,
      status: 0,
      scrollX: 0,
      videoRamAddr: 0,
      tmpVideoRamAddr: 0,
      writeToggle: false,
      spriteRamAddr: 0
    };
  }
  readCpu(id, _addr) {
    this.log.push("readCpu");
    return this.systems.has(id) ? 0 : null;
  }
  writeCpu(id, _addr, _value) {
    this.log.push("writeCpu");
    return this.systems.has(id);
  }
  readMemory(id, _region) {
    this.log.push("readMemory");
    return this.systems.has(id) ? new Uint8Array(0) : null;
  }
  getCpuRegisters(_id) {
    this.log.push("getCpuRegisters");
    return [];
  }
  stepInstruction(id) {
    this.log.push("stepInstruction");
    return this.systems.has(id) ? 1 : 0;
  }
  drainEvents(id) {
    this.log.push("drainEvents");
    return this.systems.has(id) ? [{ type: 0, operationType: 1, address: 16384, value: 128, programCounter: 32768, scanline: 0, cycle: 0 }] : [];
  }
  loadLabels(_id, _path) {
    this.log.push("loadLabels");
    return false;
  }
  setCpuRegister(id, _name, _value) {
    this.log.push("setCpuRegister");
    return this.systems.has(id);
  }
  runUntilPc(_id, _target, _maxCycles) {
    this.log.push("runUntilPc");
    return false;
  }
  setBreakpoints(id, _breakpoints) {
    this.log.push("setBreakpoints");
    return this.systems.has(id);
  }
  runUntilBreak(_id, _maxCycles) {
    this.log.push("runUntilBreak");
    return { broke: false, pc: 0, breakpointId: -1 };
  }
  setTrace(id, _on) {
    this.log.push("setTrace");
    return this.systems.has(id);
  }
  readTrace(_id, _count) {
    this.log.push("readTrace");
    return [];
  }
  stepInto(id) {
    this.log.push("stepInto");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }
  stepOver(id) {
    this.log.push("stepOver");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }
  stepOut(id) {
    this.log.push("stepOut");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }
  beginProfile(id) {
    this.log.push("beginProfile");
    return this.systems.has(id);
  }
  readProfile(_id) {
    this.log.push("readProfile");
    return [];
  }
  disassemble(_id, _addr, _count) {
    this.log.push("disassemble");
    return [];
  }
  getCallStack(_id) {
    this.log.push("getCallStack");
    return [];
  }
  zip(entries) {
    this.log.push("zip");
    this.zipCalls.push(entries.map((e) => ({ name: e.name, bytes: new Uint8Array(e.bytes) })));
    const parts = [...PK_MAGIC];
    for (const e of entries) {
      const name = enc.encode(e.name);
      pushU32(parts, name.length);
      for (const b of name) parts.push(b);
      pushU32(parts, e.bytes.length);
      for (const b of e.bytes) parts.push(b);
    }
    return new Uint8Array(parts);
  }
  unzip(bytes) {
    this.log.push("unzip");
    this.unzipCalls.push(new Uint8Array(bytes));
    if (bytes.length < 4 || !PK_MAGIC.every((b, i) => bytes[i] === b)) return null;
    const out = [];
    let off = 4;
    while (off + 4 <= bytes.length) {
      const nameLen = readU32(bytes, off);
      off += 4;
      const name = dec.decode(bytes.slice(off, off + nameLen));
      off += nameLen;
      const byteLen = readU32(bytes, off);
      off += 4;
      out.push({ name, bytes: bytes.slice(off, off + byteLen) });
      off += byteLen;
    }
    return out;
  }
  savFromJson(_json) {
    this.log.push("savFromJson");
    return Uint8Array.of(106, 107);
  }
  removeSystem(id) {
    this.log.push("removeSystem");
    return this.systems.delete(id);
  }
  openFileBrowser(opts) {
    this.log.push("openFileBrowser");
    this.fileBrowserCalls.push(opts);
    return Promise.resolve(this.browseQueue.length ? this.browseQueue.shift() : null);
  }
  applySystemSetting(id, key, value) {
    this.log.push("applySystemSetting");
    this.applySettingCalls.push({ id, key, value });
    return true;
  }
  applyRoleConfig(id, kind, config) {
    this.log.push("applyRoleConfig");
    this.applyRoleCalls.push({ id, kind, config });
    return true;
  }
  setSerialOutCapture(id, on) {
    this.log.push("setSerialOutCapture");
    this.serialOutCaptureCalls.push({ id, on });
    return true;
  }
  setAudioRouting(mode) {
    this.log.push("setAudioRouting");
    this.audioRoutingCalls.push(mode);
    return mode >= 0 && mode <= 2;
  }
  pressButton(id, button, down) {
    this.log.push("pressButton");
    this.pressButtonCalls.push({ id, button, down });
    return true;
  }
  // --- test helpers (not part of Backend) ---------------------------------
  /** The ids the mock currently considers live, sorted. */
  liveSystemIds() {
    return [...this.systems.keys()].sort((a, b) => a - b);
  }
  /** The live ids that were reconstructed from in-memory zip blobs (import), sorted. */
  restoredIds() {
    return [...this.systems.entries()].filter(([, s]) => s.restoredFromBytes).map(([id]) => id).sort((a, b) => a - b);
  }
};

// packages/retroplug/test/paths/rebase.test.ts
var identity = (p) => p;
test("rebaseToAbsolute: joins a relative field onto the base dir", () => {
  expect(rebaseToAbsolute("game.gb", "/project")).toBe("/project/game.gb");
  expect(rebaseToAbsolute("sub/game.gb", "/project")).toBe("/project/sub/game.gb");
  expect(rebaseToAbsolute("./game.gb", "/project")).toBe("/project/game.gb");
});
test("rebaseToAbsolute: collapses .. against the base", () => {
  expect(rebaseToAbsolute("../sib/game.gb", "/project/nested")).toBe("/project/sib/game.gb");
});
test("rebaseToAbsolute: absolute / empty / no-base fields are left untouched", () => {
  expect(rebaseToAbsolute("/abs/game.gb", "/project")).toBe("/abs/game.gb");
  expect(rebaseToAbsolute("", "/project")).toBe("");
  expect(rebaseToAbsolute("game.gb", "")).toBe("game.gb");
});
test("lexicallyRelative: a path at/under the base", () => {
  expect(lexicallyRelative("/project/game.gb", "/project")).toBe("game.gb");
  expect(lexicallyRelative("/project/sub/game.gb", "/project")).toBe("sub/game.gb");
});
test("lexicallyRelative: outside the base emits a .. chain; equal is '.'", () => {
  expect(lexicallyRelative("/other/game.gb", "/project")).toBe("../other/game.gb");
  expect(lexicallyRelative("/project", "/project")).toBe(".");
});
test("lexicallyRelative: different roots yield an empty relative", () => {
  expect(lexicallyRelative("C:/a/game.gb", "D:/a")).toBe("");
});
test("rebaseToRelative: a field under the base becomes forward-slash relative", () => {
  expect(rebaseToRelative("/project/sub/game.gb", "/project", identity)).toBe("sub/game.gb");
});
test("rebaseToRelative: an asset outside the base is kept absolute (no ../ chains)", () => {
  expect(rebaseToRelative("/other/game.gb", "/project", identity)).toBe("/other/game.gb");
});
test("rebaseToRelative: empty / already-relative fields are left untouched", () => {
  expect(rebaseToRelative("", "/project", identity)).toBe("");
  expect(rebaseToRelative("sub/game.gb", "/project", identity)).toBe("sub/game.gb");
});
test("rebaseToRelative: canonicalization is applied before the relative is computed", () => {
  const canon = new MockBackend("/project").canonicalize.bind(new MockBackend("/project"));
  expect(rebaseToRelative("/project/./sub/../game.gb", "/project", canon)).toBe("game.gb");
});
