import React, { useEffect, useState } from 'react';

import type { IEffect, IEffectDescBase } from '../../effects/Effect';
import { findEffect } from '../../effects/Effect';
import { LsdjEffectParameterEditor } from './LsdjEffectParameterEditor';
import { useLsdjStore } from '../../hooks/LsdjStoreHooks';

interface LsdjEffectEditorProps {
	kitKey: string;
	sampleKey?: string;
	effect: IEffect;
}

// Format parameter name from lowerCamelCase to Human Readable
function formatParameterName(paramName: string): string {
	// Handle empty string or null/undefined
	if (!paramName) {
		return '';
	}

	// Split on capital letters and join with spaces
	const words = paramName
		.replace(/([A-Z])/g, ' $1') // Insert space before capital letters
		.trim() // Remove any leading/trailing whitespace
		.toLowerCase() // Convert to lowercase
		.split(' ') // Split into words
		.map(word => word.charAt(0).toUpperCase() + word.slice(1)); // Capitalize first letter of each word

	return words.join(' ');
}

export const LsdjEffectEditor: React.FC<LsdjEffectEditorProps> = ({ kitKey, sampleKey, effect }) => {
	const removeKitEffect = useLsdjStore((state) => state.removeKitEffect);
	const removeSampleEffect = useLsdjStore((state) => state.removeSampleEffect);
	const [effectDesc, setEffectDesc] = useState<IEffectDescBase | null>(null);

	useEffect(() => {
		const effectDesc = findEffect(effect.type);
		if (effectDesc) {
			setEffectDesc(effectDesc);
		} else {
			console.error(`Effect description not found for type: ${effect.type}`);
		}
	}, [effect]);

	const handleRemove = () => {
		if (sampleKey) {
			removeSampleEffect(kitKey, sampleKey, effect.key!);
		} else {
			removeKitEffect(kitKey, effect.key!);
		}
	};

	return (
		<div className="py-2">
			<div className="flex items-center justify-between mb-2">
				<h4 className="text-white font-medium">{effectDesc?.name}</h4>
				<button
					className="text-red-400 hover:text-red-300 text-lg font-bold px-1 py-0 rounded transition-colors duration-200"
					onClick={handleRemove}
					title="Remove Effect"
				>
					-
				</button>
			</div>

			<div className="space-y-2">
				{effectDesc && Object.entries(effectDesc.parameters).map(([paramKey, parameter]) => {
					// Only show parameter if shouldShow function is not defined or returns true
					const shouldShow = !parameter.shouldShow || parameter.shouldShow(effect);

					if (!shouldShow) {
						return null;
					}

					return (
						<LsdjEffectParameterEditor
							key={`${effect.key}-${paramKey}`}
							parameter={parameter}
							effectKey={effect.key!}
							kitKey={kitKey}
							paramName={formatParameterName(paramKey)}
							paramKey={paramKey}
							sampleKey={sampleKey}
							value={(effect as any)[paramKey]}
						/>
					);
				})}
			</div>
		</div>
	);
};
