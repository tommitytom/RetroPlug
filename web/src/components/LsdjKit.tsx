import React, { useCallback, useEffect, useState } from "react";

import { EffectList } from "../components/EffectList";
import { WaveView } from "../components/WaveView";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { Uint8Buffer } from "../native/RetroPlug";
import type { EffectInstance } from "../types/EffectTypes";
import type { INamedSample } from '../types/LsdjTypes';
import { convertFloat32Buffer } from "../utils/NativeUtil";

import "../styles/RomEditorPanel.css";

export const LsdjKitEditor: React.FC<{
	id: number;
	name: string;
	kitData: Uint8Buffer|null;
	editable: boolean;
	isExpanded: boolean;
	usageCount?: number;
	onToggle: () => void;
}> = ({ id, name, kitData, editable, isExpanded, usageCount, onToggle }) => {
	const { project } = useRetroPlug();
	const [samples, setSamples] = useState<INamedSample[]>([]);
	const [kitSample, setKitSample] = useState<Float32Array | null>(null);
	const [markers, setMarkers] = useState<number[]>([]);
	const [isEffectsExpanded, setIsEffectsExpanded] = useState(false);
	const [effects, setEffects] = useState<EffectInstance[]>([]);

	/*const handleSampleClick = useCallback(
		(sampleData: Float32Array) => {
			if (audioContext) {
				playSample(audioContext, sampleData, 0.25, GAMEBOY_SAMPLE_RATE);
			}
		},
		[audioContext],
	);*/

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
			if (sampleName && sampleName !== "N/A") {
				const sampleData = kit.getSampleData(i);
				const target = new mod.Float32Buffer(sampleData.size());

				mod.convertNibblesToF32(sampleData, target);

				namedSamples.push({
					name: sampleName,
					data: convertFloat32Buffer(target),
				});
			}
		}

		const markers: number[] = [];
		const fullSampleSize = namedSamples.reduce(
			(acc, sample) => acc + sample.data.length,
			0,
		);
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

	return (
		<div className="border border-gray-700 rounded-sm overflow-hidden">
			<div
				className="px-2 py-1 bg-gray-800 font-medium flex items-center justify-between text-sm cursor-pointer hover:bg-gray-750 transition-colors duration-200"
				onClick={onToggle}
			>
				<div className="flex items-center">
					<div className="text-white mr-2 text-xs">
						{isExpanded ? "▼" : "▶"}
					</div>
					<span className="font-mono font-medium">{id.toString(16).padStart(2, '0').toUpperCase()}</span>
					<span className="font-medium mx-1">-</span>
					<span className="font-medium">{name}</span>
				</div>
				<div className="flex items-center gap-2">
					{/*(usageCount || 0) > 0 && (
						<span className={`px-2 py-1 rounded text-xs text-gray-400 bg-gray-700`}>
							Usage Count: {usageCount || 0}
						</span>
					)*/}
					<span className={`px-2 py-1 rounded text-xs ${
						editable
							? 'text-green-400 bg-green-900/30'
							: 'text-gray-400 bg-gray-700'
					}`}>
						{editable ? 'Editable' : 'Baked'}
					</span>
					<button
						className="p-1 text-red-400 hover:text-red-300 hover:bg-red-600/20 rounded-sm transition-colors duration-200"
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
				<div className={`bg-gray-900 ${editable ? 'p-2' : 'pt-2 px-2 pb-1'}`}>
					<div className="mb-2">
						<div
							onClick={() => handleSampleClick(kitSample)}
							className="sample-waveform-clickable"
							title="Click to play sample"
						>
							<WaveView
								sampleData={kitSample}
								markers={markers}
								className="w-full h-[80px] bg-gray-800 border border-gray-700 rounded-sm"
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
		</div>
	);
};
