// What a Game Boy captures of its serial out for the mode its cart's sync node runs
// (docs/modular-graph-phase-c2-design.md section 5), which is the shell's rather than a package's: it reads
// PROTOCOL_NODES, the generated table every product carries, so naming a cart's capture table here names no package.
// The controller's placement needs it the same way any
// other override does (docs/modular-graph-phase-f5a-design.md section 6.3), and the placement itself is a package's.
import { PROTOCOL_NODES, type ProtocolNodeSpec, type ProtocolRole } from "./protocolNodes";

/** What a Game Boy captures of its serial out for LSDj's decoder (its node's `serialOutCapture` setting, as the C++
 *  node spells it), which the node gives the core when it takes it, before the core first runs, and at the start of
 *  the first block of a plan that changes it. `listen`: capture the bytes it clocks out itself (LSDj as master sync's
 *  master). `drive`: that, and clock a transfer waiting on an external clock, capturing its bytes too (the
 *  Arduinoboy's part in LSDj MI.OUT). Only MI.OUT may drive: a stopped LSDj master, clocked, takes the idle bytes for
 *  another master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md). `off`: no capture. */
export type SerialOutCaptureMode = "off" | "listen" | "drive";

/** The serial-out capture of a Game Boy whose protocol nodes are `pipeline` (protocolPipeline's), for the mode its
 *  lsdj-sync node runs: `forced`, the controller's override of this system's mode (controllerSyncOverride), when it
 *  has one, else the role's own. Both LSDj-to-host MIDI modes need one: MI.OUT (midiOut) drives, playing the
 *  Arduinoboy that clocks LSDj as the serial slave; master sync (masterSync, SYNC=LSDJ) listens to the clock LSDj
 *  sends as the master. Every other mode is `off`, the forced ones among them (the controller's midiMap and off): the
 *  capture follows the node that decodes it, so a cart the controller drives in MI.MAP is never clocked under the
 *  bytes it is sent. And so is a pipeline that places no lsdj-sync, whatever a role left out of it says: what a
 *  capture delivers is decoded by that role's rp.lsdj.serial-out node, which is not placed, and a driving core would
 *  clock the serial port under the bytes the placed role writes. */
export function serialOutCaptureFor(
  pipeline: readonly ProtocolRole[],
  forced?: string | null,
): SerialOutCaptureMode {
  // The node that declares a capture table is the one whose decoder the pipeline places (LSDj's lsdj-sync), and the
  // table is its package's data: the mapping of a mode to what the core does is the cart's, while this runs in the
  // planner, which may carry no cart at all. A pipeline with no such node captures nothing, whatever a role left out of
  // it says - what a capture delivers is decoded by that role's node, which is not placed.
  const role = pipeline.find((r) => (PROTOCOL_NODES[r.node] as ProtocolNodeSpec).capture !== undefined);
  if (!role) return "off";
  const capture = (PROTOCOL_NODES[role.node] as ProtocolNodeSpec).capture!;
  const mode: unknown = forced ?? (role.config as { mode?: unknown }).mode;
  // An OWN key, not a bare index: the record's prototype would answer "constructor" or "toString" with a function,
  // and a mode is only ever one of the cart's own strings. (hasOwnProperty.call, because the shell's lib target is
  // below Object.hasOwn.)
  return typeof mode === "string" && Object.prototype.hasOwnProperty.call(capture, mode) ? capture[mode]! : "off";
}
