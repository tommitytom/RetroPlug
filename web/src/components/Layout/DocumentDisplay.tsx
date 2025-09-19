import { ChevronDown, File, FileText, Music, Save } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';

import { useDocument } from '../../contexts/DocumentContext';
import { SystemPanel } from '../../panels/SystemPanel';

export const DocumentDisplay: React.FC = () => {
	const { currentDocument, markDirty, saveDocument } = useDocument();
	const [content, setContent] = useState('');
	const [showSaveDropdown, setShowSaveDropdown] = useState(false);
	const dropdownRef = useRef<HTMLDivElement>(null);

	useEffect(() => {
		const handleKeyDown = (event: KeyboardEvent) => {
			if ((event.ctrlKey || event.metaKey) && event.key === 's') {
				event.preventDefault();
				saveDocument(event.shiftKey); // Ctrl+Shift+S for Save As
			}
		};

		const handleClickOutside = (event: MouseEvent) => {
			if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node)) {
				setShowSaveDropdown(false);
			}
		};

		window.addEventListener('keydown', handleKeyDown);
		document.addEventListener('mousedown', handleClickOutside);

		return () => {
			window.removeEventListener('keydown', handleKeyDown);
			document.removeEventListener('mousedown', handleClickOutside);
		};
	}, [saveDocument]);

	const getDocumentIcon = () => {
		switch (currentDocument?.type) {
			case 'text':
				return <FileText className="h-4 w-4" />;
			case 'audio':
				return <Music className="h-4 w-4" />;
			case 'emulator':
				return <Music className="h-4 w-4" />;
			default:
				return <File className="h-4 w-4" />;
		}
	};

	if (!currentDocument) {
		return (
			<div className="flex h-full items-center justify-center">
				<div className="text-center text-gray-500">
					<p className="mb-4">No project open</p>
				</div>
			</div>
		);
	}

	return (
		<div className="flex h-full flex-col">
			{/* Document Header */}
			<div className="flex items-center gap-2 border-b border-gray-700 bg-gray-800 px-4 py-1">
				{getDocumentIcon()}
				<span className="text-sm font-medium">
					{currentDocument.title}
					{!currentDocument.hasFilename && <span className="ml-1 text-xs text-gray-500">(unsaved)</span>}
				</span>
				{currentDocument.isDirty && <span className="text-sm text-yellow-500">•</span>}
				<div className="ml-auto flex items-center">
					<div className="relative" ref={dropdownRef}>
						<div className="flex">
							<button
								onClick={currentDocument.isDirty ? () => saveDocument() : undefined}
								disabled={!currentDocument.isDirty}
								title={currentDocument.isDirty ? 'Save (Ctrl+S)' : 'No changes to save'}
								className={`rounded-l p-1 ${
									currentDocument.isDirty ? 'text-gray-300 hover:bg-gray-700 hover:text-white' : 'text-gray-600'
								}`}
							>
								<Save className="h-4 w-4" />
							</button>
							<button
								onClick={() => setShowSaveDropdown(!showSaveDropdown)}
								className="rounded-r p-1 text-gray-300 hover:bg-gray-700 hover:text-white"
								title="Save options"
							>
								<ChevronDown className="h-3 w-3" />
							</button>
						</div>

						{showSaveDropdown && (
							<div className="absolute right-0 top-full mt-1 w-32 rounded-md border border-gray-600 bg-gray-800 py-1 shadow-lg z-10">
								<button
									onClick={() => {
										saveDocument();
										setShowSaveDropdown(false);
									}}
									className="w-full px-3 py-1 text-left text-sm text-gray-200 hover:bg-gray-700"
								>
									Save
								</button>
								<button
									onClick={() => {
										saveDocument(true);
										setShowSaveDropdown(false);
									}}
									className="w-full px-3 py-1 text-left text-sm text-gray-200 hover:bg-gray-700"
								>
									Save As...
								</button>
							</div>
						)}
					</div>
				</div>
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
