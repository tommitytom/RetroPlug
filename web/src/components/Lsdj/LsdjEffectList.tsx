import React, { useCallback } from 'react';

import { ALL_EFFECTS, createEffectInstance } from '../../effects/Effect';
import type { ILsdjKitEffect } from '../../types/LsdjTypes';
import { LsdjEffectEditor } from './LsdjEffectEditor';
import { useLsdjStore } from '../../hooks/LsdjStoreHooks';
import { generateKey } from '../../utils/LsdjUtil';
import {} from '../../effects/Effect';
import { useContextMenu } from '../../hooks/useContextMenu';
import { ContextMenu } from '../Menu/ContextMenu';

interface LsdjEffectListProps {
	kitKey: string;
	sampleKey?: string;
	title: string;
	isExpanded: boolean;
	onToggle: (expanded?: boolean) => void;
	onChange: () => void;
	onParameterChanged: (name: string, value: number | string | boolean) => void;
}

export const LsdjEffectList: React.FC<LsdjEffectListProps> = ({
	kitKey,
	sampleKey,
	title,
	isExpanded,
	onToggle,
	onChange,
	onParameterChanged,
}) => {
	const kit = useLsdjStore((state) => state.getKit(kitKey));
	const sample = useLsdjStore((state) => (sampleKey ? state.getSample(kitKey, sampleKey) : undefined));
	const addKitEffect = useLsdjStore((state) => state.addKitEffect);
	const addSampleEffect = useLsdjStore((state) => state.addSampleEffect);
	const { isVisible, position, items, showContextMenu, hideContextMenu, handleItemClick } = useContextMenu();

	const effects = sampleKey ? sample?.effects || [] : kit?.effects || [];
	const isEmpty = effects.length === 0;

	const handleAddEffect = (type: string) => {
		const effectInstance = createEffectInstance(type);
		if (!effectInstance) {
			console.error(`Failed to create effect instance of type: ${type}`);
			return;
		}

		const newEffect: ILsdjKitEffect = {
			id: 0,
			key: generateKey(),
			effect: effectInstance,
		};

		if (sampleKey) {
			addSampleEffect(kitKey, sampleKey, newEffect);
		} else {
			addKitEffect(kitKey, newEffect);
		}

		if (!isExpanded) {
			onToggle(true);
		}

		onChange();
	};

	const handleContextMenu = useCallback(
		(event: React.MouseEvent) => {
			event.preventDefault();
			event.stopPropagation();

			const menuItems = ALL_EFFECTS.map((effect) => {
				return {
					id: effect.type,
					label: effect.name,
					disabled: false,
					onClick: () => {
						handleAddEffect(effect.type);
					},
				};
			});

			showContextMenu(event, menuItems);
		},
		[showContextMenu, handleAddEffect],
	);

	return (
		<div className="mt-2 overflow-hidden rounded-sm border border-gray-700">
			<div
				className={`hover:bg-gray-750 flex items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200 ${
					isEmpty ? 'cursor-default' : 'cursor-pointer'
				}`}
				onClick={() => !isEmpty && onToggle()}
			>
				<div className="flex items-center">
					<div className="mr-2 flex h-3 w-3 items-center justify-center">
						{!isEmpty && isExpanded ? (
							<div className="h-0 w-0 border-t-6 border-r-4 border-l-4 border-t-white border-r-transparent border-l-transparent" />
						) : (
							<div className={`h-0 w-0 border-t-4 border-b-4 border-l-6 border-t-transparent border-b-transparent ${
								isEmpty ? 'border-l-gray-500' : 'border-l-white'
							}`} />
						)}
					</div>
					<span className="font-medium text-white">Effects</span>
				</div>
				<button
					className="rounded-sm px-2 py-1 text-sm font-bold text-green-400 transition-colors duration-200 hover:bg-green-600/20 hover:text-green-300"
					onClick={handleContextMenu}
					title="Add Effect"
				>
					+
				</button>
			</div>

			{!isEmpty && isExpanded && (
				<div className="bg-gray-900 p-2">
					{effects.map((effect, index) => (
						<div key={effect.key}>
							{index > 0 && <hr className="my-1 border-gray-600" />}
							<LsdjEffectEditor
								effect={effect}
								kitKey={kitKey}
								key={kitKey}
								sampleKey={sampleKey}
								onParameterChanged={onParameterChanged}
							/>
						</div>
					))}
				</div>
			)}
			<ContextMenu
				items={items}
				position={position}
				visible={isVisible}
				onClose={hideContextMenu}
				onItemClick={handleItemClick}
			/>
		</div>
	);
};
