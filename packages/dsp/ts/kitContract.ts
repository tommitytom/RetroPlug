// What a kit compiler is and what it runs on (docs/modular-graph-phase-d2-design.md section 5), apart from the
// compilers themselves: the audio driver dispatches by `KitFormat`'s id to a format a cart's app module contributed, and
// a compiler takes a `KitJobRunner` (a host's offline jobs) rather than a backend, so nothing here names the shell.
// compile.ts, which stays the shell's with the formats registered, re-exports these names.
import type { OfflineRunRequest, OfflineRunResult } from "@retroplug/core/offlineJob";
import type { KitEffect } from "./kitEffect";

/** A compiled kit: the bank, and each spec sample's reason in spec order, null exactly when the bank holds that
 *  sample whole. A reason is a failure (the slot is empty), or why a slot that compiled is still empty or short (an
 *  LSDj window with nothing to pack, a bank that ran out of room); a sample past the bank's slot count is not
 *  compiled, and its reason says so. */
export interface KitCompileResult {
  bytes: Uint8Array;
  errors: (string | null)[];
}

/** One source sample a format compiles: a source audio file (WAV/MP3/FLAC, decoded natively), a slot name, an optional
 *  source-frame window, and an effect chain. The slot's own shape is the format's (`KitFormat`'s parameter), which
 *  extends this with what its encoder takes. */
export interface KitSampleSpec {
  path: string;
  name: string;
  offset?: number;
  length?: number;
  effects: KitEffect[];
}

/** What the compilers run their jobs on: a backend's offline jobs (every native host has them; the mock rejects), of
 *  which the compilers need only the promise, not the handle's cancel.
 *  The promises settle only in a host that pumps or waits for its offline jobs
 *  (packages/retroplug/src/offline.ts): one with a UI tick
 *  pumps from it, and one without needs the poll's native wait, which createAudioDriver turns on as it is made (or
 *  setOfflineWait). In a host with neither, a compile never settles. */
export interface KitJobRunner {
  offlineRun(request: OfflineRunRequest): Promise<OfflineRunResult>;
}

/** One kit bank format a cart contributes (its package's app's `kitFormats`): the compiler an id names, so the
 *  driver dispatches by what the product registered rather than by importing a cart's assembler itself. `id` is the
 *  format's, not the cart's: risa and BlipToaster share "risa", one DMC bank. */
export interface KitFormat<S = unknown> {
  readonly id: string;
  compile(runner: KitJobRunner, spec: S): Promise<KitCompileResult>;
}
