// The midi package's facet through the real native host: its provider and its RPC facet over a SCRIPTED MIDI system
// (packages/native/src/main.cpp starts it with packages/midi/native/test-support/midi/FakeMidiBackend.hpp, a machine
// listing a Pro MK3 and an interface's port, and answering every write on each open input). No hardware, no rtmidi, and
// the same facet code the CLI mounts - this is the one door a live-MIDI client has.
//
// What only this file can hold: the live-MIDI contract at its one edge (docs/modular-graph-phase-f5b-design.md E11).
// `open("")` opens EVERY hardware input, as the contract says and as `n8-bridge` with no `--midi-in` expects; it read
// None for a while. A named input opens that port alone, and the count of replies is how many inputs a stream opened.
//
// Run: pnpm test:native midi-facet
import { test, expect } from "@retroplug/testing";
import { createMidiClient, type MidiClient } from "../src/realBackend";

declare const tjs: {
  exePath: string;
  env: Record<string, string>;
  exit(code: number): void;
  spawn(
    args: string[],
    options?: {
      env?: Record<string, string>;
      stdout?: "inherit" | "pipe" | "ignore";
      stderr?: "inherit" | "pipe" | "ignore";
    },
  ): {
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
    stdout: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    stderr: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
  };
};
declare const __SELF_BUNDLE__: string;
/** This host's opt-in to keep pumping past its bounded spin: the case below awaits a child PROCESS. */
declare const __rp_keepAlive: () => void;

// The test-only port filter, naming the rig's fakes only (RP_MIDI_PORT_ALLOW,
// packages/midi/native/src/midi/PortFilter.hpp). A case here must not open a real device even if the host process were
// ever given a real backend, and the rig's names are what the filter lets through; every other name the scripted
// machine lists (ALSA's Through, RetroPlug's own virtual ports, a second instance's link) is not hardware and would not
// be opened anyway.
const FAKES = "LPProMK3 MIDI,MIDISPORT 2x2 Port A";
tjs.env.RP_MIDI_PORT_ALLOW = FAKES;

const PRO = "LPProMK3 MIDI";
const IFACE = "MIDISPORT 2x2 Port A";

/** Drain a child's two pipes concurrently with the wait: reading one then the other deadlocks a noisy child. */
async function drain(
  stream: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null,
): Promise<string> {
  if (!stream) return "";
  const reader = stream.getReader();
  const chunks: Uint8Array[] = [];
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) chunks.push(value);
  }
  const text = new TextDecoder();
  return chunks.map((c) => text.decode(c)).join("");
}

/** A fresh client per case: the facet owns one stream, so a case closes what it opened. */
function client(): MidiClient {
  return createMidiClient();
}

/** What one write hears back: the scripted machine replies once on every input the stream has open, so this is the
 *  number of hardware inputs the selection opened (the virtual port is not one). */
function repliesToAWrite(c: MidiClient): number {
  c.send(Uint8Array.from([0x90, 0x3c, 0x7f]));
  return c.poll().length;
}

test("the facet lists the scripted machine's hardware ports only", () => {
  // The machine also lists RetroPlug's own virtual ports, a second instance's `RetroPlug Launchpad` link ports and
  // ALSA's Midi Through; the one filter drops all three, in the lister and in every open.
  const c = client();
  expect(c.listInputs()).toEqual([PRO, IFACE]);
  expect(c.listOutputs()).toEqual([PRO, IFACE]);
});

test("open('') opens EVERY hardware input: the contract's meaning, and what a bridge with no --midi-in expects", () => {
  const c = client();
  expect(c.open("RetroPlug", "")).toBe(true);
  // One reply per open input: both ports, so both opened. Read as None, this is zero and nothing ever reaches a cart.
  expect(repliesToAWrite(c)).toBe(2);
  c.close();
});

test("a named input opens that port alone", () => {
  const c = client();
  expect(c.open("RetroPlug", PRO)).toBe(true);
  expect(repliesToAWrite(c)).toBe(1);
  c.close();
});

test("a name that is not present opens no input, and the next open picks it up", () => {
  // The Launchpad probe's "(no hardware input)" is exactly this, and it must not fall back to every device.
  const c = client();
  expect(c.open("RetroPlug", "(no hardware input)")).toBe(true);
  expect(repliesToAWrite(c)).toBe(0);
  // ...and the same facet opens what a later call names.
  expect(c.open("RetroPlug", IFACE)).toBe(true);
  expect(repliesToAWrite(c)).toBe(1);
  c.close();
});

// The client name a session ASKS for is the name its virtual ports are published under, which is the contract at
// core/ts/host.ts: a DAW routes to "RetroPlug N8 In", and two sessions (a bridge beside the standalone, two bridges)
// must not share one pair. The stream announces the pair it opened, so this reads the very name the backend recorded -
// from a CHILD process, because that line goes to the child's stderr and a case inside the host cannot read its own.
// Nothing real opens: the child's backend is the same scripted machine, under the allowed fakes.
const NAME_CHILD = "RP_MIDI_NAME_CHILD";
if (tjs.env[NAME_CHILD] === "1") {
  const c = createMidiClient();
  c.open("RetroPlug N8", "");
  // A rename is a new SYSTEM, not a new selection: an output picked before it must survive it (the SDK is public to
  // consumers, which may pick before they open, and a session renamed later must not lose its mirror silently).
  c.selectOutput(IFACE);
  c.open("RetroPlug Loopback", "");
  c.close();
  tjs.exit(0);
}

test("a session's own client name is the name its virtual ports are published under", async () => {
  __rp_keepAlive(); // the child outlasts the host's bounded spin
  const child = tjs.spawn([tjs.exePath, __SELF_BUNDLE__], {
    env: { ...tjs.env, [NAME_CHILD]: "1" },
    stdout: "pipe",
    stderr: "pipe",
  });
  const [out, err, status] = await Promise.all([drain(child.stdout), drain(child.stderr), child.wait()]);
  expect(status.exit_status, `the child process failed:\n${out}\n${err}`).toBe(0);
  // The name the fake recorded: it builds "<clientName> In" / "<clientName> Out", so this is that name, byte for byte.
  expect(err.includes("MIDI: virtual ports 'RetroPlug N8 In' / 'RetroPlug N8 Out' open")).toBe(true);
  // The session's name is its own: it did not publish under the host's default name.
  expect(err.includes("MIDI: virtual ports 'RetroPlug In' / 'RetroPlug Out' open")).toBe(false);
  // ...and the SECOND name (a rename, so a new backend instance) still mirrors the output picked before it: the line
  // comes after that instance's own virtual-port line, so it is the renamed stream that reopened it.
  const renamed = err.indexOf("MIDI: virtual ports 'RetroPlug Loopback In' / 'RetroPlug Loopback Out' open");
  expect(renamed >= 0).toBe(true);
  expect(err.indexOf(`MIDI out: connected hardware port '${IFACE}'`, renamed)).toBeGreaterThan(renamed);
});
