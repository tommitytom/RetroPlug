// The runtime's ambient globals: what the txiki/QuickJS runtime provides and the ES2020 lib does not. This is
// @retroplug/core's runtime contract, which every package and the shell compile against (tsconfig.base.json's `files`
// includes it from each config's own directory): the compiler keeps `lib` minimal and `types` empty so the surface
// stays honest to the runtime (no browser DOM, no Node globals), and the handful of web-standard globals the code
// actually uses are declared here. Kept intentionally small; extend as usage grows.

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
