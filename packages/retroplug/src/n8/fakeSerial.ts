// An in-memory SerialTransport for unit tests - the TS twin of the gtest FakeSerialPort
// (packages/native/test/n8/Edio.test.cpp). Writes append to `written`; reads pop from a scripted `toRead`
// queue (an empty queue yields 0 bytes, which Edio treats as a timeout / no response).

import type { SerialTransport } from "./transport";

export class FakeSerialPort implements SerialTransport {
  readonly written: number[] = [];
  readonly toRead: number[] = [];
  flushed = false;

  write(data: Uint8Array): number {
    for (const b of data) this.written.push(b);
    return data.length;
  }

  read(size: number, _timeoutMs: number): Uint8Array {
    const n = Math.min(size, this.toRead.length);
    return new Uint8Array(this.toRead.splice(0, n)); // 0 bytes => Edio sees a timeout
  }

  flushInput(): void {
    this.flushed = true;
  }

  // Queue a little-endian 16-bit status word (what the N8 returns for CMD_STATUS).
  queueStatus(v: number): void {
    this.toRead.push(v & 0xff, (v >> 8) & 0xff);
  }

  // Queue raw bytes to be returned by subsequent reads.
  queueBytes(...bytes: number[]): void {
    this.toRead.push(...bytes);
  }
}
