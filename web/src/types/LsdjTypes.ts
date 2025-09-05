import type { NativeLsdjKitDesc } from "../native/RetroPlug";

export interface INamedSample {
	name: string;
	data: Float32Array;
}

export interface LsdjKitData {
	samples: INamedSample[];
	//effects
}

export interface LsdjKit extends NativeLsdjKitDesc {

}
