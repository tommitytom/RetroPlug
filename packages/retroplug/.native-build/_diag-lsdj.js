// packages/retroplug-greenfield/testing/harness.ts
var cases = [];
var scheduled = false;
function test(name, fn) {
  cases.push({ name, fn });
  if (!scheduled) {
    scheduled = true;
    Promise.resolve().then(runAll);
  }
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

// packages/retroplug-greenfield/src/realBackend.ts
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
  const ints = (b) => Array.from(b);
  const notImpl = (name) => {
    throw new Error(`realBackend.${name} is not implemented (async \u2014 deferred)`);
  };
  const specParams = (spec) => {
    const p = { romPath: spec.romPath, embeddedRom: spec.embeddedRom };
    if (spec.savPath != null) p.savPath = spec.savPath;
    if (spec.statePath != null) p.statePath = spec.statePath;
    if (spec.replaceId !== void 0) p.replaceId = spec.replaceId;
    if (spec.sramBytes) p.sramBytes = ints(new Uint8Array(spec.sramBytes));
    if (spec.stateBytes) p.stateBytes = ints(new Uint8Array(spec.stateBytes));
    if (spec.lsdjSyncMode != null) p.lsdjSyncMode = spec.lsdjSyncMode;
    return p;
  };
  const idOrNull = (v) => v == null ? null : v;
  return {
    // --- fs / config / codec (increment 1) --------------------------------
    readFile: (path) => bytesOrNull(call("readFile", path)),
    writeFile: (path, bytes) => call("writeFile", path, ints(bytes)),
    writeFileAtomic: (path, bytes) => call("writeFileAtomic", path, ints(bytes)),
    fileExists: (path) => call("fileExists", path),
    rename: (from, to) => call("rename", from, to),
    listDir: (dir) => call("listDir", dir),
    deleteFile: (path) => call("deleteFile", path),
    drainChangedPaths: () => call("drainChangedPaths"),
    canonicalize: (path) => call("canonicalize", path),
    readFilePrefix: (path, length) => bytesOrNull(call("readFilePrefix", path, length)),
    configDir: () => call("configDir"),
    zip: (entries) => bytesOrNull(call("zip", entries.map((e) => ({ name: e.name, bytes: ints(e.bytes) })))),
    unzip: (bytes) => call("unzip", ints(bytes)) ?? null,
    // --- emulator lifecycle / reads ---------------------------------------
    constructSystem: (spec) => idOrNull(call("constructSystem", specParams(spec))),
    duplicateSystem: (srcId, savPath) => idOrNull(call("duplicateSystem", srcId, savPath)),
    reloadSystem: (id) => idOrNull(call("reloadSystem", id)),
    removeSystem: (id) => call("removeSystem", id),
    applySystemSetting: (id, key, value) => call("applySystemSetting", id, key, typeof value === "boolean" ? value ? 1 : 0 : value),
    applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)),
    readState: (id) => bytesOrNull(call("readState", id)),
    readSram: (id) => bytesOrNull(call("readSram", id)),
    // --- async dialog (deferred; needs the emit path) ---------------------
    openFileBrowser: (_opts) => notImpl("openFileBrowser")
  };
}

// packages/retroplug-greenfield/src/dspRuntime.ts
function resolveSend2() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
function createDspRuntime() {
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
    compileScript: (source) => {
      const r = call("compileScript", source);
      return r == null ? null : r;
    },
    loadScript: (bytecode) => call("dspLoadScript", ints(bytecode)),
    setConfig: (bytes) => call("dspSetConfig", ints(bytes)),
    runBlock: (midi, block) => {
      const midiParam = midi.map((m) => ({ frame: m.frame, data: ints(m.data) }));
      const out = call("dspRunBlock", midiParam, block);
      return out;
    }
  };
}

// packages/retroplug-greenfield/src/audioDriver.ts
function resolveSend3() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
function createAudioDriver() {
  const send = resolveSend3();
  let nextId2 = 1;
  const call = (method, ...params) => {
    const reply = send({ jsonrpc: "2.0", id: nextId2++, method, params });
    if (reply == null) return void 0;
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };
  const ints = (b) => Array.from(b);
  return {
    sendMidi: (id, bytes) => call("sendMidi", id, ints(bytes)),
    pressButton: (id, button, down) => call("pressButton", id, button, down),
    screenshot: (id, path) => call("screenshot", id, path),
    renderAudio: (ms) => {
      const bytes = call("renderAudio", ms);
      return new Float32Array(bytes.slice().buffer);
    },
    setTransport: (running) => call("setTransport", running),
    setBpm: (bpm) => call("setBpm", bpm),
    dspAttach: (systemId) => call("dspAttach", systemId),
    sendDspMidi: (bytes) => call("sendDspMidi", ints(bytes))
  };
}

// packages/retroplug-greenfield/src/lsdjSav.ts
function resolveSend4() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
var nextId = 1;
function savFromJson(json) {
  const send = resolveSend4();
  const reply = send({ jsonrpc: "2.0", id: nextId++, method: "savFromJson", params: [json] });
  if (reply == null) throw new Error("savFromJson: no reply");
  if (reply.error) throw new Error(`savFromJson: [${reply.error.code}] ${reply.error.message}`);
  return reply.result;
}

// packages/retroplug-greenfield/test-native/_diag-lsdj.test.ts
var LSDJ = "/workspaces/resources/roms/lsdj/lsdj9_4_2.gb";
var START = 7;
var FULL_CLOCK = `
var was = false;
function onBlock(input) {
  if (input.transportPlaying && !was) pushSerialIn(0, 0xFA);
  was = input.transportPlaying;
  eachTick(24, function(t, o) { pushSerialIn(o, 0xF8); });
}`;
var songWith = (sync) => JSON.stringify({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: sync },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }]
  }
});
var rms = (a) => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};
var be = createRealBackend();
var dsp = createDspRuntime();
var audio = createAudioDriver();
function build(sync) {
  const sav = savFromJson(songWith(sync));
  return be.constructSystem({ romPath: LSDJ, embeddedRom: "", savPath: null, statePath: null, sramBytes: sav.slice().buffer, lsdjSyncMode: "Off" });
}
test("diag: screenshot MIDI-sync stages", () => {
  if (!be.fileExists(LSDJ)) {
    console.log("# SKIP no rom");
    return;
  }
  const id = build("Midi");
  audio.renderAudio(6e3);
  console.log(`[diag] shot boot -> ${audio.screenshot(id, "/tmp/lsdj_midi_boot.png")}`);
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  audio.renderAudio(300);
  console.log(`[diag] shot after-START -> ${audio.screenshot(id, "/tmp/lsdj_midi_afterstart.png")}`);
  dsp.loadScript(dsp.compileScript(FULL_CLOCK));
  audio.dspAttach(id);
  audio.setBpm(120);
  audio.setTransport(true);
  const r = rms(audio.renderAudio(3e3));
  console.log(`[diag] shot after-clock -> ${audio.screenshot(id, "/tmp/lsdj_midi_afterclock.png")}  RMS=${r.toFixed(5)}`);
  be.removeSystem(id);
});
test("diag: screenshot SYNC=Lsdj leader (known-audible) after START", () => {
  if (!be.fileExists(LSDJ)) {
    console.log("# SKIP no rom");
    return;
  }
  const id = build("Lsdj");
  audio.renderAudio(6e3);
  console.log(`[diag] lsdj shot boot -> ${audio.screenshot(id, "/tmp/lsdj_leader_boot.png")}`);
  audio.pressButton(id, START, true);
  audio.renderAudio(120);
  audio.pressButton(id, START, false);
  const r = rms(audio.renderAudio(3e3));
  console.log(`[diag] lsdj shot after-START -> ${audio.screenshot(id, "/tmp/lsdj_leader_afterstart.png")}  RMS=${r.toFixed(5)}`);
  be.removeSystem(id);
});
