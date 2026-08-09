// The SameBoy display knobs (the "sameboy" role's colorCorrection / dmgPalette / lightTemperature)
// reach the LIVE core and change the pixels it renders — driven through the stores, read back over
// getFrame. The analogue of app-settings.test.ts, which proves the audio knobs the same way.
//
// It also pins the model split the MENU relies on to decide which rows to show: the core applies
// colour correction and light temperature only when GB_is_cgb (both live in GB_convert_rgb15, which
// GB_palette_changed skips for non-CGB) and the DMG palette only when it isn't. Each knob is asserted
// on the model where it bites AND on the one where it must not, so if that split ever moved, the menu
// would be hiding the wrong row and this fails rather than the UI quietly going wrong.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

// Frame pixels are XRGB8888 little-endian, i.e. bytes B,G,R,X (Engine.hpp EngineFrame).
const B = 0, G = 1, R = 2;

/** Mean of each colour channel across the frame. Aggregate rather than per-pixel, so an assertion
 *  says something about the whole picture and not one arbitrary texel. */
function channelMeans(px: Uint8Array): { r: number; g: number; b: number } {
  let r = 0, g = 0, b = 0;
  const n = px.length / 4;
  for (let i = 0; i < px.length; i += 4) {
    b += px[i + B];
    g += px[i + G];
    r += px[i + R];
  }
  return { r: r / n, g: g / n, b: b / n };
}

/** Fraction of pixels whose RGB differs between two frames. */
function differingFraction(a: Uint8Array, b: Uint8Array): number {
  let diff = 0;
  const n = a.length / 4;
  for (let i = 0; i < a.length; i += 4) {
    if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) diff++;
  }
  return n ? diff / n : 0;
}

test("sameboy display knobs change the live core's rendered pixels, on the models where they apply", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const audio = createAudioDriver();

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  // Advance far enough to boot the GB and settle mGB's screen, then re-read after every edit so the
  // core has rendered fresh vblanks with the new setting (a GB frame is ~16.7 ms).
  const settle = (ms = 200) => {
    audio.renderAudio(ms);
    const f = be.getFrame(id)!;
    expect(f.published).toBeTruthy();
    return new Uint8Array(f.pixels); // copy: getFrame hands back the published buffer
  };

  audio.renderAudio(1500);
  const base = settle();

  // mGB's screen is static once booted, so any later difference is attributable to the knob and not to
  // animation. Assert that rather than assume it — if mGB ever gains a blinking element, this fails
  // loudly here instead of making the real assertions flaky.
  expect(differingFraction(base, settle())).toBe(0);

  // --- light temperature (CGB: the default model is cgbC) --------------------------------------
  // Warm scales green and blue down and leaves red alone (temperature_tint), so the red:blue ratio
  // must rise. This is the knob that bites even on a near-monochrome screen, because it's a multiply
  // applied after colour correction rather than a palette remap.
  const cold = channelMeans(base);
  expect(project.systems.setRoleConfig(id, "sameboy", { lightTemperature: 1 })).toBeTruthy();
  const warmFrame = settle();
  const warm = channelMeans(warmFrame);
  console.log(`[sameboy-display] neutral r=${cold.r.toFixed(1)} g=${cold.g.toFixed(1)} b=${cold.b.toFixed(1)}`);
  console.log(`[sameboy-display] warm    r=${warm.r.toFixed(1)} g=${warm.g.toFixed(1)} b=${warm.b.toFixed(1)}`);
  expect(differingFraction(base, warmFrame) > 0).toBeTruthy();
  expect(warm.b < cold.b).toBeTruthy(); // blue crushed
  expect(warm.r >= cold.r).toBeTruthy(); // red untouched (light_r == 1 for temperature >= 0)

  // Cool is the mirror image: blue is left alone and red is pulled down.
  expect(project.systems.setRoleConfig(id, "sameboy", { lightTemperature: -1 })).toBeTruthy();
  const coolTint = channelMeans(settle());
  console.log(`[sameboy-display] cool    r=${coolTint.r.toFixed(1)} g=${coolTint.g.toFixed(1)} b=${coolTint.b.toFixed(1)}`);
  expect(coolTint.r < cold.r).toBeTruthy();

  // Back to neutral, and the frame must return to exactly the baseline — the knob is reversible, not a
  // one-way filter baked into the buffer.
  expect(project.systems.setRoleConfig(id, "sameboy", { lightTemperature: 0 })).toBeTruthy();
  expect(differingFraction(base, settle())).toBe(0);

  // --- colour correction (CGB) -----------------------------------------------------------------
  // Every non-disabled mode must actually redraw. Asserted per mode so a mis-mapped ordinal (the enum
  // is a straight cast to GB_color_correction_mode_t) can't hide behind a sibling that happens to work.
  for (const mode of ["correctCurves", "modernBalanced", "modernBoostContrast", "reduceContrast", "lowContrast", "modernAccurate"]) {
    expect(project.systems.setRoleConfig(id, "sameboy", { colorCorrection: mode })).toBeTruthy();
    const f = settle();
    const d = differingFraction(base, f);
    console.log(`[sameboy-display] correction=${mode} differs=${(d * 100).toFixed(1)}%`);
    expect(d > 0).toBeTruthy();
  }
  expect(project.systems.setRoleConfig(id, "sameboy", { colorCorrection: "disabled" })).toBeTruthy();
  expect(differingFraction(base, settle())).toBe(0);

  // --- the DMG palette does NOT apply on a CGB core -------------------------------------------
  // The core's own gate, and the reason the menu hides this row on a CGB model.
  expect(project.systems.setRoleConfig(id, "sameboy", { dmgPalette: "dmg" })).toBeTruthy();
  expect(differingFraction(base, settle())).toBe(0);
  expect(project.systems.setRoleConfig(id, "sameboy", { dmgPalette: "grey" })).toBeTruthy();

  // --- switch to DMG: now the palette bites and the CGB-only knobs don't -----------------------
  // Fast boot leaves mGB running (not on a stable boot screen) at capture time, and the DMG
  // palette knob is not pixel-reversible against mGB's live UI. Pin slow boot for the DMG leg so the
  // palette reversibility checks compare a stable screen (this is what DMG did before it gained a fast
  // boot ROM). fastBoot must be set BEFORE the model switch — the model restart reads the current value.
  expect(project.systems.setRoleConfig(id, "sameboy", { fastBoot: false })).toBeTruthy();
  expect(project.systems.setRoleConfig(id, "sameboy", { model: "dmgB" })).toBeTruthy();
  audio.renderAudio(1500); // the core restarted — reboot + settle
  const dmgBase = settle();

  expect(project.systems.setRoleConfig(id, "sameboy", { dmgPalette: "dmg" })).toBeTruthy();
  const green = settle();
  const greenMeans = channelMeans(green);
  console.log(`[sameboy-display] dmg palette r=${greenMeans.r.toFixed(1)} g=${greenMeans.g.toFixed(1)} b=${greenMeans.b.toFixed(1)}`);
  expect(differingFraction(dmgBase, green) > 0).toBeTruthy();
  // GB_PALETTE_DMG is green-dominant at every one of its five entries, so the mean must be too.
  expect(greenMeans.g > greenMeans.r).toBeTruthy();
  expect(greenMeans.g > greenMeans.b).toBeTruthy();

  // MGB and GBL are distinct built-ins, not aliases.
  expect(project.systems.setRoleConfig(id, "sameboy", { dmgPalette: "mgb" })).toBeTruthy();
  const mgb = settle();
  expect(differingFraction(green, mgb) > 0).toBeTruthy();
  expect(project.systems.setRoleConfig(id, "sameboy", { dmgPalette: "gbl" })).toBeTruthy();
  expect(differingFraction(mgb, settle()) > 0).toBeTruthy();

  // Back to grey == the DMG baseline.
  expect(project.systems.setRoleConfig(id, "sameboy", { dmgPalette: "grey" })).toBeTruthy();
  expect(differingFraction(dmgBase, settle())).toBe(0);

  // Light temperature and colour correction are inert here — GB_palette_changed returns early for a
  // non-CGB core, so neither reaches the frame.
  expect(project.systems.setRoleConfig(id, "sameboy", { lightTemperature: 1 })).toBeTruthy();
  expect(differingFraction(dmgBase, settle())).toBe(0);
  expect(project.systems.setRoleConfig(id, "sameboy", { colorCorrection: "modernAccurate" })).toBeTruthy();
  expect(differingFraction(dmgBase, settle())).toBe(0);
});
