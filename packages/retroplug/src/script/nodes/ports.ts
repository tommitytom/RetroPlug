// What the protocol script nodes' outputs hold in one block, at most, and the bytes-message flag one of them sends
// (docs/modular-graph-phase-b3b-design.md section 5). The capacities are the DSP kernel node's, port for port
// (script/kernelNode.ts takes its own from here until it goes), since each protocol node's output carries what that
// kernel output carried for its system; a queue drops what does not fit, and the drop is counted per port.
import type { QueueCapacity } from "../../graph/descriptor";

/** RP_MESSAGE_FLUSH (graph/rp_node.h): a bytes message the device takes only after dropping what it still holds. */
export const FLUSH = 1;

// The two that pass host MIDI on, serial in (mGB, lsdj-sync's passthrough) and core MIDI (nes-n8-midi), each hold at
// least what rp.host.midi-in's output holds (kHostMidiInMessages and kHostMidiInBytes, host/graph/HostIoNodes.hpp), so
// the one place host MIDI is dropped is that edge. Core MIDI takes a message for each forwarded one; serial in at most
// one for each forwarded message, plus the sync roles' own bytes. Both have room for 4096 messages and 72 KiB: twice
// rp.host.midi-in's messages, and its bytes with 8 KiB more for a role's own (a sync role's clock and row bytes, in a
// block whose forwarded MIDI fills rp.host.midi-in), so a SysEx the host stages whole (a sample upload to a NES build)
// reaches the system whole. (The most the suites sent through them in one block, measured over test:native with the
// fixtures, parity included, 2.2 million kernel blocks up to 8192 frames: per system, 18 serial-in bytes and 8
// core-MIDI messages holding 4096 bytes, the largest 512.)
//
// The rest are sized from the same measurement: per system, 11 core-byte messages holding 11 bytes, 1 button press;
// 14 host MIDI-out messages holding 70 bytes with the kernel's 4-byte system prefixes; 5 control-surface messages
// holding 206 bytes (in one). dsp-bench's densest profile, skipped there, sends 13 messages of 39 bytes a block. Each
// of these has room for 16 times the messages or more, and the bytes as well, but for the MIDI out's (a decoded MI.OUT
// stream at its densest, a captured byte every 8 frames: 1024 in 8192). A queue costs 16 bytes a message plus its
// bytes, per plan.
export const SERIAL_IN: QueueCapacity = { messages: 4096, bytes: 73728 };
export const CORE_MIDI: QueueCapacity = { messages: 4096, bytes: 73728 };
export const CORE_BYTES: QueueCapacity = { messages: 256, bytes: 1024 };
export const BUTTONS: QueueCapacity = { messages: 64, bytes: 128 };
export const MIDI_OUT: QueueCapacity = { messages: 1024, bytes: 8192 };
export const CONTROLLER_OUT: QueueCapacity = { messages: 256, bytes: 4096 };

/** A routing output: under send-to-all it carries everything rp.host.midi-in's output passed, so it holds as much
 *  (kHostMidiInMessages and kHostMidiInBytes). */
export const ROUTED_MIDI: QueueCapacity = { messages: 2048, bytes: 65536 };

/** The controller's launches into its target's lsdj-sync node: a 3-byte row launch or release for each pad a player
 *  pressed this block, which is a handful at most. */
export const LAUNCHES: QueueCapacity = { messages: 64, bytes: 256 };
