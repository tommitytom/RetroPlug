import type { IEffect } from "../effects/Effect";

export const LSDJ_KIT_COUNT = 51;
export const LSDJ_KIT_SAMPLE_COUNT = 15;
export const GAMEBOY_SAMPLE_RATE = 11468;

export interface ILsdjKitSample {
	name: string;
	path: string;
	offset: number;
	length: number;
	effects: IEffect[];
	key?: string;
	data?: Uint8Array;
}

export type KitType = 'empty' | 'rom' | 'patched' | 'editable';

export interface ILsdjKitBase {
	type: KitType;
}

export interface ILsdjEmptyKit extends ILsdjKitBase {
	type: "empty";
};

export interface INamedKit extends ILsdjKitBase {
	name?: string;
}

export interface ILsdjRomKit extends INamedKit {
	type: "rom";
};

export interface ILsdjPatchedKit extends INamedKit {
	type: "patched";
	path: string;
};

export interface ILsdjEditableKit extends INamedKit {
	type: "editable";
	effects: IEffect[];
	samples: ILsdjKitSample[];
};

export interface ILsdjKit<T extends ILsdjKitBase = ILsdjKitBase> {
	id: number;
	kit: T;
	key?: string;
	data?: Uint8Array;
}

export interface ILsdjRom {
	name: string;
	kits: ILsdjKit[];
	key: string;
	id: number;
}

export interface IIndexedLsdjKit extends Partial<ILsdjKit> {
	id: number;
	kitType?: 'empty' | 'rom' | 'patched' | 'editable'; // Unset if the kit is empty/unused
}

export interface INamedSample {
	name: string;
	data: Float32Array;
}

export interface LsdjKitData {
	samples: INamedSample[];
	//effects
}

/*export interface LsdjKit extends NativeLsdjKitDesc {
	name: string;
}*/


export interface ILsdjKitDataSample {
	name: string;
	offset: number;
	length: number;
}

export interface ILsdjKitData {
	name: string;
	samples: ILsdjKitDataSample[];
	sampleBuffer: Float32Array;
}
