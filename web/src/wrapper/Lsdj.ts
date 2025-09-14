import type { MainModule, NativeLsdjController, Uint8Buffer } from "../native/RetroPlug";
import type { ILsdjKit } from "../types/LsdjTypes";
import { generateKey } from "../utils/LsdjUtil";
import { fromUint8Array, type SystemId } from "../utils/NativeUtil";

export class LsdjController {
	constructor(private _module: MainModule, private _nativeController: NativeLsdjController) {}

	getNextEmptyKit(system: SystemId) {
		return this._nativeController.getNextEmptyKit(system);
	}

	updateKit(system: SystemId, kitId: number, kit: ILsdjKit): void {
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
			data: undefined,
			path: kit.path ? '/mount' + kit.path : undefined,
		};

		//console.log(JSON.stringify(sanitized, null, 4));

		if (!this._nativeController.updateKit(system, kitId, JSON.stringify(sanitized))) {
			console.error("Failed to update kit:", JSON.stringify(sanitized, null, 4));
		}
	}

	getKits(system: SystemId): ILsdjKit[] {
		const kitsString = this._nativeController.getKitsString(system);
		if (!kitsString || kitsString.length === 0) {
			return [];
		}

		const kits = JSON.parse(kitsString) as ILsdjKit[];

		return kits.map((kit) => ({
			...kit,
			key: generateKey(),
			samples: kit.samples?.map((sample) => ({
				...sample,
				path: sample.path.startsWith('/mount') ? sample.path.substring(6) : sample.path,
				key: generateKey(),
				effects: sample.effects?.map((effect) => ({
					effect: { ...effect },
					key: generateKey(),
					id: effect.id,
				})),
			})),
			effects: kit.effects?.map((effect) => ({
				effect: { ...effect },
				key: generateKey(),
				id: effect.id,
			})),
			path: kit.path?.startsWith('/mount') ? kit.path.substring(6) : kit.path,
		})) as ILsdjKit[];
	}

	getKitData(systemId: SystemId, kitId: number): Uint8Buffer | null {
		return this._nativeController.getKitData(systemId, kitId);
	}

	getKitVersion(systemId: SystemId, kitId: number): number {
		return this._nativeController.getKitVersion(systemId, kitId);
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
