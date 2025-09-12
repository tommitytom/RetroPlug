import type { NativeEffect } from "../native/RetroPlug";

export abstract class Effect {
	abstract getParameters(): IEffectParameter[];
	abstract getNativeEffect(): NativeEffect;
}

export enum EffectParameterType {
	Slider,
	Dropdown,
	Toggle
}

export interface IEffectParameter {
	type: EffectParameterType;
	defaultValue: number|string|boolean;
	min?: number;
	max?: number;
	step?: number;
	options?: string[];

	getter?: () => number | string | boolean;
	setter?: (value: number | string | boolean) => void;
}

export interface IEffectDescBase {
	name: string;
	type: string;
	parameters: { [key: string]: IEffectParameter };
}

export interface IEffectDesc<T extends IEffect> extends IEffectDescBase {
	parameters: { [key in keyof Omit<T, 'type'>]: IEffectParameter };
}

function registerEffect<T extends IEffect>(name: string, type: string, parameters: { [key in keyof Omit<T, 'type'>]: IEffectParameter }): IEffectDesc<T> {
	return {
		name,
		type,
		parameters
	};
}

export interface IEffect {
	type: string;
}

export interface IGainEffect extends IEffect {
	gain: number;
}
export const GAIN_EFFECT_DESC = registerEffect<IGainEffect>('Gain', 'GainEffect', {
	gain: {
		type: EffectParameterType.Slider,
		defaultValue: 1,
		min: 0,
		max: 5,
		step: 0.01
	}
});

export interface IFilterEffect extends IEffect {
	frequency: number;
	q: number;
	feedback: number;
}
export const FILTER_EFFECT_DESC = registerEffect<IFilterEffect>('Filter', 'FilterEffect', {
	frequency: {
		type: EffectParameterType.Slider,
		defaultValue: 1000,
		min: 20,
		max: 20000,
		step: 1
	},
	q: {
		type: EffectParameterType.Slider,
		defaultValue: 0,
		min: 0,
		max: 1,
		step: 0.1
	},
	feedback: {
		type: EffectParameterType.Slider,
		defaultValue: 0,
		min: 0,
		max: 1,
		step: 0.1
	}
});

enum DitherType {
	ErrorDiffusion = "ErrorDiffusion",
	SierraLite = "SierraLite",
	JJN = "JJN",
	HighPassTPDF = "HighPassTPDF",
	ShapedTPDF = "ShapedTPDF"
}

export interface IDitherEffect extends IEffect {
	ditherType: DitherType;
}
export const DITHER_EFFECT_DESC = registerEffect<IDitherEffect>('Dither', 'DitherEffect', {
	ditherType: {
		type: EffectParameterType.Dropdown,
		defaultValue: DitherType.ErrorDiffusion,
		options: Object.values(DitherType)
	}
});

export const ALL_EFFECTS = [
	GAIN_EFFECT_DESC,
	FILTER_EFFECT_DESC,
	DITHER_EFFECT_DESC
];

export function findEffect(type: string) {
	return ALL_EFFECTS.find(effect => effect.type === type);
}

export function createEffectInstance(type: string): IEffect | undefined {
	const effect = findEffect(type);
	if (effect) {
		const effectInstance = { type };

		Object.entries(effect.parameters).forEach(([paramKey, param]) => {
			effectInstance[paramKey] = param.defaultValue;
		});

		return effectInstance;
	}
}
