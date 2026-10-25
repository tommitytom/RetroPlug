// The systems and stimuli the parity groups share: how each in-repo ROM, and the sibling-fixture risa scene,
// is brought up through the stores and made to sound. The stimulus helpers (`phrase`, `smsStart`) take the case's sample rate, and
// `smsStart` a block splitter, so the same scene can be rendered in any host block size.
import type { Rig, Segment } from "./rig";
import { buildSmsMetronomeSav, pokeMetronomeIntoWram, SMS_SYNC_IN24, SMS_SYNC_OFF, WRAM_BASE } from "../smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;
declare const __RESOURCES_DIR__: string;

export const ROMS = {
  mgb: __REPO_RESOURCES_DIR__ + "/roms/mGB.gb",
  bliptoaster: __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes",
  mmc5: __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-mmc5.nes",
  n163: __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-n163.nes",
  s5b: __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-s5b.nes",
  vrc6: __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc6.nes",
  vrc7: __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc7.nes",
  sms: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms",
  gg: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg",
  // Sibling fixtures: cases that need them skip in CI.
  lsdj: __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb",
  arduinoboy: __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb",
  nanoloop: __RESOURCES_DIR__ + "/roms/nanoloop287d.gba",
  // risa 2.3.0 carries the official host-sync receive path (the risa-sync role attaches to it), and
  // let_go.srm is a song for it.
  risa: __RESOURCES_DIR__ + "/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes",
  risaSong: __RESOURCES_DIR__ + "/roms/risa/let_go.srm",
} as const;

export type Blocks = (frames: number) => number[];

export const frames = (sampleRate: number, ms: number): number => Math.round((sampleRate * ms) / 1000);

const note = (ch: number, key: number, vel = 100) => [0x90 | ch, key, vel];
const off = (ch: number, key: number) => [0x80 | ch, key, 0];

/** A phrase of notes on `channels`, placed at irregular frames so they land inside blocks of every size,
 *  with one note-off and the next note-on sharing a frame (a tie: their list order is recorded). */
export function phrase(sampleRate: number, channels: number[], startMs = 0): { at: number; bytes: number[] }[] {
  const at = (ms: number) => frames(sampleRate, startMs + ms);
  const out: { at: number; bytes: number[] }[] = [];
  channels.forEach((ch, i) => {
    const key = 48 + ((7 * i) % 36); // 48..83, so key + 12 stays a valid MIDI key
    out.push({ at: at(3 + 37 * i), bytes: note(ch, key) });
    out.push({ at: at(211 + 37 * i), bytes: off(ch, key) });
    out.push({ at: at(211 + 37 * i), bytes: note(ch, key + 12, 90) });
    out.push({ at: at(397 + 41 * i) + 1, bytes: off(ch, key + 12) });
    out.push({ at: at(433 + 29 * i), bytes: note(ch, key + 5, 70) });
  });
  return out.sort((a, b) => a.at - b.at);
}

/** mGB (the embedded build), loaded through the stores. MGB_CHANNELS are its two pulse voices and the wave
 *  voice, the channels a case plays a phrase on. */
export function mgb(rig: Rig): number {
  const id = rig.project.systems.loadMgb();
  if (id == null) throw new Error("mGB did not build");
  return id;
}
export const MGB_CHANNELS = [0, 1, 2];

/** Another mGB, from the in-repo file: loadMgb() REPLACES the focused tile rather than adding one, so it
 *  can only ever bring up the first. */
export function mgbFile(rig: Rig): number {
  return rig.add(ROMS.mgb);
}

/** How long a system needs after power-on before MIDI reaches it: mGB's boot plus its firmware init
 *  (~1.5 s; MIDI sent sooner is dropped, and what renders is only the boot), which also covers a
 *  BlipToaster build's ~1 s init. Every MIDI-driven scene renders this first. */
export const BOOT_MS = 1500;
export const NES_CHANNELS = [0, 1, 2, 3];

export const SMS_BOOT_MS = 3000;
const PAUSE = 7;

/** smsggdj as a new system, its battery seeding the options block (the SYNC mode). The cart does not
 *  autoload a song (smsSyncSong.ts has why), so `smsStart` pokes it in. With `sync` off the song free-runs;
 *  with it on (IN24) it waits for the host transport, which sms-sync turns into its clock. */
export function smsggdj(rig: Rig, machine: "sms" | "gg", sync: boolean): number {
  const rom = rig.rom(machine === "sms" ? ROMS.sms : ROMS.gg);
  rig.writeFile(rom.replace(/\.[^./]+$/, ".sav"), buildSmsMetronomeSav(sync ? SMS_SYNC_IN24 : SMS_SYNC_OFF));
  const id = rig.project.systems.addSystem(rom);
  if (id == null) throw new Error(`smsggdj (${machine}) did not build`);
  if (sync) rig.project.systems.setRoleConfig(id, "sms-sync", { machine });
  return id;
}

/** The segments that take a just-added smsggdj from power-on to playing: boot (3 s), poke the metronome
 *  song into work RAM, PAUSE. They are the boot segments; the case renders its own after them. */
export function smsStart(sampleRate: number, blocks: Blocks, slot = 0): Segment[] {
  return [
    {
      blocks: blocks(frames(sampleRate, SMS_BOOT_MS)),
      after: (rig) => {
        const id = rig.id(slot);
        const caps = { writeCpu: (_: number, addr: number, v: number) => rig.project.systems.writeRam(id, addr - WRAM_BASE, Uint8Array.of(v)) };
        if (pokeMetronomeIntoWram(caps, id) === 0) throw new Error("the metronome song did not differ from a blank one");
        rig.press(slot, PAUSE, true);
      },
    },
    { blocks: blocks(frames(sampleRate, 100)), after: (rig) => rig.press(slot, PAUSE, false) },
    { blocks: blocks(frames(sampleRate, 100)) },
  ];
}

/** risa materializes the song into working RAM while it boots, transport off. */
export const RISA_BOOT_MS = 2500;

/** risa 2.3.0 with let_go as its battery (written beside the copy as .srm), as a new system. The risa-sync
 *  role attaches to it, so the host transport drives it. */
export function risa(rig: Rig): number {
  const copy = rig.rom(ROMS.risa);
  const song = rig.session.backend.readFile(ROMS.risaSong);
  if (!song) throw new Error(`cannot read ${ROMS.risaSong}`);
  rig.writeFile(copy.replace(/\.[^./]+$/, ".srm"), song);
  const id = rig.project.systems.addSystem(copy);
  if (id == null) throw new Error("risa did not build");
  return id;
}
