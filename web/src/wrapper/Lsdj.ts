import type { MainModule, NativeLsdjController, Uint8Buffer } from "../native/RetroPlug";
import type { ILsdjKit } from "../types/LsdjTypes";
import { fromUint8Array, type SystemId } from "../utils/NativeUtil";

export class LsdjController {
	constructor(private _module: MainModule, private _nativeController: NativeLsdjController) {}

	getNextEmptyKit(system: SystemId) {
		return this._nativeController.getNextEmptyKit(system);
	}

	updateKit(system: SystemId, kitId: number, kit: ILsdjKit): void {
		if (kit.samples) {
			const samples = new this._module.NativeUint8BufferVector();
			for (const sample of kit.samples) {
				console.assert(!!sample);
				if (sample.data) {
					const sampleData = fromUint8Array(this._module, sample.data);
					samples.push_back(sampleData);
				}
			}

			const sanitized = {
				...kit,
				key: undefined,
				samples: kit.samples?.map(sample => ({
					...sample,
					path: '/mount' + sample.path,
					data: undefined,
					key: undefined,
					effects: sample.effects?.map(effect => ({
						...effect.effect,
					})),
				})),
				effects: kit.effects?.map(effect => ({
					...effect.effect,
				})),
				data: undefined
			};

			console.log(JSON.stringify(sanitized, null, 4));

			if (!this._nativeController.setKit(system, kitId, JSON.stringify(sanitized), samples)) {
				console.error("Failed to set kit:", JSON.stringify(sanitized, null, 4));
			}

			for (let i = 0; i < samples.size(); i++) samples.get(i)?.delete();
			samples.delete();
		} else if (kit.data) {
			const data = fromUint8Array(this._module, kit.data);
			//this._nativeController.setKit(system, kitId, JSON.stringify(kit), data);
			data.delete();
		}
	}

	getKits(system: SystemId): ILsdjKit[] {
		const kitsString = this._nativeController.getKitsString(system);
		return JSON.parse(kitsString) as ILsdjKit[];
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
