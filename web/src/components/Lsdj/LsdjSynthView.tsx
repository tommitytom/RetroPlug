import { useEffect, useRef, useState } from "react";

import { useRetroPlug } from "../../contexts/RetroPlugContext";
import { type Uint8Buffer } from "../../native/RetroPlug";
import { convertSampleData } from "../../utils/LsdjUtil";
import { type SystemId } from "../../utils/NativeUtil";
import { WaveView } from "../WaveView";

interface LsdjSynthViewProps {
	system: SystemId;
	synthId: number;
}

export const LsdjSynthView: React.FC<LsdjSynthViewProps> = ({ system, synthId }) => {
	const { module, project } = useRetroPlug();
	const [sampleBuffer, setSampleBuffer] = useState<Float32Array | null>(null);
	const synthDataRef = useRef<Uint8Buffer | null>(null);

	useEffect(() => {
		const lsdj = project.lsdj;
		synthDataRef.current = lsdj.getSynthData(system, synthId)!;
		const sampleData = convertSampleData(module, synthDataRef.current);
		setSampleBuffer(sampleData);
	}, [project, synthId]);

	useEffect(() => {
		if (!sampleBuffer || !synthDataRef.current) return;

		let animationId: number;

		const updateWaveform = () => {
			const sampleData = convertSampleData(module, synthDataRef.current!);
			sampleBuffer.set(sampleData);
			animationId = requestAnimationFrame(updateWaveform);
		};

		updateWaveform();

		return () => {
			cancelAnimationFrame(animationId);
		};
	}, [sampleBuffer, module]);

	return <div>
		<WaveView
			sampleData={sampleBuffer}
			alwaysUpdate={true}
			className="h-[80px] w-full rounded-sm border border-gray-700 bg-gray-800"
		/>
	</div>
}
