// A fake MIDI stream for the menu and UI tests: the two globals (and the transport trio) the SDL host binds, over port
// lists the test owns. Nothing opens a port - the real stream is proved by retroplug-midi-test over the fake backend -
// so a test machine's MIDI gear is never listed or touched (the Zoom L6 rule and the port filter's).
import type { MidiDescription, MidiSelection, TransportState } from "@retroplug/core/host";

interface MidiHooks {
  __rp_midiDescribe?: () => MidiDescription | null;
  __rp_midiApply?: (selection: MidiSelection, mode: "diff" | "reopen") => void;
  __rp_getTransport?: () => Partial<TransportState>;
  __rp_setTransport?: (playing: boolean) => void;
  __rp_setClockBpm?: (bpm: number) => void;
}

const globals = globalThis as unknown as MidiHooks;

export interface FakeMidiDeviceOptions {
  inputs?: string[];
  outputs?: string[];
  supportsVirtual?: boolean;
  reserved?: { input: string; output: string };
  /** What the stream "opened", which is what the "(missing)" rows read. Defaults to every listed port. */
  openedInputs?: string[];
  openedOutputs?: string[];
  transport?: Partial<TransportState>;
}

export class FakeMidiDevice {
  description: MidiDescription;
  transportState: TransportState;
  /** Every selection applied, in order, with its mode. */
  readonly applied: { selection: MidiSelection; mode: string }[] = [];
  readonly tempos: number[] = [];
  readonly transports: boolean[] = [];
  private open: boolean;

  constructor(options: FakeMidiDeviceOptions = {}) {
    const inputs = options.inputs ?? ["Launchpad Pro MK3", "Arturia KeyStep 32"];
    const outputs = options.outputs ?? ["Deluge"];
    this.description = {
      inputs,
      outputs,
      openedInputs: options.openedInputs ?? inputs,
      openedOutputs: options.openedOutputs ?? outputs,
      supportsVirtual: options.supportsVirtual ?? true,
      reserved: options.reserved ?? { input: "", output: "" },
    };
    const t = options.transport ?? {};
    this.transportState = {
      playing: t.playing ?? false,
      external: t.external ?? false,
      bpm: t.bpm ?? 120,
      localBpm: t.localBpm ?? 120,
    };
    this.open = true;
  }

  install(): this {
    globals.__rp_midiDescribe = () => (this.open ? this.description : null);
    globals.__rp_midiApply = (selection, mode) => {
      this.applied.push({
        selection: { ...selection, inputs: [...selection.inputs], outputs: [...selection.outputs] },
        mode,
      });
      // What the stream does: what it resolved and the reservation let through is what opened.
      this.description = {
        ...this.description,
        openedInputs: selection.allInputs
          ? this.description.inputs.filter((n) => n !== this.description.reserved.input)
          : selection.inputs.filter(
            (n) => this.description.inputs.includes(n) && n !== this.description.reserved.input,
          ),
        openedOutputs: selection.outputs.filter(
          (n) => this.description.outputs.includes(n) && n !== this.description.reserved.output,
        ),
      };
    };
    globals.__rp_getTransport = () => ({ ...this.transportState });
    globals.__rp_setTransport = (playing) => {
      this.transports.push(playing);
      this.transportState = { ...this.transportState, playing };
    };
    globals.__rp_setClockBpm = (bpm) => {
      this.tempos.push(bpm);
      this.transportState = { ...this.transportState, localBpm: bpm, ...(this.transportState.external ? {} : { bpm }) };
    };
    return this;
  }

  uninstall(): void {
    delete globals.__rp_midiDescribe;
    delete globals.__rp_midiApply;
    delete globals.__rp_getTransport;
    delete globals.__rp_setTransport;
    delete globals.__rp_setClockBpm;
  }

  /** An external clock master starts, stops or retempos: the rows follow it without anyone touching the menu. */
  setExternal(external: boolean, bpm = this.transportState.bpm): void {
    this.transportState = { ...this.transportState, external, bpm, ...(external ? { playing: true } : {}) };
  }
}
