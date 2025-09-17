import React, { useCallback, useRef, useState } from 'react';

import { useKitList, useLsdjStore } from '../../hooks/LsdjStoreHooks';
import { sortKits } from '../../utils/LsdjUtil';
import { LsdjKitEditor } from './LsdjKitEditor';

import '../../styles/RomEditorPanel.css';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { AccessType, MemoryType } from '../../wrapper/System';
import { downloadUint8Buffer } from '../../utils/FileUtil';

interface LsdjRomEditorProps {}

export const LsdjRomEditor: React.FC<LsdjRomEditorProps> = () => {
	const kits = useKitList();
	const { module, project } = useRetroPlug();
	const systemId = useLsdjStore((state) => state.systemId); // Force re-render on store updates

	const containerRef = useRef<HTMLDivElement>(null);
	const [sortBy, setSortBy] = useState<'index' | 'editable'>('index');
	const [hideEmpty, setHideEmpty] = useState(false);
	const [expandedKits, setExpandedKits] = useState<Set<string>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);

	const sortedRomKits = sortKits(kits, sortBy);

	// Filter out empty kits if hideEmpty is enabled
	const filteredKits = hideEmpty
		? sortedRomKits.filter(kit => kit.kit.type !== 'empty')
		: sortedRomKits;

	const toggleKit = useCallback((kitKey: string, value?: boolean) => {
		setExpandedKits((prev) => {
			const newSet = new Set(prev);

			if (value === undefined) {
				if (newSet.has(kitKey)) {
					newSet.delete(kitKey);
				} else {
					newSet.add(kitKey);
				}
			} else {
				if (value) {
					newSet.add(kitKey);
				} else {
					newSet.delete(kitKey);
				}
			}

			return newSet;
		});
	}, []);

	const toggleAllKits = useCallback(() => {
		if (allExpanded) {
			setExpandedKits(new Set());
			setAllExpanded(false);
		} else {
			setExpandedKits(new Set(filteredKits.map((kit) => kit.key!)));
			setAllExpanded(true);
		}
	}, [allExpanded, filteredKits]);

	return (
		<div ref={containerRef} className="relative h-full w-full bg-gray-900">
			<div className="h-full w-full overflow-y-auto">
				<div className="min-h-full px-2 py-1">
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
									</select>
								</div>
								<div className="flex items-center gap-2">
									<input
										type="checkbox"
										id="hide-unused"
										checked={hideEmpty}
										onChange={(e) => setHideEmpty(e.target.checked)}
										className="h-4 w-4 rounded border-gray-600 bg-gray-700 text-blue-600 focus:ring-2 focus:ring-blue-500"
									/>
									<label htmlFor="hide-unused" className="text-sm font-medium text-white">
										Hide empty
									</label>
								</div>
								<button
									onClick={toggleAllKits}
									className="rounded border border-gray-600 bg-gray-700 px-3 py-1 text-sm text-white transition-colors duration-200 hover:border-gray-500 hover:bg-gray-600"
								>
									{allExpanded ? 'Collapse All' : 'Expand All'}
								</button>
								<button
									onClick={() => {
										const memory = project.getSystemMemory(systemId, MemoryType.Rom, AccessType.Read);
										if (memory && memory.getSize() > 0) {
											const romData = memory.getBuffer().clone();
											module.fixRomChecksum(romData);
											const romName = module.getRomName(romData);
											downloadUint8Buffer(romData, romName + '.gb');
										}
									}}
									className="rounded border border-green-600 bg-green-700 px-3 py-1 text-sm text-white transition-colors duration-200 hover:border-green-500 hover:bg-green-600"
								>
									Download
								</button>
							</div>
						</div>
					)}
					<div className="space-y-2">
						{filteredKits.map(
							(kit) =>
								kit.key && (
									<LsdjKitEditor
										isExpanded={expandedKits.has(kit.key)}
										onToggle={(value) => toggleKit(kit.key!, value)}
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
