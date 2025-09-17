import { useEffect } from 'react';

import { useDocument } from '../contexts/DocumentContext';
import { useIsProjectDirty, useProject } from '../hooks/RetroPlugHooks';
import { RetroPlugCanvas } from '../RetroPlugCanvas';

export const SystemPanel: React.FC = () => {
	const { markDirty, markClean, updateDocument } = useDocument();
	const project = useProject();
	const isDirty = useIsProjectDirty();

	useEffect(() => {
		updateDocument({ title: project.getProjectName() });
	}, [project]);

	useEffect(() => {
		if (isDirty) {
			markDirty();
		} else {
			markClean();
		}
	}, [project, isDirty]);

	return (
		<div className="flex h-full w-full items-center justify-center">
			<RetroPlugCanvas />
			<span>requires reset!</span>
		</div>
	);
};
