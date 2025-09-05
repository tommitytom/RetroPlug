import React, { useCallback, useEffect, useState } from "react";

import { EffectList } from "../components/EffectList";
import { WaveView } from "../components/WaveView";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { Uint8Buffer } from "../native/RetroPlug";
import type { EffectInstance } from "../types/EffectTypes";
import type { INamedSample } from '../types/LsdjTypes';
import { convertFloat32Buffer } from "../utils/NativeUtil";

import "../styles/RomEditorPanel.css";

// Displays a single LSDJ kit
export const LsdjKitEditor: React.FC<{
	id: number;
	name: string;
	kitData: Uint8Buffer|null;
	isExpanded: boolean;
	onToggle: () => void;
}> = ({ id, name, kitData, isExpanded, onToggle }) => {
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
		<div className="w-full max-w-4xl mx-auto p-2 bg-gray-800 rounded-sm shadow-lg">
			<div
				className={`flex items-center cursor-pointer hover:bg-gray-750 rounded-lg p-1 -m-1 transition-colors duration-200 ${isExpanded ? 'mb-2' : ''}`}
				onClick={onToggle}
			>
				<div className="text-white mr-2 text-sm">
					{isExpanded ? "▼" : "▶"}
				</div>
				<h2 className={`text-md font-semibold text-white flex-1 ${isExpanded ? 'border-b border-gray-600 pb-1' : ''}`}>
					{name}
				</h2>
			</div>
			{isExpanded && kitSample && (
				<>
					<div className="mb-2">
						<div
							onClick={() => handleSampleClick(kitSample)}
							className="sample-waveform-clickable"
							title="Click to play sample"
						>
							<WaveView
								sampleData={kitSample}
								markers={markers}
								className="w-full h-[80px] bg-gray-900 border border-gray-700 rounded-sm"
							/>
						</div>
					</div>
					<EffectList
						isExpanded={isEffectsExpanded}
						onToggle={() => setIsEffectsExpanded(!isEffectsExpanded)}
						onEffectsChange={handleEffectsChange}
					/>
				</>
			)}
		</div>
	);
};
