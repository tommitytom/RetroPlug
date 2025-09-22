import { useEffect, useState } from 'react';

import { useProject } from '../../hooks/RetroPlugHooks';
import { LSDJ_SYNTH_COUNT } from '../../types/LsdjTypes';
import { type SystemId } from '../../utils/NativeUtil';
import { MemoryType } from '../../wrapper/System';
import { LsdjSynthView } from './LsdjSynthView';

export const LsdjSavMemoryEditor: React.FC = () => {
	const project = useProject();
	const [systemId, setSystemId] = useState<SystemId | null>(null);

	useEffect(() => {
		const ids = project.getSystemIds().sort((a, b) => a - b); // Sort system IDs in ascending order
		if (ids.length === 0) {
			setSystemId(null);
			return;
		}

		const system = ids[0];
		setSystemId(system);

		project.subscribeToMemory(system, MemoryType.Sram);

		return () => {
			project.unsubscribeFromMemory(system, MemoryType.Sram);
		};
	}, [project]);

	return systemId !== null ? (
		<div>
			{Array.from({ length: LSDJ_SYNTH_COUNT }).map((_, index) => (
				<LsdjSynthView key={index} system={systemId} synthId={index} />
			))}
		</div>
	) : (
		<></>
	);
};
