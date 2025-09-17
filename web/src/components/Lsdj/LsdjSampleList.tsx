import React, { useCallback, useState } from 'react';

import { EditableText } from '../../components/EditableText';
import { DeleteIcon } from '../../components/DeleteIcon';
import { useLsdjStore } from '../../hooks/LsdjStoreHooks';
import type { ILsdjEditableKit, ILsdjKitSample } from '../../types/LsdjTypes';
import { sanitizeSampleName } from '../../utils/LsdjUtil';

interface LsdjSampleListProps {
	kitKey: string;
	isExpanded: boolean;
	onToggle: (value?: boolean) => void;
	title?: string;
}

interface DragItem {
	index: number;
	sample: ILsdjKitSample;
}

export const LsdjSampleList: React.FC<LsdjSampleListProps> = ({
	kitKey,
	isExpanded,
	onToggle,
	title = "Samples"
}) => {
	const kit = useLsdjStore(state => state.getKit(kitKey));
	const removeSample = useLsdjStore(state => state.removeSample);
	const renameSample = useLsdjStore(state => state.renameSample);
	const reorderSamples = useLsdjStore(state => state.reorderSamples);

	const [draggedIndex, setDraggedIndex] = useState<number | null>(null);
	const [dragOverIndex, setDragOverIndex] = useState<number | null>(null);

	if (!kit || kit.kit.type !== 'editable') {
		return null;
	}

	const editableKit = kit.kit as ILsdjEditableKit;
	const samples = editableKit.samples || [];

	const handleSampleNameChange = useCallback((sampleKey: string, newName: string) => {
		renameSample(kitKey, sampleKey, newName);
	}, [kitKey, renameSample]);

	const handleDeleteSample = useCallback((sampleKey: string) => {
		removeSample(kitKey, sampleKey);
	}, [kitKey, removeSample]);

	const handleDragStart = useCallback((e: React.DragEvent, index: number) => {
		e.stopPropagation(); // Prevent parent from handling this drag
		setDraggedIndex(index);
		e.dataTransfer.effectAllowed = 'move';
		e.dataTransfer.setData('application/x-sample-reorder', index.toString());
		e.dataTransfer.setData('text/plain', ''); // Clear any other data to prevent parent handling
	}, []);

	const handleDragOver = useCallback((e: React.DragEvent, index: number) => {
		// Only handle our internal sample reorder drags
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.preventDefault();
			e.stopPropagation();
			e.dataTransfer.dropEffect = 'move';
			setDragOverIndex(index);
		}
	}, []);

	const handleDragLeave = useCallback((e: React.DragEvent) => {
		// Only handle our internal sample reorder drags
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			// Only clear if we're actually leaving the drop zone
			if (!e.currentTarget.contains(e.relatedTarget as Node)) {
				setDragOverIndex(null);
			}
		}
	}, []);

	const handleDragEnter = useCallback((e: React.DragEvent) => {
		// Prevent parent from handling internal sample drags
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.stopPropagation();
		}
	}, []);

	const handleDrop = useCallback((e: React.DragEvent, dropIndex: number) => {
		// Only handle our internal sample reorder drags
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.preventDefault();
			e.stopPropagation();
			const dragIndex = parseInt(e.dataTransfer.getData('application/x-sample-reorder'), 10);

			if (dragIndex !== dropIndex && dragIndex >= 0 && dropIndex >= 0) {
				reorderSamples(kitKey, dragIndex, dropIndex);
			}

			setDraggedIndex(null);
			setDragOverIndex(null);
		}
	}, [kitKey, reorderSamples]);

	const handleDragEnd = useCallback(() => {
		setDraggedIndex(null);
		setDragOverIndex(null);
	}, []);

	// Container drag handlers to prevent parent interference
	const handleContainerDragOver = useCallback((e: React.DragEvent) => {
		// If this is an internal sample reorder, prevent parent from handling it
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.stopPropagation();
		}
	}, []);

	const handleContainerDragEnter = useCallback((e: React.DragEvent) => {
		// If this is an internal sample reorder, prevent parent from handling it
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.stopPropagation();
		}
	}, []);

	const handleContainerDragLeave = useCallback((e: React.DragEvent) => {
		// If this is an internal sample reorder, prevent parent from handling it
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.stopPropagation();
		}
	}, []);

	const handleContainerDrop = useCallback((e: React.DragEvent) => {
		// If this is an internal sample reorder, prevent parent from handling it
		if (e.dataTransfer.types.includes('application/x-sample-reorder')) {
			e.stopPropagation();
		}
	}, []);

	return (
		<div
			className="mt-2 overflow-hidden rounded-sm border border-gray-700"
			onDragOver={handleContainerDragOver}
			onDragEnter={handleContainerDragEnter}
			onDragLeave={handleContainerDragLeave}
			onDrop={handleContainerDrop}
		>
			<div
				className="hover:bg-gray-750 flex cursor-pointer items-center justify-between bg-gray-800 px-2 py-1 text-sm font-medium transition-colors duration-200"
				onClick={() => onToggle()}
			>
				<div className="flex items-center">
					<div className="mr-2 flex h-3 w-3 items-center justify-center">
						{isExpanded ? (
							<div className="h-0 w-0 border-t-6 border-r-4 border-l-4 border-t-white border-r-transparent border-l-transparent" />
						) : (
							<div className="h-0 w-0 border-t-4 border-b-4 border-l-6 border-t-transparent border-b-transparent border-l-white" />
						)}
					</div>
					<span className="font-medium text-white">{title}</span>
					<span className="ml-2 text-xs text-gray-400">({samples.length})</span>
				</div>
			</div>

			{isExpanded && (
				<div className="bg-gray-900 p-2">
					{samples.length === 0 ? (
						<div className="py-2 text-center text-sm text-gray-500">
							No samples in this kit
						</div>
					) : (
						<div className="space-y-1">
							{samples.map((sample, index) => (
								<div
									key={sample.key}
									draggable
									onDragStart={(e) => handleDragStart(e, index)}
									onDragEnter={handleDragEnter}
									onDragOver={(e) => handleDragOver(e, index)}
									onDragLeave={handleDragLeave}
									onDrop={(e) => handleDrop(e, index)}
									onDragEnd={handleDragEnd}
									className={`flex items-center justify-between rounded border px-2 py-1 text-xs transition-all duration-200 ${
										draggedIndex === index
											? 'border-blue-500 bg-blue-500/20 opacity-50'
											: dragOverIndex === index
											? 'border-blue-400 bg-blue-400/10'
											: 'border-gray-600 bg-gray-800 hover:bg-gray-700'
									} ${draggedIndex !== null ? 'cursor-grabbing' : 'cursor-grab'}`}
								>
									<div className="flex items-center space-x-2">
										<div className="flex h-4 w-4 items-center justify-center text-gray-300">
											<svg
												width="12"
												height="12"
												viewBox="0 0 24 24"
												fill="currentColor"
											>
												<circle cx="9" cy="12" r="2"/>
												<circle cx="9" cy="5" r="2"/>
												<circle cx="9" cy="19" r="2"/>
												<circle cx="15" cy="12" r="2"/>
												<circle cx="15" cy="5" r="2"/>
												<circle cx="15" cy="19" r="2"/>
											</svg>
										</div>
										<div className="flex items-center space-x-1">
											<EditableText
												value={sample.name || 'UNK'}
												onChange={(newName) => sample.key && handleSampleNameChange(sample.key, newName)}
												className="font-mono text-white"
												maxLength={3}
												validator={sanitizeSampleName}
												title="Click to edit sample name"
											/>
											<span className="text-gray-500">-</span>
											<span
												className="text-gray-300 hover:text-white cursor-pointer truncate max-w-48"
												title={sample.path}
											>
												{sample.path.split('/').pop() || sample.path}
											</span>
										</div>
									</div>
									<DeleteIcon
										onClick={(e) => {
											e.stopPropagation();
											sample.key && handleDeleteSample(sample.key);
										}}
										className="rounded-sm p-1 text-red-400 transition-colors duration-200 hover:bg-red-600/20 hover:text-red-300"
										title="Remove sample"
									/>
								</div>
							))}
						</div>
					)}
				</div>
			)}
		</div>
	);
};