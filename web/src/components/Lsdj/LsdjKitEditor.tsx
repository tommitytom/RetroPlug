import React, { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';

import { EditableText } from '../../components/EditableText';
import { WaveView } from '../../components/WaveView';
import type { SliceInfo } from '../../components/WaveViewTypes';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { useKit, useLsdjStore } from '../../hooks/LsdjStoreHooks';
import type { ILsdjKitData, ILsdjKitSample } from '../../types/LsdjTypes';
import { GAMEBOY_SAMPLE_RATE, KitType } from '../../types/LsdjTypes';
import { EnumUtils } from '../../utils/EnumUtil';
import { downloadUint8Array, sanitizeFilename } from '../../utils/FileUtil';
import { extractSampleData, generateKey, getKitType, sanitizeKitName } from '../../utils/LsdjUtil';
import { fromUint8Array } from '../../utils/NativeUtil';
import { playSample } from '../../wrapper/Lsdj';
import { LsdjEffectList } from './LsdjEffectList';
import { LsdjWaveView } from './LsdjWaveView';

interface LsdjKitEditorProps {
	kitKey: string;
	isExpanded: boolean;
	onToggle: (value?: boolean) => void;
	onFileDropped?: (filePath: string, file?: File) => Promise<void>;
	onError?: (error: string, operation?: string) => void;
}

export const LsdjKitEditor: React.FC<LsdjKitEditorProps> = ({
	kitKey,
	isExpanded,
	onToggle,
	onFileDropped,
	onError,
}) => {
	const { module, fileSystem, audioContext } = useRetroPlug();
	const kit = useKit(kitKey)!;
	const system = useLsdjStore((state) => state.systemId);
	const addKit = useLsdjStore((state) => state.addKit);
	const updateKit = useLsdjStore((state) => state.updateKit);
	const fetchKitData = useLsdjStore((state) => state.fetchKitData);
	const removeKit = useLsdjStore((state) => state.removeKit);
	const renameKit = useLsdjStore((state) => state.renameKit);
	const addSample = useLsdjStore((state) => state.addSample);
	const removeSample = useLsdjStore((state) => state.removeSample);
	const [isEditing, setIsEditing] = useState(false);
	const [tempName, setTempName] = useState(kit?.name || '');

	const [isEffectEditorOpen, setIsEffectEditorOpen] = useState(false);

	const [isDragOver, setIsDragOver] = useState(false);

	console.assert(!!kit);
	//console.log(kit);

	// Helper function to get color classes based on kit type
	const getKitTypeColorClasses = (kitType: KitType): string => {
		switch (kitType) {
			case KitType.Editable:
				return 'bg-green-900/30 text-green-400';
			case KitType.Patched:
				return 'bg-blue-900/30 text-blue-400';
			case KitType.Rom:
			default:
				return 'bg-gray-700 text-gray-400';
		}
	};

	const kitType = getKitType(kit);



	const handleRename = () => {
		renameKit(kitKey, tempName);
		setIsEditing(false);
	};

	const handleRemoveSample = (sampleKey: string) => {
		removeSample(kitKey, sampleKey);
	};

	const onNameChange = (newName: string) => {
		renameKit(kitKey, newName);
		//patchSystemKit(kitKey);
	};



	const handleDeleteKit = () => {
		removeKit(kitKey);
	};

	const handleDownloadClick = useCallback(
		(e: React.MouseEvent) => {
			e.preventDefault();
			e.stopPropagation();

			if (kit.data) {
				try {
					const filename = `${sanitizeFilename(kit.name)}.kit`;
					downloadUint8Array(kit.data, filename);
				} catch (error) {
					console.error('Download failed:', error);
				}
			} else {
				console.log('No kitData available for download');
			}
		},
		[kit.data],
	);

	const handleEffectToggle = (expanded?: boolean) => {
		if (expanded === undefined) {
			setIsEffectEditorOpen(!isEffectEditorOpen);
		} else {
			setIsEffectEditorOpen(expanded);
		}
	};

	// Drag and drop handlers
	const handleDragOver = useCallback((event: React.DragEvent) => {
		event.preventDefault();
		event.dataTransfer.dropEffect = 'copy';
		setIsDragOver(true);
	}, []);

	const handleDragLeave = useCallback((event: React.DragEvent) => {
		// Only clear if we're actually leaving the element
		if (!event.currentTarget.contains(event.relatedTarget as Node)) {
			setIsDragOver(false);
		}
	}, []);

	function getSampleNameFromPath(path: string): string {
		return path.split('/').pop()?.split('.').shift()?.slice(0, 3)?.toUpperCase() || "UNK";
	}

	async function sanitizeSamples(paths: string[]): Promise<ILsdjKitSample[]> {
		const samples: ILsdjKitSample[] = [];

		for (let i = 0; i < paths.length; i++) {
			const path = paths[i];
			const data = new Uint8Array(await fileSystem.readPath(path));

			samples.push({
				key: generateKey(),
				name: getSampleNameFromPath(path),
				offset: 0,
				length: 0,
				path,
				data,
				effects: [],
			});
		}

		return samples;
	}

	async function handleFileDrop(paths: string[]) {
		if (paths.length === 1 && paths[0].endsWith('.kit')) {
			console.log('Patching kit');
			updateKit(kitKey, {
				path: paths[0],
				effects: undefined,
				samples: undefined,
			});
			//patchSystemKit(kitKey);
			return;
		}

		const kitType = getKitType(kit);
		const samples = await sanitizeSamples(paths);

		switch (kitType) {
			case KitType.Editable:
				console.log('Adding samples');
				for (const sample of samples) {
					addSample(kitKey, sample);
				}

				break;
			case KitType.Patched:
			case KitType.Rom:
				console.log('Adding dynamic kit');
				updateKit(kitKey, {
					name: 'KIT',
					effects: [],
					samples
				});
				break;
		}

		//patchSystemKit(kitKey);

		onToggle(true);
	}

	const handleDrop = useCallback(
		async (event: React.DragEvent) => {
			event.preventDefault();
			setIsDragOver(false);

			try {
				// Handle files from external sources (browser file system)
				const files = Array.from(event.dataTransfer.files);
				if (files.length > 0) {
					for (const file of files) {
						if (file.name.endsWith('.kit') || file.name.endsWith('.wav') || file.name.endsWith('.sav')) {
							if (onFileDropped) {
								await onFileDropped(file.name, file);
							} else {
								// Default behavior
								console.log(`Dropped file: ${file.name}`);
							}
						}
					}
					return;
				}

				// Handle internal file tree drag (from FileExplorer)
				const filePath = event.dataTransfer.getData('text/plain');
				if (filePath) {
					if (onFileDropped) {
						await onFileDropped(filePath);
					} else {
						console.log(`Dropped file from tree: ${filePath}`);
						try {
							handleFileDrop([filePath]);
						} catch (ex) {
							console.error('Error handling file drop:', ex);
						}
					}
				}
			} catch (error) {
				const errorMessage = `Failed to process dropped file: ${error}`;
				console.error(errorMessage);
				if (onError) {
					onError(errorMessage, 'drop');
				}
			}
		},
		[onFileDropped, onError, kit],
	);

	const handleChange = () => {
		//patchSystemKit(kitKey);
	};

	return (
		<div
			className={`relative overflow-hidden rounded-sm border border-gray-700 transition-all duration-200 ${
				isDragOver ? 'border-blue-500 bg-blue-500/10 shadow-lg' : ''
			}`}
			onDragOver={handleDragOver}
			onDragLeave={handleDragLeave}
			onDrop={handleDrop}
		>
			{isDragOver && (
				<div className="absolute inset-0 z-10 flex items-center justify-center rounded-sm border-2 border-dashed border-blue-500 bg-blue-500/20">
					<div className="rounded bg-blue-600 px-3 py-2 text-sm font-medium text-white shadow-lg">
						Drop file here to load into kit
					</div>
				</div>
			)}
			<div
				className={`hover:bg-gray-750 flex cursor-pointer items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200 ${
					isDragOver ? 'bg-blue-600/20' : ''
				}`}
				onClick={() => onToggle()}
			>
				<div className="flex items-center">
					<div className="mr-2 flex h-3 w-3 items-center justify-center">
						{isExpanded ? (
							<div className="h-0 w-0 border-t-6 border-r-4 border-l-4 border-t-white border-r-transparent border-l-transparent" />
						) : (
							<div className="h-0 w-0 border-t-4 border-b-4 border-l-6 border-t-transparent border-b-transparent border-l-white" />
						)}
					</div>
					<span className="font-mono font-medium text-white">{kit.id.toString(16).padStart(2, '0').toUpperCase()}</span>
					<span className="mx-1 font-medium">-</span>
					<EditableText
						value={kit.name}
						onChange={onNameChange}
						className="font-medium text-white"
						maxLength={6}
						validator={sanitizeKitName}
						title="Click to edit kit name"
					/>
				</div>
				<div className="flex items-center gap-2">
					{/*(usageCount || 0) > 0 && (
						<span className={`px-2 py-1 rounded text-xs text-gray-400 bg-gray-700`}>
							Usage Count: {usageCount || 0}
						</span>
					)*/}
					<span className={`rounded px-2 py-1 text-xs ${getKitTypeColorClasses(kitType)}`}>
						{EnumUtils.enumToString(KitType, getKitType(kit))}
					</span>
					<button
						className={`rounded-sm p-1 transition-colors duration-200 ${
							kit.data
								? 'text-blue-400 hover:bg-blue-600/20 hover:text-blue-300'
								: 'text-gray-500 hover:bg-gray-600/20 hover:text-gray-400'
						}`}
						onClick={handleDownloadClick}
						title={kit.data ? 'Download kit' : 'Kit data not available'}
					>
						<svg
							width="12"
							height="12"
							viewBox="0 0 24 24"
							fill="none"
							stroke="currentColor"
							strokeWidth="2"
							strokeLinecap="round"
							strokeLinejoin="round"
						>
							<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
							<polyline points="7,10 12,15 17,10"></polyline>
							<line x1="12" y1="15" x2="12" y2="3"></line>
						</svg>
					</button>
					<button
						className="rounded-sm p-1 text-red-400 transition-colors duration-200 hover:bg-red-600/20 hover:text-red-300"
						onClick={(e) => {
							e.stopPropagation();
							handleDeleteKit();
						}}
						title="Delete kit"
					>
						<svg
							width="12"
							height="12"
							viewBox="0 0 24 24"
							fill="none"
							stroke="currentColor"
							strokeWidth="2"
							strokeLinecap="round"
							strokeLinejoin="round"
						>
							<polyline points="3,6 5,6 21,6"></polyline>
							<path d="m19,6v14a2,2 0 0,1-2,2H7a2,2 0 0,1-2-2V6m3,0V4a2,2 0 0,1,2-2h4a2,2 0 0,1,2,2v2"></path>
							<line x1="10" y1="11" x2="10" y2="17"></line>
							<line x1="14" y1="11" x2="14" y2="17"></line>
						</svg>
					</button>
				</div>
			</div>
			{isExpanded && (
				<div className={`bg-gray-900 ${kitType === KitType.Editable ? 'p-2' : 'px-2 pt-2 pb-1'}`}>
					<div className="mb-2">
						<LsdjWaveView system={system} kitId={kit.id} />
					</div>
					{kitType === KitType.Editable && (
						<div className="mb-2">
							<LsdjEffectList
								kitKey={kitKey}
								isExpanded={isEffectEditorOpen}
								onToggle={(expanded) => handleEffectToggle(expanded)}
								title="Sample Effects"
								key={kitKey}
								onChange={handleChange}
								onParameterChanged={() => handleChange()}
							/>
						</div>
					)}
				</div>
			)}
		</div>
	);
};
