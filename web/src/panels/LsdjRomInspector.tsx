import { useCallback, useEffect, useRef, useState } from "react";

import { FileDropZone } from "../components/FileDropZone";
import { LsdjKitEditor } from "../components/LsdjKit";
import { useProject, useSystemMemoryVersion } from "../hooks/RetroPlugHooks";
import type { NativeLsdjKitDesc, Uint8Buffer } from "../native/RetroPlug";
import type { LsdjKit } from "../types/LsdjTypes";
import { fromUint8Array, vectorToArray } from "../utils/NativeUtil";
import { deepEqual } from "../utils/StateUtil";
import type { SystemId } from "../wrapper/Project";
import { MemoryType } from "../wrapper/System";

class Timer {
	private _startTime: number | null = null;

	start() {
		this._startTime = performance.now();
	}

	stop(): number {
		if (this._startTime !== null) {
			const duration = performance.now() - this._startTime;
			this._startTime = null;
			return duration;
		}
		return 0;
	}
}

export const LsdjRomInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const project = useProject();
	const romVersion = useSystemMemoryVersion(systemId, MemoryType.Rom);
	const savVersion = useSystemMemoryVersion(systemId, MemoryType.Sram);
	const containerRef = useRef<HTMLDivElement>(null);
	const [expandedKits, setExpandedKits] = useState<Set<number>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);
	const [isDragOver, setIsDragOver] = useState(false);
	const [version, setVersion] = useState<number>(0);
	const [romKits, setRomKits] = useState<LsdjKit[]>([]);
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

		const lsdj = project.getLsdjController();
		const kitData = lsdj.getKitData(systemId, kitId);
		lsdj.delete();

		if (!kitData) return null;

		if (kitData.size() === 0) {
			kitData.delete();
			return null;
		}

		return kitData;
	}, [project]);

	const sortKits = useCallback((kits: LsdjKit[], sortMethod: typeof sortBy): LsdjKit[] => {
		let kitsCopy = [...kits];

		// Filter out unused kits if hideUnused is enabled
		if (hideUnused) {
			kitsCopy = kitsCopy.filter(kit => kit.useCount > 0);
		}

		switch (sortMethod) {
			case 'index':
				return kitsCopy.sort((a, b) => a.id - b.id);
			case 'editable':
				return kitsCopy.sort((a, b) => {
					// Editable kits first, then non-editable
					if (a.editable && !b.editable) return -1;
					if (!a.editable && b.editable) return 1;
					// If both have same editable status, sort by index
					return a.id - b.id;
				});
			case 'mostUsed':
				return kitsCopy.sort((a, b) => {
					// Sort by use count in descending order (most used first)
					if (a.useCount !== b.useCount) {
						return b.useCount - a.useCount;
					}
					// If use counts are equal, sort by index
					return a.id - b.id;
				});
			default:
				return kitsCopy;
		}
	}, [hideUnused]);

	const sortedRomKits = sortKits(romKits, sortBy);

	const handleFileDrop = useCallback(async (files: FileList) => {
		if (!project) return;

		const module = project.module;
		const lsdj = project.getLsdjController();
		const samples = new module.NativeLsdjSampleComponentVector();
		let i = 0;
		for (const file of files) {
			if (!file.name.match(/\.(wav|aiff?|mp3|ogg)$/i)) {
				console.warn('Invalid file type:', file.name);
				continue;
			}

			const sample = new module.NativeLsdjSampleComponent();
			sample.sampleId = i++;
		  	sample.length = 0;
		  	sample.offset = 0;
		  	sample.name = file.name.substring(0, 3).toUpperCase();
		  	sample.path = file.name;
		  	sample.setData(fromUint8Array(module, new Uint8Array(await file.arrayBuffer())));

			samples.push_back(sample);
		}

		const kit = new module.NativeLsdjKitComponent();
		kit.kitId = -1;
		kit.name = "KIT";
		kit.samples = samples;
		kit.effects = [{
			type: "GainEffect"
		}];

		lsdj.addKitComponent(systemId, kit);
		kit.delete();

		project.resetSystem(systemId, true);

		console.log(project.serialize());

		setVersion(prev => prev + 1);
	}, [project]);

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

	useEffect(() => {
		if (!project) return;

		const timer = new Timer();
		timer.start();

		const lsdj = project.getLsdjController();
		const descs = lsdj.getKitDescs(systemId, false);
		const kits = vectorToArray<NativeLsdjKitDesc>(descs) as LsdjKit[];

		if (!deepEqual(kits, romKits)) {
			setRomKits(kits);
		}

		descs.delete();
		lsdj.delete();

		const timeTaken = timer.stop();
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
								name={kit.name.valueOf() as string}
								id={kit.id}
								usageCount={kit.useCount}
								kitData={expandedKits.has(kit.id) ? getKitData(kit.id) : null}
								editable={kit.editable}
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
