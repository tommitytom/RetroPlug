// The shared emulator matrix for the Node-vs-QuickJS host parity test. Unlike ops.mjs (which pokes
// __rpcSend directly), this drives the REAL control plane: bootSession, the project/systems store,
// the DSP kernel and the audio driver. Both hosts import the SAME built SDK bundle
// (build/cli-sdk/retroplug-cli.js), so the only difference between the two runs is the codec and the
// JS runtime underneath.
//
// Must run on plain QuickJS as well as Node: no node: imports, no Buffer.

import { fnv1a } from "./ops.mjs";

/** FNV-1a over the raw bytes of a Float32Array, so rendered PCM can be compared exactly. */
export function hashPcm(f32) {
    return fnv1a(new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength));
}

/** Peak absolute sample, rounded, so "did it actually make sound" is checkable without a golden. */
export function peak(f32) {
    let m = 0;
    for (let i = 0; i < f32.length; i++) {
        const a = f32[i] < 0 ? -f32[i] : f32[i];
        if (a > m) m = a;
    }
    return Math.round(m * 1000) / 1000;
}

/**
 * Boot the control plane and drive both cores. `sdk` is the built SDK bundle's namespace; `nesRom`
 * is a path to a NES ROM. Returns [label, value] pairs in a fixed order.
 *
 * Coverage: bootSession (backend + registry + stores + DSP kernel load), the embedded Game Boy core,
 * an on-disk NES ROM through the Mesen core, audio rendering, snapshot reads (binary out) and the
 * debug facet's struct-heavy APU read.
 */
export function runEmu(sdk, nesRom) {
    const s = sdk.bootSession();
    const out = [];
    const rec = (label, value) => out.push([label, value]);

    rec("sampleRate", s.audio.sampleRate());

    // --- the embedded Game Boy core (mGB, no external file) ---
    const gb = s.project.systems.loadMgb();
    if (gb == null) throw new Error("loadMgb failed");
    rec("gb:id", gb);

    // Render past the splash, then drive a note. mGB is a synth: it is SILENT until MIDI arrives, and
    // comparing two buffers of zeroes would prove nothing, so the note-on is load-bearing for the test.
    s.audio.renderAudio(2000);
    s.audio.stageMidiIn([0x90, 60, 100]); // note on, ch1 -> mGB pulse 1
    const gbPcm = s.audio.renderAudio(250);
    rec("gb:pcmLen", gbPcm.length);
    rec("gb:pcmHash", hashPcm(gbPcm));
    rec("gb:peak", peak(gbPcm));

    // Snapshot reads: binary out through the emulator facet.
    const gbState = s.backend.readState(gb);
    rec("gb:stateLen", gbState ? gbState.length : null);
    rec("gb:stateHash", gbState ? fnv1a(gbState) : null);

    // --- an on-disk NES ROM through the Mesen core ---
    if (!s.backend.fileExists(nesRom)) throw new Error(`rom not found: ${nesRom}`);
    const nes = s.project.systems.addSystem(nesRom);
    if (nes == null) throw new Error(`could not load ${nesRom}`);
    rec("nes:id", nes);

    s.audio.renderAudio(500);
    s.audio.stageMidiIn([0x90, 64, 100]); // note on -> the NES 2A03 pulse 1
    const mixPcm = s.audio.renderAudio(250);
    rec("mix:pcmLen", mixPcm.length);
    rec("mix:pcmHash", hashPcm(mixPcm));
    rec("mix:peak", peak(mixPcm));

    // The debug facet: a struct-heavy live-core read (nested structs of ints and bools).
    const apu = s.backend.getApuState(nes);
    rec("nes:apuKeys", apu ? Object.keys(apu).sort().join(",") : null);
    rec("nes:pulse1Keys", apu && apu.pulse1 ? Object.keys(apu.pulse1).sort().join(",") : null);

    // Per-system rendering: one interleaved buffer per system (two loaded by now).
    const perSystem = s.audio.renderAudioPerSystem(100);
    rec("perSystem:count", perSystem.length);
    rec("perSystem:hashes", perSystem.map(hashPcm).join(","));

    return out;
}
