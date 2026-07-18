// The WAV codec now lives in the shared render library (src/render/wav.ts) so both the CLI and the
// background render worker use one implementation. This shim keeps the CLI's other sessions (export-*,
// render-*, analyze-*, sdk.ts) importing "../wav" unchanged.
export * from "../src/render/wav";
