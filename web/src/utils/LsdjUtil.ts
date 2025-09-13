import type { MainModule, Uint8Buffer } from '../native/RetroPlug';
import type { ILsdjKit, ILsdjKitData, ILsdjKitDataSample } from '../types/LsdjTypes';
import { KitType } from '../types/LsdjTypes';
import { convertFloat32Buffer } from './NativeUtil';

export function kitIsEditable(kit: ILsdjKit): boolean {
	return !!kit.samples;
}

export function getKitType(kit: ILsdjKit): KitType {
	if (kit.samples) {
		return KitType.Editable;
	}
	if (kit.path) {
		return KitType.Patched;
	}

	return KitType.Rom;
}

export function sanitizeKitName(input: string): string {
	// Convert to uppercase, allow only alphanumeric and dashes, limit to 6 characters
	return input
		.toUpperCase()
		.replace(/[^A-Z0-9-]/g, '')
		.slice(0, 6);
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
				// Editable kits first, then non-editable
				if (kitIsEditable(a) && !kitIsEditable(b)) return -1;
				if (!kitIsEditable(a) && kitIsEditable(b)) return 1;
				// If both have same editable status, sort by index
				return a.id - b.id;
			});
		/*case 'mostUsed':
				return kitsCopy.sort((a, b) => {
					// Sort by use count in descending order (most used first)
					if (a.useCount !== b.useCount) {
						return b.useCount - a.useCount;
					}
					// If use counts are equal, sort by index
					return a.id - b.id;
				});*/
		default:
			return kitsCopy;
	}
}

export const generateKey = (): string => {
	return `${Date.now()}-${Math.random()
		.toString(36)
		.substring(2, 2 + 9)}`;
};

export function extractSampleData(module: MainModule, kitData: Uint8Buffer): ILsdjKitData {
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
	const nativeSampleBuffer = new module.Float32Buffer(sampleData.size() * 2);
	module.convertNibblesToF32(sampleData, nativeSampleBuffer);
	const sampleBuffer = convertFloat32Buffer(nativeSampleBuffer);

	nativeSampleBuffer.delete();
	sampleData.delete();
	kit.delete();

	return { name: kitName, samples, sampleBuffer };
}
