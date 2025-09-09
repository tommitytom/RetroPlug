import type { IEffect } from "../effects/Effect";

export const LSDJ_KIT_COUNT = 51;
export const LSDJ_KIT_SAMPLE_COUNT = 15;
export const GAMEBOY_SAMPLE_RATE = 11468;

export interface ILsdjKitEffect<T extends IEffect = IEffect> {
	id: number;
	key: string;
	effect: T
}

export interface ILsdjKitSample {
	name: string;
	path: string;
	offset: number;
	length: number;
	effects: ILsdjKitEffect[];
	key: string;
	data?: Uint8Array;
}

export interface ILsdjKit {
	name: string;
	id: number;
	path?: string;
	samples?: ILsdjKitSample[];
	effects?: ILsdjKitEffect[];
	data?: Uint8Array;
	key: string;
}

export interface ILsdjRom {
	name: string;
	kits: ILsdjKit[];
	key: string;
	id: number;
}

export enum KitType {
	Rom,
	Patched,
	Editable
}

export interface IIndexedLsdjKit extends Partial<ILsdjKit> {
	id: number;
	kitType?: KitType; // Unset if the kit is empty/unused
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
	samples: ILsdjKitDataSample[];
	sampleBuffer: Float32Array;
}
