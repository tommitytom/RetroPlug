import type { NativeEffect } from '../native/RetroPlug';

export abstract class Effect {
	abstract getParameters(): IEffectParameter[];
	abstract getNativeEffect(): NativeEffect;
}

export enum EffectParameterType {
	Slider,
	Dropdown,
	Toggle,
}

export interface IEffectParameter {
	type: EffectParameterType;
	defaultValue: number | string | boolean;
	min?: number;
	max?: number;
	step?: number;
	options?: string[];

	getter?: () => number | string | boolean;
	setter?: (value: number | string | boolean) => void;
	shouldShow?(effect: IEffect): boolean;
}

export interface IEffectDescBase {
	name: string;
	type: string;
	parameters: { [key: string]: IEffectParameter };
}

export interface IEffectDesc<T extends IEffect> extends IEffectDescBase {
	parameters: { [key in keyof Omit<T, 'type'>]: IEffectParameter };
}

function registerEffect<T extends IEffect>(
	name: string,
	type: string,
	parameters: { [key in keyof Omit<T, 'type'>]: IEffectParameter },
): IEffectDesc<T> {
	return {
		name,
		type,
		parameters,
	};
}

export interface IEffect {
	id?: number; // Will be deprecated
	key?: string; // Will be deprecated
	type: string;
}

export interface IGainEffect extends IEffect {
	normalize: boolean;
	gain: number;
}
export const GAIN_EFFECT_DESC = registerEffect<IGainEffect>('Gain', 'GainEffect', {
	normalize: {
		type: EffectParameterType.Toggle,
		defaultValue: true,
	},
	gain: {
		type: EffectParameterType.Slider,
		defaultValue: 1,
		min: 0,
		max: 5,
		step: 0.01,
	},
});

const GAMEBOY_SAMPLE_RATE = 11468;

export enum FilterType {
	LowPass = 'LowPass',
	HighPass = 'HighPass',
	BandPass = 'BandPass',
	BandStop = 'BandStop',
	Peak = 'Peak',
	LowShelf = 'LowShelf',
	HighShelf = 'HighShelf',
	AllPass = 'AllPass',
}
export interface IFilterEffect extends IEffect {
	filterType: FilterType;
	frequency: number;
	q: number;
	gain: number;
}
export const FILTER_EFFECT_DESC = registerEffect<IFilterEffect>('Filter', 'FilterEffect', {
	filterType: {
		type: EffectParameterType.Dropdown,
		defaultValue: FilterType.LowPass,
		options: Object.values(FilterType),
	},
	frequency: {
		type: EffectParameterType.Slider,
		defaultValue: GAMEBOY_SAMPLE_RATE / 2,
		min: 20,
		max: GAMEBOY_SAMPLE_RATE / 2,
		step: 1,
	},
	q: {
		type: EffectParameterType.Slider,
		defaultValue: 1,
		min: 0.01,
		max: 10,
		step: 0.01,
	},
	gain: {
		type: EffectParameterType.Slider,
		defaultValue: 0,
		min: -12,
		max: 12,
		step: 0.01,
		shouldShow: (effect) => ['LowShelf', 'HighShelf', 'Peak'].includes((effect as IFilterEffect).filterType),
	},
});

enum DitherType {
	HighPassTPDF = 'HighPassTPDF',
	ShapedTPDF = 'ShapedTPDF',
	ErrorDiffusion = 'ErrorDiffusion',
	JJN = 'JJN',
	SierraLite = 'SierraLite',
}
export interface IDitherEffect extends IEffect {
	ditherType: DitherType;
}
export const DITHER_EFFECT_DESC = registerEffect<IDitherEffect>('Dither', 'DitherEffect', {
	ditherType: {
		type: EffectParameterType.Dropdown,
		defaultValue: DitherType.HighPassTPDF,
		options: Object.values(DitherType),
	},
});

export const ALL_EFFECTS = [GAIN_EFFECT_DESC, FILTER_EFFECT_DESC, DITHER_EFFECT_DESC];

export function findEffect(type: string) {
	return ALL_EFFECTS.find((effect) => effect.type === type);
}

export function createEffectInstance(type: string): IEffect | undefined {
	const effect = findEffect(type);
	if (effect) {
		const effectInstance = { type };

		Object.entries(effect.parameters).forEach(([paramKey, param]) => {
			(effectInstance as any)[paramKey] = param.defaultValue;
		});

		return effectInstance;
	}
}
