import React, { useCallback, useContext, useRef, useState } from 'react';

import { useKitChanges, useKitListChanges, useLsdjStore, useKitList } from '../../hooks/LsdjStoreHooks';
import type { ILsdjKit } from '../../types/LsdjTypes';
import { sortKits } from '../../utils/LsdjUtil';
import { LsdjKitEditor } from './LsdjKitEditor';
import type { LsdjStore } from '../../stores/LsdjStore';

import '../../styles/RomEditorPanel.css';
import { LsdjStoreContext } from '../../contexts/LsdjStoreContext';

interface LsdjRomEditorProps {
}

export const LsdjRomEditor: React.FC<LsdjRomEditorProps> = () => {
	// Only subscribe to the kits array with optimized equality checking
	const kits = useKitList();
	const addKit = useLsdjStore((state) => state.addKit);
	const updateKit = useLsdjStore((state) => state.updateKit);
	const removeKit = useLsdjStore((state) => state.removeKit);

	const containerRef = useRef<HTMLDivElement>(null);
	const [sortBy, setSortBy] = useState<'index' | 'editable' | 'mostUsed'>('editable');
	const [hideUnused, setHideUnused] = useState(false);
	const [expandedKits, setExpandedKits] = useState<Set<string>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);

	const sortedRomKits = sortKits(kits, sortBy);

	const toggleKit = useCallback((kitKey: string) => {
		setExpandedKits((prev) => {
			const newSet = new Set(prev);
			if (newSet.has(kitKey)) {
				newSet.delete(kitKey);
			} else {
				newSet.add(kitKey);
			}
			return newSet;
		});
	}, []);

	const toggleAllKits = useCallback(() => {
		if (allExpanded) {
			setExpandedKits(new Set());
			setAllExpanded(false);
		} else {
			setExpandedKits(new Set(sortedRomKits.map((kit) => kit.key)));
			setAllExpanded(true);
		}
	}, [allExpanded, sortedRomKits]);

	return (
		<div ref={containerRef} className="relative h-full w-full bg-gray-900">
			<div className="h-full w-full overflow-y-auto">
				<div className="min-h-full px-3 py-4">
					{kits.length > 0 && (
						<div className="mb-4 flex items-center justify-between">
							<h1 className="text-2xl font-bold text-white">Kits</h1>
							<div className="flex items-center gap-4">
								<div className="flex items-center gap-2">
									<label htmlFor="sort-select" className="text-sm font-medium text-white">
										Sort by:
									</label>
									<select
										id="sort-select"
										value={sortBy}
										onChange={(e) => setSortBy(e.target.value as typeof sortBy)}
										className="rounded border border-gray-600 bg-gray-700 px-3 py-1 text-sm text-white focus:border-blue-500 focus:outline-none"
									>
										<option value="index">Index</option>
										<option value="editable">Editable</option>
										<option value="mostUsed">Most Used</option>
									</select>
								</div>
								<div className="flex items-center gap-2">
									<input
										type="checkbox"
										id="hide-unused"
										checked={hideUnused}
										onChange={(e) => setHideUnused(e.target.checked)}
										className="h-4 w-4 rounded border-gray-600 bg-gray-700 text-blue-600 focus:ring-2 focus:ring-blue-500"
									/>
									<label htmlFor="hide-unused" className="text-sm font-medium text-white">
										Hide unused
									</label>
								</div>
								<button
									onClick={toggleAllKits}
									className="rounded border border-gray-600 bg-gray-700 px-3 py-1 text-sm text-white transition-colors duration-200 hover:border-gray-500 hover:bg-gray-600"
								>
									{allExpanded ? 'Collapse All' : 'Expand All'}
								</button>
							</div>
						</div>
					)}
					<div className="space-y-2">
						{sortedRomKits.map(
							(kit) =>
								kit.key && (
									<LsdjKitEditor
										isExpanded={expandedKits.has(kit.key)}
										onToggle={() => toggleKit(kit.key)}
										kitKey={kit.key}
										key={kit.key}
									/>
								),
						)}
					</div>
				</div>
			</div>
		</div>
	);
};
