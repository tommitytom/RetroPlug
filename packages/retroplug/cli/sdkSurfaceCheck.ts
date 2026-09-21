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
// Add a line here whenever you add to the SDK surface. It costs one line and turns a defect a
// consumer has to report into a build failure.
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
