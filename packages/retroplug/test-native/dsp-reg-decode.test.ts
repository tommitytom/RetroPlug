// F6 (cli/reg-decode.ts): decodeExpansionWrites reconstructs the ROM's programmed expansion-audio
// registers from the drainEvents write log, and its freqReg agrees with F1's live decoded state
// (getExpansionAudioState().period) - the white-box write-path <-> live-state cross-check.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { decodeExpansionWrites } from "../cli/reg-decode";
import { type DebugEvent, type ExpansionAudioState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const VRC6 = __REPO_RESOURCES_DIR__ + "/roms/n8-midi-vrc6.nes";

test("decodeExpansionWrites(vrc6) reconstructs the note-on registers; freqReg matches the live period", () => {
  const s = bootSession();
  if (!s.backend.fileExists(VRC6)) { console.log("# SKIP: no VRC6 rom"); return; }
  const id = s.project.systems.addSystem(VRC6);
  if (id == null) throw new Error("addSystem failed");

  // Warm the debugger early (drainEvents inits it lazily + only logs while live), fire a note, poll densely
  // around the note-on to catch the write frame, then snapshot the live state mid-note.
  const acc: DebugEvent[] = [];
  const snap: { v: ExpansionAudioState | null } = { v: null };
  const tl = new Timeline().at(10, (sess) => sess.backend.drainEvents(id));
  tl.note(200, 69, { channel: 6, velocity: 127, durationMs: 400 });
  for (let t = 150; t <= 340; t += 4) tl.at(t, (sess) => { const e = sess.backend.drainEvents(id); if (e.length) acc.push(...e); });
  tl.at(450, (sess) => { snap.v = sess.backend.getExpansionAudioState(id); });
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1100 });
  s.project.systems.removeSystem(id);

  const dec = decodeExpansionWrites(acc, "vrc6");
  expect(dec.vrc6 != null && dec.vrc6.length === 3).toBeTruthy();
  const p1 = dec.vrc6![0];
  console.log(`# decoded pulse1: freqReg=${p1.freqReg} enabled=${p1.enabled} duty=${p1.duty} vol=${p1.volume} shift=${p1.freqShift} halt=${p1.haltAudio}`);

  // The note-on programmed a real timer + keyed the channel on.
  expect(p1.kind === "pulse" && p1.enabled && p1.freqReg > 0 && p1.volume > 0).toBeTruthy();
  // Cross-check F6 (write log) against F1 (live state): same frequency register.
  const live = snap.v!;
  expect(live != null && live.chip === "vrc6").toBeTruthy();
  expect(p1.freqReg === live.channels[0].period).toBeTruthy();
  // Voice 2 is the saw.
  expect(dec.vrc6![2].kind === "saw").toBeTruthy();
});

// Synthetic register writes: deterministic unit coverage for the VRC7 / S5B / N163 decoders (which have no
// dedicated ROM on the RetroPlug side) and the VRC6 masked-address routing, straight from known bytes.
let seq = 0;
function mkWrites(pairs: [number, number][]): DebugEvent[] {
  return pairs.map(([address, value]) => ({ type: 0, operationType: 1, address, value, programCounter: 0x8000 + seq, scanline: 0, cycle: seq++ }));
}

test("decodeExpansionWrites(vrc6) honors the masked $9003 mirror ($9007 -> shift/halt)", () => {
  // $9007 masks to $9003 in Mesen (addr & 0xF003); an exact-address decode would miss the frequency shift.
  const d = decodeExpansionWrites(mkWrites([[0x9000, 0x3f], [0x9001, 0xfd], [0x9002, 0x80], [0x9007, 0x02]]), "vrc6").vrc6!;
  expect(d[0].freqReg === 253 && d[0].enabled && d[0].duty === 3 && d[0].volume === 15).toBeTruthy();
  expect(d[0].freqShift === 4).toBeTruthy(); // $9007 (=$9003 mirror) bit1 -> shift 4
});

test("decodeExpansionWrites(vrc7) decodes fnum/block/key and honors the $E000 mute", () => {
  // reg 0x10=fnum lo, reg 0x20=(fnum hi | block | key). 0x22 | (0x11&1)<<8 = 0x122 = 290; block 0; key on.
  const d = decodeExpansionWrites(mkWrites([[0x9010, 0x10], [0x9030, 0x22], [0x9010, 0x20], [0x9030, 0x11]]), "vrc7").vrc7!;
  expect(d[0].fnum === 0x122 && d[0].block === 0 && d[0].key).toBeTruthy();
  // While muted ($E000 bit6) the OPLL register writes are disregarded, so nothing lands.
  const m = decodeExpansionWrites(mkWrites([[0xe000, 0x40], [0x9010, 0x10], [0x9030, 0x22]]), "vrc7").vrc7!;
  expect(m[0].fnum === 0 && !m[0].key).toBeTruthy();
});

test("decodeExpansionWrites(s5b) reconstructs a 12-bit period + active-low tone enable", () => {
  // reg0/1 = period lo/hi, reg7 = mixer (tone enable active-low). period ch0 = 0x7f | (0x01<<8) = 0x17f.
  const d = decodeExpansionWrites(mkWrites([[0xc000, 0x00], [0xe000, 0x7f], [0xc000, 0x01], [0xe000, 0x01], [0xc000, 0x07], [0xe000, 0x3e]]), "s5b").s5b!;
  expect(d[0].period === 0x17f && d[0].toneEnabled).toBeTruthy(); // reg7 bit0 = 0 -> enabled
});

test("decodeExpansionWrites(n163) replays the RAM pointer to the channel-7 sound registers", () => {
  // Program channel 7 (base 0x78): freq 0x3E7, wave length 16, volume 0x0F, numChannels 1 (reg 0x7F).
  const d = decodeExpansionWrites(mkWrites([
    [0xf800, 0x78], [0x4800, 0xe7],  // ram[0x78] = freq lo
    [0xf800, 0x7a], [0x4800, 0x03],  // ram[0x7a] = freq mid
    [0xf800, 0x7c], [0x4800, 0xf0],  // ram[0x7c] = freq hi(low2) + wave length (256 - 0xF0 = 16)
    [0xf800, 0x7f], [0x4800, 0x0f],  // ram[0x7f] = numChannels(hi nibble 0 -> 1) + vol ch7 0x0F
  ]), "n163").n163!;
  expect(d[7].freqReg === 0x3e7 && d[7].waveLen === 16 && d[7].numChannels === 1 && d[7].enabled).toBeTruthy();
});
