// base64 (Uint8Array <-> ASCII) — runtime-independent, no btoa/atob dependency (txiki has neither).
// Used for the DPF getState/setState string boundary (pluginControlPlane) and for embedding binary asset
// blobs in JSON config (LSDj asset overrides in a project's role config).

const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const B64REV = (() => {
  const m = new Int16Array(128).fill(-1);
  for (let i = 0; i < B64.length; i++) m[B64.charCodeAt(i)] = i;
  return m;
})();

export function b64encode(bytes: Uint8Array): string {
  let out = "";
  for (let i = 0; i < bytes.length; i += 3) {
    const b0 = bytes[i];
    const b1 = i + 1 < bytes.length ? bytes[i + 1] : 0;
    const b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
    out += B64[b0 >> 2];
    out += B64[((b0 & 3) << 4) | (b1 >> 4)];
    out += i + 1 < bytes.length ? B64[((b1 & 15) << 2) | (b2 >> 6)] : "=";
    out += i + 2 < bytes.length ? B64[b2 & 63] : "=";
  }
  return out;
}

export function b64decode(s: string): Uint8Array {
  let count = 0;
  for (let i = 0; i < s.length; i++) if (B64REV[s.charCodeAt(i) & 127] >= 0) count++;
  const out = new Uint8Array(Math.floor((count * 3) / 4));
  let bits = 0;
  let nbits = 0;
  let oi = 0;
  for (let i = 0; i < s.length; i++) {
    const v = B64REV[s.charCodeAt(i) & 127];
    if (v < 0) continue;
    bits = (bits << 6) | v;
    nbits += 6;
    if (nbits >= 8) {
      nbits -= 8;
      out[oi++] = (bits >> nbits) & 0xff;
    }
  }
  return out;
}
