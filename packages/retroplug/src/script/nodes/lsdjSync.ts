// LSDj's sync, host to Game Boy (the `lsdj-sync` role's audio-thread work into the cart; dspRoles.ts registers the
// role, and buildGraph places this node for it before the systems): each block, the configured mode of LsdjSyncMode,
// the glue the role ran around the protocol's pure parts under the DSP kernel, ported line for line. Its scratch is
// the role's one scratch bag, kept on the instance, so a mode change keeps what the role kept. The Game Boy-to-host
// direction, the MI.OUT and master-sync decoders, is rp.lsdj.serial-out's (lsdjSerialOut.ts), placed for the same
// role after the systems, where it reads its Game Boy's serial out in the block the core captured it.
//
// What the modes read: the routed MIDI on `midi`, then the controller's launches on `launches`, as ONE inbox in that
// order, the order the kernel's inbox held them in (its launches appended at frame 0 after the routed events, so a
// frame merge would not be the same).
//
// What they send: serial-in bytes, a run of same-frame bytes to a message as the kernel node sent them; a button
// press as [button, down] at frame 0. The frame-ordered queue raises a byte sent behind a later one (Arduinoboy's
// START after a row byte, MI.MAP's clocks after its rows) as it raised the kernel's.
//
// Nothing is allocated in a warm block: the serial-in run and the button press are made once and reused.
import { bytes, defineNode, events, type Block, type Message } from "@retroplug/core/defineNode";
import { LsdjSyncMode } from "../../settingsEnums";
import {
  KEYBOARD_LOW_OCTAVE_MAP,
  KEYBOARD_LOW_START,
  KEYBOARD_NOTE_MAP,
  KEYBOARD_NOTE_START,
  KEYBOARD_OCT_DN,
  KEYBOARD_OCT_UP,
  isExtendedScancode,
  toGbSerialByte,
} from "../../lsdjKeyboardMap";
import {
  LSDJ_CLOCK,
  LSDJ_START,
  LSDJ_START_BUTTON,
  LSDJ_STOP,
  MIDIMAP_CLOCK,
  MIDIMAP_NOTEOFF,
  channelOf,
  isNoteOff,
  isNoteOn,
  midiMapRow,
} from "../../lsdjSyncBytes";
import { TickClock } from "@retroplug/core/clock";
import { lsdjSyncSettings, type LsdjSyncSettings } from "./lsdjSyncSettings";
import { BUTTONS, SERIAL_IN } from "@retroplug/core/ports";

export const LSDJ_SYNC_NODE_TYPE = "rp.lsdj.sync";

/** The role's scratch bag, every mode's fields in one, as the kernel kept it for the stage: a mode reads what another
 *  left (the Arduinoboy and MidiSync auto-arms share armDown) until the node goes. */
interface LsdjState {
  // MidiSyncArduinoboy
  playing?: boolean;
  divisor?: number;
  prevTransport?: boolean;
  armDown?: number;
  // MidiSync's auto-arm
  prevT?: boolean;
  // MidiMap
  lastRow?: number;
  // KeyboardMidi
  octave?: number;
}

export const lsdjSyncNode = defineNode<LsdjSyncSettings>({
  id: LSDJ_SYNC_NODE_TYPE,
  name: "LSDj Sync",
  category: "protocol",
  settings: lsdjSyncSettings,
  reshapes: true,
  ports: {
    midi: events.in(),
    launches: events.in(),
    serialIn: bytes.out(SERIAL_IN),
    buttons: events.out(BUTTONS),
  },
  create: ({ settings: initial }) => {
    let config = initial;
    const st: LsdjState = {};
    const clock = new TickClock();
    let block: Block | null = null; // the block being processed, which every send goes on
    // This block's routed MIDI and launches. The views inside are JS-owned copies the script host refills each block,
    // so holding them after process returns retains memory but never reads freed memory; only eachEvent reads them,
    // inside process, after process has set them for the block.
    let midi: readonly Message[] = [];
    let launches: readonly Message[] = [];
    const press = new Uint8Array(2);

    // The serial-in bytes not yet sent: consecutive bytes at runFrame, sent as one message when a byte at another
    // frame comes, and at the end of the block. Reused, so it allocates nothing once grown.
    const run: number[] = [];
    let runFrame = 0;
    const sendRun = () => {
      block!.send("serialIn", runFrame, run);
      run.length = 0;
    };
    const pushSerialIn = (frame: number, byte: number) => {
      if (run.length > 0 && frame !== runFrame) sendRun();
      runFrame = frame;
      run.push(byte & 0xff);
    };
    // At frame 0: the core takes a role's press with no frame.
    const pressButton = (button: number, down: boolean) => {
      press[0] = button;
      press[1] = down ? 1 : 0;
      block!.send("buttons", 0, press);
    };
    // Each inbox event, through `handle`: the routed MIDI, then the launches.
    const eachEvent = (handle: (e: Message) => void) => {
      for (let i = 0; i < midi.length; i++) handle(midi[i]);
      for (let i = 0; i < launches.length; i++) handle(launches[i]);
    };

    // MidiSync's clock, at its tick's frame.
    const clockAtTick = (_t: number, off: number) => pushSerialIn(off, LSDJ_CLOCK);
    // MidiSyncArduinoboy's clock, at frame 0, NOT at its tick's frame. LSDj in Arduinoboy-slave mode toggles its
    // serial-clock-enable (SC) between bytes, so the SameBoy offset gate, which holds a byte until its frame, misses
    // LSDj's ready window and starves the clock (LSDj goes silent). The walk still sends the tempo-correct number of
    // clocks a block; only the intra-block time is dropped.
    const clockAtZero = () => pushSerialIn(0, LSDJ_CLOCK);
    // MidiMap's tick, at frame 0 for the same reason: Arduinoboy's Mode_LSDJ_Map turns each host 0xF8 into a 0xFF,
    // and LSDj advances one phrase step per 6 of them.
    const mapClockAtZero = () => pushSerialIn(0, MIDIMAP_CLOCK);

    // MidiSyncArduinoboy: notes drive runtime state. 24/25 toggle the play flag, 26-29 set the tempo divisor, 30 and
    // up push a raw row byte (note - 30).
    const arduinoboyEvent = (e: Message) => {
      if (!isNoteOn(e.data[0])) return;
      const note = e.data[1];
      if (note === 24) st.playing = true;
      else if (note === 25) st.playing = false;
      else if (note === 26) st.divisor = 1;
      else if (note === 27) st.divisor = 2;
      else if (note === 28) st.divisor = 4;
      else if (note === 29) st.divisor = 8;
      else if (note >= 30) pushSerialIn(e.frame, note - 30);
    };
    // MidiMap: a NoteOn is a row byte LSDj reads as a song-row jump; the NoteOff of the row last sounded sends the
    // 0xFE handshake.
    const midiMapEvent = (e: Message) => {
      const status = e.data[0];
      const note = e.data.length >= 2 ? e.data[1] : 0;
      if (isNoteOn(status)) {
        const row = midiMapRow(channelOf(status), note);
        if (row < 0) return;
        pushSerialIn(e.frame, row & 0xff);
        st.lastRow = row;
      } else if (isNoteOff(status)) {
        if (midiMapRow(channelOf(status), note) === st.lastRow) {
          pushSerialIn(e.frame, MIDIMAP_NOTEOFF);
          st.lastRow = -1;
        }
      }
    };
    // Slide LSDj's keyboard octave toward `target` with OCT_UP / OCT_DN scancodes; the new octave.
    const slideKeyboardOctave = (frame: number, target: number, current: number): number => {
      if (target === current) return current;
      const code = toGbSerialByte(target > current ? KEYBOARD_OCT_UP : KEYBOARD_OCT_DN);
      let steps = Math.abs(target - current);
      while (steps-- > 0) pushSerialIn(frame, code);
      return target;
    };
    // KeyboardMidi: a NoteOn as LSDj's PS/2 scancodes, sliding the octave cursor to follow the note.
    const keyboardEvent = (e: Message) => {
      if (!isNoteOn(e.data[0])) return;
      let note = e.data[1];
      if (note >= KEYBOARD_NOTE_START) {
        note -= KEYBOARD_NOTE_START;
        st.octave = slideKeyboardOctave(e.frame, Math.floor(note / 12), st.octave!);
        const idx = note >= 0x3c ? (note % 12) + 0x0c : note % 12; // two rows of note keys
        pushSerialIn(e.frame, toGbSerialByte(KEYBOARD_NOTE_MAP[idx]));
      } else if (note >= KEYBOARD_LOW_START) {
        const command = KEYBOARD_LOW_OCTAVE_MAP[note - KEYBOARD_LOW_START];
        // Cursor keys need the extended (0xE0) prefix; every byte is mangled to LSDj's GB-serial form.
        if (isExtendedScancode(command)) pushSerialIn(e.frame, toGbSerialByte(0xe0));
        pushSerialIn(e.frame, toGbSerialByte(command));
      }
    };
    // MidiPassthrough: every byte verbatim.
    const passthroughEvent = (e: Message) => {
      const data = e.data;
      for (let j = 0; j < data.length; j++) pushSerialIn(e.frame, data[j]);
    };

    const dispatch = () => {
      const transport = clock.block.transport;
      switch (config.mode) {
        case LsdjSyncMode.MidiSync: {
          // Optional auto-arm: SYNC=MIDI LSDj follows the clock only once START has parked it in "wait for MIDI", so
          // tap START on the transport's rise (press for 2 blocks, then release).
          if (config.autoStart) {
            if (transport && !st.prevT) { pressButton(LSDJ_START_BUTTON, true); st.armDown = 2; }
            else if (st.armDown && st.armDown > 0) { if (--st.armDown === 0) pressButton(LSDJ_START_BUTTON, false); }
            st.prevT = transport;
          }
          const divisor = config.tempoDivisor || 1;
          clock.walk(24 / divisor, clockAtTick);
          break;
        }
        case LsdjSyncMode.MidiSyncArduinoboy: {
          if (st.divisor === undefined) st.divisor = config.tempoDivisor || 1;
          // autoStart: tap START on the host transport's rise to park a SYNC=Lsdj cart in "wait for sync" before the
          // note-24 play-enable and the clock arrive, so a headless render starts on the grid.
          if (config.autoStart) {
            if (transport && !(st.prevTransport ?? false)) { pressButton(LSDJ_START_BUTTON, true); st.armDown = 2; }
            else if (st.armDown && st.armDown > 0) { if (--st.armDown === 0) pressButton(LSDJ_START_BUTTON, false); }
          }
          eachEvent(arduinoboyEvent);
          if (transport !== (st.prevTransport ?? false)) {
            pushSerialIn(0, transport ? LSDJ_START : LSDJ_STOP);
            st.prevTransport = transport;
          }
          if (st.playing) clock.walk(24 / (st.divisor || 1), clockAtZero);
          break;
        }
        case LsdjSyncMode.MidiMap: {
          if (st.lastRow === undefined) st.lastRow = -1;
          eachEvent(midiMapEvent);
          // MI.MAP is a sync mode, so it clocks the cart too, transport-gated by the walk.
          const divisor = config.tempoDivisor || 1;
          clock.walk(24 / divisor, mapClockAtZero);
          break;
        }
        case LsdjSyncMode.KeyboardMidi:
          if (st.octave === undefined) st.octave = 4;
          eachEvent(keyboardEvent);
          break;
        case LsdjSyncMode.MidiPassthrough:
          eachEvent(passthroughEvent);
          break;
        case LsdjSyncMode.MidiOut:
        case LsdjSyncMode.MasterSync:
          break; // LSDj's serial out, which rp.lsdj.serial-out decodes after the Game Boy
        default:
          break; // Off / Keyboard send nothing here
      }
    };

    return {
      // A throw goes to the script host, which counts it and runs this again next block: what was sent before it
      // stands, the held run included. A call the deadline stops skips the finally, so a run it left is cleared here
      // rather than sent in a block it was not pushed in.
      process(b) {
        block = b;
        run.length = 0;
        clock.begin(b);
        midi = b.messages("midi");
        launches = b.messages("launches");
        try {
          dispatch();
        } finally {
          if (run.length > 0) sendRun();
        }
      },
      onSettings(next) {
        config = next;
      },
    };
  },
});
