import type { NativeEffect } from "../native/RetroPlug";

export enum EffectParameterType {
	Slider,
	Dropdown,
	Toggle
}

export interface IEffectParameter {
	name: string;
	type: EffectParameterType;
	min?: number;
	max?: number;
	options?: string[];

	getter: () => number | string | boolean;
	setter: (value: number | string | boolean) => void;
}

export abstract class Effect {
	abstract getParameters(): IEffectParameter[];
	abstract getNativeEffect(): NativeEffect;
}
