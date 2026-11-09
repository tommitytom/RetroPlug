// The WAV codec: the pure functions live in @retroplug/dsp/wav, the streaming writer in the shared render library,
// which re-exports both. This shim keeps the CLI's sessions importing "../wav" unchanged.
export * from "../src/render/wav";
