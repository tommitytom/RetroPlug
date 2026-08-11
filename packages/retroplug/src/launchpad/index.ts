// Novation Launchpad support: a pure protocol layer with no I/O and no RetroPlug dependencies.
//
// Encode LEDs, decode presses, and diff a surface so only what changed goes on the wire. Knows nothing
// about who is driving it - LSDj, risa, anything else - which is what keeps it testable with no device
// and no native build. Protocol reference: the Launchpad Pro [MK3] Programmer's Reference Manual.
export {
  PRO_MK3, PROFILES, GRID_SIZE, PRO_MK3_PORT_HINT, NOVATION_ID, PRO_MK3_FAMILY,
  isPad, padIndex, padAt, buttonName, controlKind,
  type LaunchpadProfile, type Pad, type ControlKind,
} from "./profile";

export {
  Palette, LED_OFF, palette, rgb, needsSysex, isRenderable,
  lightMessage, colourSpec, ledLightingSysex,
  enterProgrammerMode, exitToLiveMode, selectLayout, setStandaloneMode,
  deviceInquiry, parseInquiryReply,
  type Colour, type Led,
} from "./protocol";

export { decodeMessage, decodeMessages, type SurfaceEvent, type SurfaceEventKind } from "./decode";
export { Surface, BULK_THRESHOLD, type FlushResult } from "./surface";
