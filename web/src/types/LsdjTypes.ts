export const LSDJ_KIT_COUNT = 51;
export const LSDJ_KIT_SAMPLE_COUNT = 15;
export const GAMEBOY_SAMPLE_RATE = 11468;

export interface ILsdjKitEffect {
	type: string;
}

export interface ILsdjKitSample {
	name: string;
	path: string;
	offset: number;
	length: number;
	effects?: ILsdjKitEffect[];
	data?: Uint8Array;
}

export interface ILsdjKit {
	name: string;
	path?: string;
	samples?: ILsdjKitSample[];
	effects?: ILsdjKitEffect[];
	data?: Uint8Array;
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
