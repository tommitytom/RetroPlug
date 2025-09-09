import React from 'react';

import { SliderProperty } from '../../components/SliderProperty';
import { EffectParameterType, type IEffectParameter } from '../../effects/Effect';
import { useLsdjStore } from '../../hooks/LsdjStoreHooks';

interface LsdjEffectParameterEditorProps {
	kitKey: string;
	sampleKey?: string;
	effectKey: string;
	paramName: string;
	paramKey: string;
	value: number | string | boolean;
	parameter: IEffectParameter;
	onParameterChanged: (paramKey: string, value: number|string|boolean) => void;
}

export const LsdjEffectParameterEditor: React.FC<LsdjEffectParameterEditorProps> = ({
	kitKey,
	sampleKey,
	effectKey,
	paramName,
	paramKey,
	value,
	parameter,
	onParameterChanged
}) => {
	const updateKitEffect = useLsdjStore((state) => state.updateKitEffect);
	const updateSampleEffect = useLsdjStore((state) => state.updateSampleEffect);

	const handleChange = (newValue: number | string | boolean) => {
		onParameterChanged(paramKey, newValue);

		if (sampleKey) {
			updateSampleEffect(kitKey, sampleKey, effectKey, { [paramKey]: newValue } as any);
		} else {
			updateKitEffect(kitKey, effectKey, { [paramKey]: newValue } as any);
		}
	};

	return (
		<div className="flex items-center space-x-2">
			<label className="w-24 text-sm font-medium text-gray-300 text-right whitespace-nowrap">{paramName}:</label>
			{parameter.type === EffectParameterType.Slider && (
				<SliderProperty
					min={parameter.min}
					max={parameter.max}
					step={parameter.step}
					defaultValue={value as number}
					onChange={handleChange}
				/>
			)}
			{parameter.type === EffectParameterType.Dropdown && (
				<select
					title={`${paramName} Dropdown`}
					value={value as string}
					onChange={(e) => handleChange(e.target.value)}
					className="flex-1 rounded border border-gray-600 bg-gray-700 px-2 py-1 text-xs text-white focus:border-blue-500 focus:outline-none"
				>
					{parameter.options?.map((option) => (
						<option key={option} value={option}>
							{option}
						</option>
					))}
				</select>
			)}
			{parameter.type === EffectParameterType.Toggle && (
				<input
					title={`${paramName} Toggle`}
					type="checkbox"
					checked={value as boolean}
					onChange={(e) => handleChange(e.target.checked)}
					className="h-4 w-4 rounded border-gray-600 bg-gray-700 text-blue-600 focus:ring-2 focus:ring-blue-500"
				/>
			)}
		</div>
	);
};
