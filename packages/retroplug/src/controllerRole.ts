// The `launchpad` DSP role: what actually RUNS a controller session, once per audio block.
//
// Everything under src/controller/ is a library; this is the only thing that calls it. It owns a
// ControllerSession in the project-scope scratch bag, feeds it the block's control-surface traffic and
// clock, and posts whatever the session produces - LED bytes back to the device, row launches onward to
// the cart.
//
// PROJECT scope, not per-system, and that is forced rather than chosen (docs/launchpad-plan.md 6.3): on
// the real-Game-Boy path there is no SystemBase to attach to at all. A project stage runs before every
// system pipeline, so a launch pushed into a system's inbox here is read by that system's own translators
// in the SAME block - which is what lets the app hand its launches to the shipped `midiMap` role instead
// of reimplementing the MI.MAP wire protocol.
//
// The tick is derived from `ppqStart` rather than counted: `Math.floor(ppqStart * 24)` is the same
// quantity walkTicks derives its own ticks from, so the session's clock and the cart's cannot drift apart,
// and a stopped transport freezes both.

import { z, clampedInt, enumField } from "./configSchema";
import type { ProjectBehavior, ProjectCtx } from "./dspKernel";
import type { MidiEvent } from "./midiRouting";
import type { RoleRegistry } from "./systemRoles";
import { ControllerRegistry, ControllerSession, lsdjMidiMapTarget, registerControllerApps } from "./controller";
// The leaf, not ./lsdj/playback: the barrel also exports the observed model and the sav-derived table
// builder, which would pull the WRAM reader and the whole sav codec into the DSP bundle.
import { PredictedLsdjModel, normaliseRowTicks } from "./lsdj/playback/predict";

/** 24 PPQN, the resolution every Arduinoboy-family sync mode counts in. */
const TICKS_PER_QUARTER = 24;

import { CONTROLLER_TARGET_VALUES, type ControllerTarget } from "./settingsEnums";

/** The app registry the role resolves against. Built once - apps are static in v1 (registered TS modules,
 *  not runtime-loaded scripts), so there is nothing to rebuild per project. */
const apps = new ControllerRegistry();
registerControllerApps(apps);

interface RoleState {
  session?: ControllerSession | null;
  /** Whether a device was attached on the previous block - the edge detector below. */
  attached?: boolean;
}

const launchpad: ProjectBehavior = (c: ProjectCtx) => {
  const st = c.state as RoleState;
  if (st.session === undefined) st.session = buildSession(c);
  const session = st.session;
  if (!session) return; // unknown app id, or no song table - nothing to run

  // TAKE THE DEVICE on the block a link first reports one, not when the session is built. The session is
  // built on the first block after a structure push, which is almost always LONG before the user plugs a
  // Launchpad in and connects it - and a device always boots into Live mode, so Programmer mode has to be
  // re-entered every time one appears. connect() also invalidates the shadow buffer, so a reconnect
  // repaints the whole surface instead of diffing against LEDs a fresh device never had.
  const attached = c.block.controllerConnected === true;
  if (attached && !st.attached) {
    const hello = session.connect();
    for (let i = 0; i < hello.length; i++) c.emitControllerOut(hello[i]);
  }
  st.attached = attached;

  // Run even with nothing attached: `update` is what drives the predictor, so the model stays correct and
  // the LEDs are right on the first block after a device appears. The messages then go nowhere, which costs
  // a disconnected host only the surface diff (empty in the steady state).
  const messages = session.update({
    input: toMessages(c.controllerIn),
    tick: Math.floor(c.block.ppqStart * TICKS_PER_QUARTER),
    transport: c.block.transport,
  });
  for (let i = 0; i < messages.length; i++) c.emitControllerOut(messages[i]);
};

/** Build the session from config, or null when the config names nothing runnable. Called once, on the
 *  first block after a structure push - the kernel keeps the scratch bag across blocks, so the session
 *  (and its shadow buffer, and the app's own state) survives. */
function buildSession(c: ProjectCtx): ControllerSession | null {
  const type = apps.get((c.config.app as string) ?? "");
  if (!type) return null;

  const model = PredictedLsdjModel.fromRowTicks(normaliseRowTicks(c.config.songRowTicks));
  const systemId = resolveSystemId(c, (c.config.systemId as number) ?? 0);
  const target = (c.config.target as ControllerTarget) ?? "system";

  // The ONE difference between driving an emulated cart and driving a real Game Boy: where `send` puts
  // the bytes. Everything upstream - the app, the quantiser, the predictor - is identical, so both paths
  // carry the same launch stream by construction (docs/launchpad-plan.md 7.3).
  //
  // A fresh event per launch, deliberately: the inbox holds the reference until the system pipeline reads
  // it, so a reused object would alias if two launches landed in one block (two pads pressed together
  // under `immediate`). Launches happen only when a player presses something, so this is rare enough that
  // reusing it would be trading a real bug for nothing.
  const send = target === "midiOut"
    ? (data: number[]) => c.emitMidiOut(systemId, 0, data)
    : (data: number[]) => c.toSystem(systemId, { frame: 0, data });

  return new ControllerSession(type.app, {
    playback: model,
    target: lsdjMidiMapTarget(send),
    config: type.schema.parse(c.config.appConfig ?? {}),
  });
}

/** The configured system, or the first one when the config says 0 - so a single-cart project needs no id.
 *  An id that names no live system is left as-is: `toSystem` ignores it, which is a silent no-op rather
 *  than launching into whichever cart happens to be first. */
function resolveSystemId(c: ProjectCtx, configured: number): number {
  if (configured > 0) return configured;
  return c.block.systems.length > 0 ? c.block.systems[0].id : -1;
}

/** The session takes raw messages; the block carries frame-tagged events. The frame is dropped because a
 *  control surface's timing is not musical - a pad press is quantised by the app, not placed by sample. */
function toMessages(events: readonly MidiEvent[]): number[][] {
  const out: number[][] = [];
  for (let i = 0; i < events.length; i++) out.push(events[i].data);
  return out;
}

// Two shapes configSchema has no builder for: an opaque nested config object, and the row-ticks table.
// Both are validated by their real consumers (the app's own schema; normaliseRowTicks), so here they only
// have to survive the parse as SOMETHING of the right kind rather than be described twice.
const stringOr = (def: string) => z.preprocess((v) => (typeof v === "string" && v ? v : def), z.string());
const objectOr = () => z.preprocess((v) => (v && typeof v === "object" && !Array.isArray(v) ? v : {}), z.unknown());
const arrayOr = () => z.preprocess((v) => (Array.isArray(v) ? v : []), z.unknown());

/** Register the controller role. Called from registerDspRoles, because this has to exist in the DSP
 *  bundle's registry (the audio thread's), not only the control plane's. */
export function registerControllerRole(registry: RoleRegistry): void {
  registry.registerRole({
    kind: "launchpad",
    category: "feature",
    scope: "project",
    // app: which controller app to run (a ControllerRegistry id). target/systemId: where launches go.
    // appConfig: the app's own config, validated by ITS schema rather than here. songRowTicks: the
    // derived per-row timing table (see kernelProjection) - the model's only input, and the reason a
    // DSP-thread role can predict a song it cannot read.
    schema: z.object({
      app: stringOr("lsdj-midimap"),
      target: enumField(CONTROLLER_TARGET_VALUES, "system"),
      systemId: clampedInt(0, 0x7fffffff, 0),
      appConfig: objectOr(),
      songRowTicks: arrayOr(),
    }),
    dsp: launchpad,
  });
}
