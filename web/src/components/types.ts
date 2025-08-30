import type { NativeLsdjKit } from "../native/RetroPlug";

export interface INamedSample {
	name: string;
	data: Float32Array;
}

export interface IIndexedKit {
	id: number;
	name: string;
	kit: NativeLsdjKit;
}
