// components.tsx
import React, { useCallback, useEffect, useState } from 'react';

import { EditableText } from '../../components/EditableText';
import { WaveView } from '../../components/WaveView';
import type { SliceInfo } from '../../components/WaveViewTypes';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import { GAMEBOY_SAMPLE_RATE, KitType, type ILsdjKitData } from '../../types/LsdjTypes';
import { downloadUint8Array, sanitizeFilename } from '../../utils/FileUtil';
import { fromUint8Array } from '../../utils/NativeUtil';
import { useLsdjStore } from './hooks';
import { LsdjEffectList } from './LsdjEffectList';
import { extractSampleData, getKitType, sanitizeKitName } from './util';
import { EnumUtils } from '../../utils/EnumUtil';
import { playSample } from '../../wrapper/Lsdj';
import { createPortal } from 'react-dom';

interface LsdjKitEditorProps {
	kitKey: string;
	isExpanded: boolean;
	onToggle: () => void;
}

export const LsdjKitEditor: React.FC<LsdjKitEditorProps> = ({ kitKey, isExpanded, onToggle }) => {
	const { app, audioContext } = useRetroPlug();
	const kit = useLsdjStore((state) => state.getKit(kitKey))!;
	const removeKit = useLsdjStore((state) => state.removeKit);
	const renameKit = useLsdjStore((state) => state.renameKit);
	const addSample = useLsdjStore((state) => state.addSample);
	const removeSample = useLsdjStore((state) => state.removeSample);
	const [isEditing, setIsEditing] = useState(false);
	const [tempName, setTempName] = useState(kit?.name || '');
	const [kitSampleData, setKitSampleData] = useState<ILsdjKitData | null>(null);
	const [isEffectEditorOpen, setIsEffectEditorOpen] = useState(false);
	const [sampleUnderCursor, setSampleUnderCursor] = useState<string | null>(null);
	const [mousePosition, setMousePosition] = useState<{ x: number; y: number } | null>(null);

	console.assert(!!kit);

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

	useEffect(() => {
		if (app && kit.data && isExpanded) {
			const sampleData = extractSampleData(app.module!, fromUint8Array(app.module!, kit.data));
			console.log(sampleData);
			setKitSampleData(sampleData);
		} else {
			setKitSampleData(null);
		}
	}, [app, kit.data, isExpanded]);

	const handleRename = () => {
		renameKit(kitKey, tempName);
		setIsEditing(false);
	};

	const handleAddSample = () => {
		const newSample = {
			id: Date.now(), // Simple ID generation
			key: `sample-${Date.now()}`,
			name: 'New Sample',
			path: '',
			offset: 0,
			length: 1000,
			effects: [],
		};
		addSample(kitKey, newSample);
	};

	const handleRemoveSample = (sampleKey: string) => {
		removeSample(kitKey, sampleKey);

	};

	const onNameChange = (newName: string) => {
		//renameKit(kitKey, newName);
	};

	const handleSliceClick = (slice: SliceInfo) => {
		if (audioContext && kitSampleData) {
			playSample(audioContext, kitSampleData.sampleBuffer.slice(slice.startSample, slice.endSample), 0.25, GAMEBOY_SAMPLE_RATE);
		}
	};

	const handleSliceMouseMove = useCallback(
		(slice: SliceInfo | null) => {
			if (slice) {
				setSampleUnderCursor(kitSampleData?.samples[slice.index]?.name || null);
			} else {
				setSampleUnderCursor(null);
			}
		},
		[kitSampleData, setSampleUnderCursor],
	);

	const handleWaveViewMouseMove = useCallback((event: React.MouseEvent) => {
		setMousePosition({ x: event.clientX, y: event.clientY });
	}, [setMousePosition]);

	const handleWaveViewMouseLeave = useCallback(() => {
		setMousePosition(null);
		setSampleUnderCursor(null);
	}, [setMousePosition, setSampleUnderCursor]);

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

	return (
		<div className="overflow-hidden rounded-sm border border-gray-700">
			<div
				className="hover:bg-gray-750 flex cursor-pointer items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200"
				onClick={onToggle}
			>
				<div className="flex items-center">
					<div className="mr-2 text-xs text-white">{isExpanded ? '▼' : '▶'}</div>
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
					<span
						className={`rounded px-2 py-1 text-xs ${getKitTypeColorClasses(kitType)}`}
					>
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
			{isExpanded && kitSampleData && (
				<div className={`bg-gray-900 ${kitType === KitType.Editable ? 'p-2' : 'px-2 pt-2 pb-1'}`}>
					<div className="mb-2">
						<div
							onMouseMove={handleWaveViewMouseMove}
							onMouseLeave={handleWaveViewMouseLeave}
							title="Click to play sample"
						>
							<WaveView
								sampleData={kitSampleData.sampleBuffer}
								markers={kitSampleData.samples.map((s) => s.offset)}
								onSliceClick={handleSliceClick}
								onSliceMouseMove={handleSliceMouseMove}
								className="h-[80px] w-full rounded-sm border border-gray-700 bg-gray-800"
							/>
						</div>
					</div>
					{kitType === KitType.Editable && (
						<div className="mb-2">
							<LsdjEffectList
								kitKey={kitKey}
								isExpanded={isEffectEditorOpen}
								onToggle={(expanded) => handleEffectToggle(expanded)}
								title="Sample Effects"
								key={kitKey}
							/>
						</div>
					)}
				</div>
			)}
			{sampleUnderCursor &&
				mousePosition &&
				createPortal(
					<div
						className="pointer-events-none fixed z-50 rounded border border-gray-600 bg-gray-900 px-2 py-1 text-xs text-white shadow-lg"
						style={{
							left: `${mousePosition.x - 13}px`,
							top: `${mousePosition.y + 25}px`,
						}}
					>
						{sampleUnderCursor}
					</div>,
					document.body,
				)}
		</div>
	);
};
