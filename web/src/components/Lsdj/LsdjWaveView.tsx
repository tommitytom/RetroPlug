import { useCallback, useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import { useRetroPlug } from "../../contexts/RetroPlugContext";
import { useSystemKitVersion } from "../../hooks/RetroPlugHooks";
import { GAMEBOY_SAMPLE_RATE, type ILsdjKitData } from "../../types/LsdjTypes";
import { extractSampleData } from "../../utils/LsdjUtil";
import { type SystemId } from "../../utils/NativeUtil";
import { playSample } from "../../wrapper/Lsdj";
import { WaveView } from "../WaveView";
import { type SliceInfo } from "../WaveViewTypes";

interface LsdjWaveViewProps {
	system: SystemId;
	kitId: number;
	onNameUpdated?: (name: string) => void;
}

export const LsdjWaveView: React.FC<LsdjWaveViewProps> = ({ system, kitId, onNameUpdated }) => {
	const { module, audioContext, project } = useRetroPlug();
	const [sampleUnderCursor, setSampleUnderCursor] = useState<string | null>(null);
	const [mousePosition, setMousePosition] = useState<{ x: number; y: number } | null>(null);
	const [kitSampleData, setKitSampleData] = useState<ILsdjKitData | null>(null);
	const currentNameRef = useRef<string | null>(null);
	const kitVersion = useSystemKitVersion(system, kitId);

	useEffect(() => {
		const kitData = project.lsdj.getKitData(system, kitId);
		if (kitData && kitData.size() > 0) {
			const sampleData = extractSampleData(module, kitData);
			setKitSampleData(sampleData);

			if (currentNameRef.current !== sampleData.name) {
				onNameUpdated?.(sampleData.name);
				currentNameRef.current = sampleData.name;
			}
		} else {
			setKitSampleData(null);
		}
	}, [project, kitVersion]);

	const handleSliceClick = (slice: SliceInfo) => {
		if (audioContext && kitSampleData) {
			playSample(
				audioContext,
				kitSampleData.sampleBuffer.slice(slice.startSample, slice.endSample),
				0.25,
				GAMEBOY_SAMPLE_RATE,
			);
		}
	};

	const handleSliceMouseMove = useCallback(
		(slice: SliceInfo | null) => {
			if (slice) {
				setSampleUnderCursor(kitSampleData?.samples[slice.index - 1]?.name || null);
			} else {
				setSampleUnderCursor(null);
			}
		},
		[kitSampleData, setSampleUnderCursor],
	);

	const handleWaveViewMouseMove = useCallback(
		(event: React.MouseEvent) => {
			setMousePosition({ x: event.clientX, y: event.clientY });
		},
		[setMousePosition],
	);

	const handleWaveViewMouseLeave = useCallback(() => {
		setMousePosition(null);
		setSampleUnderCursor(null);
	}, [setMousePosition, setSampleUnderCursor]);

	return <div onMouseMove={handleWaveViewMouseMove} onMouseLeave={handleWaveViewMouseLeave}>
		<WaveView
			sampleData={kitSampleData?.sampleBuffer}
			markers={kitSampleData?.samples.map((s) => s.offset)}
			onSliceClick={handleSliceClick}
			onSliceMouseMove={handleSliceMouseMove}
			className="h-[80px] w-full rounded-sm border border-gray-700 bg-gray-800"
		/>
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
}
