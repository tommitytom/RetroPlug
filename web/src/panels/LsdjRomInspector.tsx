import { useCallback, useEffect, useRef, useState } from "react";

import { FileDropZone } from "../components/FileDropZone";
import { useProject, useSystemMemoryVersion } from "../hooks/RetroPlugHooks";
import type { Uint8Buffer } from "../native/RetroPlug";
import { LSDJ_KIT_COUNT, type IIndexedLsdjKit, type ILsdjKit } from "../types/LsdjTypes";
import { type SystemId } from "../utils/NativeUtil";
import { kitIsEditable } from "../utils/LsdjUtil";

import { MemoryType } from "../wrapper/System";

export const LsdjRomInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const project = useProject();
	const romVersion = useSystemMemoryVersion(systemId, MemoryType.Rom);
	const savVersion = useSystemMemoryVersion(systemId, MemoryType.Sram);
	const containerRef = useRef<HTMLDivElement>(null);
	const [expandedKits, setExpandedKits] = useState<Set<number>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);
	const [isDragOver, setIsDragOver] = useState(false);
	const [version, setVersion] = useState<number>(0);
	const [romKits, setRomKits] = useState<IIndexedLsdjKit[]>([]);
	const [sortBy, setSortBy] = useState<'index' | 'editable' | 'mostUsed'>('editable');
	const [hideUnused, setHideUnused] = useState(false);

	const toggleKit = useCallback((kitId: number) => {
		setExpandedKits(prev => {
			const newSet = new Set(prev);
			if (newSet.has(kitId)) {
				newSet.delete(kitId);
			} else {
				newSet.add(kitId);
			}
			return newSet;
		});
	}, []);

	const getKitData = useCallback((kitId: number): Uint8Buffer | null => {
		if (!project) return null;

		const kitData = project.lsdj.getKitData(systemId, kitId);
		if (!kitData) return null;

		if (kitData.size() === 0) {
			kitData.delete();
			return null;
		}

		return kitData;
	}, [project]);

	const sortKits = useCallback((kits: IIndexedLsdjKit[], sortMethod: typeof sortBy): IIndexedLsdjKit[] => {
		let kitsCopy = [...kits];

		// Filter out unused kits if hideUnused is enabled
		if (hideUnused) {
			//kitsCopy = kitsCopy.filter(kit => kit.useCount > 0);
		}

		switch (sortMethod) {
			case 'index':
				// Fill in gaps
				return kitsCopy.sort((a, b) => a.id - b.id);
			case 'editable':
				return kitsCopy.sort((a, b) => {
					// Editable kits first, then non-editable
					if (kitIsEditable(a) && !kitIsEditable(b)) return -1;
					if (!kitIsEditable(a) && kitIsEditable(b)) return 1;
					// If both have same editable status, sort by index
					return a.id - b.id;
				});
			/*case 'mostUsed':
				return kitsCopy.sort((a, b) => {
					// Sort by use count in descending order (most used first)
					if (a.useCount !== b.useCount) {
						return b.useCount - a.useCount;
					}
					// If use counts are equal, sort by index
					return a.id - b.id;
				});*/
			default:
				return kitsCopy;
		}
	}, [hideUnused]);

	const sortedRomKits = sortKits(romKits, sortBy);

	const handleFileDrop = useCallback(async (files: FileList) => {
		if (!project) return;

		const lsdj = project.lsdj;

		const kit: ILsdjKit = {
			name: "KIT",
			samples: [],
			effects: [{
				type: "GainEffect"
			}]
		};

		for (const file of files) {
			if (!file.name.match(/\.(wav|aiff?|mp3|ogg)$/i)) {
				console.warn('Invalid file type:', file.name);
				continue;
			}

			kit.samples!.push({
				name: file.name.substring(0, 3).toUpperCase(),
				path: file.name,
				offset: 0,
				length: 0,
				data: new Uint8Array(await file.arrayBuffer())
			});
		}

		const kitId = lsdj.getNextEmptyKit(systemId);
		lsdj.setKit(systemId, kitId, kit);
		project.resetSystem(systemId, true);

		setVersion(prev => prev + 1);
	}, [project]);

	useEffect(() => {
		if (!project) return;

		const lsdj = project.lsdj;
		const kits = lsdj.getKits(systemId);
		console.log(kits);

		const indexedKits: IIndexedLsdjKit[] = [];
		for (let i = 0; i < LSDJ_KIT_COUNT; i++) {
			if (kits[i]) {
				indexedKits.push({ ...kits[i], id: i, kitType: getKitType(kits[i]) });
			} else {
				indexedKits.push({ id: i });
			}
		}

		if (!deepEqual(indexedKits, romKits)) {
			setRomKits(indexedKits);
		}
	}, [project, romVersion, version, savVersion, romKits, setRomKits]);

	const toggleAllKits = useCallback(() => {
		if (allExpanded) {
			setExpandedKits(new Set());
			setAllExpanded(false);
		} else {
			setExpandedKits(new Set(sortedRomKits.map(kit => kit.id)));
			setAllExpanded(true);
		}
	}, [allExpanded, sortedRomKits]);

	// Update allExpanded state based on individual kit states
	useEffect(() => {
		if (sortedRomKits.length === 0) {
			setAllExpanded(false);
			return;
		}
		const allKitsExpanded = sortedRomKits.every(kit => expandedKits.has(kit.id));
		setAllExpanded(allKitsExpanded);
	}, [expandedKits, sortedRomKits]);

	// Use native DOM events for more reliable drag and drop
	useEffect(() => {
		const container = containerRef.current;
		if (!container) return;

		let dragCounter = 0;

		const handleDragEnter = (e: DragEvent) => {
			e.preventDefault();
			e.stopPropagation();
			dragCounter++;

			if (e.dataTransfer?.types.includes('Files')) {
				setIsDragOver(true);
			}
		};

		const handleDragLeave = (e: DragEvent) => {
			e.preventDefault();
			e.stopPropagation();
			dragCounter--;

			if (dragCounter === 0) {
				setIsDragOver(false);
			}
		};

		const handleDragOver = (e: DragEvent) => {
			e.preventDefault();
			e.stopPropagation();
		};

		const handleDrop = (e: DragEvent) => {
			e.preventDefault();
			e.stopPropagation();
			dragCounter = 0;
			setIsDragOver(false);

			const files = e.dataTransfer?.files;
			if (files && files.length > 0) {
				handleFileDrop(files);
			}
		};

		container.addEventListener('dragenter', handleDragEnter);
		container.addEventListener('dragleave', handleDragLeave);
		container.addEventListener('dragover', handleDragOver);
		container.addEventListener('drop', handleDrop);

		return () => {
			container.removeEventListener('dragenter', handleDragEnter);
			container.removeEventListener('dragleave', handleDragLeave);
			container.removeEventListener('dragover', handleDragOver);
			container.removeEventListener('drop', handleDrop);
		};
	}, [handleFileDrop]);

	return (
		<div
			ref={containerRef}
			className="w-full h-full bg-gray-900 relative"
		>
			{isDragOver && (
				<div className="absolute inset-0 z-50 bg-black bg-opacity-50 flex items-center justify-center">
					<div className="w-4/5 h-4/5">
						<FileDropZone
							onFileDrop={handleFileDrop}
							title="Drop audio samples here"
							subtitle="Drop to add samples to kit"
							supportedFormats="Supported formats: .wav, .aiff, .mp3, .ogg"
						/>
					</div>
				</div>
			)}
			<div className="w-full h-full overflow-y-auto">
				<div className="min-h-full py-4 px-3">
					{romKits.length > 0 && (
						<div className="mb-4 flex justify-between items-center">
							<h1 className="text-2xl font-bold text-white">Kits</h1>
							<div className="flex items-center gap-4">
								<div className="flex items-center gap-2">
									<label htmlFor="sort-select" className="text-white text-sm font-medium">
										Sort by:
									</label>
									<select
										id="sort-select"
										value={sortBy}
										onChange={(e) => setSortBy(e.target.value as typeof sortBy)}
										className="px-3 py-1 bg-gray-700 text-white rounded border border-gray-600 focus:border-blue-500 focus:outline-none text-sm"
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
										className="w-4 h-4 text-blue-600 bg-gray-700 border-gray-600 rounded focus:ring-blue-500 focus:ring-2"
									/>
									<label htmlFor="hide-unused" className="text-white text-sm font-medium">
										Hide unused
									</label>
								</div>
								<button
									onClick={toggleAllKits}
									className="px-3 py-1 bg-gray-700 hover:bg-gray-600 text-white text-sm rounded border border-gray-600 hover:border-gray-500 transition-colors duration-200"
								>
									{allExpanded ? "Collapse All" : "Expand All"}
								</button>
							</div>
						</div>
					)}
					<div className="space-y-2">
						{sortedRomKits.map((kit) => (
							<LsdjKitEditor
								key={`${kit.name}-${kit.id}`}
								name={kit.name || 'empty'}
								id={kit.id}
								kitData={expandedKits.has(kit.id) ? getKitData(kit.id) : null}
								editable={kitIsEditable(kit)}
								isExpanded={expandedKits.has(kit.id)}
								onToggle={() => toggleKit(kit.id)}
							/>
						))}
					</div>
				</div>
			</div>
		</div>
	);
};
