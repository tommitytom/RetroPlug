import React, { useState } from "react";
import { type IEffectParameter } from "../effects/Effect";
import { EffectItem } from "./EffectItem";
import { type EffectInstance } from "../types/EffectTypes";
import { useRetroPlug } from "../contexts/RetroPlugContext";

interface EffectListProps {
	isExpanded: boolean;
	onToggle: () => void;
	effects?: EffectInstance[];
	onEffectsChange?: (effects: EffectInstance[]) => void;
}

export const EffectList: React.FC<EffectListProps> = ({
	isExpanded,
	onToggle,
	effects: externalEffects,
	onEffectsChange,
}) => {
	const { app } = useRetroPlug();
	const [internalEffects, setInternalEffects] = useState<EffectInstance[]>([]);

	const effects = externalEffects || internalEffects;
	const setEffects = onEffectsChange || setInternalEffects;

	const handleRemoveEffect = (effectId: string) => {
		const newEffects = effects.filter((effect) => effect.id !== effectId);
		setEffects(newEffects);
	};

	const handleParameterChange = (
		effectId: string,
		parameter: IEffectParameter,
		value: number | string | boolean,
	) => {
		parameter.setter(value);
		// Force re-render by updating the effects array
		//setEffects([...effects]);
	};

	return (
		<div className="border border-gray-700 rounded-sm overflow-hidden mt-2">
			<div
				className="px-2 py-1 bg-gray-800 font-medium flex items-center justify-between text-sm cursor-pointer hover:bg-gray-750 transition-colors duration-200"
				onClick={onToggle}
			>
				<div className="flex items-center">
					<div className="text-white mr-2 text-xs">{isExpanded ? "▼" : "▶"}</div>
					<span className="font-medium">Effects</span>
				</div>
				<button
					className="text-green-400 hover:text-green-300 hover:bg-green-600/20 text-sm font-bold px-2 py-1 rounded-sm transition-colors duration-200"
					onClick={(e) => {
						e.stopPropagation();
						setEffects([
							...effects,
							{
								id: `biquad-${effects.length + 1}`,
								name: `Filter`,
								effect: app!.createBiquadEffect()
							},
						]);
					}}
					title="Add Effect"
				>
					+
				</button>
			</div>

			{isExpanded && (
				<div className="p-2 bg-gray-900">
					{effects.map((effect, index) => (
						<div key={effect.id}>
							{index > 0 && <hr className="border-gray-600 my-1" />}
							<EffectItem
								effect={effect}
								onRemove={handleRemoveEffect}
								onParameterChange={handleParameterChange}
							/>
						</div>
					))}
				</div>
			)}
		</div>
	);
};
