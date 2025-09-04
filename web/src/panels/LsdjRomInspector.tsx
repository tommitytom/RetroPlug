import { useCallback, useEffect, useState } from "react";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import { useProject, useSystemMemory } from "../hooks/RetroPlugHooks";
import type { NativeLsdjKit, NativeLsdjKitDesc, NativeLsdjRom } from "../native/RetroPlug";
import { SystemId } from "../wrapper/Project";
import { MemoryType } from "../wrapper/System";
import { LSDJ_KIT_COUNT } from "../wrapper/Lsdj";
import { LsdjKit } from "../components/LsdjKit";
import { FileDropZone } from "../components/FileDropZone";

interface IIndexedKit {
	id: number;
	name: string;
	kit: NativeLsdjKit;
}

export const LsdjRomInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const { app, audioContext } = useRetroPlug();
	const project = useProject();
	const romData = useSystemMemory(systemId, MemoryType.Rom);
	const [rom, setRom] = useState<NativeLsdjRom | null>(null);
	const [romKits, setRomKits] = useState<IIndexedKit[]>([]);
	const [expandedKits, setExpandedKits] = useState<Set<number>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);
	const [isDragOver, setIsDragOver] = useState(false);

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

	const handleFileDrop = useCallback((files: FileList) => {
		// Placeholder callback for handling dropped audio files
		console.log('Files dropped:', files);
		// TODO: Implement audio sample import functionality
	}, []);

	const handleDragEnter = useCallback((e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(true);
	}, []);

	const handleDragLeave = useCallback((e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(false);
	}, []);

	const handleDragOver = useCallback((e: React.DragEvent) => {
		e.preventDefault();
	}, []);

	useEffect(() => {
		if (!project) return;
		const lsdj = project.getLsdjController();
		const descs = lsdj.getKitDescs(systemId);

		const allKits: NativeLsdjKitDesc[] = [];
		for (let i = 0; i < descs.size(); ++i) {
			allKits.push(descs.get(i)!);
		}

		descs.delete();
		lsdj.delete();

		console.log(allKits);
	}, [project, romData]);

	const toggleAllKits = useCallback(() => {
		if (allExpanded) {
			setExpandedKits(new Set());
			setAllExpanded(false);
		} else {
			setExpandedKits(new Set(romKits.map(kit => kit.id)));
			setAllExpanded(true);
		}
	}, [allExpanded, romKits]);

	useEffect(() => {
		if (!app || !romData) return;
		const module = app.module!;
		setRom(new module.NativeLsdjRom(romData));
	}, [app, romData]);

	useEffect(() => {
		if (!rom) return;

		const indexedKits: IIndexedKit[] = [];

		for (let i = 0; i < LSDJ_KIT_COUNT; ++i) {
			if (!rom.kitIsEmpty(i)) {
				const kit = rom.getKit(i);

				if (kit && kit.isValid) {
					indexedKits.push({ id: i, name: kit.getName(), kit });
				}
			}
		}

		setRomKits(indexedKits);
		// Reset expanded state when ROM changes
		setExpandedKits(new Set());
		setAllExpanded(false);

		//return () => rom.delete();
	}, [rom]);

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
		<div className="w-full h-full bg-gray-900">
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
							<LsdjKit
								key={`${kit.name}-${kit.id}`}
								kit={kit}
								audioContext={audioContext}
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
