// `retroplug-cli launchpad-probe` - point the Launchpad protocol layer at a REAL device and see what happens.
//
// Two things it exists to settle, neither of which a unit test can:
//
//   1. Does src/launchpad's encoder actually drive the hardware? 37 golden-vector tests prove we reproduce
//      the manual's hex; only a device can prove the manual was right about what that hex does. The probe
//      paints a pattern you look at.
//   2. Are the edge-button CC numbers correct? profile.ts marks them as the widely-used community mapping
//      rather than something the manual states in text (its layout diagram is an image), and M2's paging
//      depends on them. Press each button and the probe prints whether the profile agrees.
//
// Pure TS over src/launchpad + the MIDI client - no new protocol code, which is the point: whatever this
// proves is proved about the module the plugin and standalone use.

import type { CliTool } from "../tools";
import type { Session } from "../session";
import { keepAlive, exitProcess } from "../session";
import { createMidiClient, type MidiClient } from "../../src/realBackend";
import {
  PRO_MK3, Surface, decodeMessage, enterProgrammerMode, exitToLiveMode,
  Palette, palette,
  type Led,
} from "../../src/launchpad";

declare function setInterval(handler: () => void, ms: number): number;

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

const hex = (bytes: Uint8Array | number[]): string =>
  Array.from(bytes).map((b) => b.toString(16).padStart(2, "0")).join(" ");

/** Ports whose name contains the model's hint. The Pro MK3 exposes three interfaces and only one of them
 *  carries Programmer-mode traffic, so picking wrong is the classic first failure - this is also the first
 *  real test of that claim. */
const matching = (names: string[]): string[] => names.filter((n) => n.includes(PRO_MK3.portHint));

function pick(names: string[], explicit: string | undefined, what: string): string {
  if (explicit) {
    if (!names.includes(explicit)) throw new Error(`no MIDI ${what} named "${explicit}".\n${listing(names)}`);
    return explicit;
  }
  const hinted = matching(names);
  if (hinted.length === 0) {
    throw new Error(
      `no MIDI ${what} matching "${PRO_MK3.portHint}". Plug in a ${PRO_MK3.name}, or pass --${what === "input" ? "in" : "out"} <name>.\n` +
        listing(names),
    );
  }
  return hinted[0];
}

const listing = (names: string[]): string =>
  names.length ? `available: ${names.map((n) => `"${n}"`).join(", ")}` : "no MIDI ports detected";

function printPorts(midi: MidiClient): void {
  for (const [what, names] of [["inputs", midi.listInputs()], ["outputs", midi.listOutputs()]] as const) {
    console.log(`MIDI ${what}:`);
    if (names.length === 0) console.log("  (none)");
    for (const n of names) console.log(`  ${n}${n.includes(PRO_MK3.portHint) ? "   <- matches the Pro MK3 hint" : ""}`);
  }
}

/** A pattern that is obviously OURS: a diagonal ramp across the grid, so a wrong row/column flip or a
 *  bottom-up/top-down mix-up is visible at a glance rather than looking plausible. Every named edge button
 *  is lit dim, which is also how you tell whether the CC map addresses the buttons you expect. */
function paint(surface: Surface): void {
  for (let y = 0; y < 8; y++) {
    for (let x = 0; x < 8; x++) {
      // Colour by row, brightness by column - the top-left pad is dark, the bottom-right the brightest.
      const led: Led = { mode: "static", colour: palette(1 + y * 8 + x) };
      surface.setPad(x, y, led);
    }
  }
  for (const name of Object.keys(PRO_MK3.buttons)) {
    surface.setButton(name, { mode: "static", colour: palette(Palette.greenDim) });
  }
}

function runProbe(args: string[]): void {
  const seconds = Math.max(1, Number(flag(args, "--seconds") ?? 60) | 0);
  const midi = createMidiClient();

  const inName = pick(midi.listInputs(), flag(args, "--in"), "input");
  const outName = pick(midi.listOutputs(), flag(args, "--out"), "output");

  // "" would open EVERY hardware input and merge them, so name the port explicitly.
  if (!midi.open("RetroPlug Launchpad Probe", inName)) throw new Error("no MIDI system available");
  midi.selectOutput(outName);

  const send = (bytes: number[]): void => midi.send(bytes);

  // The device always boots into Live mode, so Programmer mode has to be asked for every time. It also
  // BLANKS the surface, which is why the shadow buffer is invalidated before the first paint.
  send(enterProgrammerMode(PRO_MK3));
  const surface = new Surface(PRO_MK3);
  surface.invalidate();
  paint(surface);
  const first = surface.flush();
  for (const m of first.messages) send(m);

  console.log(`in:  "${inName}"`);
  console.log(`out: "${outName}"`);
  console.log(`painted ${first.dirty} LEDs in ${first.messages.length} message(s).`);
  console.log("");
  console.log("Look at the device: an 8x8 colour ramp, dark at the TOP-LEFT, brightest at the BOTTOM-RIGHT,");
  console.log("with every edge button dim green. If the ramp is flipped, the grid mapping is wrong.");
  console.log("");
  console.log("Now press things. Every pad and button is reported below; press each EDGE button in turn to");
  console.log("check the profile's CC map - an UNKNOWN line means profile.ts has that button's number wrong.");
  console.log("");

  const unknownCcs = new Map<number, number>();
  let events = 0;

  keepAlive();

  setInterval(() => {
    for (const bytes of midi.poll()) {
      const data = Array.from(bytes);
      const ev = decodeMessage(PRO_MK3, data);

      if (ev) {
        if (ev.kind !== "down") continue; // one line per press, not three with the release and aftertouch
        events++;
        if (ev.pad) console.log(`  [${hex(bytes)}] pad (x=${ev.pad.x}, y=${ev.pad.y})  vel=${ev.velocity}`);
        else console.log(`  [${hex(bytes)}] button "${ev.button}"  (CC ${ev.index})`);
        continue;
      }

      // decodeMessage returns null for a CC it has no name for, which is PRECISELY the finding this probe
      // is looking for: a real button whose number profile.ts does not know. Distinguish that from genuinely
      // uninteresting traffic (sysex replies, clock) rather than lumping it in as "not surface input".
      if (data.length >= 3 && (data[0] & 0xf0) === 0xb0) {
        if (data[2] === 0) continue; // the release of an unnamed button; its press was already reported
        events++;
        unknownCcs.set(data[1], (unknownCcs.get(data[1]) ?? 0) + 1);
        console.log(`  [${hex(bytes)}] UNNAMED button, CC ${data[1]} - profile.ts has no name for this one`);
        continue;
      }
      console.log(`  [${hex(bytes)}] (not surface input)`);
    }
  }, 5);

  let left = seconds;
  setInterval(() => {
    if (--left > 0) return;
    console.log("");
    console.log(`done: ${events} press(es) seen.`);
    if (unknownCcs.size) {
      console.log(`UNNAMED CCs: ${[...unknownCcs.keys()].sort((a, b) => a - b).join(", ")}`);
      console.log("Those numbers are real controls the profile does not know - correct PRO_MK3_BUTTONS.");
    }
    finish(midi, surface, send);
    exitProcess(0);
  }, 1000);
}

/** Hand the device back. Blank the surface, then LEAVE PROGRAMMER MODE - while it is set over sysex the
 *  device's own Settings menu is locked out, so skipping this strands the user's hardware until they
 *  power-cycle it. */
function finish(midi: MidiClient, surface: Surface, send: (b: number[]) => void): void {
  surface.clear();
  for (const m of surface.flush().messages) send(m);
  send(exitToLiveMode(PRO_MK3));
  midi.close();
  console.log("device returned to Live mode.");
}

/** The loopback self-test, which needs no Launchpad at all.
 *
 *  It exists because the probe cannot test the RECEIVE half: a Launchpad sends notes and CCs, and only emits
 *  sysex in reply to an inquiry. Until now RtMidi was told to ignore sysex outright, so nothing long could
 *  reach us. This sends the largest realistic message - a whole-surface bulk-LED write - out of our own
 *  virtual port and checks it arrives byte-identical, with the caller having wired our output back to our
 *  input (tools/run-launchpad-loopback.sh does that with aconnect).
 *
 *  A cap or a filter anywhere on that path shows up as a truncated message or no message at all. */
function runLoopback(seconds: number): void {
  const midi = createMidiClient();
  // A selection that matches no port opens NO hardware inputs, leaving only our own virtual pair - so the
  // only thing that can arrive is what we sent. "" would mean "every hardware input", and any attached gear
  // would be chattering into the same ring we are trying to read one exact message out of.
  if (!midi.open(LOOPBACK_CLIENT, "(no hardware input)")) throw new Error("no MIDI system available");
  midi.selectOutput(""); // virtual output only - we are talking to ourselves

  const surface = new Surface(PRO_MK3);
  surface.invalidate();
  paint(surface);
  const payload = surface.flush().messages.find((m) => m[0] === 0xf0);
  if (!payload) throw new Error("expected the full-surface repaint to produce a bulk sysex");

  console.log(`client "${LOOPBACK_CLIENT}" open; payload is ${payload.length} bytes of sysex.`);
  console.log("waiting for the caller to connect our output back to our input...");

  keepAlive();

  const same = (a: Uint8Array, b: number[]): boolean =>
    a.length === b.length && b.every((v, i) => a[i] === v);

  let ticks = 0;
  let sent = false;
  setInterval(() => {
    ticks++;
    // Give the caller a moment to run aconnect before the one send.
    if (ticks === LOOPBACK_SEND_TICK) {
      midi.send(payload);
      sent = true;
      console.log(`sent ${payload.length} bytes.`);
    }
    if (sent) {
      for (const bytes of midi.poll()) {
        if (same(bytes, payload)) {
          console.log(`OK: received all ${bytes.length} bytes back, byte-identical.`);
          midi.close();
          exitProcess(0);
          return;
        }
        console.log(`  received ${bytes.length} bytes (not our payload): ${hex(bytes.slice(0, 8))}...`);
      }
    }
    if (ticks > seconds * 10) {
      console.log(`FAIL: the ${payload.length}-byte sysex did not come back within ${seconds}s.`);
      midi.close();
      exitProcess(1);
    }
  }, 100);
}

const LOOPBACK_CLIENT = "RetroPlug Loopback";
const LOOPBACK_SEND_TICK = 20; // 2 s at the 100 ms tick, so the caller has time to wire the ports

const HELP = [
  "usage: retroplug-cli launchpad-probe [--list] [--loopback] [--in <name>] [--out <name>] [--seconds N]",
  "",
  "  Drive a real Novation Launchpad with RetroPlug's own protocol layer: enter Programmer mode, paint a",
  "  test pattern, and report every press decoded through the device profile. Use it to confirm the grid",
  "  mapping and the edge-button CC numbers against hardware. Returns the device to Live mode on exit.",
  "",
  "  --list          list the MIDI input + output ports (marking any that match the Pro MK3) and exit",
  "  --loopback      self-test with NO device: send a whole-surface bulk-LED sysex out of our own virtual",
  "                  port and check it arrives intact (the caller wires output->input; see",
  "                  tools/run-launchpad-loopback.sh)",
  "  --in <name>     read from this MIDI input   (default: the port matching \"" + PRO_MK3.portHint + "\")",
  "  --out <name>    light this MIDI output      (default: the same)",
  "  --seconds <N>   how long to listen before restoring Live mode (default 60)",
].join("\n");

export const launchpadProbeTool: CliTool = {
  name: "launchpad-probe",
  summary: "drive a real Novation Launchpad and report what it sends back",
  help: HELP,
  longRunning: true,
  run(_s: Session, args: string[]): void {
    if (args.includes("--list")) {
      printPorts(createMidiClient());
      exitProcess(0);
      return;
    }
    if (args.includes("--loopback")) {
      runLoopback(Math.max(1, Number(flag(args, "--seconds") ?? 15) | 0));
      return;
    }
    runProbe(args);
  },
};
