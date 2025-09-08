import React from 'react';

import { SliderProperty } from '../../components/SliderProperty';
import { EffectParameterType, type IEffectParameter } from '../../effects/Effect';
import { useLsdjStore } from './hooks';

interface LsdjEffectParameterEditorProps {
	kitKey: string;
	sampleKey?: string;
	effectKey: string;
	paramName: string;
	paramKey: string;
	value: number | string | boolean;
	parameter: IEffectParameter;
}

export const LsdjEffectParameterEditor: React.FC<LsdjEffectParameterEditorProps> = ({
	kitKey,
	sampleKey,
	effectKey,
	paramName,
	paramKey,
	value,
	parameter,
}) => {
	const updateKitEffect = useLsdjStore((state) => state.updateKitEffect);
	const updateSampleEffect = useLsdjStore((state) => state.updateSampleEffect);

	const handleChange = (newValue: number | string | boolean) => {
		console.log(`Changing ${paramName} from ${value} to ${newValue}`, kitKey);

		if (sampleKey) {
			updateSampleEffect(kitKey, sampleKey, effectKey, { [paramKey]: newValue } as any);
		} else {
			updateKitEffect(kitKey, effectKey, { [paramKey]: newValue } as any);
		}
	};

	console.log(`Current value of ${paramName}:`, value);

	return (
		<div className="flex items-center space-x-2">
			<label className="w-16 text-sm font-medium text-gray-300">{paramName}:</label>
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

	/*
	return (
		<div className="parameter-editor text-white">
			<label>
				{paramName}:
				<input
					type="range"
					min="0"
					max={paramName === 'gain' ? 2 : paramName === 'freq' ? 20000 : 100}
					step={0.01}
					value={value}
					onChange={(e) => handleChange(parseFloat(e.target.value))}
				/>
				<span>{value.toFixed(2)}</span>
			</label>
		</div>
	);
	*/
};
