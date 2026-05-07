import React, { useState, useCallback } from "react";

interface FileDropZoneProps {
	onFileDrop: (files: FileList) => void;
	title?: string;
	subtitle?: string;
	supportedFormats?: string;
}

export const FileDropZone: React.FC<FileDropZoneProps> = ({
	onFileDrop,
	title = "Drag and drop files here",
	subtitle = "Drop files here!",
	supportedFormats = "Supported formats: .gb, .gbc"
}) => {
	const [isDragOver, setIsDragOver] = useState(false);

	const handleDragEnter = useCallback((e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(true);
	}, []);

	const handleDragLeave = useCallback((e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(false);
	}, []);

	const handleDragOver = useCallback((e: React.DragEvent) => {
		e.preventDefault();
	}, []);

	const handleDrop = useCallback((e: React.DragEvent) => {
		e.preventDefault();
		setIsDragOver(false);

		const files = e.dataTransfer.files;
		if (files.length > 0) {
			onFileDrop(files);
		}
	}, [onFileDrop]);

	return (
		<div
			onDragEnter={handleDragEnter}
			onDragLeave={handleDragLeave}
			onDragOver={handleDragOver}
			onDrop={handleDrop}
			className={`
				w-full h-full
				border-2 border-dashed rounded-lg
				p-10 text-center cursor-pointer
				flex flex-col items-center justify-center
				rom-drop-area
				${isDragOver ? 'drag-over' : ''}
			`}
		>
			<div className={`text-lg font-medium text-gray-300 rom-drop-text ${isDragOver ? 'drag-over' : ''}`}>
				{isDragOver ? subtitle : title}
			</div>
			<div className="text-sm text-gray-500 mt-2">
				{supportedFormats}
			</div>
		</div>
	);
};
