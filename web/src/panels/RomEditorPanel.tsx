/*
import React, { useCallback, useEffect, useState } from "react";

import { FileDropZone } from "../components/FileDropZone";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { NativeLsdjRom } from "../native/RetroPlug";
import {
	LSDJ_KIT_COUNT
} from "../wrapper/Lsdj";

import "../styles/RomEditorPanel.css";

export const RomEditorPanel: React.FC = () => {
	const { app, audioContext } = useRetroPlug();
	const [rom, setRom] = useState<NativeLsdjRom | null>(null);
	const [kits, setKits] = useState<IIndexedKit[]>([]);
	const [expandedKits, setExpandedKits] = useState<Set<number>>(new Set());
	const [allExpanded, setAllExpanded] = useState(false);

	const handleFileDrop = useCallback(
		async (files: FileList) => {
			if (!app) return;

			for (let i = 0; i < files.length; i++) {
				const file = files[i];
				if (file.name.endsWith(".gb")) {
					const fileData = new Uint8Array(await file.arrayBuffer());
					const accessor = new app.module!.MemoryAccessor(
						app.module!.NativeMemoryType.Rom,
						fromUint8Array(app.module!, fileData),
						0,
					);
					const rom = new app.module!.NativeLsdjRom(accessor);
					setRom(rom);
					return;
				}
			}
		},
		[app],
	);

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

	const toggleAllKits = useCallback(() => {
		if (allExpanded) {
			setExpandedKits(new Set());
			setAllExpanded(false);
		} else {
			setExpandedKits(new Set(kits.map(kit => kit.id)));
			setAllExpanded(true);
		}
	}, [allExpanded, kits]);

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

		setKits(indexedKits);
		// Reset expanded state when ROM changes
		setExpandedKits(new Set());
		setAllExpanded(false);

		//return () => rom.delete();
	}, [rom]);

	// Update allExpanded state based on individual kit states
	useEffect(() => {
		if (kits.length === 0) {
			setAllExpanded(false);
			return;
		}
		const allKitsExpanded = kits.every(kit => expandedKits.has(kit.id));
		setAllExpanded(allKitsExpanded);
	}, [expandedKits, kits]);

	return (
		<div className="w-full h-full bg-gray-900">
			{rom === null ? (
				<FileDropZone
					onFileDrop={handleFileDrop}
					title="Drag and drop ROM files here"
					subtitle="Drop files here!"
					supportedFormats="Supported formats: .gb, .gbc"
				/>
			) : (
				<div className="w-full h-full overflow-y-auto">
					<div className="min-h-full py-4 px-3">
						{kits.length > 0 && (
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
							{kits.map((kit) => (
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
			)}
		</div>
	);
};
*/