import React from "react";
import { type IEffectParameter } from "../effects/Effect";
import { EffectParameter } from "./EffectParameter";
import { type EffectInstance } from "../types/EffectTypes";

interface EffectItemProps {
	effect: EffectInstance;
	onRemove: (effectId: string) => void;
	onParameterChange: (effectId: string, parameter: IEffectParameter, value: number | string | boolean) => void;
}

export const EffectItem: React.FC<EffectItemProps> = ({
	effect,
	onRemove,
	onParameterChange,
}) => {
	return (
		<div className="py-2">
			<div className="flex items-center justify-between mb-2">
				<h4 className="text-white font-medium">{effect.name}</h4>
				<button
					className="text-red-400 hover:text-red-300 text-lg font-bold px-1 py-0 rounded transition-colors duration-200"
					onClick={() => onRemove(effect.id)}
					title="Remove Effect"
				>
					-
				</button>
			</div>

			<div className="space-y-2">
				{effect.effect.getParameters().map((parameter, paramIndex) => (
					<EffectParameter
						key={paramIndex}
						parameter={parameter}
						onParameterChange={(value) => onParameterChange(effect.id, parameter, value)}
					/>
				))}
			</div>
		</div>
	);
};
