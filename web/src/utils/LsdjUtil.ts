import type { MainModule, Uint8Buffer } from '../native/RetroPlug';
import type { ILsdjKit, ILsdjKitData, ILsdjKitDataSample } from '../types/LsdjTypes';
import { convertFloat32Buffer } from './NativeUtil';

export function kitIsEditable(kit: ILsdjKit): boolean {
	return kit.kit.type === 'editable';
}

export function sanitizeKitName(input: string): string {
	// Convert to uppercase, allow only alphanumeric and dashes, limit to 6 characters
	return input
		.toUpperCase()
		.replace(/[^A-Z0-9-]/g, '')
		.slice(0, 6);
}

export function sanitizeSampleName(input: string): string {
	// Convert to uppercase, allow only alphanumeric, limit to 3 characters
	return input
		.toUpperCase()
		.replace(/[^A-Z0-9]/g, '')
		.slice(0, 3);
}

export type SortBy = 'index' | 'editable' | 'mostUsed';

export function sortKits(kits: ILsdjKit[], sortMethod: SortBy) {
	let kitsCopy = [...kits];

	switch (sortMethod) {
		case 'index':
			// Fill in gaps
			return kitsCopy.sort((a, b) => a.id - b.id);
		case 'editable':
			return kitsCopy.sort((a, b) => {
				// Define priority order: editable, patched, rom, empty
				const typePriority = { 'editable': 0, 'patched': 1, 'rom': 2, 'empty': 3 };

				const aPriority = typePriority[a.kit.type];
				const bPriority = typePriority[b.kit.type];

				// If types are different, sort by priority
				if (aPriority !== bPriority) {
					return aPriority - bPriority;
				}

				// If same type, sort by index
				return a.id - b.id;
			});
		default:
			return kitsCopy;
	}
}

export function getLastEmptyKitIdx(kits: ILsdjKit[]): number {
	for (let i = kits.length - 1; i >= 0; i--) {
		if (kits[i].kit.type !== 'empty') {
			return i;
		}
	}

	return -1;
}

export const generateKey = (): string => {
	return `${Date.now()}-${Math.random()
		.toString(36)
		.substring(2, 2 + 9)}`;
};

export function convertSampleData(module: MainModule, data: Uint8Buffer): Float32Array {
	const nativeSampleBuffer = new module.Float32Buffer(data.size() * 2);
	module.convertNibblesToF32(data, nativeSampleBuffer);
	const sampleBuffer = convertFloat32Buffer(nativeSampleBuffer);
	nativeSampleBuffer.delete();
	return sampleBuffer;
}

export function extractKitSampleData(module: MainModule, kitData: Uint8Buffer): ILsdjKitData {
	const kit = new module.NativeLsdjKit(kitData, 0);

	const samples: ILsdjKitDataSample[] = [];
	const sampleCount = kit.getSampleCount();

	for (let i = 0; i < sampleCount; ++i) {
		const sampleName = kit.getSampleName(i);
		if (sampleName && sampleName !== 'N/A') {
			samples.push({
				name: sampleName,
				offset: kit.getSampleOffset(i) * 2,
				length: kit.getSampleDataLength(i) * 2,
			});
		}

		// Delete sampleName?
	}

	const kitName = kit.getName();

	const sampleData = kit.getSampleData();
	const sampleBuffer = convertSampleData(module, sampleData);
	sampleData.delete();
	kit.delete();

	return { name: kitName, samples, sampleBuffer };
}
