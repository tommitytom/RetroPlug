import React, { useState } from 'react';

import { useLsdjStore } from './hooks';
import { LsdjEffectList } from './LsdjEffectList';

interface LsdjSampleEditorProps {
	kitKey: string;
	sampleKey: string;
}

export const LsdjSampleEditor: React.FC<LsdjSampleEditorProps> = ({ kitKey, sampleKey }) => {
	const sample = useLsdjStore((state) => state.getSample(kitKey, sampleKey));
	const renameSample = useLsdjStore((state) => state.renameSample);
	const [isEditing, setIsEditing] = useState(false);
	const [tempName, setTempName] = useState(sample?.name || '');

	if (!sample) return null;

	const handleRename = () => {
		renameSample(kitKey, sampleKey, tempName);
		setIsEditing(false);
	};

	return (
		<div className="sample-editor">
			<div className="sample-header">
				{isEditing ? (
					<input
						value={tempName}
						onChange={(e) => setTempName(e.target.value)}
						onBlur={handleRename}
						onKeyPress={(e) => e.key === 'Enter' && handleRename()}
						autoFocus
					/>
				) : (
					<h3 onClick={() => setIsEditing(true)}>{sample.name}</h3>
				)}
			</div>
			<div className="sample-info">
				<p>Offset: {sample.offset}</p>
				<p>Length: {sample.length}</p>
			</div>
			<LsdjEffectList kitKey={kitKey} sampleKey={sampleKey} title="Sample Effects" isExpanded={true} onToggle={() => {}} />
		</div>
	);
};