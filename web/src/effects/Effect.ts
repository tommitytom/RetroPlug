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
	parameters: { [key: string]: IEffectParameter };
}

export interface IEffectDesc<T extends IEffect> extends IEffectDescBase {
	parameters: { [key in keyof T]: IEffectParameter };
}

function registerEffect<T extends IEffect>(name: string, parameters: { [key in keyof T]: IEffectParameter }): IEffectDesc<T> {
	return {
		name,
		parameters
	};
}

export interface IEffect {}

export interface IGainEffect extends IEffect {
	gain: number;
}
export const GAIN_EFFECT_DESC = registerEffect<IGainEffect>('Gain', {
	gain: {
		type: EffectParameterType.Slider,
		defaultValue: 1,
		min: 0,
		max: 2,
		step: 0.01
	}
});

export interface IFilterEffect extends IEffect {
	frequency: number;
	q: number;
	feedback: number;
}
export const FILTER_EFFECT_DESC = registerEffect<IFilterEffect>('Filter', {
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

const ALL_EFFECTS = [
	GAIN_EFFECT_DESC,
	FILTER_EFFECT_DESC
];

export function findEffect(name: string) {
	return ALL_EFFECTS.find(effect => effect.name === name);
}

export function createEffectInstance(name: string): IEffect | undefined {
	const effect = findEffect(name);
	if (effect) {
		const effectInstance = {};

		Object.entries(effect.parameters).forEach(([paramKey, param]) => {
			effectInstance[paramKey] = param.defaultValue;
		});

		return effectInstance;
	}
}
