import type { MainModule, NativeBiquadEffect, NativeEffect, NativeFilterType } from "../native/RetroPlug";
import { Effect, EffectParameterType, type IEffectParameter } from "./Effect";
import { EnumUtils } from '../utils/EnumUtil';

enum FilterType {
	LowPass,
	HighPass,
	BandPass,
	LowShelf,
	HighShelf,
	Peak,
	Notch,
	AllPass
}

function toFilterType(module: MainModule, type: NativeFilterType): FilterType {
	switch (type) {
		case module.NativeFilterType.LowPass:
			return FilterType.LowPass;
		case module.NativeFilterType.HighPass:
			return FilterType.HighPass;
		case module.NativeFilterType.BandPass:
			return FilterType.BandPass;
		case module.NativeFilterType.LowShelf:
			return FilterType.LowShelf;
		case module.NativeFilterType.HighShelf:
			return FilterType.HighShelf;
		case module.NativeFilterType.Peak:
			return FilterType.Peak;
		case module.NativeFilterType.AllPass:
			return FilterType.AllPass;
	}

	return FilterType.LowPass;
}

function fromFilterType(module: MainModule, type: FilterType): NativeFilterType {
	switch (type) {
		case FilterType.LowPass:
			return module.NativeFilterType.LowPass;
		case FilterType.HighPass:
			return module.NativeFilterType.HighPass;
		case FilterType.BandPass:
			return module.NativeFilterType.BandPass;
		case FilterType.LowShelf:
			return module.NativeFilterType.LowShelf;
		case FilterType.HighShelf:
			return module.NativeFilterType.HighShelf;
		case FilterType.Peak:
			return module.NativeFilterType.Peak;
		case FilterType.AllPass:
			return module.NativeFilterType.AllPass;
	}

	return module.NativeFilterType.LowPass;
}

export class BiquadEffect extends Effect {
	private _module: MainModule;
	private _effect: NativeBiquadEffect;

	constructor(module: MainModule, effect: NativeBiquadEffect) {
		super();
		this._module = module;
		this._effect = effect;
	}

	getNativeEffect(): NativeEffect {
		return this._effect;
	}

	getParameters(): IEffectParameter[] {
		return [
			{
				name: "Type",
				type: EffectParameterType.Dropdown,
				options: EnumUtils.getAllKeys(FilterType),
				getter: () => {
					const filterType = toFilterType(this._module, this._effect.getFilterType());
					return EnumUtils.enumToString(FilterType, filterType);
				},
				setter: (value) => {
					const filterType = EnumUtils.stringToEnum(FilterType, value as string);
					console.assert(!!filterType);
					this._effect.setFilterType(fromFilterType(this._module, filterType as FilterType));
				}
			},
			{
				name: "Frequency",
				type: EffectParameterType.Slider,
				min: 20.0,
				max: 20000.0,
				getter: () => this._effect.getFrequency(),
				setter: (value) => {
					this._effect.setFrequency(value as number);
				}
			},
			{
				name: "Q",
				type: EffectParameterType.Slider,
				min: 0.1,
				max: 10.0,
				getter: () => this._effect.getQ(),
				setter: (value) => {
					this._effect.setQ(value as number);
				}
			},
			{
				name: "Gain",
				type: EffectParameterType.Slider,
				min: 0,
				max: 5.0,
				getter: () => this._effect.getGain(),
				setter: (value) => {
					this._effect.setGain(value as number);
				}
			}
		];
	}
}
