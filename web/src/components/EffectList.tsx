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
		<div className="w-full bg-gray-800 border border-gray-700 rounded-xs shadow-lg mt-2">
			<div
				className={`flex items-center cursor-pointer hover:bg-gray-750 rounded-t-lg p-1 transition-colors duration-200`}
				onClick={onToggle}
			>
				<div className="text-white mr-2 text-sm">{isExpanded ? "▼" : "▶"}</div>
				<h3 className="text-md font-semibold text-white flex-1">Effects</h3>
				<button
					className="text-white hover:text-gray-300 text-xl font-bold px-2 py-1 rounded transition-colors duration-200"
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
				<div className="px-2 pb-2">
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
