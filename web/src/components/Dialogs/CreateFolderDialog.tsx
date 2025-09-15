import React, { useState, useRef, useEffect } from 'react';
import { sanitizeFilename, validateFilename } from '../../utils/FileUtil';

export const CreateFolderDialog: React.FC<{
	onSelect: (path: string) => void;
	onClose: () => void;
}> = ({ onSelect, onClose }) => {
	const [fileName, setFileName] = useState<string | null>(null);
	const [error, setError] = useState<string>('');
	const inputRef = useRef<HTMLInputElement>(null);

	useEffect(() => {
		// Focus the input when the component mounts
		if (inputRef.current) {
			inputRef.current.focus();
		}
	}, []);

	const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
		const rawValue = e.target.value;
		const sanitized = sanitizeFilename(rawValue, true); // Use strict mode for folders
		const validationError = validateFilename(sanitized);

		setFileName(sanitized);
		setError(validationError);
	};

	const isValid = fileName && fileName.length > 0 && error === '';

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();
		if (!fileName || error) {
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
						ref={inputRef}
						type="text"
						value={fileName || ''}
						onChange={handleInputChange}
						className={`w-full rounded-lg border px-3 py-2 text-white placeholder-gray-400 transition-colors focus:ring-1 focus:outline-none ${
							error
								? 'border-red-500 bg-red-900/20 focus:border-red-400 focus:ring-red-400'
								: 'border-gray-600 bg-gray-700 focus:border-blue-500 focus:ring-blue-500'
						}`}
						placeholder="Enter folder name..."
					/>
					{error && (
						<p className="mt-2 text-sm text-red-400">{error}</p>
					)}
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
						disabled={!isValid}
						className="flex-1 rounded-lg bg-blue-600 px-4 py-2 text-white transition-colors hover:bg-blue-700 disabled:cursor-not-allowed disabled:bg-gray-600 disabled:text-gray-400"
					>
						Select
					</button>
				</div>
			</form>
		</div>
	);
};
