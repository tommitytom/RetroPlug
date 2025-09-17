import { useEffect } from 'react';

import { useDocument } from '../contexts/DocumentContext';
import { useIsProjectDirty, useProject } from '../hooks/RetroPlugHooks';
import { RetroPlugCanvas } from '../RetroPlugCanvas';

export const SystemPanel: React.FC = () => {
	const { markDirty, markClean, updateDocument } = useDocument();
	const project = useProject();
	const { isDirty, requiresReset } = useIsProjectDirty();

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
		<div className="relative h-full w-full">
			<div className="flex h-full w-full items-center justify-center">
				<RetroPlugCanvas />
			</div>
			{requiresReset && (
				<div className="absolute bottom-8 left-1/2 flex -translate-x-1/2 transform flex-col items-center gap-2">
					<span>Requires reset to enable patched kits</span>
					<button
						className="rounded bg-blue-500 px-4 py-2 text-white hover:bg-blue-600"
						onClick={() => project.resetSystems()}
					>
						Reset
					</button>
				</div>
			)}
		</div>
	);
};
