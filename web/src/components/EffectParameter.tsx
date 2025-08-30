import React from "react";
import { EffectParameterType, type IEffectParameter } from "../effects/Effect";

interface EffectParameterProps {
	parameter: IEffectParameter;
	onParameterChange: (value: number | string | boolean) => void;
}

export const EffectParameter: React.FC<EffectParameterProps> = ({
	parameter,
	onParameterChange,
}) => {
	return (
		<div className="flex items-center space-x-2">
			<label className="text-gray-300 text-sm font-medium w-16">
				{parameter.name}:
			</label>
			{parameter.type === EffectParameterType.Slider && (
				<>
					<input
						title={`${parameter.name} Slider`}
						type="range"
						min={parameter.min || 0}
						max={parameter.max || 1}
						step="0.01"
						value={parameter.getter() as number}
						onChange={(e) => onParameterChange(parseFloat(e.target.value))}
						className="flex-1 h-1 bg-gray-600 rounded-lg appearance-none cursor-pointer"
						style={{
							background: `linear-gradient(to right, #3b82f6 0%, #3b82f6 ${((parameter.getter() as number - (parameter.min || 0)) / ((parameter.max || 1) - (parameter.min || 0))) * 100}%, #4b5563 ${((parameter.getter() as number - (parameter.min || 0)) / ((parameter.max || 1) - (parameter.min || 0))) * 100}%, #4b5563 100%)`,
						}}
					/>
					<input
						title={`${parameter.name} Editor`}
						type="number"
						min={parameter.min || 0}
						max={parameter.max || 1}
						step="0.01"
						value={(parameter.getter() as number).toFixed(2)}
						onChange={(e) => {
							const value = parseFloat(e.target.value) || 0;
							const clampedValue = Math.max(
								parameter.min || 0,
								Math.min(parameter.max || 1, value)
							);
							onParameterChange(clampedValue);
						}}
						className="w-16 px-1 py-0 text-xs bg-gray-700 text-white border border-gray-600 rounded focus:outline-none focus:border-blue-500"
					/>
				</>
			)}
			{parameter.type === EffectParameterType.Dropdown && (
				<select
					title={`${parameter.name} Dropdown`}
					value={parameter.getter() as string}
					onChange={(e) => onParameterChange(e.target.value)}
					className="flex-1 px-2 py-1 text-xs bg-gray-700 text-white border border-gray-600 rounded focus:outline-none focus:border-blue-500"
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
					title={`${parameter.name} Toggle`}
					type="checkbox"
					checked={parameter.getter() as boolean}
					onChange={(e) => onParameterChange(e.target.checked)}
					className="w-4 h-4 text-blue-600 bg-gray-700 border-gray-600 rounded focus:ring-blue-500 focus:ring-2"
				/>
			)}
		</div>
	);
};
