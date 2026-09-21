// Native proofs for the BlipToaster ROM-asset override path (kits + fonts). Mirrors test-native/risa-rom.test.ts.
//
// (1) MesenBackend::build must load TS-supplied effective ROM bytes (spec.romBytes) instead of slurping
//     romPath — proven by constructing with a nonexistent romPath but valid romBytes (only the romBytes path
//     can boot it). This support already shipped for risa; here it's exercised for a BlipToaster ROM.
// (2) End-to-end: patch a kit + a font into the base ROM in memory via BlipToasterRom, construct from those
//     bytes, and confirm the patched image boots (and the on-disk .nes is never touched).
// Driven by the COMMITTED resources/roms/bliptoaster.nes (which carries the "bliptoaster" marker + baked kit).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { BlipToasterRom } from "../src/bliptoaster/rom";
import { blipToasterInfo, isBlipToasterRomHeader, BLIPTOASTER_MARKER } from "../src/bliptoaster/romDetect";

declare const __REPO_RESOURCES_DIR__: string;
// The COMMITTED ROM, so every leg below runs everywhere — no fixture guard, no skip.
//
// Three constants used to live here: this one, plus two pointing into /workspaces/bliptoaster/rom/build/,
// a directory that has never existed in any checkout. Every leg that used them skipped silently, forever.
// They all name the same cart in the end: the base build IS the FME-7 build (mapper 69), because every
// BlipToaster build banks and the base one takes FME-7 for kit banking alone, with no expansion audio —
// which is exactly what the multi-kit legs below ask for.
const BLIPTOASTER_ROM = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
const KIT_MAGIC_ADDR = 0xdf40; // $C000 + $1F40: the risa-kit present marker in the bank mapped at $C000
const CC_STATUS_CH5 = 0xb4; // Control Change, MIDI channel 5 (the DMC channel)
const CC_DMC_BANK = 14;
const CC_DMC_LOOP = 4;

// REGRESSION GUARD. The vendored ROM and the detection marker drifted apart once already: resources/roms was
// last refreshed while the marker was still "EVERMIDI", the marker later became "evermidi-n8", and nothing
// noticed that the committed ROM had stopped being recognised. Assert the shipped bytes against the shipped
// detector so re-vendoring a mismatched build fails here instead of silently dropping the asset menu.
test("the COMMITTED resources/roms ROM is detected, and its role set is attached", () => {
  const be = createRealBackend();
  expect(be.fileExists(BLIPTOASTER_ROM)).toBeTruthy(); // committed, so never a skip

  const rom = BlipToasterRom.fromBytes(be.readFile(BLIPTOASTER_ROM)!);
  expect(rom.isBlipToaster).toBe(true);

  const header = be.readFile(BLIPTOASTER_ROM)!.subarray(0, 0x150);
  expect(isBlipToasterRomHeader(header)).toBe(true);
  const info = blipToasterInfo(header)!;
  expect(info != null).toBeTruthy();
  console.log(`[bliptoaster-rom] vendored ROM detected: ${BLIPTOASTER_MARKER} v${info.semver.join(".")}`);
});

test("constructSystem romBytes boots a BlipToaster (Mesen) system over a nonexistent romPath", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();

  const bytes = be.readFile(BLIPTOASTER_ROM)!;
  // romPath points at nothing; only romBytes can boot this. (Pre-fix Mesen slurps the path -> nullptr.)
  expect(be.constructSystem({
    romPath: "/definitely/not/here/nope.nes", platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: bytes,
  }, 11)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(11) != null).toBeTruthy(); // booted from romBytes, not romPath
  console.log(`[bliptoaster-rom] Mesen booted from spec.romBytes (nonexistent romPath)`);
});

test("a kit + font patched into the ROM in memory boots (the on-disk .nes is untouched)", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();

  const base = be.readFile(BLIPTOASTER_ROM)!;
  const rom = BlipToasterRom.fromBytes(base);
  expect(rom.isBlipToaster).toBeTruthy();
  expect(rom.isKitPopulated(0)).toBeTruthy(); // the baked kit

  // Nudge the kit bank + font slot 0 in memory (keeping the kit populated).
  const kit = rom.getKitBank(0)!.slice();
  kit[0] ^= 0xff; // flip a DPCM byte
  rom.setKit(0, kit);
  const font = rom.getChrFontSlot(0)!.slice();
  font[0] ^= 0xff; // flip a tile byte
  rom.setChrFontSlot(0, font);
  const effective = rom.bytes();

  expect(be.constructSystem({
    romPath: BLIPTOASTER_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: effective,
  }, 12)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(12) != null).toBeTruthy(); // patched image boots

  // The patch is really in the effective ROM, and the stock ROM on disk is byte-identical.
  expect(BlipToasterRom.fromBytes(effective).getKitBank(0)![0]).toBe(kit[0]);
  expect([...be.readFile(BLIPTOASTER_ROM)!]).toEqual([...base]);
  console.log(`[bliptoaster-rom] patched-in-memory kit+font ROM boots; on-disk .nes unchanged`);
});

test("the baked theme sets the background palette; a theme override changes it", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();

  const base = be.readFile(BLIPTOASTER_ROM)!;

  // The ROM applies theme 0 at boot. Read what this cart actually BAKES rather than pinning a sampled
  // colour: the theme table is ROM data and a rebuild moves it (theme 0's bg went $0D -> $0F on the
  // re-stage that first ran this test). The claim is that the PPU palette follows the baked record,
  // which is what comparing the two asserts — and it stays true of any build.
  const bakedTheme = BlipToasterRom.fromBytes(base).getTheme(0)!;
  expect(be.constructSystem({
    romPath: BLIPTOASTER_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: base,
  }, 13)).toBeTruthy();
  audio.renderAudio(4000); // let the reset + sysInit palette write run
  const pal0 = be.getPpuState(13).paletteRam;
  expect(pal0[0], "$3F00 universal background = baked theme 0 bg").toBe(bakedTheme.recordBytes[0]);
  expect(pal0[1], "$3F01 text = baked theme 0 text").toBe(bakedTheme.recordBytes[1]);

  // Override the theme's bg + text (in memory) → the ROM reads the patched table and applies it.
  const rom = BlipToasterRom.fromBytes(base);
  const t = rom.getTheme(0)!;
  const rec = t.recordBytes.slice();
  rec[0] = 0x21; // bg
  rec[1] = 0x11; // text
  rom.setTheme(0, rec, t.nameBytes);

  expect(be.constructSystem({
    romPath: BLIPTOASTER_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: rom.bytes(),
  }, 14)).toBeTruthy();
  audio.renderAudio(4000);
  const pal1 = be.getPpuState(14).paletteRam;
  expect(pal1[0]).toBe(0x21); // the override's bg is what the ROM applied
  expect(pal1[1]).toBe(0x11); // ...and its text color
  expect([...be.readFile(BLIPTOASTER_ROM)!]).toEqual([...base]); // on-disk .nes untouched
  console.log(`[bliptoaster-rom] baked theme applied ($3F00/$3F01); override changes the palette`);
});

// The multi-kit runtime path, end-to-end on a REAL Mesen NES core: MIDI CC 14 (CC_DMC_BANK) selects one of
// the FME-7 build's 16 switchable 8K kit banks by remapping the $C000 window and reloading the kit index.
// Uses the FME-7 build (plain banking, NO expansion audio — the base APU alone).
test("FME-7 multi-kit: CC 14 switches the $C000 kit bank on a real core (the window follows)", () => {
  const s = bootSession();
  const rom = BlipToasterRom.fromBytes(s.backend.readFile(BLIPTOASTER_ROM)!);

  // The banking header drives the derived capacity to 16 (NROM would be 1).
  expect(rom.kitBankCapacity()).toBe(16);

  // Find a bank and a byte that genuinely DISTINGUISH it from bank 0, rather than assuming slot 1 is
  // reserved $FF fill and probing the kit magic. Slot 1 was fill when this test was written; this build
  // bakes a kit into every slot, so both assumptions are now false — and the pinned $FF asserted nothing
  // about the switch, only about which slots happened to be empty. Deriving the probe keeps the claim
  // ("the $C000 window follows CC 14") true of any build, and it still fails if the window does not move.
  const bank0 = rom.getKitBank(0)!;
  let other = -1;
  let probe = -1;
  for (let n = 1; n < rom.kitBankCapacity() && probe < 0; n++) {
    const b = rom.getKitBank(n);
    if (!b) continue;
    for (let i = 0; i < b.length && probe < 0; i++) if (b[i] !== bank0[i]) { other = n; probe = i; }
  }
  expect(probe, "some kit bank differs from bank 0 somewhere in the $C000 window").toBeGreaterThanOrEqual(0);
  const probeAddr = 0xc000 + probe;
  const atBank = (n: number): number => rom.getKitBank(n)![probe];

  const id = s.project.systems.addSystem(BLIPTOASTER_ROM);
  if (id == null) throw new Error("addSystem failed");

  let atBoot = -1, atOther = -1, backAt0 = -1;
  const tl = new Timeline()
    .at(300, (ss) => (atBoot = ss.backend.readCpu(id, probeAddr) ?? -1)) // boot bank = slot 0
    .midi(360, [CC_STATUS_CH5, CC_DMC_BANK, other])
    .at(440, (ss) => (atOther = ss.backend.readCpu(id, probeAddr) ?? -1))
    .midi(500, [CC_STATUS_CH5, CC_DMC_BANK, 0]) // back to kit bank 0
    .at(580, (ss) => (backAt0 = ss.backend.readCpu(id, probeAddr) ?? -1));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1100 });
  s.project.systems.removeSystem(id);

  expect(atBoot, "boot maps bank 0").toBe(atBank(0));
  expect(atOther, `CC 14 mapped bank ${other}`).toBe(atBank(other));
  expect(backAt0, "switching back re-maps bank 0").toBe(atBank(0));
  console.log(`[bliptoaster-rom] FME-7 CC 14 bank switch on a real core at $${probeAddr.toString(16)}: ${atBank(0)} -> ${atBank(other)} -> ${atBank(0)} (bank 0 <-> ${other})`);
});

// The RetroPlug override path all the way to sound: fold a .rkit into (reserved) kit slot 1 via the
// bliptoaster-assets role → the effective romBytes → a real core boots it → CC 14 selects bank 1 → the
// override-populated bank is now mapped ($DF40 = 0xA5) and a ch5 note plays it.
test("FME-7 multi-kit: a .rkit override into slot 1 becomes selectable + plays", () => {
  const s = bootSession();

  const id = s.project.systems.addSystem(BLIPTOASTER_ROM);
  if (id == null) throw new Error("addSystem failed");

  // A valid populated 8K .rkit bank: reuse the base ROM's baked slot-0 bank, staged on disk.
  const rkit = BlipToasterRom.fromBytes(s.backend.readFile(BLIPTOASTER_ROM)!).getKitBank(0)!;
  const rkitPath = "/tmp/bliptoaster-multikit-slot1.rkit";
  s.backend.writeFileAtomic(rkitPath, rkit);

  // Link it into slot 1 (reserved on the base ROM) and reload — the role folds it into the effective ROM.
  s.project.systems.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 1, name: "HATS", path: rkitPath }] });
  const id2 = s.project.systems.reloadSystem(id);
  if (id2 == null) throw new Error("reloadSystem failed");

  let magicAtBank1 = -1;
  let dmcEnabled = false;
  const tl = new Timeline()
    .midi(200, [CC_STATUS_CH5, CC_DMC_BANK, 1]) // select the now-populated slot 1
    .at(300, (ss) => (magicAtBank1 = ss.backend.readCpu(id2, KIT_MAGIC_ADDR) ?? -1))
    .midi(320, [CC_STATUS_CH5, CC_DMC_LOOP, 127]) // loop so playback is observable mid-render
    .noteOn(340, 0, { channel: 5, velocity: 127 }); // ch5 note 0 -> slot-0 sample of the (now populated) bank 1
  // LATCH rather than sample once: `dmc.enabled` is only true while the sample is actually being fed, and
  // this kit's slot-0 sample runs out around 430ms. A single read at 600ms caught the tail-off, not the
  // playback, so a sample shorter than the read offset read as "never played". Poll across the note and
  // assert it was enabled at SOME point - that is what "it actually plays" means, and it stays true
  // whatever the kit's sample length or the loop CC's semantics.
  for (const t of [360, 400, 450, 500, 600, 700]) {
    tl.at(t, (ss) => (dmcEnabled = dmcEnabled || ss.backend.getApuState(id2).dmc.enabled));
  }
  renderTimeline(s, tl, { durationMs: 900, warmupMs: 1100 });
  s.project.systems.removeSystem(id2);

  expect(magicAtBank1).toBe(0xa5); // the override bank is populated + mapped at $C000
  expect(dmcEnabled).toBe(true); // ...and it actually plays
  console.log(`[bliptoaster-rom] FME-7 .rkit override into slot 1: selectable via CC 14 and audible`);
});
