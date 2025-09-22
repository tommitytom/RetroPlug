import { useCallback, useEffect, useRef, useState } from "react";

import { useRetroPlug } from "../../contexts/RetroPlugContext";
import { type Uint8Buffer } from "../../native/RetroPlug";
import { convertSampleData } from "../../utils/LsdjUtil";
import { type SystemId } from "../../utils/NativeUtil";
import { downloadUint8Array, sanitizeFilename } from "../../utils/FileUtil";
import { toUint8Array } from "../../utils/NativeUtil";
import { WaveView } from "../WaveView";

interface LsdjSynthViewProps {
	system: SystemId;
	synthId: number;
	isExpanded?: boolean;
	onToggle?: (value?: boolean) => void;
	onFileDropped?: (filePath: string, file?: File) => Promise<void>;
	onError?: (error: string, operation?: string) => void;
}

export const LsdjSynthView: React.FC<LsdjSynthViewProps> = ({
	system,
	synthId,
	isExpanded = false,
	onToggle,
	onFileDropped,
	onError
}) => {
	const { module, project, fileSystem } = useRetroPlug();
	const [sampleBuffer, setSampleBuffer] = useState<Float32Array | null>(null);
	const [isDragOver, setIsDragOver] = useState(false);

	useEffect(() => {
		const lsdj = project.lsdj;
		const synthData = lsdj.getSynthData(system, synthId)!;
		const sampleData = convertSampleData(module, synthData);
		setSampleBuffer(sampleData);
	}, [project, synthId, system, module, isExpanded]);

	useEffect(() => {
		if (!sampleBuffer) return;

		let animationId: number;

		const updateWaveform = () => {
			const synthData = project.lsdj.getSynthData(system, synthId)!;
			const sampleData = convertSampleData(module, synthData);
			sampleBuffer.set(sampleData);
			animationId = requestAnimationFrame(updateWaveform);
		};

		updateWaveform();

		return () => {
			cancelAnimationFrame(animationId);
		};
	}, [sampleBuffer, module, project]);

	// Drag and drop handlers
	const handleDragOver = useCallback(
		(event: React.DragEvent) => {
			event.preventDefault();
			if (event.dataTransfer.effectAllowed !== 'move') return;

			event.dataTransfer.dropEffect = 'move'; // Match the effectAllowed from FileExplorer
			setIsDragOver(true);
		},
		[setIsDragOver],
	);

	const handleDragLeave = useCallback(
		(event: React.DragEvent) => {
			// Only clear if we're actually leaving the element
			if (!event.currentTarget.contains(event.relatedTarget as Node)) {
				setIsDragOver(false);
			}
		},
		[setIsDragOver],
	);

	const handleDrop = useCallback(
		async (event: React.DragEvent) => {
			event.preventDefault();
			setIsDragOver(false);

			try {
				// Handle internal file tree drag (from FileExplorer)
				const filePath = event.dataTransfer.getData('text/plain');
				if (filePath) {
					if (onFileDropped) {
						await onFileDropped(filePath);
					} else {
						// Placeholder callback - parse the dropped file path
						try {
							const paths = JSON.parse(filePath);
							if (paths.length > 0 && paths[0].endsWith('.snt')) {
								console.log(`Dropped .snt file: ${paths[0]}`);

								const fileData = await fileSystem.readPath(paths[0]);
								if (!project.lsdj.setSynthData(system, synthId, fileData)) {
									console.error(`Failed to set synth data for synth ID ${synthId}`);
								}
							} else {
								console.warn('Only .snt files are supported for synth drops');
							}
						} catch (ex) {
							console.error('Error handling synth file drop:', ex);
						}
					}
				} else {
					console.log('No filePath found in dataTransfer');
				}
			} catch (error) {
				const errorMessage = `Failed to process dropped synth file: ${error}`;
				console.error(errorMessage);
				if (onError) {
					onError(errorMessage, 'drop');
				}
			}
		},
		[onFileDropped, onError],
	);

	const handleToggle = useCallback(() => {
		if (onToggle) {
			onToggle();
		}
	}, [onToggle]);

	const handleDownloadClick = useCallback(
		(e: React.MouseEvent) => {
			e.preventDefault();
			e.stopPropagation();

			const synthData = project.lsdj.getSynthData(system, synthId);
			if (!synthData) {
				return;
			}

			try {
				const synthName = `synth_${synthId.toString(16).padStart(2, '0')}`;
				const filename = `${sanitizeFilename(synthName)}.snt`;

				// Convert the synth data to a downloadable format
				// For now, we'll download the raw synth data - this might need adjustment
				// depending on the actual format you want
				downloadUint8Array(toUint8Array(synthData), filename);
			} catch (error) {
				console.error('Download failed:', error);
			}
		},
		[synthId],
	);

	return (
		<div
			className={`relative overflow-hidden rounded-sm border border-gray-700 transition-all duration-200 ${
				isDragOver ? 'border-blue-500 bg-blue-500/10 shadow-lg' : ''
			}`}
			onDragOver={handleDragOver}
			onDragLeave={handleDragLeave}
			onDrop={handleDrop}
		>
			{isDragOver && (
				<div className="absolute inset-0 z-10 flex items-center justify-center rounded-sm border-2 border-dashed border-blue-500 bg-blue-500/20">
					<span className="text-blue-300 font-medium">Drop .snt file to load synth</span>
				</div>
			)}
			<div
				className={`hover:bg-gray-750 flex cursor-pointer items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200 ${
					onToggle ? 'cursor-pointer' : 'cursor-default'
				} ${isDragOver ? 'bg-blue-600/20' : ''}`}
				onClick={onToggle ? handleToggle : undefined}
			>
				<div className="flex items-center">
					{onToggle && (
						<div className="mr-2 flex h-3 w-3 items-center justify-center">
							{isExpanded ? (
								<div className="h-0 w-0 border-t-6 border-r-4 border-l-4 border-t-white border-r-transparent border-l-transparent" />
							) : (
								<div className="h-0 w-0 border-t-4 border-b-4 border-l-6 border-t-transparent border-b-transparent border-l-white" />
							)}
						</div>
					)}
					<span className="font-mono font-medium text-white">{synthId.toString(16).padStart(2, '0').toUpperCase()}</span>
					<span className="mx-1 font-medium text-white">-</span>
					<span className="font-medium text-white">Synth</span>
				</div>
				<div className="flex items-center gap-2">
					<span className="rounded px-2 py-1 text-xs bg-purple-900/30 text-purple-400">
						Wave
					</span>
					<button
						className={`rounded-sm p-1 transition-colors duration-200 ${
							sampleBuffer
								? 'text-blue-400 hover:bg-blue-600/20 hover:text-blue-300'
								: 'text-gray-500 hover:bg-gray-600/20 hover:text-gray-400'
						}`}
						onClick={handleDownloadClick}
						title={sampleBuffer ? 'Download synth' : 'Synth data not available'}
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
				</div>
			</div>
			{(!onToggle || isExpanded) && (
				<div className="bg-gray-900 p-2">
					<WaveView
						sampleData={sampleBuffer}
						alwaysUpdate={true}
						className="h-[80px] w-full rounded-sm border border-gray-700 bg-gray-800"
					/>
				</div>
			)}
		</div>
	);
}
