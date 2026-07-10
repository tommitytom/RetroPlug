// Ambient declarations for globals the txiki/QuickJS runtime provides that aren't in the
// ES2020 lib. The tsconfig keeps `lib` minimal + `types: []` to stay honest to the runtime
// surface (no browser DOM / Node globals), so the handful of web-standard globals the code
// actually uses are declared here. Kept intentionally small — extend as usage grows.

declare const console: {
  log(...args: unknown[]): void;
  info(...args: unknown[]): void;
  warn(...args: unknown[]): void;
  error(...args: unknown[]): void;
  debug(...args: unknown[]): void;
};

declare class TextEncoder {
  readonly encoding: string;
  encode(input?: string): Uint8Array;
}

declare class TextDecoder {
  constructor(label?: string, options?: { fatal?: boolean; ignoreBOM?: boolean });
  readonly encoding: string;
  decode(input?: ArrayBuffer | ArrayBufferView, options?: { stream?: boolean }): string;
}
