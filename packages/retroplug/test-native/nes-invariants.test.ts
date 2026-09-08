// Continuous invariants: a conditional watchpoint that holds for a WHOLE run, checked on every write
// the ROM makes rather than at the moments a test happens to sample.
//
// Mesen's headless driver records a breakpoint hit and RETURNS instead of blocking for a UI thread, so
// the ordinary render loop can notice one, log it and carry on. That is what makes the debugger's
// condition evaluator usable from a timeline-driven test: before this, watchpoints only fired under
// `runUntilBreak`, which single-steps and produces no audio, so in practice nobody reached for them and
// bugs were hunted by sampling a variable at `Timeline.at()` callbacks and hoping.
//
// The two recipes below (a value bound, and a PPU write outside vblank) are the ones nesvj's bugs
// needed: `live_ofs > 15` and "$2007 written while the picture is being drawn".
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";


/** Wrap 6502 code as a bootable NROM cartridge: 16 KB PRG at $C000 + 8 KB CHR. */
function nromRom(code: number[]): Uint8Array {
  const PRG = 16 * 1024;
  const rom = new Uint8Array(16 + PRG + 8 * 1024);
  rom.set([0x4e, 0x45, 0x53, 0x1a, 1, 1], 0);
  if (code.length > PRG - 6) throw new Error("program does not fit in one PRG bank");
  rom.set(code, 16);
  rom.set([0x00, 0xc0, 0x00, 0xc0, 0x00, 0xc0], 16 + PRG - 6); // vectors; RESET -> $C000
  return rom;
}

/** `code`, then a `jmp` to itself. */
function parkAfter(code: number[]): number[] {
  const here = 0xc000 + code.length;
  return [...code, 0x4c, here & 0xff, here >> 8];
}

/** `LDA #v` / `STA $zp` : a write the watchpoint can see, with `v` as its `value`. */
function storeZp(zp: number, v: number): number[] {
  return [0xa9, v & 0xff, 0x85, zp & 0xff];
}

function boot(rom: Uint8Array, path: string) {
  const s = bootSession();
  expect(s.backend.writeFile(path, rom)).toBeTruthy();
  const id = s.project.systems.addSystem(path);
  if (id == null) throw new Error(`addSystem failed for ${path}`);
  return { s, id };
}

const WATCHED_ZP = 0x10;

test("a watchpoint fires during an ordinary render, not only under runUntilBreak", () => {
  // The whole mechanism in one assertion. The ROM writes 5 (fine) then 32 (a violation) and parks;
  // the render is a plain renderAudio, with no stepping anywhere.
  const { s, id } = boot(
    nromRom(parkAfter([...storeZp(WATCHED_ZP, 5), ...storeZp(WATCHED_ZP, 32)])),
    "/tmp/rp-invariant-fires.nes",
  );

  expect(s.backend.setBreakpoints(id, [
    { type: "write", start: WATCHED_ZP, end: WATCHED_ZP, condition: "value > 15" },
  ])).toBeTruthy();

  s.audio.renderAudio(20);

  const batch = s.backend.drainBreakHits(id);
  expect(batch.hits.length).toEqual(1); // the 5 did not trip it; the 32 did
  expect(batch.overflow).toEqual(0);
  expect(batch.hits[0].address).toEqual(WATCHED_ZP);
  expect(batch.hits[0].value).toEqual(32);
  expect(batch.hits[0].isWrite).toBeTruthy();
  // Where it happened, which is what turns "something went wrong" into a place to look.
  expect(batch.hits[0].scanline).toBeGreaterThanOrEqual(-1);
  expect(batch.hits[0].cpuCycle).toBeGreaterThan(0);

  // Take-not-peek: the parked ROM writes nothing more, so a second drain is empty.
  s.audio.renderAudio(20);
  expect(s.backend.drainBreakHits(id).hits.length).toEqual(0);

  s.project.systems.removeSystem(id);
});

test("an armed invariant does not perturb the run it is watching", () => {
  // The property that makes these safe to leave on. If arming a watchpoint changed what the ROM did,
  // an invariant would be a Heisenbug generator rather than a check.
  //
  // The measure is TIMING, not memory contents: the ROM spins a 16-bit counter as fast as it can, so
  // its value after a fixed render is a direct count of instructions executed. (Comparing RAM would
  // not work: Mesen powers NES RAM on RANDOM, so two boots differ wherever the ROM hasn't written.)
  //
  // The watchpoint sits on the counter itself with a condition that can never hold, so it is
  // EVALUATED on every single increment and fires on none: the most expensive way to be armed.
  const COUNT_LO = 0x10;
  const COUNT_HI = 0x11;
  const spin = [
    // Zero the counter first: NES RAM powers on RANDOM, so an un-initialised one would start from
    // garbage and the two runs would differ by that garbage rather than by anything under test.
    0xa9, 0x00,           // $C000  LDA #$00
    0x85, COUNT_LO,       // $C002  STA $10
    0x85, COUNT_HI,       // $C004  STA $11
    0xe6, COUNT_LO,       // $C006  INC $10     <- loop
    0xd0, 0xfc,           // $C008  BNE $C006
    0xe6, COUNT_HI,       // $C00A  INC $11
    0x4c, 0x06, 0xc0,     // $C00C  JMP $C006
  ];
  const rom = nromRom(spin);

  const read16 = (s: ReturnType<typeof boot>["s"], id: number) =>
    (s.backend.readCpu(id, COUNT_LO) ?? -1) + ((s.backend.readCpu(id, COUNT_HI) ?? -1) << 8);

  const bare = boot(rom, "/tmp/rp-invariant-bare.nes");
  bare.s.audio.renderAudio(30);
  const without = read16(bare.s, bare.id);
  bare.s.project.systems.removeSystem(bare.id);

  const watched = boot(rom, "/tmp/rp-invariant-watched.nes");
  expect(watched.s.backend.setBreakpoints(watched.id, [
    { type: "write", start: COUNT_LO, end: COUNT_HI, condition: "value > 255" },
  ])).toBeTruthy();
  watched.s.audio.renderAudio(30);
  const withArmed = read16(watched.s, watched.id);
  expect(watched.s.backend.drainBreakHits(watched.id).hits.length).toEqual(0);
  watched.s.project.systems.removeSystem(watched.id);

  expect(without).toBeGreaterThan(0); // the loop actually ran, so equality below means something
  expect(withArmed).toEqual(without);
});

test("renderTimeline fails the run when an invariant is violated, and names where", () => {
  const { s, id } = boot(
    nromRom(parkAfter([...storeZp(WATCHED_ZP, 5), ...storeZp(WATCHED_ZP, 32)])),
    "/tmp/rp-invariant-timeline.nes",
  );

  let message = "";
  try {
    renderTimeline(s, new Timeline(), {
      durationMs: 20,
      invariants: [{ system: id, address: WATCHED_ZP, max: 15, label: "watched" }],
    });
  } catch (e) {
    message = String(e);
  }
  expect(message.includes('invariant "watched" violated')).toBeTruthy();
  expect(message.includes("scanline")).toBeTruthy();
  expect(message.includes("value 32")).toBeTruthy();

  s.project.systems.removeSystem(id);
});

test("renderTimeline passes a run that respects its invariant, and disarms afterwards", () => {
  const { s, id } = boot(
    nromRom(parkAfter([...storeZp(WATCHED_ZP, 5), ...storeZp(WATCHED_ZP, 15)])), // 15 is the bound, not past it
    "/tmp/rp-invariant-ok.nes",
  );

  renderTimeline(s, new Timeline(), {
    durationMs: 20,
    invariants: [{ system: id, address: WATCHED_ZP, max: 15 }],
  });

  // Disarmed on the way out, so a later render is back to the ordinary un-watched path.
  expect(s.backend.drainBreakHits(id).hits.length).toEqual(0);
  s.audio.renderAudio(20);
  expect(s.backend.drainBreakHits(id).hits.length).toEqual(0);

  s.project.systems.removeSystem(id);
});

// --- the PPU recipe: a write outside vblank ----------------------------------------------------
//
// nesvj's third bug: an NMI fallback reused a per-vblank budget sized for a cheaper unit, so 28 of
// them overran a 7459-cycle vblank. The overrunning writes land wherever the rendering pipeline's VRAM
// address happens to be (the NAMETABLE), so border cells started pointing at image tiles. CHR
// byte-identity tests saw nothing; it was found by photographing a television.

const PPU_MASK = 0x2001;
const PPU_STATUS = 0x2002;
const PPU_DATA = 0x2007;

/** `LDA #v` / `STA $abs`. */
function storeAbs(addr: number, v: number): number[] {
  return [0xa9, v & 0xff, 0x8d, addr & 0xff, addr >> 8];
}

/** Turn rendering on (background + sprites, no clipping). */
const ENABLE_RENDERING = storeAbs(PPU_MASK, 0x1e);

// A write to $2007 is only legal while the picture is NOT being drawn. `scanline >= 0 && scanline < 240`
// is exactly the visible region, so a hit is an illegal write and nothing else.
const OUTSIDE_VBLANK = "scanline >= 0 && scanline < 240";

test("a $2007 write while the picture is being drawn is caught", () => {
  // Rendering on, then $2007 written in a tight loop with no regard for where the beam is.
  const code = [
    ...ENABLE_RENDERING,
    0xa9, 0x55,                              // LDA #$55
    0x8d, PPU_DATA & 0xff, PPU_DATA >> 8,    // STA $2007
    0x4c, 0x05, 0xc0,                        // JMP back to the LDA
  ];
  const { s, id } = boot(nromRom(code), "/tmp/rp-invariant-ppu-bad.nes");

  expect(s.backend.setBreakpoints(id, [
    { type: "write", start: PPU_DATA, end: PPU_DATA, condition: OUTSIDE_VBLANK },
  ])).toBeTruthy();
  s.audio.renderAudio(40); // a couple of frames

  const batch = s.backend.drainBreakHits(id);
  // It fires a lot, which is the point: the capture cap keeps the buffer bounded and still reports.
  expect(batch.hits.length + batch.overflow).toBeGreaterThan(0);
  const hit = batch.hits[0];
  expect(hit.address).toEqual(PPU_DATA);
  expect(hit.scanline).toBeGreaterThanOrEqual(0);
  expect(hit.scanline).toBeLessThan(240);

  s.project.systems.removeSystem(id);
});

test("a $2007 write gated on vblank is not", () => {
  // The control, and the reason the condition has to be exact: a ROM doing the right thing must not
  // fail, or the check gets switched off.
  //
  // Wait for $2002 bit 7 (vblank has begun), write once, loop. Reading $2002 clears the flag, so this
  // writes exactly once per frame and always inside vblank.
  const code = [
    ...ENABLE_RENDERING,
    0xad, PPU_STATUS & 0xff, PPU_STATUS >> 8, // LDA $2002       <- $C005
    0x10, 0xfb,                               // BPL -5 (back to the LDA)
    0xa9, 0x55,                               // LDA #$55
    0x8d, PPU_DATA & 0xff, PPU_DATA >> 8,     // STA $2007
    0x4c, 0x05, 0xc0,                         // JMP $C005
  ];
  const { s, id } = boot(nromRom(code), "/tmp/rp-invariant-ppu-good.nes");

  expect(s.backend.setBreakpoints(id, [
    { type: "write", start: PPU_DATA, end: PPU_DATA, condition: OUTSIDE_VBLANK },
  ])).toBeTruthy();
  s.audio.renderAudio(100); // several frames, so "it never got going" can't pass for "it never fired"

  const batch = s.backend.drainBreakHits(id);
  expect(batch.hits.length).toEqual(0);
  expect(batch.overflow).toEqual(0);

  s.project.systems.removeSystem(id);
});

// --- the budget recipe: how much of vblank a ROM actually spent ---------------------------------
//
// The same mechanism answers the OTHER question nesvj had no way to ask. It sizes four separate
// per-vblank budgets by sweeping a value until output breaks, because an overrun is silent: the
// writes that miss simply land somewhere else. A watchpoint on $2001 turns that into a subtraction,
// because every hit carries the CPU cycle it fired at.

test("cycles spent under forced blank are measurable from $2001 hits", () => {
  // Rendering off at the top of vblank, a fixed amount of work, rendering back on. The gap between
  // the two $2001 writes IS the budget consumed, and it is the number every one of those sweeps is
  // really trying to find.
  const code = [
    0xa9, 0x1e,             // $C000  LDA #$1E
    0x8d, 0x01, 0x20,       // $C002  STA $2001      rendering on
    0xad, 0x02, 0x20,       // $C005  LDA $2002      <- wait for vblank
    0x10, 0xfb,             // $C008  BPL $C005
    0xa9, 0x00,             // $C00A  LDA #$00
    0x8d, 0x01, 0x20,       // $C00C  STA $2001      rendering OFF: the window opens
    0xa2, 0x14,             // $C00F  LDX #$14       20 iterations of...
    0xca,                   // $C011  DEX            <- ...a 5-cycle loop
    0xd0, 0xfd,             // $C012  BNE $C011
    0xa9, 0x1e,             // $C014  LDA #$1E
    0x8d, 0x01, 0x20,       // $C016  STA $2001      rendering on: the window closes
    0x4c, 0x05, 0xc0,       // $C019  JMP $C005
  ];
  const { s, id } = boot(nromRom(code), "/tmp/rp-invariant-budget.nes");

  // No condition: every $2001 write is interesting, because the pair is the measurement.
  expect(s.backend.setBreakpoints(id, [{ type: "write", start: PPU_MASK, end: PPU_MASK }])).toBeTruthy();
  s.audio.renderAudio(60); // a few frames

  const hits = s.backend.drainBreakHits(id).hits;
  const off = hits.findIndex((h) => h.value === 0x00);
  expect(off).toBeGreaterThanOrEqual(0);
  const on = hits.findIndex((h, i) => i > off && h.value === 0x1e);
  expect(on).toBeGreaterThan(off);

  // ~101 cycles of work plus the two instruction pairs around it. The band is wide on purpose: what
  // is being proved is that the number is a real cycle measurement, not that it is exactly 107.
  const spent = Number(hits[on].cpuCycle) - Number(hits[off].cpuCycle);
  expect(spent).toBeGreaterThan(80);
  expect(spent).toBeLessThan(200);

  // And it opened where a ROM would open it: in vblank, past the visible region.
  expect(hits[off].scanline).toBeGreaterThanOrEqual(240);

  s.project.systems.removeSystem(id);
});
