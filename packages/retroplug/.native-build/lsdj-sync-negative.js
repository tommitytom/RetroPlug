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

// packages/retroplug/src/realBackend.ts
var pendingBrowse = null;
var browseResolverInstalled = false;
function installBrowseResolver() {
  if (browseResolverInstalled) return;
  browseResolverInstalled = true;
  globalThis.__rp_onFileBrowserResult = (path) => {
    const resolve = pendingBrowse;
    pendingBrowse = null;
    resolve?.(path ?? null);
  };
}
function browseFile(opts) {
  const hook = globalThis.__rp_openFileBrowser;
  if (typeof hook !== "function") return Promise.resolve(null);
  if (pendingBrowse) return Promise.resolve(null);
  installBrowseResolver();
  return new Promise((resolve) => {
    pendingBrowse = resolve;
    hook(opts.title, opts.patterns.join(" "), !!opts.saving, opts.defaultName ?? "");
  });
}
function resolveSend() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
function createRealBackend() {
  const send = resolveSend();
  let nextId2 = 1;
  const call = (method, ...params) => {
    const reply = send({ jsonrpc: "2.0", id: nextId2++, method, params });
    if (reply == null) return void 0;
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };
  const bytesOrNull = (v) => v == null ? null : v;
  const specParams = (spec, id) => {
    const p = { id, romPath: spec.romPath, platform: spec.platform, core: spec.core, embeddedRom: spec.embeddedRom };
    if (spec.savPath != null) p.savPath = spec.savPath;
    if (spec.statePath != null) p.statePath = spec.statePath;
    if (spec.replaceId !== void 0) p.replaceId = spec.replaceId;
    if (spec.sramBytes) p.sramBytes = spec.sramBytes;
    if (spec.stateBytes) p.stateBytes = spec.stateBytes;
    if (spec.settings != null) p.settings = spec.settings;
    return p;
  };
  return {
    // --- fs / config / codec (increment 1) --------------------------------
    readFile: (path) => bytesOrNull(call("readFile", path)),
    writeFile: (path, bytes) => call("writeFile", path, bytes),
    writeFileAtomic: (path, bytes) => call("writeFileAtomic", path, bytes),
    fileExists: (path) => call("fileExists", path),
    rename: (from, to) => call("rename", from, to),
    listDir: (dir) => call("listDir", dir),
    deleteFile: (path) => call("deleteFile", path),
    drainChangedPaths: () => call("drainChangedPaths"),
    canonicalize: (path) => call("canonicalize", path),
    readFilePrefix: (path, length) => bytesOrNull(call("readFilePrefix", path, length)),
    configDir: () => call("configDir"),
    version: () => call("version"),
    zip: (entries) => bytesOrNull(call("zip", entries)),
    // {name, bytes: Uint8Array} matches BackendZipInput
    unzip: (bytes) => call("unzip", bytes) ?? null,
    savFromJson: (json) => call("savFromJson", json),
    // Bytestring result → Uint8Array
    // --- emulator lifecycle / reads ---------------------------------------
    constructSystem: (spec, id) => call("constructSystem", specParams(spec, id)),
    removeSystem: (id) => call("removeSystem", id),
    applySystemSetting: (id, key, value) => call("applySystemSetting", id, key, typeof value === "boolean" ? value ? 1 : 0 : value),
    applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)),
    setSerialOutCapture: (id, on) => call("setSerialOutCapture", id, on),
    setAudioRouting: (mode) => call("setAudioRouting", mode),
    pressButton: (id, button, down) => call("pressButton", id, button, down),
    readState: (id) => bytesOrNull(call("readState", id)),
    readSram: (id) => bytesOrNull(call("readSram", id)),
    getFrame: (id) => {
      const r = call("getFrame", id);
      if (r == null || r.width === 0) return null;
      return { width: r.width, height: r.height, published: r.published, pixels: r.data ?? new Uint8Array(0) };
    },
    // --- live-core debug reads (spec/09-cli-debugging.md) ------------------
    // Field-for-field mirrors of the native reflect-cpp structs → a direct cast (the DspAllocStats pattern).
    getApuState: (id) => call("getApuState", id),
    getPpuState: (id) => call("getPpuState", id),
    readCpu: (id, addr) => call("readCpu", id, addr),
    writeCpu: (id, addr, value) => call("writeCpu", id, addr, value),
    readMemory: (id, region) => bytesOrNull(call("readMemory", id, region)),
    getCpuRegisters: (id) => call("getCpuRegisters", id),
    stepInstruction: (id) => Number(call("stepInstruction", id)),
    drainEvents: (id) => call("drainEvents", id),
    loadLabels: (id, path) => call("loadLabels", id, path),
    setCpuRegister: (id, name, value) => call("setCpuRegister", id, name, value),
    runUntilPc: (id, target, maxCycles) => call("runUntilPc", id, target, maxCycles),
    setBreakpoints: (id, breakpoints) => call("setBreakpoints", id, breakpoints.map((b) => ({ type: b.type, start: b.start, end: b.end ?? 0, condition: b.condition ?? "" }))),
    runUntilBreak: (id, maxCycles) => call("runUntilBreak", id, maxCycles),
    setTrace: (id, on) => call("setTrace", id, on),
    readTrace: (id, count) => call("readTrace", id, count),
    stepInto: (id) => call("stepInto", id),
    stepOver: (id) => call("stepOver", id),
    stepOut: (id) => call("stepOut", id),
    beginProfile: (id) => call("beginProfile", id),
    readProfile: (id) => call("readProfile", id),
    disassemble: (id, addr, count) => call("disassemble", id, addr, count),
    getCallStack: (id) => call("getCallStack", id),
    // --- async dialog (UI-direct native hook; see browseFile above) -------
    openFileBrowser: (opts) => browseFile(opts)
  };
}

// packages/retroplug/src/audioDriver.ts
function resolveSend2() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
function createAudioDriver() {
  const send = resolveSend2();
  let nextId2 = 1;
  const call = (method, ...params) => {
    const reply = send({ jsonrpc: "2.0", id: nextId2++, method, params });
    if (reply == null) return void 0;
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };
  const ints = (b) => Array.from(b);
  return {
    pressButton: (id, button, down) => call("pressButton", id, button, down),
    screenshot: (id, path) => call("screenshot", id, path),
    renderAudio: (ms) => {
      const bytes = call("renderAudio", ms);
      return new Float32Array(bytes.slice().buffer);
    },
    renderAudioPerSystem: (ms) => {
      const bufs = call("renderAudioPerSystem", ms);
      return bufs.map((b) => new Float32Array(b.slice().buffer));
    },
    setTransport: (running) => call("setTransport", running),
    setBpm: (bpm) => call("setBpm", bpm),
    stageMidiIn: (bytes) => call("stageMidiIn", ints(bytes)),
    drainMidiOut: () => call("drainMidiOut") ?? [],
    dspAllocStats: () => call("dspAllocStats"),
    dspResetAllocStats: (disableAutoGc) => call("dspResetAllocStats", disableAutoGc),
    dspRunGc: () => call("dspRunGc"),
    dspTraceReset: (arm) => call("dspTraceReset", arm),
    dspTrace: () => call("dspTrace"),
    dspTraceNames: () => call("dspTraceNames"),
    startAudio: () => call("startAudio"),
    stopAudio: () => call("stopAudio"),
    sleepMs: (ms) => call("sleepMs", ms),
    audioCaptured: () => call("audioCaptured"),
    systemCount: () => call("systemCount"),
    drainReleased: () => call("drainReleased")
  };
}

// packages/retroplug/src/lsdjSav.ts
function resolveSend3() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
var nextId = 1;
function savFromJson(json) {
  const send = resolveSend3();
  const reply = send({ jsonrpc: "2.0", id: nextId++, method: "savFromJson", params: [json] });
  if (reply == null) throw new Error("savFromJson: no reply");
  if (reply.error) throw new Error(`savFromJson: [${reply.error.code}] ${reply.error.message}`);
  return reply.result;
}

// packages/retroplug/test-native/lsdj-sync-negative.test.ts
var LSDJ = "/workspaces/resources/roms/lsdj/lsdj9_4_2.gb";
var START = 7;
var songSav = (sync) => savFromJson(JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: sync },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }]
  }
}));
var construct = (be, id, sync) => be.constructSystem({
  romPath: LSDJ,
  platform: "gb",
  core: "sameboy",
  embeddedRom: "",
  savPath: null,
  statePath: null,
  sramBytes: songSav(sync)
}, id);
var rms = (a) => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};
test("LSDj link-cable sync control: the follower stays silent without SYNC=LSDJ", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-sync-negative: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const audio = createAudioDriver();
  const leader = 1, follower = 2;
  expect(construct(be, leader, "None")).toBeTruthy();
  expect(construct(be, follower, "None")).toBeTruthy();
  expect(be.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
  expect(be.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
  audio.renderAudio(6e3);
  audio.pressButton(leader, START, true);
  audio.renderAudio(120);
  audio.pressButton(leader, START, false);
  const bufs = audio.renderAudioPerSystem(4e3);
  expect(bufs.length).toBe(2);
  const r0 = rms(bufs[0]), r1 = rms(bufs[1]);
  console.log(`[lsdj-sync-negative] leader RMS=${r0.toFixed(5)} follower RMS=${r1.toFixed(5)}`);
  expect(r0 > 1e-3).toBeTruthy();
  expect(r1 < 5e-4).toBeTruthy();
});
