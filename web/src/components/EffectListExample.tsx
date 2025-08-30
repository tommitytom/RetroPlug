import React, { useState, useEffect } from "react";
import { EffectList } from "./EffectList";
import { DitherEffect } from "../effects/DitherEffect";
import { BiquadEffect } from "../effects/BiquadEffect";
import { type EffectInstance } from "../types/EffectTypes";

export const EffectListExample: React.FC = () => {
	const [isExpanded, setIsExpanded] = useState(true);
	const [effects, setEffects] = useState<EffectInstance[]>([]);

	// This is just an example - in a real implementation, you would get these from your native module
	useEffect(() => {
		// Example of how you might initialize effects when you have the native module available
		// const exampleEffects: EffectInstance[] = [
		// 	{
		// 		id: "dither-1",
		// 		name: "Dither Effect",
		// 		effect: new DitherEffect(nativeModule, nativeDitherEffect)
		// 	},
		// 	{
		// 		id: "biquad-1",
		// 		name: "Biquad Filter",
		// 		effect: new BiquadEffect(nativeModule, nativeBiquadEffect)
		// 	}
		// ];
		// setEffects(exampleEffects);
	}, []);

	const handleToggle = () => {
		setIsExpanded(!isExpanded);
	};

	const handleEffectsChange = (newEffects: EffectInstance[]) => {
		setEffects(newEffects);
	};

	return (
		<div className="p-4">
			<h2 className="text-white text-lg mb-4">Dynamic Effect List Example</h2>
			<EffectList
				isExpanded={isExpanded}
				onToggle={handleToggle}
				effects={effects}
				onEffectsChange={handleEffectsChange}
			/>

			{effects.length === 0 && (
				<div className="mt-4 p-4 bg-gray-700 rounded text-gray-300 text-sm">
					<p>No effects loaded. In a real implementation, effects would be created from the native module:</p>
					<pre className="mt-2 text-xs bg-gray-800 p-2 rounded">
{`// Example usage:
const ditherEffect = new DitherEffect(module, nativeDitherEffect);
const biquadEffect = new BiquadEffect(module, nativeBiquadEffect);

const effectInstances = [
  { id: "dither-1", name: "Dither", effect: ditherEffect },
  { id: "biquad-1", name: "Filter", effect: biquadEffect }
];`}
					</pre>
				</div>
			)}
		</div>
	);
};
