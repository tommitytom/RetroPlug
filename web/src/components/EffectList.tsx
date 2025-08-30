import React, { useState } from 'react';

interface Effect {
	id: string;
	name: string;
	gain: number;
	dither: number;
}

interface EffectListProps {
	isExpanded: boolean;
	onToggle: () => void;
}

export const EffectList: React.FC<EffectListProps> = ({ isExpanded, onToggle }) => {
	const [effects, setEffects] = useState<Effect[]>([
		{
			id: 'default',
			name: 'Default',
			gain: 0.5,
			dither: 0.0
		}
	]);

	const handleEffectChange = (effectId: string, property: keyof Effect, value: number) => {
		setEffects(prevEffects =>
			prevEffects.map(effect =>
				effect.id === effectId
					? { ...effect, [property]: value }
					: effect
			)
		);
	};

	const handleRemoveEffect = (effectId: string) => {
		setEffects(prevEffects => prevEffects.filter(effect => effect.id !== effectId));
	};

	const handleSliderChange = (effectId: string, property: keyof Effect, event: React.ChangeEvent<HTMLInputElement>) => {
		const value = parseFloat(event.target.value);
		handleEffectChange(effectId, property, value);
	};

	const handleInputChange = (effectId: string, property: keyof Effect, event: React.ChangeEvent<HTMLInputElement>) => {
		const value = parseFloat(event.target.value) || 0;
		const clampedValue = Math.max(0, Math.min(1, value));
		handleEffectChange(effectId, property, clampedValue);
	};

	return (
		<div className="w-full bg-gray-800 border border-gray-700 rounded-xs shadow-lg mt-2">
			<div
				className={`flex items-center cursor-pointer hover:bg-gray-750 rounded-t-lg p-1 transition-colors duration-200`}
				onClick={onToggle}
			>
				<div className="text-white mr-2 text-sm">
					{isExpanded ? "▼" : "▶"}
				</div>
				<h3 className="text-md font-semibold text-white flex-1">
					Effects
				</h3>
				<button
					className="text-white hover:text-gray-300 text-xl font-bold px-2 py-1 rounded transition-colors duration-200"
					onClick={(e) => {
						e.stopPropagation();
						// TODO: Add new effect functionality
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
							<div className="py-2">
								<div className="flex items-center justify-between mb-2">
									<h4 className="text-white font-medium">{effect.name}</h4>
									<button
										className="text-red-400 hover:text-red-300 text-lg font-bold px-1 py-0 rounded transition-colors duration-200"
										onClick={() => handleRemoveEffect(effect.id)}
										title="Remove Effect"
									>
										-
									</button>
								</div>

								<div className="space-y-2">
									{/* Gain Slider */}
									<div className="flex items-center space-x-2">
										<label className="text-gray-300 text-sm font-medium w-12">Gain:</label>
										<input
											title="Gain Slider"
											type="range"
											min="0"
											max="1"
											step="0.01"
											value={effect.gain}
											onChange={(e) => handleSliderChange(effect.id, 'gain', e)}
											className="flex-1 h-1 bg-gray-600 rounded-lg appearance-none cursor-pointer"
											style={{
												background: `linear-gradient(to right, #3b82f6 0%, #3b82f6 ${effect.gain * 100}%, #4b5563 ${effect.gain * 100}%, #4b5563 100%)`
											}}
										/>
										<input
											title="Gain Editor"
											type="number"
											min="0"
											max="1"
											step="0.01"
											value={effect.gain.toFixed(2)}
											onChange={(e) => handleInputChange(effect.id, 'gain', e)}
											className="w-14 px-1 py-0 text-xs bg-gray-700 text-white border border-gray-600 rounded focus:outline-none focus:border-blue-500"
										/>
									</div>

									{/* Dither Slider */}
									<div className="flex items-center space-x-2">
										<label className="text-gray-300 text-sm font-medium w-12">Dither:</label>
										<input
											title="Dither Slider"
											type="range"
											min="0"
											max="1"
											step="0.01"
											value={effect.dither}
											onChange={(e) => handleSliderChange(effect.id, 'dither', e)}
											className="flex-1 h-1 bg-gray-600 rounded-lg appearance-none cursor-pointer"
											style={{
												background: `linear-gradient(to right, #3b82f6 0%, #3b82f6 ${effect.dither * 100}%, #4b5563 ${effect.dither * 100}%, #4b5563 100%)`
											}}
										/>
										<input
											title="Dither Editor"
											type="number"
											min="0"
											max="1"
											step="0.01"
											value={effect.dither.toFixed(2)}
											onChange={(e) => handleInputChange(effect.id, 'dither', e)}
											className="w-14 px-1 py-0 text-xs bg-gray-700 text-white border border-gray-600 rounded focus:outline-none focus:border-blue-500"
										/>
									</div>
								</div>
							</div>
						</div>
					))}
				</div>
			)}
		</div>
	);
};
