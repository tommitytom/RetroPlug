// Compile-only. Nothing imports this and it is never bundled; its whole job is to make the shipped
// SDK declaration file (`cli/sdk-types.d.ts`) fail `pnpm typecheck` when it drifts from the
// implementation it describes.
//
// That file is hand-maintained, and consumers reach it through a tsconfig `paths` entry rather than
// by compiling our source, so nothing in this repo used to check it. It has shipped out of step
// twice: once with `deleteFile` / `listDir` / `rename` implemented but undeclared, and once with
// `TimelineInvariant` declared but `renderTimeline`'s own signature still missing `invariants` -
// which type-checks fine HERE, because the app compiles against the real modules, and only breaks in
// a consumer's editor. This file compiles against the DECLARATION, via the same `retroplug-cli`
// specifier they use.
//
// Two things are checked, and the hand-written usage below is only the second of them.
//
// The FIRST is a direct comparison of the two surfaces, which is what this file was missing: everything
// here used to resolve through the `retroplug-cli` specifier, i.e. the declaration on both sides, so no
// drift between the declaration and the implementation could be seen at all. Nine types were declared
// and not exported from the barrel that calls itself the SSOT, and that is exactly the class of drift
// this file exists to catch.
//
// Add a line here whenever you add to the SDK surface. It costs one line and turns a defect a
// consumer has to report into a build failure.
import type * as Dts from "retroplug-cli";
import type * as Sdk from "./sdk";
import {
  bootSession,
  hostArgs,
  Timeline,
  renderTimeline,
  skip,
  type BreakHitBatch,
  type CoreTransportStats,
  type TimelineInvariant,
} from "retroplug-cli";


// ── The surfaces, compared ────────────────────────────────────────────────────────────────────────

/** Fails to compile unless the argument resolves to exactly `true`.
 *
 *  A mismatch yields the MESSAGE rather than `false`, because the message is what the compiler then
 *  prints: `Type '"AudioDriver: ..."' does not satisfy the constraint 'true'` tells the next person
 *  what drifted, where a bare `false` sends them to read this file first. It must not yield `never`
 *  either - `never extends true` is TRUE, so a `never` would quietly satisfy the assertion. */
type Assert<T extends true> = T;
type Covers<A, B, Msg extends string> = [A] extends [B] ? true : Msg;
type Exact<A, B, Msg extends string> =
  Covers<A, B, `${Msg}: the declaration describes something the implementation does not`> extends true
    ? Covers<B, A, `${Msg}: the implementation has something the declaration does not`>
    : `${Msg}: the declaration describes something the implementation does not`;

// Values: the NAME sets, two-way, so the declaration can neither omit an export nor invent one. This is
// deliberately not a comparison of the types behind those names, because the declaration is a curated
// NARROWER view on purpose - its `Backend` omits 25 methods a ROM author has no use for - so neither
// direction of a type-level namespace comparison can hold, and demanding one would only pressure the
// declaration into growing surface nobody wants. A missing or invented NAME is always a defect, and it
// is the drift that has actually shipped: `deleteFile` / `listDir` / `rename` were implemented and
// undeclared, and a consumer had to report it.
export type _NoUndeclaredValues = Assert<
  Covers<keyof typeof Sdk, keyof typeof Dts, "cli/sdk.ts exports a value cli/sdk-types.d.ts does not declare">
>;
export type _NoPhantomValues = Assert<
  Covers<keyof typeof Dts, keyof typeof Sdk, "cli/sdk-types.d.ts declares a value cli/sdk.ts does not export">
>;

// Plain data types: exact. A drifted field on either side is a consumer's compile error otherwise.
export type _CoreTransportStats = Assert<Exact<Sdk.CoreTransportStats, Dts.CoreTransportStats, "CoreTransportStats">>;
export type _BreakHit = Assert<Exact<Sdk.BreakHit, Dts.BreakHit, "BreakHit">>;
export type _BreakHitBatch = Assert<Exact<Sdk.BreakHitBatch, Dts.BreakHitBatch, "BreakHitBatch">>;
export type _TimelineInvariant = Assert<Exact<Sdk.TimelineInvariant, Dts.TimelineInvariant, "TimelineInvariant">>;
export type _PngImageData = Assert<Exact<Sdk.PngImageData, Dts.PngImageData, "PngImageData">>;
export type _LoadResult = Assert<Exact<Sdk.LoadResult, Dts.LoadResult, "LoadResult">>;
export type _NoteOpts = Assert<Exact<Sdk.NoteOpts, Dts.NoteOpts, "NoteOpts">>;

// The two big interfaces are declared as a deliberate SUBSET - a ROM author does not need the DSP
// profiling knobs - so these are one-way: the real thing must satisfy everything declared, while the
// declaration stays free to omit. That direction still catches a declared member that does not exist or
// whose type moved, which is the drift that has actually shipped. It does NOT catch an omission, so a
// method the shipped example sessions call belongs in the declaration on purpose, not by accident.
export type _SystemsStore = Assert<Covers<Sdk.SystemsStore, Dts.SystemsStore, "SystemsStore: the declaration describes a member the implementation does not have">>;
export type _AudioDriver = Assert<Covers<Sdk.AudioDriver, Dts.AudioDriver, "AudioDriver: the declaration describes a member the implementation does not have">>;

/** Never called. Exported so `noUnusedLocals` doesn't strip the very thing being checked. */
export function sdkSurfaceCheck(): void {
  const s = bootSession();
  const rom = hostArgs()[0];
  const id = s.project.systems.addSystem(rom)!;

  // The transport readouts (spec/09).
  const stats: CoreTransportStats = s.backend.getCoreTransportStats(id);
  void (stats.rxDepth + stats.rxCapacity + stats.droppedBytes + stats.wirePending + stats.txDepth);

  // Passive break capture, and the shape of one hit.
  const batch: BreakHitBatch = s.backend.drainBreakHits(id);
  for (const hit of batch.hits) {
    void (hit.breakpointId + hit.pc + hit.address + hit.value + hit.scanline + hit.cycle);
    void Number(hit.cpuCycle);
    void hit.isWrite;
  }
  void batch.overflow;

  // Continuous invariants on a timeline run: the declaration that was missing.
  const invariants: TimelineInvariant[] = [
    { system: id, symbol: "live_ofs", max: 15 },
    { system: id, address: 0x2007, condition: "scanline >= 0 && scanline < 240", label: "ppu" },
  ];
  renderTimeline(s, new Timeline(), { durationMs: 100, warmupMs: 900, invariants });

  // `skip` must be declared returning `never`, not void: that is what lets `return skip(...)` sit in a
  // void test body, which is how most call sites are written.
  const guard = (why: string): void => {
    if (why) return skip(why);
  };
  guard(rom ? "" : "no ROM argument");

  // The lossy-transport knob is a role config, so it is only checked for shape.
  s.project.systems.setRoleConfig(id, "mesen", { fifo: "hardware", fifoBytesPerSecond: 20000 });
}
