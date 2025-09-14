import { File, FileText, Gamepad2, Music } from 'lucide-react';
import { useEffect, useState } from 'react';
import { useDocument } from '../../contexts/DocumentContext';
import type { Document, DocumentType } from './types';
import { SystemPanel } from '../../panels/SystemPanel';

export const DocumentDisplay: React.FC = () => {
	const { currentDocument, markDirty, saveDocument } = useDocument();
	const [content, setContent] = useState('');

	useEffect(() => {
		if (currentDocument?.type === 'text') {
			//setContent(currentDocument.content);
		}
	}, [currentDocument]);

	useEffect(() => {
		const handleKeyDown = (event: KeyboardEvent) => {
			if ((event.ctrlKey || event.metaKey) && event.key === 's') {
				event.preventDefault();
				saveDocument();
			}
		};

		window.addEventListener('keydown', handleKeyDown);

		return () => {
			window.removeEventListener('keydown', handleKeyDown);
		};
	}, [saveDocument]);

	const getDocumentIcon = () => {
		switch (currentDocument?.type) {
			case 'text':
				return <FileText className="h-4 w-4" />;
			case 'audio':
				return <Music className="h-4 w-4" />;
			case 'emulator':
				return <Gamepad2 className="h-4 w-4" />;
			default:
				return <File className="h-4 w-4" />;
		}
	};

	if (!currentDocument) {
		return <div className="flex h-full items-center justify-center text-gray-500">No document open</div>;
	}

	return (
		<div className="flex h-full flex-col">
			{/* Document Header */}
			<div className="flex items-center gap-2 border-b border-gray-700 bg-gray-800 px-4 py-2">
				{getDocumentIcon()}
				<span className="text-sm font-medium">{currentDocument.title}</span>
				{currentDocument.isDirty && <span className="text-xs text-yellow-500">•</span>}
			</div>

			{/* Document Content */}
			<div className="flex-1 overflow-auto p-4">
				{currentDocument.type === 'text' ? (
					<textarea
						className="h-full w-full resize-none rounded border border-gray-800 bg-gray-950 p-4 font-mono text-sm text-gray-100 outline-none focus:border-blue-600"
						value={content}
						onChange={(e) => {
							setContent(e.target.value);
							markDirty();
						}}
						placeholder="Start typing..."
					/>
				) : currentDocument.type === 'audio' ? (
					<div className="flex h-full items-center justify-center text-gray-500">
						<div className="text-center">
							<Music className="mx-auto mb-4 h-16 w-16 opacity-50" />
							<p>Audio player would be rendered here</p>
							<p className="mt-2 text-xs text-gray-600">Save will prompt for export settings</p>
							<button
								onClick={() => markDirty()}
								className="mt-4 rounded bg-gray-800 px-3 py-1 text-xs transition-colors hover:bg-gray-700"
							>
								Mark as Modified
							</button>
						</div>
					</div>
				) : currentDocument.type === 'emulator' ? (
					<div className="flex h-full items-center justify-center text-gray-500">
						<SystemPanel />
					</div>
				) : (
					<div className="flex h-full items-center justify-center text-gray-500">Unsupported document type</div>
				)}
			</div>
		</div>
	);
};
