import React, { useCallback, useEffect, useState } from "react";

import { FileDropZone } from "../components/FileDropZone";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { NativeLsdjKit, NativeLsdjRom } from "../native/RetroPlug";
import "../styles/RomEditorPanel.css";
import { convertFile, convertFloat32Buffer } from "../utils/FileUtil";
import {
	GAMEBOY_SAMPLE_RATE,
	LSDJ_KIT_COUNT,
	LSDJ_KIT_SAMPLE_COUNT,
	playSample,
} from "../wrapper/Lsdj";

interface IIndexedKit {
	id: number;
	name: string;
	kit: NativeLsdjKit;
}

function renderWaveForm(
	canvas: HTMLCanvasElement,
	sampleData: Float32Array,
	markers: number[],
) {
	const ctx = canvas.getContext("2d");
	if (!ctx) return;

	const width = canvas.clientWidth;
	const height = canvas.clientHeight;

	ctx.clearRect(0, 0, width, height);

	ctx.save();
	ctx.strokeStyle = "white";
	ctx.lineWidth = 1;
	ctx.beginPath();

	const step = Math.ceil(sampleData.length / width);
	for (let x = 0; x < width; x++) {
		const min = Math.min(...sampleData.subarray(x * step, (x + 1) * step));
		const max = Math.max(...sampleData.subarray(x * step, (x + 1) * step));
		ctx.moveTo(x + 0.5, ((1 + min) * height) / 2);
		ctx.lineTo(x + 0.5, ((1 + max) * height) / 2);
	}
	ctx.stroke();
	ctx.restore();

	ctx.save();
	ctx.strokeStyle = "red";
	ctx.lineWidth = 1;
	ctx.beginPath();
	for (const marker of markers) {
		const scaledMarker = (marker / sampleData.length) * width;
		ctx.moveTo(scaledMarker + 0.5, 0);
		ctx.lineTo(scaledMarker + 0.5, height);
	}
	ctx.stroke();
	ctx.restore();
}

type WaveViewProps = {
	sampleData?: Float32Array | null;
	markers?: number[];
	className?: string;
};
const WaveView = ({ sampleData, markers, className }: WaveViewProps) => {
	const canvasRef = React.useRef<HTMLCanvasElement | null>(null);

	useEffect(() => {
		const canvas = canvasRef.current;
		if (!canvas || !sampleData) return;

		const dpr = window.devicePixelRatio || 1;
		const width = canvas.clientWidth;
		const height = canvas.clientHeight;
		if (width === 0 || height === 0) return;

		canvas.width = width * dpr;
		canvas.height = height * dpr;

		const ctx = canvas.getContext("2d");
		if (ctx) {
			ctx.setTransform(1, 0, 0, 1, 0, 0);
			ctx.scale(dpr, dpr);
		}

		renderWaveForm(canvas, sampleData, markers || []);
	}, [sampleData, markers]);

	return <canvas ref={canvasRef} className={className} />;
};

interface INamedSample {
	name: string;
	data: Float32Array;
}

// Displays a list of samples for a kit
const LsdjSampleList: React.FC<{
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
		<div className="space-y-6">
			{samples.map((sample, idx) => (
				<div
					key={`${sample.name}-${idx}`}
					className="bg-gray-750 rounded-lg p-4 border border-gray-600"
				>
					<div
						onClick={() => handleSampleClick(sample.data)}
						className="sample-clickable text-blue-400 hover:text-blue-300 font-medium mb-3 transition-colors duration-200"
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
							className="w-full h-[100px] bg-gray-900 border border-gray-700 rounded-md"
						/>
					</div>
				</div>
			))}
		</div>
	);
};

// Displays a single LSDJ kit
const LsdjKit: React.FC<{
	kit: IIndexedKit;
	audioContext: AudioContext | null;
}> = ({ kit, audioContext }) => {
	const { app } = useRetroPlug();
	const [samples, setSamples] = useState<INamedSample[]>([]);
	const [kitSample, setKitSample] = useState<Float32Array | null>(null);
	const [markers, setMarkers] = useState<number[]>([]);

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
		<div className="w-full max-w-4xl mx-auto p-6 bg-gray-800 rounded-lg shadow-lg">
			<h2 className="text-xl font-bold text-white mb-6 border-b border-gray-600 pb-2">
				{kit.name}
			</h2>
			<WaveView
				sampleData={kitSample}
				markers={markers}
				className="w-full h-[100px] bg-gray-900 border border-gray-700 rounded-md"
			/>
		</div>
	);
};

export const RomEditorPanel: React.FC = () => {
	const { app, audioContext } = useRetroPlug();
	const [rom, setRom] = useState<NativeLsdjRom | null>(null);
	const [kits, setKits] = useState<IIndexedKit[]>([]);

	const handleFileDrop = useCallback(
		async (files: FileList) => {
			if (!app) return;

			for (let i = 0; i < files.length; i++) {
				const file = files[i];
				if (file.name.endsWith(".gb")) {
					const fileData = await convertFile(app, file);
					const accessor = new app.module!.MemoryAccessor(
						app.module!.NativeMemoryType.Rom,
						fileData,
						0,
					);
					const rom = new app.module!.NativeLsdjRom(accessor);
					setRom(rom);
					return;
				}
			}
		},
		[app],
	);

	useEffect(() => {
		if (!rom) return;

		const indexedKits: IIndexedKit[] = [];

		for (let i = 0; i < LSDJ_KIT_COUNT; ++i) {
			if (!rom.kitIsEmpty(i)) {
				const kit = rom.getKit(i);

				if (kit && kit.isValid) {
					indexedKits.push({ id: i, name: kit.getName(), kit });
				}
			}
		}

		setKits(indexedKits);

		//return () => rom.delete();
	}, [rom]);

	return (
		<div className="w-full h-full bg-gray-900">
			{rom === null ? (
				<FileDropZone
					onFileDrop={handleFileDrop}
					title="Drag and drop ROM files here"
					subtitle="Drop files here!"
					supportedFormats="Supported formats: .gb, .gbc"
				/>
			) : (
				<div className="w-full h-full overflow-y-auto">
					<div className="min-h-full py-8 px-4">
						<div className="space-y-8">
							{kits.map((kit) => (
								<LsdjKit
									key={`${kit.name}-${kit.id}`}
									kit={kit}
									audioContext={audioContext}
								/>
							))}
						</div>
					</div>
				</div>
			)}
		</div>
	);
};
