// Imports the SDK by the same specifier a real consumer test uses, so the materialization checks prove
// the written file is IMPORTABLE and working - not merely present on disk. encodeWav is a pure function
// with no emulator dependency, which keeps this fixture fast.
import { encodeWav } from "../sdk/retroplug-cli.js";

declare const tjs: { exit(c: number): void };

const pcm = new Float32Array([0, 0.5, -0.5, 1]);
const wav: Uint8Array = encodeWav(pcm, 44100, 1);
const riff = String.fromCharCode(wav[0], wav[1], wav[2], wav[3]);

console.log("TAP version 13");
console.log("1..1");
if (riff === "RIFF" && wav.length > 44) {
  console.log("ok 1 - the materialized SDK is importable and encodeWav works");
  tjs.exit(0);
} else {
  console.log(`not ok 1 - bad wav: riff=${riff} len=${wav.length}`);
  tjs.exit(1);
}
