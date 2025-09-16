import type { MainModule, NativeLsdjController, Uint8Buffer } from "../native/RetroPlug";
import { LSDJ_KIT_COUNT, type ILsdjEditableKit, type ILsdjKit, type ILsdjKitBase, type ILsdjPatchedKit } from "../types/LsdjTypes";
import { generateKey } from "../utils/LsdjUtil";
import { type SystemId } from "../utils/NativeUtil";

export interface ILsdjApiKit<T extends ILsdjKitBase = ILsdjKitBase> {
	id: number;
	kit: T;
}

export class LsdjController {
	constructor(private _module: MainModule, private _nativeController: NativeLsdjController) {}

	getNextEmptyKit(system: SystemId) {
		return this._nativeController.getNextEmptyKit(system);
	}

	removeKit(system: SystemId, kitId: number): boolean {
		return this._nativeController.removeKit(system, kitId);
	}

	updateKit(system: SystemId, kitContainer: Readonly<ILsdjKit>): void {
		const kitData = JSON.parse(JSON.stringify(kitContainer)) as ILsdjKit;

		kitData.key = undefined;
		if (kitData.kit.type === "patched") {
			const kit = kitData.kit as ILsdjPatchedKit;
			kit.path = '/mount' + kit.path;
		} else if (kitData.kit.type === "editable") {
			const kit = kitData.kit as ILsdjEditableKit;
			kit.effects.forEach((effect) => { effect.key = undefined; });
			kit.samples.forEach((sample) => {
				sample.path = '/mount' + sample.path;
				sample.key = undefined;
				sample.effects.forEach((effect) => { effect.key = undefined; });
			});
		}

		console.log(JSON.stringify(kitData, null, 4));

		if (!this._nativeController.updateKit(system, kitData.id, JSON.stringify(kitData))) {
			console.error("Failed to update kit:", JSON.stringify(kitData, null, 4));
		}
	}

	getKits(system: SystemId): ILsdjKit[] {
		const kitsString = this._nativeController.getKitsString(system);
		if (!kitsString || kitsString.length === 0) {
			return [];
		}

		const kits = JSON.parse(kitsString) as ILsdjKit[];

		// Add mount to paths and generate keys
		kits.forEach((kitContainer) => {
			kitContainer.key = generateKey();
			if (kitContainer.kit.type === "patched") {
				const kit = kitContainer.kit as ILsdjPatchedKit;
				kit.path = kit.path.startsWith('/mount') ? kit.path.substring(6) : kit.path;
			} else if (kitContainer.kit.type === "editable") {
				const kit = kitContainer.kit as ILsdjEditableKit;
				kit.effects.forEach((effect) => { effect.key = generateKey(); });
				kit.samples.forEach((sample) => {
					sample.path = sample.path.startsWith('/mount') ? sample.path.substring(6) : sample.path;
					sample.key = generateKey();
					sample.effects.forEach((effect) => { effect.key = generateKey(); });
				});
			}
		});

		for (let i = 0; i < LSDJ_KIT_COUNT; i++) {
			if (!kits.find((k) => k.id === i)) {
				kits.push({
					id: i,
					kit: { type: "empty" },
					key: generateKey(),
				});
			}
		}

		console.log(JSON.stringify(kits, null, 4));

		return kits;
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
