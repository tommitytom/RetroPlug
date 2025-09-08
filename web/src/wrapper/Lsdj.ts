import type { MainModule, NativeLsdjController, Uint8Buffer } from "../native/RetroPlug";
import type { ILsdjKit } from "../types/LsdjTypes";
import { fromUint8Array, type SystemId } from "../utils/NativeUtil";

export class LsdjController {
	constructor(private _module: MainModule, private _nativeController: NativeLsdjController) {}

	getNextEmptyKit(system: SystemId) {
		return this._nativeController.getNextEmptyKit(system);
	}

	setKit(system: SystemId, kitId: number, kit: ILsdjKit) {
		if (kit.samples) {
			const samples = new this._module.NativeUint8BufferVector();
			for (const sample of kit.samples) {
				if (sample.data) {
					samples.push_back(fromUint8Array(this._module, sample.data));
					delete sample.data;
				}
			}

			this._nativeController.setKit(system, kitId, JSON.stringify(kit), samples);

			for (let i = 0; i < samples.size(); i++) samples.get(i)?.delete();
			samples.delete();
		} else if (kit.data) {
			const data = fromUint8Array(this._module, kit.data);
			//this._nativeController.setKit(system, kitId, JSON.stringify(kit), data);
			data.delete();
		}
	}

	getKits(system: SystemId): Record<number, ILsdjKit> {
		const kitsString = this._nativeController.getKitsString(system);
		return JSON.parse(kitsString) as Record<number, ILsdjKit>;
	}

	getKitData(systemId: SystemId, kitId: number): Uint8Buffer | null {
		return this._nativeController.getKitData(systemId, kitId);
	}
}

// Utility function to play an audio sample using Web Audio API
export function playSample(audioContext: AudioContext, sampleData: Float32Array, volume: number, sampleRate: number) {
	if (!audioContext || !sampleData || sampleData.length === 0) return;

	// Create an audio buffer
	const buffer = audioContext.createBuffer(1, sampleData.length, sampleRate);
	const channelData = buffer.getChannelData(0);

	// Copy the sample data to the buffer
	for (let i = 0; i < sampleData.length; i++) {
		channelData[i] = sampleData[i] * volume;
	}

	// Create and configure buffer source
	const source = audioContext.createBufferSource();
	source.buffer = buffer;
	source.connect(audioContext.destination);

	// Play the sample
	source.start();
}
