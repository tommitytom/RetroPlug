// The shared operation list for the Node-vs-QuickJS codec parity test. BOTH hosts run this exact
// module against their own `__rpcSend`, so the comparison is of two codecs over identical inputs
// rather than of hand-written expectations.
//
// Everything here must run on plain QuickJS as well as Node: no node: imports, no Buffer, no
// TextEncoder-dependent paths. The checksum is hand-rolled for that reason.

/** FNV-1a over a byte view, as 8 lowercase hex chars. Pure JS so txiki and Node agree. */
export function fnv1a(bytes) {
    let h = 0x811c9dc5;
    for (let i = 0; i < bytes.length; i++) {
        h ^= bytes[i];
        h = Math.imul(h, 0x01000193) >>> 0;
    }
    return h.toString(16).padStart(8, "0");
}

/** Canonicalize an RPC result so the two hosts produce comparable JSON. Binary collapses to its
 *  length + checksum, which is what actually needs to match; everything else passes through. The
 *  CONSTRUCTOR NAME is recorded too - that is the assertion that binary crossed as a Uint8Array and
 *  not as an array of numbers (the whole point of the NativeAstCodec opt-in). */
export function canon(v) {
    if (v === null || v === undefined) return null;
    if (v instanceof Uint8Array) return { kind: "u8", len: v.length, hash: fnv1a(v) };
    if (Array.isArray(v)) return v.map(canon);
    if (typeof v === "object") {
        const out = {};
        for (const k of Object.keys(v).sort()) out[k] = canon(v[k]);
        return out;
    }
    return v;
}

/** Deterministic 4 KB test payload. */
export function payload() {
    const p = new Uint8Array(4096);
    for (let i = 0; i < p.length; i++) p[i] = (i * 31 + 7) & 0xff;
    return p;
}

/**
 * Run the operation matrix (the host facet, and an offline job on the emulator facet) against `send` (a synchronous
 * __rpcSend) using `tmp` as a scratch directory. Returns [label, canonicalResult] pairs in a fixed order.
 *
 * Coverage, in codec terms: string/int/bool scalars, an empty std::optional, rfl::Bytestring in BOTH
 * directions, std::vector<std::string>, std::vector<struct> with a nested Bytestring, a struct
 * parameter with a nested Bytestring, a struct of vectors of structs with absent optionals and a nested optional
 * struct (an offline job's request) and its completion's nested vectors of structs, and the JSON-RPC error envelope.
 */
export function runOps(send, tmp) {
    let id = 0;
    const raw = (method, ...params) => send({ jsonrpc: "2.0", id: ++id, method, params });
    const call = (method, ...params) => {
        const r = raw(method, ...params);
        if (r && r.error) throw new Error(`rpc ${method}: [${r.error.code}] ${r.error.message}`);
        return r ? r.result : undefined;
    };

    const out = [];
    const rec = (label, value) => out.push([label, canon(value)]);

    const blob = payload();
    const blobPath = tmp + "/blob.bin";
    const textPath = tmp + "/hello.txt";

    // scalars
    rec("version", call("version"));
    rec("fileExists:absent", call("fileExists", tmp + "/definitely-not-here"));

    // Bytestring IN, then OUT - the round trip is the fidelity check.
    rec("writeFile", call("writeFile", blobPath, blob));
    rec("readFile", call("readFile", blobPath));
    rec("writeFile:small", call("writeFile", textPath, new Uint8Array([104, 105, 33])));
    rec("readFile:small", call("readFile", textPath));
    rec("fileExists:present", call("fileExists", blobPath));

    // empty std::optional
    rec("readFile:absent", call("readFile", tmp + "/definitely-not-here"));

    // uint32 param + prefix read
    rec("readFilePrefix", call("readFilePrefix", blobPath, 16));

    // std::vector<std::string> (sorted - directory order is not guaranteed)
    const listed = call("listDir", tmp);
    rec("listDir", Array.isArray(listed) ? [...listed].sort() : listed);

    // std::vector<struct{string, Bytestring}> IN -> Bytestring OUT -> std::vector<struct> OUT
    const zipped = call("zip", [
        { name: "a.txt", bytes: new Uint8Array([104, 105]) },
        { name: "b.bin", bytes: blob },
    ]);
    rec("zip", zipped);
    const entries = call("unzip", zipped);
    rec("unzip", entries);

    // struct{uint32, uint32, Bytestring} IN -> Bytestring OUT -> struct OUT
    const px = new Uint8Array(8 * 8 * 4);
    for (let i = 0; i < px.length; i++) px[i] = (i * 7) & 0xff;
    const png = call("pngEncode", { width: 8, height: 8, rgba: px });
    rec("pngEncode", png);
    rec("pngDecode", call("pngDecode", png));

    // zero-length binary - the null-data-pointer edge in the reader
    const emptyPath = tmp + "/empty.bin";
    rec("writeFile:empty", call("writeFile", emptyPath, new Uint8Array(0)));
    rec("readFile:empty", call("readFile", emptyPath));

    // An offline job: a struct of vectors of structs IN (optional fields absent, a nested optional struct, interleaved
    // float32 as a Bytestring) -> an optional-field answer OUT -> vectors of structs with nested Bytestrings OUT. A
    // samples input through rp.dsp.gain into a recorder, beside a windowed file input (a 16-bit WAV written here).
    const wav = new Uint8Array(44 + 64 * 2);
    const dv = new DataView(wav.buffer);
    [..."RIFF"].forEach((c, i) => dv.setUint8(i, c.charCodeAt(0)));
    dv.setUint32(4, 36 + 128, true);
    [..."WAVEfmt "].forEach((c, i) => dv.setUint8(8 + i, c.charCodeAt(0)));
    dv.setUint32(16, 16, true);
    dv.setUint16(20, 1, true);
    dv.setUint16(22, 1, true);
    dv.setUint32(24, 22050, true);
    dv.setUint32(28, 44100, true);
    dv.setUint16(32, 2, true);
    dv.setUint16(34, 16, true);
    [..."data"].forEach((c, i) => dv.setUint8(36 + i, c.charCodeAt(0)));
    dv.setUint32(40, 128, true);
    for (let i = 0; i < 64; i++) dv.setInt16(44 + 2 * i, (i * 997) % 20000 - 10000, true);
    const wavPath = tmp + "/offline.wav";
    call("writeFile", wavPath, wav);
    const floats = new Float32Array(256);
    for (let i = 0; i < floats.length; i++) floats[i] = ((i * 37) % 101) / 101 - 0.5;
    const doc = JSON.stringify({
        nodes: [
            { id: "p", type: "rp.core.buffer-player", settings: { channels: 1, layout: "mono", loop: false }, resources: { sample: "res:1" } },
            { id: "g", type: "rp.dsp.gain", settings: { channels: 1 }, params: { gain: 0.25 } },
            { id: "rec", type: "rp.core.buffer-recorder", settings: { channels: 1 }, resources: { target: "res:3" } },
        ],
        wires: [{ from: "p.out", to: "g.in" }, { from: "g.out", to: "rec.in" }],
    });
    const submitted = call("submitOfflineJob", {
        document: doc,
        inputs: [
            { samples: new Uint8Array(floats.buffer), channels: 1, rate: 48000 },
            { path: wavPath, offset: 8, length: 0, cap: { frames: 40, rate: 11468, arithmetic: "float-ratio" } },
        ],
        outputs: [{ channels: 1, frames: 256, rate: 48000 }],
        returns: [3, 2],
        frames: 256,
        rate: 48000,
    });
    rec("submitOfflineJob", { taken: typeof submitted.id === "number", error: submitted.error });
    // How many drains it takes is timing, so only what holds on every host is recorded: the job was in flight until
    // it came back, and nothing was after.
    let completions = [];
    const inFlight = [];
    for (let i = 0; i < 100 && completions.length === 0; i++) {
        const drained = call("drainOfflineJobs", 100);
        inFlight.push(drained.inFlight);
        completions = drained.completions;
    }
    rec("drainOfflineJobs", completions.map((c) => ({ ...c, id: c.id === submitted.id })));
    rec("drainOfflineJobs:inFlight", {
        last: inFlight[inFlight.length - 1],
        before: inFlight.slice(0, -1).every((n) => n === 1),
    });
    rec("submitOfflineJob:refused", call("submitOfflineJob", {
        inputs: [{ path: wavPath, cap: { frames: 1, rate: 1, arithmetic: "nope" } }],
        outputs: [],
        returns: [],
        frames: 0,
        rate: 48000,
    }));
    rec("cancelOfflineJob:none", call("cancelOfflineJob", 12345));

    // error envelope
    rec("error:unknownMethod", raw("noSuchMethod"));
    // wrong parameter type - the reader's cast failure, surfaced as an rpc error
    rec("error:badParams", raw("fileExists", 42));

    return out;
}
