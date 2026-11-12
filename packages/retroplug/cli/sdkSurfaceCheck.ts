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
import type { ConstructSpec as RealConstructSpec } from "../src/backend";
import type { EngineGraph as RealEngineGraph } from "../src/engineGraph";
import { bootSession as realBootSession, DirectGraph } from "./sdk";
import { CONSOLES, REAL_CARTS } from "../src/generated/consoles";
import {
  bootSession,
  hostArgs,
  Timeline,
  renderTimeline,
  skip,
  interleaveStereoStreams,
  deinterleaveStereo,
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
// NARROWER view on purpose - its `Backend` omits 24 methods a ROM author has no use for - so neither
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

// The class, the spec, the graph type and the three callables are compared shape by shape rather than name by name.
// The class is one-way (the declaration is a curated view: `setRouting` widens the real routing union to `string`),
// the graph type stays opaque on purpose, `ConstructSpec` keeps every real field with its own optionality while
// declaring `platform` and `core` as plain strings, and the two pure helpers and the session runner must agree exactly.
export type _DirectGraphCtorArity = Assert<
  Exact<ConstructorParameters<typeof Sdk.DirectGraph>["length"], ConstructorParameters<typeof Dts.DirectGraph>["length"],
    "DirectGraph's constructor takes a different number of arguments than the declaration says">
>;
export type _DirectGraph = Assert<
  Covers<InstanceType<typeof Sdk.DirectGraph>, InstanceType<typeof Dts.DirectGraph>,
    "DirectGraph: the declaration describes a member the class does not have">
>;
export type _ConstructSpec = Assert<
  Covers<RealConstructSpec, Dts.ConstructSpec,
    "ConstructSpec: the declaration describes a field the real spec does not have">
>;
// The four optional fields above are the ones that drifted once (`ConstructSpec`'s additions), and a one-way check cannot
// see a field the declaration DROPS or makes `never` (an omitted optional field still satisfies the wider real type).
// So the key set is exact both ways, and every value is compared both ways except the two the declaration widens on
// purpose: `platform` and `core` are plain strings there, because a consumer must not import the registry's id types.
export type _ConstructSpecKeys = Assert<
  Exact<keyof RealConstructSpec, keyof Dts.ConstructSpec, "ConstructSpec's fields">
>;
type ConstructSpecValue<K extends keyof RealConstructSpec> =
  Exact<RealConstructSpec[K], Dts.ConstructSpec[K], `ConstructSpec.${K & string}`>;
export type _ConstructSpecValues = Assert<
  [
    ConstructSpecValue<"romPath">, ConstructSpecValue<"embeddedRom">, ConstructSpecValue<"savPath">,
    ConstructSpecValue<"statePath">, ConstructSpecValue<"romBytes">, ConstructSpecValue<"sramBytes">,
    ConstructSpecValue<"stateBytes">, ConstructSpecValue<"coreConfig">,
  ] extends [true, true, true, true, true, true, true, true] ? true : "ConstructSpec: a field's type drifted"
>;
export type _EngineGraphOpaque = Assert<
  keyof Dts.EngineGraph extends never ? true : "EngineGraph: the declaration names a member, so it is no longer opaque"
>;
export type _EngineGraph = Assert<
  Covers<RealEngineGraph, Dts.EngineGraph, "EngineGraph: the real graph does not satisfy the declared one">
>;
export type _RunSessionArity = Assert<
  Exact<Parameters<typeof Sdk.runSession>["length"], Parameters<typeof Dts.runSession>["length"],
    "runSession takes a different number of arguments than the declaration says">
>;
export type _RunSessionReturn = Assert<
  Exact<ReturnType<typeof Sdk.runSession>, ReturnType<typeof Dts.runSession>, "runSession's return type">
>;
export type _RunSessionArg = Assert<
  Covers<Parameters<typeof Dts.runSession>, Parameters<typeof Sdk.runSession>,
    "runSession: the declaration's callback does not match the implementation's">
>;
export type _Interleave = Assert<
  Exact<Parameters<typeof Sdk.interleaveStereoStreams>, Parameters<typeof Dts.interleaveStereoStreams>,
    "interleaveStereoStreams' parameters">
>;
export type _InterleaveReturn = Assert<
  Exact<ReturnType<typeof Sdk.interleaveStereoStreams>, ReturnType<typeof Dts.interleaveStereoStreams>,
    "interleaveStereoStreams' return type">
>;
export type _Deinterleave = Assert<
  Exact<Parameters<typeof Sdk.deinterleaveStereo>, Parameters<typeof Dts.deinterleaveStereo>,
    "deinterleaveStereo's parameters">
>;
export type _DeinterleaveReturn = Assert<
  Exact<ReturnType<typeof Sdk.deinterleaveStereo>, ReturnType<typeof Dts.deinterleaveStereo>,
    "deinterleaveStereo's return type">
>;

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
  void (stats.rxDepth + stats.rxCapacity + stats.droppedBytes + stats.wirePending + stats.txDepth + stats.linkFrames + stats.linkResent + stats.dmcFetchesDuringDma + stats.cartReadsDuringDma);

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

  // A direct graph shares the session's engine graph, and its instance satisfies the declaration's class.
  const real = realBootSession();
  const direct: Dts.DirectGraph = new DirectGraph(real.backend, real.project.graph);
  void direct.ids();
  // The declared constructor takes a session's backend and graph: the file's own session (from the declaration) is the
  // narrower view, so this one is built from the implementation.
  void s;
  // The declaration's ConstructSpec is a narrower view of the real one, which a caller may hand it: a wider view, in
  // fact, so every field below is the real one's and only `platform` and `core` read as plain strings.
  // The console is the one a REAL cart drives - the FIFO knob at the end is that console's role config, not another
  // console's - read from the registry by the cart's platform, so this check spells no console's knowledge either.
  const realCart = REAL_CARTS[0]!;
  const descriptor = CONSOLES.find((console) =>
    console.platforms.some((platform) => platform.id === realCart.platform),
  )!;
  const spec = { romPath: rom ?? "", platform: realCart.platform, core: descriptor.core, embeddedRom: "",
    savPath: null, statePath: null } satisfies Dts.ConstructSpec;
  void spec;
  // The two stereo helpers split and join a render's PCM.
  void interleaveStereoStreams([deinterleaveStereo(new Float32Array(4))[0]]);
  // `runSession` is not called here: it exits the process, which this compile-only check must not do.

  // The lossy-transport knob is a role config, so it is only checked for shape.
  s.project.systems.setRoleConfig(id, descriptor.core, { fifo: "hardware", fifoBytesPerSecond: 20000 });
}
