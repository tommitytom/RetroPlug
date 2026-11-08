// The PORTABLE CORE: everything a control surface needs that is not a host, as one entry point.
//
// This is what a host other than the desktop builds - the planned one being MicroQuickJS on a Pico 2, on top of
// pico/ - so it is held to what that engine runs, and the scenario mode of
// packages/retroplug/scripts/build-device-pack.mjs proves it: the bundle may import nothing outside the package's
// own tree and its declared dependencies (no zod, no backend, no native seam), it is lowered to ES5, and a
// scenario run under MicroQuickJS must produce byte-for-byte what the same scenario produces here.
//
// The rules that keeps to, beyond "no I/O":
//
//   - ES5 SYNTAX is the compiler's problem, not the source's: classes, arrows, let/const, destructuring and spread
//     are all lowered. Write normal TS.
//   - The LIBRARY is ours to respect. MicroQuickJS has no Map, Set or Symbol, iterates only arrays with for-of, and
//     lacks most ES2015+ methods (Object.values/entries/assign, Array find/includes/fill/from, Number.isInteger,
//     padStart...). Plain objects, arrays and loops cover every use here.
//   - Arrays may not be written past their end (no holes): build them by push, or full-length up front.
//
// What is deliberately NOT here: the app registry's config schemas (zod, which belong to the control plane), the
// script node that runs a session on the audio thread (ts/controllerSession.ts), the LSDj app and its launch target
// (they live with the tracker, in its bridge package) and every UI seam. A host wires these pieces itself - feed the
// session bytes and a 24 PPQN tick, write what it returns to the device, and hand the target a `send` that reaches
// the cart.

export { ControllerSession, isPredictive } from "./session";
export type { ControllerApp, ControllerCtx, SessionInput, SessionOptions } from "./session";
export type { PlaybackGrid, PlaybackModel, PlaybackPosition, PredictivePlaybackModel } from "@retroplug/tracker/playbackModel";
export { nullTarget, type TrackerTarget } from "./trackerTarget";
export { PortScan, IDLE_SCAN, MAX_REPLIES, GAP_MS } from "./portScan";
export type { MidiPortApi, ScanReply, ScanStatus } from "./portScan";

export {
  PRO_MK3, PROFILES, profileForFamily, profilesForFamily, familyHex, deviceName,
  deviceInquiry, parseInquiryReply, modelQuery, isModelReply, enterProgrammerMode, exitToLiveMode,
  Surface, decodeMessage, decodeMessages, Palette, palette, rgb,
} from "./index";
export type { LaunchpadProfile, Led, SurfaceEvent } from "./index";
