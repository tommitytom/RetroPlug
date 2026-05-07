
import type { MainModule, NativeDitherEffect, NativeEffect, NativeDitherMode } from "../native/RetroPlug";
import { Effect, EffectParameterType, type IEffectParameter } from "./Effect";
import { EnumUtils } from '../utils/EnumUtil';

enum DitherMode {
	ErrorDiffusion,
	SierraLite,
	HighPassTPDF,
	ShapedTPDF2ndOrder,
	JJNErrorDiffusion
}

function toDitherMode(module: MainModule, mode: NativeDitherMode): DitherMode {
	switch (mode) {
		case module.NativeDitherMode.ErrorDiffusion:
			return DitherMode.ErrorDiffusion;
		case module.NativeDitherMode.SierraLite:
			return DitherMode.SierraLite;
		case module.NativeDitherMode.HighPassTPDF:
			return DitherMode.HighPassTPDF;
		case module.NativeDitherMode.ShapedTPDF2ndOrder:
			return DitherMode.ShapedTPDF2ndOrder;
		case module.NativeDitherMode.JJNErrorDiffusion:
			return DitherMode.JJNErrorDiffusion;
	}
	return DitherMode.ErrorDiffusion;
}

function fromDitherMode(module: MainModule, mode: DitherMode): NativeDitherMode {
	switch (mode) {
		case DitherMode.ErrorDiffusion:
			return module.NativeDitherMode.ErrorDiffusion;
		case DitherMode.SierraLite:
			return module.NativeDitherMode.SierraLite;
		case DitherMode.HighPassTPDF:
			return module.NativeDitherMode.HighPassTPDF;
		case DitherMode.ShapedTPDF2ndOrder:
			return module.NativeDitherMode.ShapedTPDF2ndOrder;
		case DitherMode.JJNErrorDiffusion:
			return module.NativeDitherMode.JJNErrorDiffusion;
	}
	return module.NativeDitherMode.ErrorDiffusion;
}

export class DitherEffect extends Effect {
	private _module: MainModule;
	private _effect: NativeDitherEffect;

	constructor(module: MainModule, effect: NativeDitherEffect) {
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
				name: "Mode",
				type: EffectParameterType.Dropdown,
				options: EnumUtils.getAllKeys(DitherMode),
				getter: () => {
					const mode = toDitherMode(this._module, this._effect.getMode());
					return EnumUtils.enumToString(DitherMode, mode);
				},
				setter: (value) => {
					const mode = EnumUtils.stringToEnum(DitherMode, value as string);
					console.assert(mode !== undefined);
					this._effect.setMode(fromDitherMode(this._module, mode as DitherMode));
				}
			},
			{
				name: "Enabled",
				type: EffectParameterType.Toggle,
				getter: () => this._effect.isEnabled(),
				setter: (value) => {
					this._effect.setEnabled(!!value);
				}
			}
		];
	}
}
