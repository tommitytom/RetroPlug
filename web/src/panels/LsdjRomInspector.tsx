import { useCallback, useEffect, useRef, useState } from "react";

import { FileDropZone } from "../components/FileDropZone";
import { LsdjKitEditor } from "../components/LsdjKit";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import { useProject, useSystemMemoryVersion } from "../hooks/RetroPlugHooks";
import type { Uint8Buffer } from "../native/RetroPlug";
import type { LsdjKit } from "../types/LsdjTypes";
import { fromUint8Array, vectorToArray } from "../utils/NativeUtil";
import type { SystemId } from "../wrapper/Project";
import { MemoryType } from "../wrapper/System";

export const LsdjRomInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const project = useProject();
	const romVersion = useSystemMemoryVersion(systemId, MemoryType.Rom);
	const [expandedKits, setExpandedKits] = useState<Set<number>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);
	const [isDragOver, setIsDragOver] = useState(false);
	const containerRef = useRef<HTMLDivElement>(null);
	const [version, setVersion] = useState<number>(0);
	const [romKits, setRomKits] = useState<LsdjKit[]>([]);

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

		lsdj.addKitComponent(systemId, {
			kitId: -1,
			name: "KIT",
			samples
		});

		console.log(project.serialize());

		setVersion(prev => prev + 1);
	}, []);

	// Use native DOM events for more reliable drag and drop
	useEffect(() => {
		const container = containerRef.current;
		if (!container) return;

		let dragCounter = 0;

		const handleDragEnter = (e: DragEvent) => {
			e.preventDefault();
			e.stopPropagation();
			dragCounter++;
			console.log('Native drag enter detected', dragCounter);

			if (e.dataTransfer?.types.includes('Files')) {
				console.log('Files detected, showing drop zone');
				setIsDragOver(true);
			}
		};

		const handleDragLeave = (e: DragEvent) => {
			e.preventDefault();
			e.stopPropagation();
			dragCounter--;
			console.log('Native drag leave detected', dragCounter);

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
		const lsdj = project.getLsdjController();
		const descs = lsdj.getKitDescs(systemId);

		const kits = vectorToArray<LsdjKit>(descs);
		setRomKits(kits);

		descs.delete();
		lsdj.delete();
	}, [project, romVersion, version]);

	const toggleAllKits = useCallback(() => {
		if (allExpanded) {
			setExpandedKits(new Set());
			setAllExpanded(false);
		} else {
			setExpandedKits(new Set(romKits.map(kit => kit.id)));
			setAllExpanded(true);
		}
	}, [allExpanded, romKits]);

	// Update allExpanded state based on individual kit states
	useEffect(() => {
		if (romKits.length === 0) {
			setAllExpanded(false);
			return;
		}
		const allKitsExpanded = romKits.every(kit => expandedKits.has(kit.id));
		setAllExpanded(allKitsExpanded);
	}, [expandedKits, romKits]);

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
							<button
								onClick={toggleAllKits}
								className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg transition-colors duration-200"
							>
								{allExpanded ? "Collapse All" : "Expand All"}
							</button>
						</div>
					)}
					<div className="space-y-2">
						{romKits.map((kit) => (
							<LsdjKitEditor
								key={`${kit.name}-${kit.id}`}
								name={kit.name.valueOf() as string}
								id={kit.id}
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
