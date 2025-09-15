import React, { useEffect, useState } from 'react';

import { LsdjStoreProvider } from '../../contexts/LsdjStoreProvider';
import { useProject } from '../../hooks/RetroPlugHooks';
import { type ILsdjRom } from '../../types/LsdjTypes';
import { type SystemId } from '../../utils/NativeUtil';
import { LsdjRomEditor } from './LsdjRomEditor';

export const LsdjRomMemoryEditor: React.FC = () => {
	const project = useProject();
	const [rom, setRom] = useState<ILsdjRom | null>(null);
	const [systemIds, setSystemIds] = useState<SystemId[]>([]);

	useEffect(() => {
		const ids = project.getSystemIds().sort((a, b) => a - b); // Sort system IDs in ascending order
		setSystemIds(ids);
	}, [project, setSystemIds]);

	useEffect(() => {
		if (systemIds.length === 0) {
			setRom(null);
			return;
		}

		try {
			const lsdj = project.lsdj;
			let kits = lsdj.getKits(systemIds[0]);

			//if (!deepEqual(indexedKits, romKits)) {
			setRom({
				id: 0,
				key: `lsdj-rom-${systemIds[0]}`, // Use system ID as stable key
				name: 'LSDj',
				kits,
			});
			//}
		} catch (ex) {
			console.error('Failed to inspect ROM', ex);
		}
	}, [project, systemIds]);

	return rom ? (
		<LsdjStoreProvider lsdj={project.lsdj} system={systemIds[0]} initialRom={rom}>
			<LsdjRomEditor />
		</LsdjStoreProvider>
	) : (
		<></>
	);
};
