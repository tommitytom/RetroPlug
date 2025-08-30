import React, { useCallback, useEffect, useState } from "react";

import type { IIndexedKit, INamedSample } from '../components/types';
import { WaveView } from "../components/WaveView";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { NativeLsdjKit } from "../native/RetroPlug";
import "../styles/RomEditorPanel.css";
import { convertFloat32Buffer } from "../utils/FileUtil";
import {
	GAMEBOY_SAMPLE_RATE,
	LSDJ_KIT_SAMPLE_COUNT,
	playSample
} from "../wrapper/Lsdj";

// Displays a single LSDJ kit
export const LsdjKit: React.FC<{
	kit: IIndexedKit;
	audioContext: AudioContext | null;
	isExpanded: boolean;
	onToggle: () => void;
}> = ({ kit, audioContext, isExpanded, onToggle }) => {
	const { app } = useRetroPlug();
	const [samples, setSamples] = useState<INamedSample[]>([]);
	const [kitSample, setKitSample] = useState<Float32Array | null>(null);
	const [markers, setMarkers] = useState<number[]>([]);

	const handleSampleClick = useCallback(
		(sampleData: Float32Array) => {
			if (audioContext) {
				playSample(audioContext, sampleData, 0.25, GAMEBOY_SAMPLE_RATE);
			}
		},
		[audioContext],
	);

	useEffect(() => {
		if (!app || !kit) {
			setSamples([]);
			return;
		}

		const mod = app.module!;

		const namedSamples: INamedSample[] = [];
		for (let i = 0; i < LSDJ_KIT_SAMPLE_COUNT; ++i) {
			const sampleName = kit.kit.getSampleName(i);
			if (sampleName && sampleName !== "N/A") {
				const sampleData = kit.kit.getSampleData(i);
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
	}, [kit, setSamples]);

	return (
		<div className="w-full max-w-4xl mx-auto p-2 bg-gray-800 rounded-lg shadow-lg">
			<div
				className={`flex items-center cursor-pointer hover:bg-gray-750 rounded-lg p-1 -m-1 transition-colors duration-200 ${isExpanded ? 'mb-2' : ''}`}
				onClick={onToggle}
			>
				<div className="text-white mr-2 text-sm">
					{isExpanded ? "▼" : "▶"}
				</div>
				<h2 className={`text-lg font-semibold text-white flex-1 ${isExpanded ? 'border-b border-gray-600 pb-1' : ''}`}>
					{kit.name}
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
								className="w-full h-[80px] bg-gray-900 border border-gray-700 rounded-md"
							/>
						</div>
					</div>
				</>
			)}
		</div>
	);
};