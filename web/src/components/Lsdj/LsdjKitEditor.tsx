import React, { useCallback, useState } from 'react';

import { EditableText } from '../../components/EditableText';
import { DeleteIcon } from '../../components/DeleteIcon';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { createEffectInstance, type IEffect } from '../../effects/Effect';
import type { FileSystemWorkerAPI } from '../../filesystem/FileSystemWorker';
import { useKit, useLsdjStore } from '../../hooks/LsdjStoreHooks';
import { useProject } from '../../hooks/RetroPlugHooks';
import type { ILsdjEditableKit, ILsdjKitSample, ILsdjPatchedKit, INamedKit, KitType } from '../../types/LsdjTypes';
import { downloadUint8Array, sanitizeFilename } from '../../utils/FileUtil';
import { generateKey, sanitizeKitName } from '../../utils/LsdjUtil';
import { toUint8Array } from '../../utils/NativeUtil';
import { LsdjEffectList } from './LsdjEffectList';
import { LsdjSampleList } from './LsdjSampleList';
import { LsdjWaveView } from './LsdjWaveView';

// Helper function to get color classes based on kit type
const getKitTypeColorClasses = (kitType: KitType): string => {
	switch (kitType) {
		case 'editable':
			return 'bg-green-900/30 text-green-400';
		case 'patched':
			return 'bg-blue-900/30 text-blue-400';
		case 'empty':
			return 'bg-gray-600 text-gray-500';
		case 'rom':
		default:
			return 'bg-gray-700 text-gray-400';
	}
};

function getSampleNameFromPath(path: string): string {
	return path.split('/').pop()?.split('.').shift()?.slice(0, 3)?.toUpperCase() || 'UNK';
}

async function sanitizeSamples(
	fileSystem: FileSystemWorkerAPI,
	paths: string[],
): Promise<{ name: string; samples: ILsdjKitSample[] }> {
	const samples: ILsdjKitSample[] = [];
	let kitName = 'GR8KIT';

	for (let i = 0; i < paths.length; i++) {
		const path = paths[i];

		if (await fileSystem.isDirectory(path)) {
			kitName = sanitizeKitName(path.split('/').pop() || 'GR8KIT');

			const files = await fileSystem.listPath(path);
			for (const file of files.children ?? []) {
				samples.push({
					key: generateKey(),
					name: getSampleNameFromPath(file.path),
					offset: 0,
					length: 0,
					path: file.path,
					effects: [],
				});
			}
		} else {
			samples.push({
				key: generateKey(),
				name: getSampleNameFromPath(path),
				offset: 0,
				length: 0,
				path,
				effects: [],
			});
		}
	}

	return { name: kitName, samples };
}

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
	const { fileSystem } = useRetroPlug();
	const project = useProject();
	const kit = useKit(kitKey)!;
	const system = useLsdjStore((state) => state.systemId);
	const updateKit = useLsdjStore((state) => state.updateKit);
	const removeKit = useLsdjStore((state) => state.removeKit);
	const renameKit = useLsdjStore((state) => state.renameKit);
	const addSamples = useLsdjStore((state) => state.addSamples);
	const [isEffectEditorOpen, setIsEffectEditorOpen] = useState(false);
	const [isSampleListOpen, setIsSampleListOpen] = useState(false);
	const [isDragOver, setIsDragOver] = useState(false);

	console.assert(!!kit);
	const kitType = kit.kit.type;

	const onNameChange = useCallback(
		(newName: string, triggerUpdate: boolean) => {
			renameKit(kitKey, newName, triggerUpdate);
		},
		[kitKey, renameKit],
	);

	const handleDeleteKit = useCallback(() => {
		removeKit(kitKey);
	}, [kitKey, removeKit]);

	const handleDownloadClick = useCallback(
		(e: React.MouseEvent) => {
			e.preventDefault();
			e.stopPropagation();

			if (kit.kit.type === 'empty') {
				return;
			}

			const namedKit = kit.kit as INamedKit;
			const kitData = project.lsdj.getKitData(system, kit.id);
			if (kitData && namedKit.name) {
				try {
					const filename = `${sanitizeFilename(namedKit.name)}.kit`;
					downloadUint8Array(toUint8Array(kitData), filename);
				} catch (error) {
					console.error('Download failed:', error);
				}
			}
		},
		[kit.id, project],
	);

	const handleEffectToggle = useCallback(
		(expanded?: boolean) => {
			if (expanded === undefined) {
				setIsEffectEditorOpen(!isEffectEditorOpen);
			} else {
				setIsEffectEditorOpen(expanded);
			}
		},
		[isEffectEditorOpen],
	);

	const handleSampleListToggle = useCallback(
		(expanded?: boolean) => {
			if (expanded === undefined) {
				setIsSampleListOpen(!isSampleListOpen);
			} else {
				setIsSampleListOpen(expanded);
			}
		},
		[isSampleListOpen],
	);

	// Drag and drop handlers
	const handleDragOver = useCallback(
		(event: React.DragEvent) => {
			event.preventDefault();
			event.dataTransfer.dropEffect = 'move'; // Match the effectAllowed from FileExplorer
			setIsDragOver(true);
		},
		[setIsDragOver],
	);

	const handleDragLeave = useCallback(
		(event: React.DragEvent) => {
			// Only clear if we're actually leaving the element
			if (!event.currentTarget.contains(event.relatedTarget as Node)) {
				setIsDragOver(false);
			}
		},
		[setIsDragOver],
	);

	async function handleFileDrop(paths: string[]) {
		const DEFAULT_EFFECTS: string[] = ['GainEffect', 'FilterEffect', 'DitherEffect'];

		console.log('Dropped paths:', paths);

		if (paths.length === 1 && paths[0].endsWith('.kit')) {
			console.log('Patching kit');
			updateKit(kitKey, { type: "patched", path: paths[0] } as ILsdjPatchedKit);
			onToggle(true);
			return;
		}

		const { name: kitName, samples } = await sanitizeSamples(fileSystem, paths);

		switch (kit.kit.type) {
			case 'editable':
				console.log('Adding samples');
				addSamples(kitKey, samples);
				break;
			case 'empty':
			case 'patched':
			case 'rom':
				console.log('Adding dynamic kit');
				updateKit(kitKey, {
					type: 'editable',
					name: kitName,
					effects: DEFAULT_EFFECTS.map<IEffect>((effectType, idx) => {
						const effectInstance = createEffectInstance(effectType);
						if (!effectInstance) {
							console.error(`Failed to create effect instance of type: ${effectType}`);
							return { id: idx, key: generateKey() } as IEffect;
						}

						effectInstance.id = idx;
						effectInstance.key = generateKey();
						return effectInstance;
					}),
					samples,
				} as ILsdjEditableKit);
				break;
		}

		onToggle(true);
		setIsSampleListOpen(true);
	}

	const handleDrop = useCallback(
		async (event: React.DragEvent) => {
			event.preventDefault();
			setIsDragOver(false);

			try {
				// Handle internal file tree drag (from FileExplorer)
				const filePath = event.dataTransfer.getData('text/plain');
				if (filePath) {
					if (onFileDropped) {
						await onFileDropped(filePath);
					} else {
						console.log(`Dropped file from tree: ${filePath}`);
						try {
							handleFileDrop(JSON.parse(filePath));
						} catch (ex) {
							console.error('Error handling file drop:', ex);
						}
					}
				} else {
					console.log('No filePath found in dataTransfer');
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

	// For empty kits, show only the drop area
	if (kitType === 'empty') {
		return (
			<div
				className={`relative overflow-hidden rounded-sm border-2 border-dashed transition-all duration-200 ${
					isDragOver ? 'border-blue-500 bg-blue-500/10 shadow-lg' : 'border-gray-500 bg-gray-500/10'
				}`}
				onDragOver={handleDragOver}
				onDragLeave={handleDragLeave}
				onDrop={handleDrop}
			>
				<div className="flex items-center justify-center px-2 py-1 text-sm font-medium">
					<span className="font-mono font-medium text-gray-400">{kit.id.toString(16).padStart(2, '0').toUpperCase()}</span>
					<span className="mx-1 font-medium text-gray-400">-</span>
					<span className={`font-medium ${isDragOver ? 'text-blue-300' : 'text-gray-400'}`}>
						{isDragOver ? 'Drop file here to load into kit' : `Empty`}
					</span>
				</div>
			</div>
		);
	}

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
					<span className="font-mono font-medium text-white">{(kit.id + 1).toString(16).padStart(2, '0').toUpperCase()}</span>
					<span className="mx-1 font-medium">-</span>
					<EditableText
						value={(kit.kit as INamedKit).name || ''}
						onChange={(value) => onNameChange(value, true)}
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
						{kitType.charAt(0).toUpperCase() + kitType.slice(1)}
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
					<DeleteIcon
						onClick={(e) => {
							e.stopPropagation();
							handleDeleteKit();
						}}
						title="Delete kit"
					/>
				</div>
			</div>
			{isExpanded && (
				<div className={`bg-gray-900 ${kitType === 'editable' ? 'p-2' : 'px-2 pt-2 pb-1'}`}>
					<div className="mb-2">
						<LsdjWaveView system={system} kitId={kit.id} onNameUpdated={(name) => onNameChange(name, false)} />
					</div>
					{kitType === 'editable' && (
						<div className="space-y-2">
							<LsdjSampleList
								kitKey={kitKey}
								isExpanded={isSampleListOpen}
								onToggle={(expanded) => handleSampleListToggle(expanded)}
								title="Samples"
							/>
							<LsdjEffectList
								kitKey={kitKey}
								isExpanded={isEffectEditorOpen}
								onToggle={(expanded) => handleEffectToggle(expanded)}
								title="Sample Effects"
							/>
						</div>
					)}
				</div>
			)}
		</div>
	);
};
