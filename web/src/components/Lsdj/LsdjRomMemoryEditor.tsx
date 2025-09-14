import React, { useEffect, useState } from 'react';

import { LsdjStoreProvider } from '../../contexts/LsdjStoreProvider';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import type { LsdjStore } from '../../stores/LsdjStore';
import { type ILsdjKit, type ILsdjRom } from '../../types/LsdjTypes';
import { generateKey } from '../../utils/LsdjUtil';
import { toUint8Array, type SystemId } from '../../utils/NativeUtil';
import { LsdjRomEditor } from './LsdjRomEditor';
import { useProject } from '../../hooks/RetroPlugHooks';

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

			console.log('got kits!', kits, systemIds[0]);


			/*
			kits.map((kit) => {
				const buffer = project.lsdj.getKitData(systemIds[0], kit.id);
				if (buffer) kit.data = toUint8Array(buffer);
			});
			*/

			//if (!deepEqual(indexedKits, romKits)) {
			setRom({
				id: 0,
				key: generateKey(),
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
