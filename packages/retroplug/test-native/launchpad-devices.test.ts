// The shell's OWN module, launchpadDevices.ts, against the headless host's real facet
// (docs/modular-graph-phase-f3-design.md section 8): the restore that claims the pair config.json remembers, the scan
// stepped to its end that persists the pair it found, Forget that gives the device back, a claim the lister cannot see,
// and the cross-PROCESS refusal. Everything below the module is the production code a standalone runs - the pack's
// provider, its facet, the claim path and the cross-process lock - over the host's SCRIPTED device
// (packages/launchpad/native/test/FakePorts.hpp), so the decisions launchpadDevices makes are held here and not only in
// `pnpm sdl:controller`, which no CI runs.
//
// The fake has a Pro MK3 on "LPProMK3 MIDI" both ways, which answers the Universal Device Inquiry and its own layout
// query, and an interface's port that answers nothing.
//
// Run: pnpm test:native launchpad-devices
import { test, expect } from "@retroplug/testing";
import { createAudioDriver } from "../src/audioDriver";
import { createRealBackend } from "../src/realBackend";
import { UserConfigStore } from "../src/userConfigStore";
import { createLaunchpadClient } from "@retroplug/launchpad/client";
import { PRO_MK3, exitToLiveMode } from "@retroplug/launchpad";
import { makeCall } from "../src/rpcClient";
import {
  applyLaunchpadScan, connectLaunchpad, forgetLaunchpad, getLaunchpadConfig, pollLaunchpadScan,
  resetLaunchpadScanForTest, restoreLaunchpad, setLaunchpadClient, setLaunchpadScanClock, startLaunchpadScan,
} from "../ui/screens/menu/launchpadDevices";

declare const __CONFIG_DIR__: string;
/** The test host's own seam: this file runs INSIDE the native host (txiki.js), whose spawn runs a second process. */
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
    kill(signal: string): void;
    stdout: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
    stderr: { getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> } } | null;
  };
};
declare const __SELF_BUNDLE__: string;
/** The test host's opt-in to keep pumping past its bounded spin (packages/native/src/main.cpp): a case that awaits a
 *  child PROCESS needs it, because the bounded spin has no sleep and never services the child's exit in time. */
declare const __rp_keepAlive: () => void;

const PRO = "LPProMK3 MIDI";
const PRO_NAME = PRO_MK3.name; // "Launchpad Pro [MK3]", the name a scan records
const CLOSE = Uint8Array.from(exitToLiveMode(PRO_MK3));

// ---------------------------------------------------------------------------------------------------------------
// The second PROCESS. The claim is one per machine across processes, so the only real proof of it is another process:
// this same bundle, re-run by the host with RP_LAUNCHPAD_HOLDER set, which claims the device through the facet and writes
// what the facet answered where the parent reads it. The branch runs before any test is registered, and exits: this
// process is a holder, not a test run.
// ---------------------------------------------------------------------------------------------------------------
const HOLDER_ENV = "RP_LAUNCHPAD_HOLDER";
const ANSWER = `${__CONFIG_DIR__}/launchpad-holder.json`;
if (tjs.env[HOLDER_ENV] === "1") {
  const client = createLaunchpadClient(makeCall());
  const held = client.claim(PRO, PRO, CLOSE);
  const answer = JSON.stringify({ connected: held.connected, error: held.error, input: held.input });
  createRealBackend().writeFile(ANSWER, new TextEncoder().encode(answer));
  tjs.exit(0);
}

/** Drain the child's pipes concurrently with the wait: reading one then the other deadlocks a noisy child. */
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

/** A store over the host's own config.json, read as a fresh one reads it (a new store starts at the DEFAULTS until load). */
function store(): UserConfigStore {
  const cfg = new UserConfigStore(createRealBackend());
  cfg.load();
  return cfg;
}

// FIRST in the file on purpose: it is the one case that runs a SECOND process against this host's claim directory, and a
// child that touched that directory would make every later case's cross-process answer depend on this one having run. Held
// here, it also proves it needs nothing but a fresh config: no earlier case has claimed anything.
test("a second PROCESS cannot claim the port this one holds, and says so in the facet's words", async () => {
  __rp_keepAlive(); // the child outlasts the host's bounded spin
  const cfg = store();
  const client = createLaunchpadClient(makeCall());
  cfg.setControlSurface({ input: PRO, output: PRO, device: PRO_NAME, enabled: true });
  restoreLaunchpad(cfg);
  expect(client.status().connected).toBe(true);
  try {
    createRealBackend().deleteFile(ANSWER);
    const child = tjs.spawn([tjs.exePath, __SELF_BUNDLE__], {
      env: { ...tjs.env, [HOLDER_ENV]: "1" },
      stdout: "pipe",
      stderr: "pipe",
    });
    const [out, err, status] = await Promise.all([drain(child.stdout), drain(child.stderr), child.wait()]);
    expect(status.exit_status, `the holder process failed:\n${out}\n${err}`).toBe(0);
    const raw = createRealBackend().readFile(ANSWER);
    expect(raw === null).toBe(false);
    const answer = JSON.parse(new TextDecoder().decode(raw!)) as { connected: boolean; error: string };
    // The facet's own refusal (LaunchpadRpc's claim), reached through launchpadDevices' client in ANOTHER process while
    // this one holds the lock: the second host's claim opened nothing and took nothing.
    expect(answer.connected).toBe(false);
    expect(answer.error).toBe("in use by another RetroPlug");
    // ...and this process still holds it, so the child's run changed nothing here.
    expect(client.status().connected).toBe(true);
    expect(client.status().input).toBe(PRO);
  } finally {
    client.release();
    cfg.setControlSurface({ input: "", output: "", device: "", enabled: false });
  }
});

test("the restore claims the pair config.json remembers, then tells the host it is done", () => {
  const audio = createAudioDriver();
  const cfg = store();
  cfg.setControlSurface({ input: PRO, output: PRO, device: PRO_NAME, enabled: true });
  audio.drainProductCalls(); // whatever earlier cases did, this case reads from here
  restoreLaunchpad(cfg);
  const status = createLaunchpadClient(makeCall()).status();
  expect(status.connected).toBe(true);
  expect(status.input).toBe(PRO);
  // The product's own record, in order: the pair kept out of the shared stream BEFORE it is opened, then the rendezvous's
  // end. A product holds its MIDI and audio until that second line, which is why it matters that it always arrives.
  expect(audio.drainProductCalls()).toEqual([`reserve_midi:${PRO}|${PRO}`, "restored"]);
});

test("a scan stepped to its end finds the device, persists the pair and claims it", () => {
  const audio = createAudioDriver();
  const cfg = store();
  const client = createLaunchpadClient(makeCall());
  setLaunchpadClient(client);
  resetLaunchpadScanForTest();
  const clock = { ms: 1000 };
  setLaunchpadScanClock(() => clock.ms);
  try {
    // Wanted, but nothing known yet: what a user who has just enabled the feature, or one upgrading from the legacy
    // host, has in config.json.
    cfg.setControlSurface({ input: "", output: "", device: "", enabled: true });
    if (client.status().connected) client.release();
    restoreLaunchpad(cfg); // nothing to claim, and the word still goes out
    audio.drainProductCalls();
    expect(startLaunchpadScan()).toBe(true);
    // The frame poll's own step loop: the scan is a clocked state machine, and nothing advances it but this.
    for (let i = 0; i < 1000; i++) {
      const s = pollLaunchpadScan();
      if (!s || !s.busy) break;
      clock.ms += 16;
    }
    const done = pollLaunchpadScan()!;
    expect(done.busy).toBe(false);
    const found = applyLaunchpadScan(cfg, done);
    expect(found?.input).toBe(PRO);
    expect(found?.name).toBe(PRO_NAME);
    // Persisted (a fresh store reads config.json back) and claimed: the answer the instance submenu gates on.
    const saved = store().controlSurface();
    expect(saved.input).toBe(PRO);
    expect(saved.output).toBe(PRO);
    expect(saved.device).toBe(PRO_NAME);
    expect(client.status().connected).toBe(true);
    // The scan's held edge around the loose ports, then the claim: the stream is told about the inputs it must give up
    // BEFORE anything is opened, and the pair is reserved before the link opens it.
    const calls = audio.drainProductCalls();
    expect(calls.includes("suspend_hardware_inputs:1")).toBe(true);
    expect(calls.indexOf("suspend_hardware_inputs:1")).toBeLessThan(calls.lastIndexOf(`reserve_midi:${PRO}|${PRO}`));
    expect(calls[calls.length - 1]).toBe(`reserve_midi:${PRO}|${PRO}`);
  } finally {
    setLaunchpadClient(undefined);
    resetLaunchpadScanForTest();
  }
});

test("Forget clears the pair AND the name, and gives the device back", () => {
  const audio = createAudioDriver();
  const cfg = store();
  const client = createLaunchpadClient(makeCall());
  cfg.setControlSurface({ input: PRO, output: PRO, device: PRO_NAME, enabled: true });
  restoreLaunchpad(cfg);
  expect(client.status().connected).toBe(true);
  audio.drainProductCalls();
  forgetLaunchpad(cfg);
  // The pair and the NAME go; "Use in Project" is the project's switch, not the device's, and stays as the user left it.
  expect(cfg.controlSurface()).toEqual({ input: "", output: "", enabled: true, device: "" });
  expect(client.status().connected).toBe(false);
  // The pair rejoins the shared stream: an empty reservation for the product, which is what opens the ports again.
  expect(audio.drainProductCalls()).toEqual(["reserve_midi:|"]);
});

test("Connect resolves the hinted default, and a pair whose device is away is refused in the facet's words", () => {
  const cfg = store();
  const client = createLaunchpadClient(makeCall());
  if (client.status().connected) client.release();
  cfg.setControlSurface({ input: "", output: "", device: "", enabled: false });
  const before = getLaunchpadConfig(cfg)!;
  connectLaunchpad(cfg, true, before);
  expect(getLaunchpadConfig(cfg)!.connected).toBe(true);
  expect(cfg.controlSurface().input).toBe(PRO); // the Pro's own port, found by its hint, not picked by the user

  // Now a saved pick whose device is not on the machine: the claim fails, the row says why in the FACET's words
  // (LaunchpadRpc's presence check), and nothing is left held.
  client.release();
  cfg.setControlSurface({ input: "Some Synth", output: "Some Synth", device: "", enabled: true });
  const warn = console.warn;
  const warned: string[] = [];
  console.warn = (...parts: unknown[]) => void warned.push(parts.map(String).join(" "));
  try {
    restoreLaunchpad(cfg);
  } finally {
    console.warn = warn;
  }
  expect(warned.length).toBe(1);
  expect(warned[0].includes("not connected: Some Synth")).toBe(true);
  expect(client.status().connected).toBe(false);
  expect(getLaunchpadConfig(cfg)!.error).toBe("not connected: Some Synth");
  // The pick is kept: the device may be plugged back in, and forgetting it would make the user scan again.
  expect(cfg.controlSurface().input).toBe("Some Synth");
  cfg.setControlSurface({ input: "", output: "", device: "", enabled: false });
});
