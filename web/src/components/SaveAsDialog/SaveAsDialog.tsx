import { File } from 'lucide-react';
import { useState } from 'react';

import type { DocumentType } from '../Layout/types';

interface SaveAsDialogProps {
	documentType: DocumentType;
	defaultFilename?: string;
	onSave: (filename: string) => void;
	onCancel: () => void;
}

export const SaveAsDialog: React.FC<SaveAsDialogProps> = ({
	documentType,
	defaultFilename = '',
	onSave,
	onCancel,
}) => {
	const [filename, setFilename] = useState(defaultFilename);

	const getFileExtension = (docType: DocumentType) => {
		switch (docType) {
			case 'text': return '.txt';
			case 'audio': return '.mp3';
			case 'emulator': return '.sav';
			default: return '';
		}
	};

	const handleSave = () => {
		const finalFilename = filename.trim() || `Untitled${getFileExtension(documentType)}`;
		onSave(finalFilename);
	};

	const handleKeyDown = (e: React.KeyboardEvent) => {
		if (e.key === 'Enter' && filename.trim()) {
			handleSave();
		} else if (e.key === 'Escape') {
			onCancel();
		}
	};

	return (
		<div>
			<div className="mb-4">
				<File className="mx-auto h-12 w-12 text-blue-400" />
			</div>
			<div className="space-y-4">
				<div>
					<label className="mb-2 block text-sm font-medium text-gray-300">
						Enter filename:
					</label>
					<input
						type="text"
						value={filename}
						onChange={(e) => setFilename(e.target.value)}
						onKeyDown={handleKeyDown}
						placeholder={`Untitled`}
						className="w-full rounded-lg border border-gray-600 bg-gray-800 px-3 py-2 text-white placeholder-gray-400 focus:border-blue-500 focus:outline-none"
						autoFocus
					/>
				</div>
			</div>

			<div className="mt-6 flex gap-3">
				<button
					onClick={onCancel}
					className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
				>
					Cancel
				</button>
				<button
					onClick={handleSave}
					disabled={!filename.trim()}
					className="flex-1 rounded-lg bg-blue-600 px-4 py-2 text-white transition-colors hover:bg-blue-700 disabled:bg-gray-600 disabled:cursor-not-allowed"
				>
					Save
				</button>
			</div>
		</div>
	);
};