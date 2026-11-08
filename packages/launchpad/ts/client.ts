// The Launchpad facet's TS client (docs/modular-graph-phase-f3-design.md section 3), as @retroplug/n8's createN8Client
// is the N8's: the control plane's only door to the pack's ports, over the host's RPC `call`. Nothing here knows the
// protocol (that is profile.ts / protocol.ts / portScan.ts) or any native type: the client decodes the facet's answers
// into what a menu line or a scan needs, and a host that mounted no facet makes each call throw its own error.
//
// The claimed-pair half is the selection's, the pool half a scan's. Both are synchronous, as the facet is: a scan steps
// a few calls per UI frame with a clock it is told (portScan.ts), so no native thread scans and no wall-clock stall can
// wedge it.

/** One `call(method, ...args)` of the host's RPC server: the products' is synchronous and answers the method's value,
 *  throwing when the host did not mount the facet or the call failed. */
export type LaunchpadCall = (method: string, ...args: unknown[]) => unknown;

/** The system's MIDI ports, by the names the backend reports: unfiltered, because a Launchpad also speaks TRS/DIN and
 *  arrives under an interface's name. */
export interface LaunchpadPorts {
  inputs: string[];
  outputs: string[];
}

/** The claimed pair's state: what the menu's status line reads. `generation` is the claim's number (the session's
 *  `connected` value), `dropped` what the link lost since the claim. */
export interface LaunchpadStatus {
  connected: boolean;
  generation: number;
  sent: number;
  dropped: number;
  input: string;
  output: string;
  error: string;
}

/** What a scan's read answers: the messages that arrived since the last read, and how many the queue lost. */
export interface LaunchpadRead {
  messages: Uint8Array[];
  dropped: number;
}

export interface LaunchpadClient {
  /** The system's MIDI inputs and outputs. */
  ports(): LaunchpadPorts;
  /** Claim a pair, with the bytes that let the device go (replayed on release and at shutdown). */
  claim(input: string, output: string, closeBytes: Uint8Array): LaunchpadStatus;
  /** Give the device back: its close bytes first, then the shared stream may have the pair again. */
  release(): LaunchpadStatus;
  status(): LaunchpadStatus;
  /** Write a message to the claimed pair now (letting the device go while it stays claimed). */
  send(bytes: Uint8Array): boolean;
  /** The startup rendezvous's end: a product that held MIDI and audio for the claim resumes them here. */
  restored(): boolean;
  // --- the pool, a scan's ---
  openInputs(names: string[]): number[];
  openOutput(name: string): number;
  sendOn(handle: number, bytes: Uint8Array): boolean;
  read(handle: number): LaunchpadRead;
  close(handle: number): boolean;
  portError(): string;
}

const strings = (value: unknown): string[] =>
  Array.isArray(value) ? value.filter((v): v is string => typeof v === "string") : [];

const bool = (value: unknown): boolean => value === true;

const bytes = (value: unknown): Uint8Array => {
  if (value instanceof Uint8Array) return value;
  if (Array.isArray(value)) return Uint8Array.from(value as number[]);
  return new Uint8Array(0);
};

const statusOf = (value: unknown): LaunchpadStatus => {
  const v = (value ?? {}) as Partial<LaunchpadStatus>;
  return {
    connected: bool(v.connected),
    generation: Number(v.generation ?? 0),
    sent: Number(v.sent ?? 0),
    dropped: Number(v.dropped ?? 0),
    input: typeof v.input === "string" ? v.input : "",
    output: typeof v.output === "string" ? v.output : "",
    error: typeof v.error === "string" ? v.error : "",
  };
};

/** A client of the Launchpad facet over `call`. Creating one calls nothing. */
export function createLaunchpadClient(call: LaunchpadCall): LaunchpadClient {
  return {
    ports: () => {
      const v = (call("launchpadPorts") ?? {}) as Partial<LaunchpadPorts>;
      return { inputs: strings(v.inputs), outputs: strings(v.outputs) };
    },
    claim: (input, output, closeBytes) => statusOf(call("launchpadClaim", input, output, closeBytes)),
    release: () => statusOf(call("launchpadRelease")),
    status: () => statusOf(call("launchpadStatus")),
    send: (data) => bool(call("launchpadSend", data)),
    restored: () => bool(call("launchpadRestored")),
    openInputs: (names) => {
      const value = call("launchpadOpenInputs", names);
      return Array.isArray(value) ? value.map((h) => Number(h ?? 0)) : [];
    },
    openOutput: (name) => Number(call("launchpadOpenOutput", name) ?? 0),
    sendOn: (handle, data) => bool(call("launchpadPortSend", handle, data)),
    read: (handle) => {
      const v = (call("launchpadPortRead", handle) ?? {}) as { messages?: unknown[]; dropped?: unknown };
      return {
        messages: (v.messages ?? []).map(bytes),
        dropped: Number(v.dropped ?? 0),
      };
    },
    close: (handle) => bool(call("launchpadPortClose", handle)),
    portError: () => String(call("launchpadPortError") ?? ""),
  };
}
