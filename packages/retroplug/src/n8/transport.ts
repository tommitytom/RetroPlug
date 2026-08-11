// The TS twin of the native ISerialPort seam (packages/native/src/host/n8/Edio.hpp): the ONE thing the
// TS N8 stack needs from native. `edio.ts` and everything above it (menu, ROM/save orchestration) run
// purely in TS on top of this interface, so they are unit-testable against a fake transport (fakeSerial.ts)
// and host-agnostic. The facet-backed implementation is `createSerialClient()` in ../realBackend.ts.

/** A single open serial byte pipe. Mirrors native ISerialPort (write / read / flushInput). */
export interface SerialTransport {
  /** Write raw bytes; returns the number written. */
  write(data: Uint8Array): number;
  /** Read up to `size` bytes, blocking up to `timeoutMs`. Returns the bytes read (may be shorter; empty on timeout). */
  read(size: number, timeoutMs: number): Uint8Array;
  /** Drop any buffered input. */
  flushInput(): void;
}

/** An open port: a transport plus lifecycle. */
export interface OpenSerialPort extends SerialTransport {
  readonly port: string;
  close(): void;
}

/** A discovered serial port (mirrors native SerialPortInfo). */
export interface SerialPortInfo {
  port: string;
  isN8: boolean;
}

/** Port enumeration + open. The scriptable entry to the physical N8 from the TS side. */
export interface SerialClient {
  /** All serial ports, flagging Everdrive N8 units (USB VID:PID 38df:0017). */
  listPorts(): SerialPortInfo[];
  /** Open a port; throws if it can't be opened. */
  open(port: string): OpenSerialPort;
}
