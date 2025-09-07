import React, { useCallback } from "react";
import { playSample } from "../wrapper/Lsdj";
import { WaveView } from "./WaveView";
import { GAMEBOY_SAMPLE_RATE, type INamedSample } from "../types/LsdjTypes";

export const LsdjSampleList: React.FC<{
	samples: INamedSample[];
	audioContext: AudioContext | null;
}> = ({ samples, audioContext }) => {
	const handleSampleClick = useCallback(
		(sampleData: Float32Array) => {
			if (audioContext) {
				playSample(audioContext, sampleData, 0.25, GAMEBOY_SAMPLE_RATE);
			}
		},
		[audioContext],
	);

	return (
		<div className="space-y-1">
			{samples.map((sample, idx) => (
				<div
					key={`${sample.name}-${idx}`}
					className="bg-gray-750 rounded p-1 border border-gray-600"
				>
					<div
						onClick={() => handleSampleClick(sample.data)}
						className="sample-clickable text-blue-400 hover:text-blue-300 text-sm mb-1 transition-colors duration-200"
						title="Click to play sample"
					>
						{sample.name}
					</div>
					<div
						onClick={() => handleSampleClick(sample.data)}
						className="sample-waveform-clickable"
						title="Click to play sample"
					>
						<WaveView
							sampleData={sample.data}
							markers={[]}
							className="w-full h-[50px] bg-gray-900 border border-gray-700 rounded-md"
						/>
					</div>
				</div>
			))}
		</div>
	);
};
