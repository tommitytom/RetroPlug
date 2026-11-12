// The probe's timers (packages/launchpad/ts/cli/launchpadProbe.ts) cannot strand the hardware: an error a timer
// callback throws ends the session with exit 1, and a Launchpad left in Programmer mode has its own Settings menu
// locked until a power-cycle. So a throw in any of the probe's timer bodies must first send the blank and the
// Live-mode sysex and close the ports, then exit 1, once.
import { test, expect } from "@retroplug/testing";
import type { CliHost } from "@retroplug/core/cliTool";
import { launchpadProbeTool, releasingOnThrow } from "../cli/launchpadProbe";
import { PRO_MK3 } from "../profile";
import { exitToLiveMode } from "../protocol";

const quiet = <T>(f: () => T, errors: string[] = []): T => {
  const real = console.error;
  const log = console.log;
  console.error = (m: string) => void errors.push(m);
  console.log = () => {};
  try {
    return f();
  } finally {
    console.error = real;
    console.log = log;
  }
};

test("a body that throws runs the release and exits 1, once, and is not run again", () => {
  const exits: number[] = [];
  let released = 0;
  let runs = 0;
  const errors: string[] = [];
  const tick = releasingOnThrow(
    { process: { exit: (c: number) => void exits.push(c) } } as unknown as Pick<CliHost, "process">,
    () => void released++,
    () => {
      runs++;
      throw new Error("facet gone");
    },
  );
  quiet(() => {
    tick();
    tick();
  }, errors);
  expect([exits, released, runs]).toEqual([[1], 1, 1]);
  expect(errors).toEqual(["launchpad-probe: facet gone"]);
});

test("a release that throws too still ends the session with the timer's error reported", () => {
  const exits: number[] = [];
  const errors: string[] = [];
  const tick = releasingOnThrow(
    { process: { exit: (c: number) => void exits.push(c) } } as unknown as Pick<CliHost, "process">,
    () => {
      throw new Error("release failed");
    },
    () => {
      throw new Error("first");
    },
  );
  quiet(tick, errors);
  expect(exits).toEqual([1]);
  expect(errors).toEqual(["launchpad-probe: first"]);
});

test("the probe's poll, with the facet's read failing, leaves Programmer mode and exits 1", () => {
  const sent: number[][] = [];
  const closed: number[] = [];
  const exits: number[] = [];
  const call = (method: string, ...params: unknown[]): unknown => {
    if (method === "launchpadPorts") return { inputs: [PRO_MK3.portHint], outputs: [PRO_MK3.portHint] };
    if (method === "launchpadOpenOutput") return 5;
    if (method === "launchpadOpenInputs") return [6];
    if (method === "launchpadPortSend") {
      sent.push(Array.from(params[1] as Uint8Array));
      return true;
    }
    if (method === "launchpadPortClose") {
      closed.push(params[0] as number);
      return true;
    }
    if (method === "launchpadPortRead") throw new Error("the read failed");
    return undefined;
  };
  const host = {
    devices: { rpc: call },
    process: { keepAlive: () => {}, exit: (c: number) => void exits.push(c) },
  } as unknown as CliHost;
  const timers: (() => void)[] = [];
  const real = (globalThis as unknown as { setInterval: unknown }).setInterval;
  (globalThis as unknown as { setInterval: unknown }).setInterval = (f: () => void) => timers.push(f);
  try {
    quiet(() => launchpadProbeTool.run(host, ["--seconds", "5"]));
  } finally {
    (globalThis as unknown as { setInterval: unknown }).setInterval = real;
  }
  expect(timers.length).toBe(2); // the poll and the countdown
  quiet(() => timers[0]());
  expect(exits).toEqual([1]);
  expect(sent.some((m) => m.join(",") === exitToLiveMode(PRO_MK3).join(","))).toBe(true);
  expect(closed.sort()).toEqual([5, 6]);
});
