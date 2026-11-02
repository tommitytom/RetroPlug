// A render's bytes do not depend on how much audio each of its pulls asks for (docs/modular-graph-phase-d3-design.md
// section 4): in an engine job the engine runs whole blocks whatever a call asks for and carries the rest of its last
// block to the next call, so the emulation and its audio fall on the same block grid however the render library
// chunks its stream. Pulled through RenderOpts.pullMs, the test knob over the stream's 100 ms chunks.
//
// Where the block grid shows. A render with no running transport and no MIDI emulates the same whatever its blocks
// are, so its samples would not follow the pull with exact blocks either; the host-synced smsggdj render is the one
// here whose would: its sync node turns the running transport into the cart's clock block by block, and with exact
// blocks (each call its own length) its file moves with the pull. That case is the one exact blocks fail.
//
// What the grid cannot cover. Between its calls the library also acts: it presses play (Start; risa's three
// gestures), loads smsggdj's song, and on an auto-detect render polls the song's state after each pull. An act lands
// at the block after the call before it ends, so it moves with that call's length by construction. The gestures and
// the song load use lengths of their own (100 ms holds, 250 ms steps, and the 1500 ms settle), which pullMs leaves
// alone, so a fixed-length render's files are identical at any pull, presses and all, as the other cases pin. An
// auto-detect render's pull is its detection poll, though, and it ends at the start of the first pull its poll reads
// stopped, so its samples agree and its length moves with the pull, by less than the longest one.
//
// The cores power on in a fixed state (the runner sets RETROPLUG_DETERMINISTIC for this file). The smsggdj renders
// need no fixture; the LSDj and risa ones read the fixture tree (RETROPLUG_RESOURCES_DIR) and SKIP without it.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { caseNamed, missingFixture, renderCase, writeScratch, type Rendered } from "./render-cases";

const be = createRealBackend();
createAudioDriver(); // the offline poll's wait: this host has no UI tick

const SCRATCH = `${be.configDir()}/render-chunks`; // this run's own config dir, which the runner deletes
writeScratch(be, SCRATCH);

const PULLS = [100, 37, 250]; // ms: the default, one no block length divides, and one longer than the hold gestures

/** `name` rendered once per pull in `pulls`, all at once on the engine's pool. */
const atPulls = (name: string, pulls: number[], keepBytes = false): Promise<Rendered[]> =>
  Promise.all(pulls.map((pullMs) => renderCase(be, caseNamed(name), SCRATCH, `pull${pullMs}`, { pullMs }, keepBytes)));

/** A fixed-length render's files at every pull are the default pull's: same hashes, same sizes. */
function sameFiles(name: string, rendered: Rendered[]): void {
  for (const r of rendered) expect(r.result.status, `${name}: ${r.result.message}`).toBe("done");
  expect(rendered[0].files.some((f) => f.sounds), `${name} sounds`).toBe(true);
  const files = (r: Rendered) => r.files.map((f) => `${f.suffix} ${f.hash} ${f.size}`);
  for (let k = 1; k < rendered.length; k++)
    expect(files(rendered[k]), `${name} pulled at ${PULLS[k]} ms`).toEqual(files(rendered[0]));
}

test("a host-synced smsggdj render writes the same file at every pull", async () => {
  const name = "smsggdj/sms/sync-in24/transport-120bpm";
  sameFiles(name, await atPulls(name, PULLS));
});

test("a fixed-length smsggdj render (its song loaded and played) writes the same file at every pull", async () => {
  const name = "smsggdj/sms/index-0/mix";
  sameFiles(name, await atPulls(name, PULLS));
});

test("a fixed-length LSDj split render (Start pressed) writes the same stems at every pull", async () => {
  const missing = missingFixture(be, "lsdj");
  if (missing) skip(`render-chunks: ${missing}`);
  const name = "lsdj/tripledipper/fixed-5s/channels";
  sameFiles(name, await atPulls(name, PULLS));
});

test("a fixed-length risa split render (its three gestures pressed) writes the same pins at every pull", async () => {
  const missing = missingFixture(be, "risa");
  if (missing) skip(`render-chunks: ${missing}`);
  const name = "risa/blumarbl/fixed-3s/pins";
  sameFiles(name, await atPulls(name, PULLS));
});

test("an LSDj render to its HFF stop writes the same samples at every pull, ending within a pull", async () => {
  const missing = missingFixture(be, "lsdj");
  if (missing) skip(`render-chunks: ${missing}`);
  const pulls = [100, 50, 200];
  const rendered = await atPulls("lsdj/telemelt/hff/name-machete/mix", pulls, true);
  for (const r of rendered) expect(r.result.status, r.result.message).toBe("done");
  const pcm = rendered.map((r) => r.files[0].bytes!.subarray(44)); // past the WAV header
  const shortest = Math.min(...pcm.map((p) => p.length));
  for (let k = 1; k < pcm.length; k++) {
    let differ = -1;
    for (let i = 0; i < shortest && differ < 0; i++) if (pcm[k][i] !== pcm[0][i]) differ = i;
    expect(differ, `the samples pulled at ${pulls[k]} ms against ${pulls[0]} ms`).toBe(-1);
  }
  const lengths = rendered.map((r) => r.result.renderedMs);
  console.log(`# HFF lengths at pulls ${pulls.join(", ")} ms: ${lengths.join(", ")} ms`);
  expect(Math.max(...lengths) - Math.min(...lengths) < Math.max(...pulls)).toBe(true);
});
