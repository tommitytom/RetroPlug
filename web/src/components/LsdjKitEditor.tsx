import React, { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';

import { useRetroPlug } from '../contexts/RetroPlugContext';
import type { Uint8Buffer } from '../native/RetroPlug';
import type { EffectInstance } from '../types/EffectTypes';
import { GAMEBOY_SAMPLE_RATE, type INamedSample } from '../types/LsdjTypes';
import { downloadUint8Buffer, sanitizeFilename } from '../utils/FileUtil';
import { convertFloat32Buffer } from '../utils/NativeUtil';
import { playSample } from '../wrapper/Lsdj';
import { EditableText } from './EditableText';
import { EffectList } from './EffectList';
import { WaveView } from './WaveView';
import type { SliceInfo } from './WaveViewTypes';

export const LsdjKitEditor: React.FC<{
	id: number;
	name: string;
	kitData: Uint8Buffer | null;
	editable: boolean;
	isExpanded: boolean;
	usageCount?: number;
	onToggle: () => void;
	onNameChange?: (newName: string) => void;
}> = ({ id, name, kitData, editable, isExpanded, usageCount, onToggle, onNameChange }) => {
	const { project, audioContext } = useRetroPlug();
	const [samples, setSamples] = useState<INamedSample[]>([]);
	const [kitSample, setKitSample] = useState<Float32Array | null>(null);
	const [markers, setMarkers] = useState<number[]>([]);
	const [isEffectsExpanded, setIsEffectsExpanded] = useState(false);
	const [effects, setEffects] = useState<EffectInstance[]>([]);
	const [sampleUnderCursor, setSampleUnderCursor] = useState<INamedSample | null>(null);
	const [mousePosition, setMousePosition] = useState<{ x: number; y: number } | null>(null);

	const handleSampleClick = (buf: Float32Array) => {};

	useEffect(() => {
		if (!project || !kitData || !isExpanded) {
			setSamples([]);
			return;
		}

		const mod = project.module;
		const kit = new mod.NativeLsdjKit(kitData, id);

		const namedSamples: INamedSample[] = [];
		const sampleCount = kit.getSampleCount();
		for (let i = 0; i < sampleCount; ++i) {
			const sampleName = kit.getSampleName(i);
			if (sampleName && sampleName !== 'N/A') {
				const sampleData = kit.getSampleData(i);
				const target = new mod.Float32Buffer(sampleData.size());

				mod.convertNibblesToF32(sampleData, target);

				namedSamples.push({
					name: sampleName,
					data: convertFloat32Buffer(target),
				});
			}
		}

		kit.delete();

		const markers: number[] = [];
		const fullSampleSize = namedSamples.reduce((acc, sample) => acc + sample.data.length, 0);
		const fullSample = new Float32Array(fullSampleSize);
		let offset = 0;
		for (const sample of namedSamples) {
			fullSample.set(sample.data, offset);
			offset += sample.data.length;

			markers.push(offset);
		}

		setSamples(namedSamples);
		setKitSample(fullSample);
		setMarkers(markers);
	}, [isExpanded, id, kitData, setSamples]);

	const handleEffectsChange = useCallback((newEffects: any) => {
		// Handle effects change
		//kit.kit.getSampleData
	}, []);

	// Name validation function for EditableText
	const validateAndNormalizeName = useCallback((input: string): string => {
		// Convert to uppercase, allow only alphanumeric and dashes, limit to 6 characters
		return input
			.toUpperCase()
			.replace(/[^A-Z0-9-]/g, '')
			.slice(0, 6);
	}, []);

	const handleSliceClick = useCallback(
		(slice: SliceInfo) => {
			if (audioContext) {
				playSample(audioContext, samples[slice.index].data, 0.25, GAMEBOY_SAMPLE_RATE);
			}
		},
		[audioContext, samples],
	);

	const handleSliceMouseMove = useCallback(
		(slice: SliceInfo | null) => {
			if (slice) {
				setSampleUnderCursor(samples[slice.index]);
			} else {
				setSampleUnderCursor(null);
			}
		},
		[samples, setSampleUnderCursor],
	);

	const handleWaveViewMouseMove = useCallback((event: React.MouseEvent) => {
		// Update mouse position for tooltip positioning
		setMousePosition({ x: event.clientX, y: event.clientY });
	}, []);

	const handleWaveViewMouseLeave = useCallback(() => {
		setMousePosition(null);
		setSampleUnderCursor(null);
	}, []);

	return (
		<div className="overflow-hidden rounded-sm border border-gray-700">
			<div
				className="hover:bg-gray-750 flex cursor-pointer items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200"
				onClick={onToggle}
			>
				<div className="flex items-center">
					<div className="mr-2 text-xs text-white">{isExpanded ? '▼' : '▶'}</div>
					<span className="font-mono font-medium">{id.toString(16).padStart(2, '0').toUpperCase()}</span>
					<span className="mx-1 font-medium">-</span>
					<EditableText
						value={name}
						onChange={onNameChange}
						className="font-medium"
						maxLength={6}
						validator={validateAndNormalizeName}
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
						className={`rounded px-2 py-1 text-xs ${
							editable ? 'bg-green-900/30 text-green-400' : 'bg-gray-700 text-gray-400'
						}`}
					>
						{editable ? 'Editable' : 'Baked'}
					</span>
					<button
						className={`rounded-sm p-1 transition-colors duration-200 ${
							kitData
								? 'text-blue-400 hover:bg-blue-600/20 hover:text-blue-300'
								: 'text-gray-500 hover:bg-gray-600/20 hover:text-gray-400'
						}`}
						onClick={(e) => {
							e.preventDefault();
							e.stopPropagation();

							if (kitData) {
								try {
									const filename = `${sanitizeFilename(name)}.kit`;
									downloadUint8Buffer(kitData, filename);
								} catch (error) {
									console.error('Download failed:', error);
								}
							} else {
								console.log('No kitData available for download');
							}
						}}
						title={kitData ? 'Download kit' : 'Kit data not available'}
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
							// TODO: Add delete functionality
							console.log('Delete kit:', id, name);
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
			{isExpanded && kitSample && (
				<div className={`bg-gray-900 ${editable ? 'p-2' : 'px-2 pt-2 pb-1'}`}>
					<div className="mb-2">
						<div
							onClick={() => handleSampleClick(kitSample)}
							onMouseMove={handleWaveViewMouseMove}
							onMouseLeave={handleWaveViewMouseLeave}
							title="Click to play sample"
						>
							<WaveView
								sampleData={kitSample}
								markers={markers}
								onSliceClick={handleSliceClick}
								onSliceMouseMove={handleSliceMouseMove}
								className="h-[80px] w-full rounded-sm border border-gray-700 bg-gray-800"
							/>
						</div>
					</div>
					{editable && (
						<EffectList
							isExpanded={isEffectsExpanded}
							onToggle={() => setIsEffectsExpanded(!isEffectsExpanded)}
							onEffectsChange={handleEffectsChange}
						/>
					)}
				</div>
			)}
			{/* Sample name tooltip - rendered at document body level */}
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
						{sampleUnderCursor.name}
					</div>,
					document.body,
				)}
		</div>
	);
};
