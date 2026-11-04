// @retroplug/n8's TypeScript half (docs/modular-graph-phase-e2-design.md section 7): the client of the pack's RPC facet
// (packages/n8/native/src/n8/desktop/N8Rpc.hpp). It takes the app's RPC call rather than importing the app's backend,
// so the pack depends on nothing of packages/retroplug.
//
// This is the facet's streaming-link half, which the UI's Settings > N8 drives (ui/screens/menu/n8Devices.ts): the
// serial ports, the chosen one, the link's state and its lookahead, read and set on the host's N8 provider. The state
// lives in the native host (the serial link, n8.cfg), so every read is fresh.

/** The app's synchronous RPC call (packages/retroplug/src/rpcClient.ts's Call): it answers the method's result, and
 *  throws when the method fails or the host has no such method. */
export type N8Call = (method: string, ...params: unknown[]) => unknown;

export interface N8Port {
  port: string; // OS serial port name (/dev/ttyACM0, COM3, ...)
  isN8: boolean; // detected Everdrive N8 Pro (USB VID:PID 38df:0017)
}

export interface N8LinkConfig {
  ports: N8Port[]; // available serial ports
  selectedPort: string; // "" = none chosen (connect auto-picks the attached N8)
  connected: boolean; // the serial link is open + handshaken
  enabled: boolean; // the user's "stream to the N8" toggle
  lookaheadMs: number; // timed-release latency the serial thread applies
  bytes: number; // bytes forwarded since connect (status)
  error: string; // last error, or "" (status)
}

/** The facet's link methods. Each applies at once in the host, which persists it to n8.cfg. */
export interface N8LinkClient {
  linkConfig(): N8LinkConfig;
  setPort(port: string): void;
  connect(enabled: boolean): void;
  setLookahead(ms: number): void;
}

export interface N8Client {
  link: N8LinkClient;
}

// The reply read tolerantly, field by field, as the UI read the hook object it replaces: a host one version apart must
// not take the submenu down.
function readConfig(raw: unknown): N8LinkConfig {
  const c = (raw ?? {}) as Partial<Record<keyof N8LinkConfig, unknown>>;
  const ports = Array.isArray(c.ports)
    ? c.ports.map((p: { port?: unknown; isN8?: unknown } | null) => ({
        port: typeof p?.port === "string" ? p.port : "",
        isN8: !!p?.isN8,
      }))
    : [];
  return {
    ports,
    selectedPort: typeof c.selectedPort === "string" ? c.selectedPort : "",
    connected: !!c.connected,
    enabled: !!c.enabled,
    lookaheadMs: typeof c.lookaheadMs === "number" ? c.lookaheadMs : 0,
    bytes: typeof c.bytes === "number" ? c.bytes : typeof c.bytes === "bigint" ? Number(c.bytes) : 0,
    error: typeof c.error === "string" ? c.error : "",
  };
}

/** A client of the N8 facet over `call`. Creating one calls nothing; a host that did not mount the facet makes each
 *  method throw (the call's own error). */
export function createN8Client(call: N8Call): N8Client {
  return {
    link: {
      linkConfig: () => readConfig(call("n8LinkConfig")),
      setPort: (port) => void call("n8SetPort", port),
      connect: (enabled) => void call("n8Connect", enabled),
      setLookahead: (ms) => void call("n8SetLookahead", Math.trunc(ms)),
    },
  };
}
