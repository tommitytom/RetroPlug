import React, { useState } from 'react';

export const CreateFolderDialog: React.FC<{
	onSelect: (path: string) => void;
	onClose: () => void;
}> = ({ onSelect, onClose }) => {
	const [fileName, setFileName] = useState<string | null>(null);

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();
		if (!fileName) {
			onClose();
		} else {
			onSelect(fileName);
		}
	};

	return (
		<div>
			<form onSubmit={handleSubmit} className="space-y-6">
				<div>
					<label className="mb-3 block text-sm font-medium text-white">Choose a name for your sample folder:</label>
					<input
						type="text"
						value={fileName || ''}
						onChange={(e) => setFileName(e.target.value.trim())}
						className="w-full rounded-lg border border-gray-600 bg-gray-700 px-3 py-2 text-white placeholder-gray-400 transition-colors focus:border-blue-500 focus:ring-1 focus:ring-blue-500 focus:outline-none"
					/>
				</div>
				<div className="flex gap-3">
					<button
						type="button"
						onClick={onClose}
						className="flex-1 rounded-lg bg-gray-700 px-4 py-2 text-gray-200 transition-colors hover:bg-gray-600"
					>
						Cancel
					</button>
					<button
						type="submit"
						disabled={!fileName}
						className="flex-1 rounded-lg bg-blue-600 px-4 py-2 text-white transition-colors hover:bg-blue-700 disabled:cursor-not-allowed disabled:bg-gray-600 disabled:text-gray-400"
					>
						Select
					</button>
				</div>
			</form>
		</div>
	);
};
