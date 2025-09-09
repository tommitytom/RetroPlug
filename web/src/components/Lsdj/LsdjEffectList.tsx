import React from 'react';

import { createEffectInstance } from '../../effects/Effect';
import type { ILsdjKitEffect } from '../../types/LsdjTypes';
import { LsdjEffectEditor } from './LsdjEffectEditor';
import { useLsdjStore } from '../../hooks/LsdjStoreHooks';
import { generateKey } from '../../utils/LsdjUtil';

interface LsdjEffectListProps {
	kitKey: string;
	sampleKey?: string;
	title: string;
	isExpanded: boolean;
	onToggle: (expanded?: boolean) => void;
}

export const LsdjEffectList: React.FC<LsdjEffectListProps> = ({ kitKey, sampleKey, title, isExpanded, onToggle }) => {
	const kit = useLsdjStore((state) => state.getKit(kitKey));
	const sample = useLsdjStore((state) => (sampleKey ? state.getSample(kitKey, sampleKey) : undefined));
	const addKitEffect = useLsdjStore((state) => state.addKitEffect);
	const addSampleEffect = useLsdjStore((state) => state.addSampleEffect);

	const effects = sampleKey ? sample?.effects || [] : kit?.effects || [];

	const handleAddEffect = (type: string) => {
		const effectInstance = createEffectInstance(type);
		if (!effectInstance) {
			console.error(`Failed to create effect instance of type: ${type}`);
			return;
		}

		const newEffect: ILsdjKitEffect = {
			id: 0,
			key: generateKey(),
			type,
			effectInstance,
		};

		if (sampleKey) {
			addSampleEffect(kitKey, sampleKey, newEffect);
		} else {
			addKitEffect(kitKey, newEffect);
		}
	};

	return (
		<div className="mt-2 overflow-hidden rounded-sm border border-gray-700">
			<div
				className="hover:bg-gray-750 flex cursor-pointer items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200"
				onClick={() => onToggle()}
			>
				<div className="flex items-center">
					<div className="mr-2 text-xs text-white">{isExpanded ? '▼' : '▶'}</div>
					<span className="font-medium text-white">Effects</span>
				</div>
				<button
					className="rounded-sm px-2 py-1 text-sm font-bold text-green-400 transition-colors duration-200 hover:bg-green-600/20 hover:text-green-300"
					onClick={(e) => {
						e.stopPropagation();
						handleAddEffect('Filter');
						onToggle(true);
					}}
					title="Add Effect"
				>
					+
				</button>
			</div>

			{isExpanded && (
				<div className="bg-gray-900 p-2">
					{effects.map((effect, index) => (
						<div key={effect.key}>
							{index > 0 && <hr className="my-1 border-gray-600" />}
							<LsdjEffectEditor effect={effect} kitKey={kitKey} key={kitKey} sampleKey={sampleKey} />
						</div>
					))}
				</div>
			)}
		</div>
	);
};
