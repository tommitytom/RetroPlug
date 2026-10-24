// One parity group: its cases run in order through one Rig, each as its own test(), compared against (or
// recorded into) test-native/parity/baseline/<group>.json. docs/modular-graph-phase-b0-design.md has the
// why; the env switches:
//   RP_PARITY_UPDATE=1     record instead of compare (only on PARITY_TARGET; one there over a baseline
//                          recorded elsewhere re-labels it, which must be unfiltered with no case skipped)
//   RP_PARITY_ONLY=<case>  run just that case (a group without it runs nothing and passes, with a note)
//   RP_PARITY_DUMP=<dir>   write every case a check runs (passing or not) to <dir>/<group>/<case>/: its lanes
//                          as float WAVs and its MIDI out as JSON, so a run at an older commit, where the case
//                          still passes, writes the same files to compare against
//   RP_PARITY_REQUIRE_TARGET=1  in check mode, fail every case when this host is not the baseline's target
//                          (CI's target job sets it, so exact audio checking cannot silently fade out)
//   RP_PARITY_EMIT=<file>  internal: the child a recording spawns to run one case alone writes its digest here
import { test, expect, skip } from "../../testing/harness";
import { Rig, type Segment } from "./rig";
import {
  bytesDigest,
  compareAudio,
  compareDigests,
  laneSounds,
  midiDigest,
  stableJson,
  stableJsonLine,
  type CaseDigest,
  type MidiOutEntry,
} from "./digest";

declare const __PARITY_DIR__: string;
declare const __SELF_BUNDLE__: string;
declare const __CONFIG_DIR__: string;
/** The test host's opt-in to keep pumping past its bounded spin (packages/native/src/main.cpp). */
declare const __rp_keepAlive: () => void;

interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare function setTimeout(fn: () => void, ms: number): number;
declare function clearTimeout(id: number): void;
declare const tjs: {
  exePath: string;
  env: Record<string, string>;
  spawn(
    args: string[],
    options?: { env?: Record<string, string>; stdout?: "inherit" | "pipe" | "ignore"; stderr?: "inherit" | "pipe" | "ignore" },
  ): { wait(): Promise<{ exit_status: number; term_signal: string | null }>; kill(signal: string): void; stdout: ByteStream | null; stderr: ByteStream | null };
};

export interface ParityCase {
  name: string;
  /** Fixture files the case needs; it skips (counted) when one is absent. */
  needs?: string[];
  /** Hz; default 48000. */
  sampleRate?: number;
  outputs: 2 | 4 | 6 | 8;
  /** Lanes that must sound after the first (boot) segment: a recording refuses to write silence for them.
   *  Empty for a case whose recorded behaviour IS silence, which says why in a comment. */
  audible: number[];
  setup(rig: Rig): void;
  segments: Segment[];
}

interface Baseline {
  target: string;
  cases: Record<string, CaseDigest>;
}

/** The one target whose float output the committed baselines hold (design sections 1 and 5.2). A recording
 *  only ever happens on it. Moving it is one change: set this to the new host's buildTarget(), re-record
 *  every group unfiltered on that host with the fixtures present (each baseline naming the old target is
 *  re-labelled, every digest recorded fresh), and move CI's linux-x64 image to it in the same commit. */
const PARITY_TARGET = "linux-x64-gnu13";

/** How long a recording waits for one case's alone-run before killing it. */
const ALONE_TIMEOUT_MS = 10 * 60 * 1000;

const env = tjs.env;
const UPDATE = env.RP_PARITY_UPDATE === "1";
const ONLY = env.RP_PARITY_ONLY || "";
const EMIT = env.RP_PARITY_EMIT || "";
const DUMP = env.RP_PARITY_DUMP || "";
const REQUIRE_TARGET = env.RP_PARITY_REQUIRE_TARGET === "1";

const text = new TextDecoder();
const bytes = (s: string) => new TextEncoder().encode(s);
const isSkip = (e: unknown): boolean => typeof e === "object" && e !== null && (e as { rpSkip?: unknown }).rpSkip === true;

/** Run one case from a reset, returning its digest and (when asked) its lanes per segment. `unstimulated`
 *  drops every segment's MIDI, controller traffic and transport, for the check that they matter. */
function runCase(
  rig: Rig,
  c: ParityCase,
  keepLanes: boolean,
  unstimulated = false,
): { digest: CaseDigest; lanes: Float32Array[][]; midi: MidiOutEntry[] } {
  rig.reset(c.sampleRate ?? 48000);
  c.setup(rig);
  const segments = [];
  const lanes: Float32Array[][] = [];
  const midi: MidiOutEntry[] = [];
  for (const seg of c.segments) {
    const r = rig.render(c.outputs, unstimulated ? { blocks: seg.blocks, shape: seg.shape } : seg);
    segments.push(r.digest);
    midi.push(...r.midiOut);
    if (keepLanes) lanes.push(r.lanes);
    seg.after?.(rig);
  }
  const sram: CaseDigest["sram"] = {};
  const ram: CaseDigest["ram"] = {};
  rig.project.systems.view().forEach((v, slot) => {
    const b = v.battery ? rig.project.systems.readSram(v.id) : null;
    if (b) sram[String(slot)] = bytesDigest(b);
    const r = rig.project.systems.readRam(v.id);
    if (r) ram[String(slot)] = bytesDigest(r);
  });
  return { digest: { segments, midiOut: midiDigest(midi), sram, ram }, lanes, midi };
}

/** A mono 32-bit float WAV. */
function floatWav(lane: Float32Array, sampleRate: number): Uint8Array {
  const out = new Uint8Array(44 + lane.length * 4);
  const v = new DataView(out.buffer);
  const tag = (at: number, s: string) => s.split("").forEach((ch, i) => v.setUint8(at + i, ch.charCodeAt(0)));
  tag(0, "RIFF");
  v.setUint32(4, 36 + lane.length * 4, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  v.setUint32(16, 16, true);
  v.setUint16(20, 3, true); // IEEE float
  v.setUint16(22, 1, true);
  v.setUint32(24, sampleRate, true);
  v.setUint32(28, sampleRate * 4, true);
  v.setUint16(32, 4, true);
  v.setUint16(34, 32, true);
  tag(36, "data");
  v.setUint32(40, lane.length * 4, true);
  out.set(new Uint8Array(lane.buffer, lane.byteOffset, lane.length * 4), 44);
  return out;
}

function dump(rig: Rig, group: string, c: ParityCase, lanes: Float32Array[][], midi: MidiOutEntry[]): string {
  const dir = `${DUMP}/${group}/${c.name}`;
  const outputs = lanes[0]?.length ?? 0;
  for (let l = 0; l < outputs; l++) {
    const whole = new Float32Array(lanes.reduce((n, seg) => n + seg[l].length, 0));
    let at = 0;
    for (const seg of lanes) {
      whole.set(seg[l], at);
      at += seg[l].length;
    }
    rig.writeFile(`${dir}/lane${l}.wav`, floatWav(whole, c.sampleRate ?? 48000));
  }
  rig.writeFile(`${dir}/midi-out.json`, bytes(stableJson(midi.map((m) => ({ ...m, data: Array.from(m.data) })))));
  return dir;
}

async function drain(stream: ByteStream | null, sink: Uint8Array[]): Promise<void> {
  if (!stream) return;
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) sink.push(value);
  }
}

/** Run case `name` alone in a fresh host process and return its digest, so a recording can prove a
 *  case's hashes do not depend on the cases before it. */
let aloneRuns = 0;

async function runAlone(rig: Rig, name: string): Promise<CaseDigest> {
  __rp_keepAlive(); // the child outlasts the host's bounded pump
  const emit = `${__CONFIG_DIR__}/parity-alone.json`;
  rig.session.backend.deleteFile(emit);
  // The child gets its OWN config dir, so it shares no disk with this process: a case that reads a file an
  // earlier case wrote (a ROM copy, a battery) then differs alone, and is refused as order-dependent. Nothing
  // removes it here: the test runner deletes this process's whole temp dir, subdirectories included.
  const childConfig = `${__CONFIG_DIR__}/alone-${++aloneRuns}`;
  const child = tjs.spawn([tjs.exePath, __SELF_BUNDLE__], {
    env: { ...env, RETROPLUG_USER_CONFIG_DIR: childConfig, RP_PARITY_UPDATE: "", RP_PARITY_ONLY: name, RP_PARITY_EMIT: emit },
    stdout: "pipe",
    stderr: "pipe",
  });
  let timedOut = false;
  const timer = setTimeout(() => {
    timedOut = true;
    child.kill("SIGKILL");
  }, ALONE_TIMEOUT_MS);
  const chunks: Uint8Array[] = [];
  const [, , status] = await Promise.all([drain(child.stdout, chunks), drain(child.stderr, chunks), child.wait()]);
  clearTimeout(timer);
  const written = rig.session.backend.readFile(emit);
  const output = () => chunks.map((c) => text.decode(c)).join("");
  if (timedOut) throw new Error(`running ${name} alone timed out after ${ALONE_TIMEOUT_MS / 1000}s and was killed:\n${output()}`);
  if (status.exit_status !== 0 || !written)
    throw new Error(`running ${name} alone failed (exit ${status.exit_status}, signal ${status.term_signal ?? "none"}):\n${output()}`);
  return JSON.parse(text.decode(written)) as CaseDigest;
}

export function runGroup(group: string, cases: ParityCase[]): void {
  const names = new Set<string>();
  for (const c of cases) {
    if (names.has(c.name)) throw new Error(`case "${c.name}" is listed twice in ${group}`);
    names.add(c.name);
  }
  // RP_PARITY_ONLY names one case of one group, and `pnpm test:native parity` runs every group: the others
  // register no case and pass, saying so (a throw here failed every group but one).
  if (ONLY && !names.has(ONLY)) {
    test(`${group}: no case selected`, () => console.log(`# ${group}: RP_PARITY_ONLY=${ONLY} is not in this group`));
    return;
  }

  const rig = new Rig();
  const file = `${__PARITY_DIR__}/baseline/${group}.json`;
  const host = rig.audio.buildTarget();
  const raw = rig.session.backend.readFile(file);
  const baseline: Baseline | null = raw ? (JSON.parse(text.decode(raw)) as Baseline) : null;
  // A recording happens only on PARITY_TARGET. One over a baseline recorded on another target RE-LABELS it,
  // and a re-label keeps no digest from the old target: it must record every case, so it is refused when
  // filtered (here) or when any case skipped (at the write).
  const relabelFrom = baseline && baseline.target !== PARITY_TARGET ? baseline.target : null;
  const recordRefusal =
    host !== PARITY_TARGET
      ? `this host is ${host}, and a recording only happens on PARITY_TARGET (${PARITY_TARGET}, parity/group.ts)`
      : relabelFrom && ONLY
        ? `the baseline was recorded on ${relabelFrom}: re-labelling it to ${PARITY_TARGET} records every case fresh, so run it without RP_PARITY_ONLY`
        : null;
  const target = baseline?.target ?? host;
  const onTarget = target === host;
  const missing = (c: ParityCase) => (c.needs ?? []).find((p) => !rig.session.backend.fileExists(p));

  // The child of a recording: run the one case and hand back its digest, nothing else.
  if (EMIT) {
    test(`${ONLY} (alone)`, () => {
      const c = cases.find((x) => x.name === ONLY)!;
      rig.session.backend.writeFile(EMIT, bytes(JSON.stringify(runCase(rig, c, false).digest)));
    });
    return;
  }

  const recorded: Record<string, CaseDigest> = {};
  let failed = 0;
  const skipped: string[] = [];
  const selected = cases.filter((c) => !ONLY || c.name === ONLY);

  for (const c of selected) {
    const label = UPDATE ? `${c.name} (recording)` : onTarget ? c.name : `${c.name} (deterministic here; audio baseline is ${target})`;
    test(label, async () => {
      const absent = missing(c);
      if (absent) {
        skipped.push(c.name);
        skip(`needs ${absent}`);
      }
      try {
        if (UPDATE) {
          if (recordRefusal) throw new Error(recordRefusal);
          const { digest } = runCase(rig, c, false);
          const silent = c.audible.filter((l) => !laneSounds(digest, l));
          if (silent.length) throw new Error(`lane(s) ${silent.join(", ")} never sound: refusing to record silence`);
          // A stimulus the systems ignore (MIDI sent before a cart finished booting, say) would record
          // nothing but the boot.
          const stimulated = c.segments.some((s) => s.midi?.length || s.controller?.length || s.transport?.length);
          if (stimulated) {
            // An unstimulated run that THROWS (an `after` hook asserting stimulated state) proves the stimulus
            // changed something.
            let same = false;
            try {
              same = compareDigests(digest, runCase(rig, c, false, true).digest, { audio: true }).length === 0;
            } catch {
              same = false;
            }
            if (same) throw new Error("its MIDI, controller traffic and transport change nothing: refusing to record a stimulus no system hears");
          }
          // A filtered recording (ONLY) runs the case first in its process, so it is the alone-run by
          // construction; the next unfiltered check is what compares it to the group order.
          if (!ONLY) {
            const alone = await runAlone(rig, c.name);
            const differs = compareDigests(digest, alone, { audio: true });
            if (differs.length) throw new Error(`differs when run alone, so it depends on the cases before it:\n${differs.join("\n")}`);
          }
          recorded[c.name] = digest;
          return;
        }

        if (REQUIRE_TARGET && !onTarget)
          throw new Error(
            `this host is ${host} but the baseline was recorded on ${target}, so exact audio would not be compared: ` +
              (host === PARITY_TARGET
                ? `PARITY_TARGET already names this host, so re-record this group unfiltered with the fixtures present to re-label it`
                : `run on ${target}, or move the target (PARITY_TARGET in parity/group.ts set to ${host}, every group re-recorded ` +
                  `unfiltered on it with the fixtures present, and CI's linux-x64 image moved in the same commit)`) +
              ` (spec/06-build-test.md)`,
          );
        const expected = baseline?.cases[c.name];
        if (!expected) throw new Error(`no baseline entry: record it with RP_PARITY_UPDATE=1 pnpm test:native parity/${group}`);
        const run = runCase(rig, c, DUMP !== "");
        const dumped = DUMP ? dump(rig, group, c, run.lanes, run.midi) : "";
        const problems = onTarget ? compareDigests(expected, run.digest, { audio: true }) : [];
        if (!onTarget) {
          problems.push(...compareDigests(expected, run.digest, { audio: false }));
          problems.push(...compareAudio(run.digest, runCase(rig, c, false).digest).map((p) => `not deterministic here: ${p}`));
        }
        if (problems.length) {
          // The first difference first (where to look), then this run's whole digest on one line, so a CI
          // failure carries CI's output and not only the diff.
          const where = dumped ? `\nlanes and MIDI out written to ${dumped}` : "";
          throw new Error(`${problems.join("\n")}\nactual digest on ${host}: ${stableJsonLine(run.digest)}${where}`);
        }
      } catch (e) {
        if (!isSkip(e)) failed++;
        throw e;
      }
    });
  }

  // Last: write the recording, or check the baseline holds no case the table has dropped.
  test(UPDATE ? `${group}: write the baseline` : `${group}: the baseline holds only these cases`, () => {
    if (UPDATE) {
      if (recordRefusal) throw new Error(`${recordRefusal}, so nothing was written`);
      if (failed) throw new Error(`${failed} case(s) failed, so nothing was written`);
      if (relabelFrom && skipped.length)
        throw new Error(
          `re-labelling ${group} from ${relabelFrom} to ${PARITY_TARGET} must record every case, and ${skipped.join(", ")} skipped ` +
            `(a missing fixture), so nothing was written: a kept entry would carry ${relabelFrom}'s digests under ${PARITY_TARGET}`,
        );
      if (Object.keys(recorded).length === 0) {
        console.log(`# ${group}: nothing was recorded (every case skipped), so the baseline file was left as it was`);
        return;
      }
      // A filtered recording replaces only its case. An unfiltered one drops entries for cases no longer in
      // the table, but a case skipped for a missing fixture keeps the entry it had (never in a re-label,
      // which refused above if anything skipped).
      const kept: Record<string, CaseDigest> = ONLY ? { ...(baseline?.cases ?? {}) } : {};
      if (!ONLY) for (const n of names) if (!(n in recorded) && baseline?.cases[n]) kept[n] = baseline.cases[n];
      Object.assign(kept, recorded);
      expect(rig.session.backend.writeFileAtomic(file, bytes(stableJson({ target: PARITY_TARGET, cases: kept })))).toBeTruthy();
      return;
    }
    if (ONLY || !baseline) return;
    const stale = Object.keys(baseline.cases).filter((n) => !names.has(n));
    expect(stale.length === 0, `entries for cases no longer in the table (re-record to drop them): ${stale.join(", ")}`).toBeTruthy();
  });
}
