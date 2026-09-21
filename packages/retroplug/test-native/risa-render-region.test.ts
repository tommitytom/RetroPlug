// The instance's configuration reaching the render. A render boots a FRESH core from the ROM on disk, so
// whatever the live instance is set to has to be carried into the job (RenderOpts.roles / gainDb, filled by
// the UI's System > Render) or the offline core comes up at the role schema's DEFAULTS — rendering a PAL
// project at NTSC, a cart's expansion chip at unity whatever its Expansion Volume says.
//
// Region is what proves it, and it has to be: this cart boots with RANDOMIZED NES RAM, so two renders of the
// same song differ by ~3 dB and NO level comparison here means anything. The console clock is structural
// instead — PAL's 50 Hz against NTSC's 60 makes the same song play ~16% faster, which the HFF end-detection
// reads off the cart's own sequencer rather than off the audio.
//
// Only the NTSC leg is rendered here. The PAL leg is ECOLI_PAL_MS, measured and asserted by the sibling
// risa-render-hff test — rendering both regions in one process meant two ~35 s renders back to back, which
// made this the native suite's longest file by a factor of three. Split, the two run concurrently, and the
// check is no weaker: a carried role that never reached the core leaves NTSC at the PAL length, i.e. a ratio
// of exactly 1.000, which is nowhere near the tolerance below.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { runRenderJob } from "../src/render";
import { RISA_ROM, ECOLI_SRM, ECOLI_PAL_MS, newCtx, baseOpts } from "./risa-render-lib";

test("a carried role config configures the render's core (region moves the detected song length)", () => {
  const be = createRealBackend();
  if (!be.fileExists(RISA_ROM) || !be.fileExists(ECOLI_SRM)) skip(`risa-render-region: missing ${RISA_ROM} or ${ECOLI_SRM}`);
  const res = runRenderJob(newCtx(be), baseOpts({
    sav: ECOLI_SRM, maxDurationMs: 80000, out: "/tmp/rp-risa-region-ntsc.wav",
    roles: [{ kind: "mesen", config: { region: "ntsc" } }],
  }));
  expect(res.hff, "ntsc: the song's HFF end was detected").toBe(true);

  const ntsc = res.lengthMs ?? 0;
  const ratio = ntsc / ECOLI_PAL_MS;
  console.log(`[risa-render-region] carried region: pal ${ECOLI_PAL_MS} ms (ref) -> ntsc ${ntsc} ms (ratio ${ratio.toFixed(3)})`);

  // 50/60 = 0.833 at the frame rate the cart sequences on; the detection granularity is a render chunk, so
  // allow a little either side. A role that never reached the core would put this at exactly 1.000.
  expect(ratio, "NTSC runs the same song ~16% faster").toBeCloseTo(0.84, 0.03);
});
