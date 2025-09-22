import { useCallback, useEffect, useState } from 'react';

import { useProject } from '../../hooks/RetroPlugHooks';
import { LSDJ_SYNTH_COUNT } from '../../types/LsdjTypes';
import { type SystemId } from '../../utils/NativeUtil';
import { MemoryType } from '../../wrapper/System';
import { LsdjSynthView } from './LsdjSynthView';

export const LsdjSavMemoryEditor: React.FC = () => {
	const project = useProject();
	const [systemId, setSystemId] = useState<SystemId | null>(null);
	const [expandedSynths, setExpandedSynths] = useState<Set<number>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);

	useEffect(() => {
		const ids = project.getSystemIds().sort((a, b) => a - b); // Sort system IDs in ascending order
		if (ids.length === 0) {
			setSystemId(null);
			return;
		}

		const system = ids[0];
		setSystemId(system);

		project.subscribeToMemory(system, MemoryType.Sram);

		return () => {
			project.unsubscribeFromMemory(system, MemoryType.Sram);
		};
	}, [project]);

	const handleSynthToggle = useCallback((synthId: number, value?: boolean) => {
		setExpandedSynths((prev) => {
			const newSet = new Set(prev);

			if (value === undefined) {
				if (newSet.has(synthId)) {
					newSet.delete(synthId);
				} else {
					newSet.add(synthId);
				}
			} else {
				if (value) {
					newSet.add(synthId);
				} else {
					newSet.delete(synthId);
				}
			}

			return newSet;
		});
	}, []);

	const toggleAllSynths = useCallback(() => {
		if (allExpanded) {
			setExpandedSynths(new Set());
			setAllExpanded(false);
		} else {
			const allSynthIds = Array.from({ length: LSDJ_SYNTH_COUNT }, (_, i) => i);
			setExpandedSynths(new Set(allSynthIds));
			setAllExpanded(true);
		}
	}, [allExpanded]);

	return systemId !== null ? (
		<div className="relative h-full w-full bg-gray-900">
			<div className="h-full w-full overflow-y-auto">
				<div className="min-h-full px-2 py-1">
					<div className="mb-4 flex items-center justify-between">
						<h1 className="text-2xl font-bold text-white">Synths</h1>
						<div className="flex items-center gap-4">
							<button
								onClick={toggleAllSynths}
								className="rounded border border-gray-600 bg-gray-700 px-3 py-1 text-sm text-white transition-colors duration-200 hover:border-gray-500 hover:bg-gray-600"
							>
								{allExpanded ? 'Collapse All' : 'Expand All'}
							</button>
						</div>
					</div>
					<div className="space-y-2">
						{Array.from({ length: LSDJ_SYNTH_COUNT }).map((_, index) => (
							<LsdjSynthView
								key={index}
								system={systemId}
								synthId={index}
								isExpanded={expandedSynths.has(index)}
								onToggle={(value) => handleSynthToggle(index, value)}
							/>
						))}
					</div>
				</div>
			</div>
		</div>
	) : (
		<></>
	);
};
