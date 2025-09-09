import React, { useEffect, useState } from 'react';

import { LsdjStoreProvider } from '../../contexts/LsdjStoreProvider';
import { useRetroPlug } from '../../contexts/RetroPlugContext';
import type { ILsdjKit, ILsdjRom } from '../../types/LsdjTypes';
import { generateKey } from '../../utils/LsdjUtil';
import { type SystemId, toUint8Array } from '../../utils/NativeUtil';
import { LsdjRomEditor } from './LsdjRomEditor';

const addKeysToKits = (kits: ILsdjKit[]): ILsdjKit[] => {
	return kits.map((kit) => ({
		...kit,
		key: generateKey(),
		samples: kit.samples?.map((sample) => ({
			...sample,
			key: generateKey(),
			effects: sample.effects?.map((effect) => ({
				...effect,
				key: generateKey(),
			})),
		})),
	}));
};

export const LsdjRomMemoryEditor: React.FC<{ system: SystemId }> = ({ system }) => {
	const { project } = useRetroPlug();
	const [rom, setRom] = useState<ILsdjRom | null>(null);

	useEffect(() => {
		if (!project) return;

		const lsdj = project.lsdj;
		let kits = lsdj.getKits(system);
		console.log('before keys:');
		console.log(kits);
		kits = addKeysToKits(kits);
		console.log('added keys:');
		console.log(kits);

		kits.map((kit) => {
			const buffer = project.lsdj.getKitData(system, kit.id);
			if (buffer) kit.data = toUint8Array(buffer);
		});

		//if (!deepEqual(indexedKits, romKits)) {
		setRom({
			id: 0,
			key: generateKey(),
			name: 'LSDj',
			kits,
		});
		//}
	}, [project, system]);

	const handleKitAdded = (kitKey: string, kit?: ILsdjKit) => {
		console.log('Kit added:', kitKey, kit);
	};

	const handleKitRemoved = (kitKey: string, kit?: ILsdjKit) => {
		console.log('Kit removed:', kitKey, kit);
	};

	const handleKitChanged = (kitKey: string, kit?: ILsdjKit) => {
		console.log('Kit changed:', kitKey, kit);
	};

	console.log('rendering root!');

	return rom ? (
		<LsdjStoreProvider initialRom={rom}>
			<LsdjRomEditor onKitAdded={handleKitAdded} onKitRemoved={handleKitRemoved} onKitChanged={handleKitChanged} />
		</LsdjStoreProvider>
	) : (
		<></>
	);
};
