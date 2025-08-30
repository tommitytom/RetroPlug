import React, { useCallback, useEffect, useState } from "react";

import { FileDropZone } from "../components/FileDropZone";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import type { NativeLsdjKit, NativeLsdjRom } from "../native/RetroPlug";
import "../styles/RomEditorPanel.css";
import { convertFile, convertFloat32Buffer } from "../utils/FileUtil";
import { GAMEBOY_SAMPLE_RATE, LSDJ_KIT_COUNT, LSDJ_KIT_SAMPLE_COUNT } from "../wrapper/Lsdj";

// Utility function to play an audio sample using Web Audio API
function playSample(audioContext: AudioContext, sampleData: Float32Array, volume: number, sampleRate: number) {
	if (!audioContext || !sampleData || sampleData.length === 0) return;

	// Create an audio buffer
	const buffer = audioContext.createBuffer(1, sampleData.length, sampleRate);
	const channelData = buffer.getChannelData(0);

	// Copy the sample data to the buffer
	for (let i = 0; i < sampleData.length; i++) {
		channelData[i] = sampleData[i] * volume;
	}

	// Create and configure buffer source
	const source = audioContext.createBufferSource();
	source.buffer = buffer;
	source.connect(audioContext.destination);

	// Play the sample
	source.start();
}

interface IIndexedKit {
	id: number;
	name: string;
	kit: NativeLsdjKit;
}


function renderWaveForm(canvas: HTMLCanvasElement, sampleData: Float32Array) {
	const ctx = canvas.getContext("2d");
	if (!ctx) return;

	const width = canvas.width;
	const height = canvas.height;

	ctx.strokeStyle = 'white';
	ctx.clearRect(0, 0, width, height);
	ctx.beginPath();

	const step = Math.ceil(sampleData.length / width);
	for (let x = 0; x < width; x++) {
		const min = Math.min(...sampleData.subarray(x * step, (x + 1) * step));
		const max = Math.max(...sampleData.subarray(x * step, (x + 1) * step));
		ctx.moveTo(x, (1 + min) * height / 2);
		ctx.lineTo(x, (1 + max) * height / 2);
	}

	ctx.stroke();
}

type WaveViewProps = {
	sampleData: Float32Array;
	markers: number[];
}
const WaveView = ({ sampleData, markers }: WaveViewProps) => {
	const canvasRef = React.useRef<HTMLCanvasElement | null>(null);

	useEffect(() => {
		if (!sampleData) return;
		renderWaveForm(canvasRef.current!, sampleData);
	}, [sampleData]);

	return (
		<canvas ref={canvasRef} />
	);
}

interface INamedSample {
	name: string;
	data: Float32Array;
}

// Displays a single LSDJ kit
const LsdjKit: React.FC<{ kit: IIndexedKit; audioContext: AudioContext | null }> = ({ kit, audioContext }) => {
	const {app} = useRetroPlug();
	const [samples, setSamples] = useState<INamedSample[]>([]);

	useEffect(() => {
		if (!app || !kit) {
			setSamples([]);
			return;
		}

		const mod = app.module!;

		const namedSamples: INamedSample[] = [];
		for (let i = 0; i < LSDJ_KIT_SAMPLE_COUNT; ++i) {
			const sampleName = kit.kit.getSampleName(i);
			if (sampleName && sampleName !== 'N/A') {
				const sampleData = kit.kit.getSampleData(i);
				const target = new mod.Float32Buffer(sampleData.size());
				mod.convertNibblesToF32(sampleData, target);
				namedSamples.push({ name: sampleName, data: convertFloat32Buffer(target) });
			}
		}

		setSamples(namedSamples);
	}, [kit, setSamples]);

	const handleSampleClick = useCallback((sampleData: Float32Array) => {
		if (audioContext) {
			playSample(audioContext, sampleData, 0.25, GAMEBOY_SAMPLE_RATE);
		}
	}, [audioContext]);

	return (
		<div>
			<div key={`${kit.name}-${kit.id}`}>{kit.name}</div>
			<div>
				{samples.map((sample, idx) => (
					<div key={`${sample.name}-${idx}`}>
						<div
							onClick={() => handleSampleClick(sample.data)}
							className="sample-clickable"
							title="Click to play sample"
						>
							{sample.name}
						</div>
						<div
							onClick={() => handleSampleClick(sample.data)}
							className="sample-waveform-clickable"
							title="Click to play sample"
						>
							<WaveView sampleData={sample.data} />
						</div>
					</div>
				))}
			</div>
		</div>
	);
};

export const RomEditorPanel: React.FC = () => {
	const { app, audioContext } = useRetroPlug();
	const [rom, setRom] = useState<NativeLsdjRom | null>(null);
	const [kits, setKits] = useState<IIndexedKit[]>([]);

	const handleFileDrop = useCallback(async (files: FileList) => {
		if (!app) return;

		for (let i = 0; i < files.length; i++) {
			const file = files[i];
			if (file.name.endsWith('.gb')) {
				const fileData = await convertFile(app, file);
				const accessor = new app.module!.MemoryAccessor(app.module!.NativeMemoryType.Rom, fileData, 0);
				const rom = new app.module!.NativeLsdjRom(accessor);
				setRom(rom);
				return;
			}
		}
	}, [app]);

	useEffect(() => {
		if (!rom) return;

		const indexedKits: IIndexedKit[] = [];

		for (let i = 0; i < LSDJ_KIT_COUNT; ++i) {
			if (!rom.kitIsEmpty(i)) {
				const kit = rom.getKit(i);

				if (kit && kit.isValid) {
					indexedKits.push({ id: i, name: kit.getName(), kit });
					break;
				}
			}
		}

		setKits(indexedKits);

		//return () => rom.delete();
	}, [rom]);

	return (
		<div className="w-full h-full">
			{rom === null ? (
				<FileDropZone
					onFileDrop={handleFileDrop}
					title="Drag and drop ROM files here"
					subtitle="Drop files here!"
					supportedFormats="Supported formats: .gb, .gbc"
				/>
			) : (
				<div className="w-full h-full flex items-center justify-center">
					<div className="text-lg font-medium text-gray-300">
						{kits.map((kit) => (
							<LsdjKit key={`${kit.name}-${kit.id}`} kit={kit} audioContext={audioContext} />
						))}
					</div>
				</div>
			)}
		</div>
	);
};
