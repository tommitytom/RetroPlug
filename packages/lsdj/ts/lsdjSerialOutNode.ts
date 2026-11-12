// LSDj's serial out, decoded to host MIDI: the `lsdj-sync` role's Game Boy-to-host direction (@retroplug/lsdj's app
// registers the role, and buildGraph places this node for it after the systems, as sys-<id>/lsdj-sync/out, where
// rp.lsdj.sync, before them, takes the host-to-Game Boy one). Each block it reads what its Game Boy captured on its
// link port in that block, each byte at the frame its core completed it, over a forward wire from the system's
// `serialOut`, and in the role's two MIDI-out modes decodes it (lsdjArduinoboy.ts): MI.OUT (`midiOut`,
// Arduinoboy's Mode_LSDJ_Midiout: notes, CCs, program changes and realtime) or master sync (`masterSync`, a clock a
// byte, a silence the stop). Each message goes on `midiOut` at the frame the decoder gives it, a frame of this block,
// so the host hears it where LSDj sent it.
//
// The other modes send nothing and read nothing of their input, which is empty there: the core captures for the two
// MIDI-out modes alone, and the capture follows the mode this node runs, the MI.MAP controller's forced one included
// (the shell's serialOutCaptureFor, spec/04), so a cart the controller forces (to `midiMap`, or to `off`) captures
// nothing and is as silent on host MIDI as one set to that mode. The node is placed whatever the mode, so a mode
// switch is a change of settings, as it is for rp.lsdj.sync, and never of the document. Its settings are the role's
// config through the schema rp.lsdj.sync takes too (lsdjSyncSettings.ts), the MI.MAP controller's forced mode
// included; it reads only `mode`.
//
// MI.OUT's bit accumulator and pending command are kept on the instance across mode changes; master sync's state is
// dropped whenever the mode is not master sync, so coming back opens a fresh run. Nothing is allocated in a warm
// block: the byte and frame arrays are refilled in place, and the decoders hand each message's bytes to emitMidiOut,
// which sends them from arrays made with the instance.
import { bytes, defineNode, events, type Block } from "@retroplug/core/defineNode";
import { LsdjSyncMode } from "./enums";
import {
  arduinoboyDecodeSerialOut,
  arduinoboyMasterSyncBlock,
  type ArduinoboyState,
  type MasterSyncBlock,
  type MasterSyncState,
} from "./lsdjArduinoboy";
import { lsdjSyncSettings, type LsdjSyncSettings } from "./lsdjSyncSettings";
import { MIDI_OUT } from "@retroplug/core/ports";

export const LSDJ_SERIAL_OUT_NODE_TYPE = "rp.lsdj.serial-out";

export const lsdjSerialOutNode = defineNode<LsdjSyncSettings>({
  id: LSDJ_SERIAL_OUT_NODE_TYPE,
  name: "LSDj Serial Out",
  category: "protocol",
  settings: lsdjSyncSettings,
  reshapes: true,
  ports: {
    serialOut: bytes.in(),
    midiOut: events.out(MIDI_OUT),
  },
  create: ({ settings: initial }) => {
    let mode = initial.mode;
    let block: Block | null = null; // the block being processed, which every send goes on
    const miOut: ArduinoboyState = {};
    let masterSync: MasterSyncState | undefined;
    // This block's length and rate, as master sync reads them: the rate is the block's transport's, not create's, since
    // the instance outlives a change of the host's rate (the host re-activates it), and a run carried over one is
    // counted again at the new rate.
    const span: MasterSyncBlock = { frames: 0, sampleRate: 44100 };
    // This block's serial-out bytes, and each one's frame in it, index for index, as the decoders take them. Refilled
    // each block, so they allocate nothing once grown.
    const serialOut: number[] = [];
    const serialOutFrames: number[] = [];
    // A decoded message, at the frame the decoder gives it. A message is 1 to 3 bytes and send sends all of the array
    // it is given, so each length has an array of its own, made once here; send copies a message into the port's
    // queue, so its array is free again when send returns.
    const out1 = new Uint8Array(1);
    const out2 = new Uint8Array(2);
    const out3 = new Uint8Array(3);
    const emitMidiOut = (frame: number, len: number, b0: number, b1: number, b2: number) => {
      const data = len === 1 ? out1 : len === 2 ? out2 : out3;
      data[0] = b0;
      if (len > 1) data[1] = b1;
      if (len > 2) data[2] = b2;
      block!.send("midiOut", frame, data);
    };

    return {
      process(b) {
        block = b;
        // Master sync's state would go stale while another mode runs: dropped here, coming back starts clean rather
        // than from a stale run or a stale last byte.
        if (masterSync !== undefined && mode !== LsdjSyncMode.MasterSync) masterSync = undefined;
        if (mode !== LsdjSyncMode.MidiOut && mode !== LsdjSyncMode.MasterSync) return;
        serialOut.length = 0;
        serialOutFrames.length = 0;
        const captured = b.messages("serialOut");
        for (let i = 0; i < captured.length; i++) {
          const m = captured[i];
          for (let j = 0; j < m.data.length; j++) {
            serialOut.push(m.data[j]);
            serialOutFrames.push(m.frame);
          }
        }
        if (mode === LsdjSyncMode.MidiOut) {
          if (serialOut.length > 0) arduinoboyDecodeSerialOut(serialOut, serialOutFrames, miOut, emitMidiOut);
          return;
        }
        span.frames = b.frames;
        span.sampleRate = b.transport.sampleRate;
        arduinoboyMasterSyncBlock(serialOut, serialOutFrames, span, (masterSync ??= {}), emitMidiOut);
      },
      onSettings(next) {
        mode = next.mode;
      },
    };
  },
});
