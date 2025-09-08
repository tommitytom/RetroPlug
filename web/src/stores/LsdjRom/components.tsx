// components.tsx
import React, { useEffect, useState } from 'react';

import type { ILsdjKit, ILsdjRom } from '../../types/LsdjTypes';
import { LsdjKitEditor } from './LsdjKitEditor';
import { LsdjRomEditor } from './LsdjRomEditor';
import { LsdjStoreProvider } from './provider';

export const LsdjRomFileEditor: React.FC<{ rom: ILsdjRom }> = ({ rom }) => {
	return (
		<LsdjStoreProvider initialRom={rom}>
			<ChangeListener />
			<LsdjRomEditor />
		</LsdjStoreProvider>
	);
};


// ============= App Examples =============
// Example 1: Multiple ROM Editors
export const MultipleRomEditorsApp: React.FC = () => {
	const [roms, setRoms] = useState<ILsdjRom[]>([]);

	useEffect(() => {
		fetch('TR-606.kit').then(async (response) => {
			const data = new Uint8Array(await response.arrayBuffer());
			setRoms([
				{
					id: 1,
					key: 'rom1',
					name: 'ROM 1',
					kits: [
						{
							id: 0,
							key: 'kit1',
							name: 'Kit 1',
							samples: [
								{
									id: 0,
									key: 'sample1',
									name: 'Kick',
									path: '/samples/kick.wav',
									offset: 0,
									length: 1000,
									effects: [],
								},
							],
							effects: [],
							data,
						},
						{
							id: 1,
							key: 'kit2',
							name: 'Kit 2',
							samples: [
								{
									id: 0,
									key: 'sample1',
									name: 'Kick',
									path: '/samples/kick.wav',
									offset: 0,
									length: 1000,
									effects: [],
								},
							],
							effects: [],
							data,
						},
					],
				}
			]);
		});
	}, []);

	const handleChange = (state, prevState) => {

	};

	return (
		<div className="app">
			{roms.length > 0 && (
				<LsdjStoreProvider key={roms[0].key} initialRom={roms[0]}>
				</LsdjStoreProvider>
			)}
		</div>
	);
};

// Example 2: Standalone Kit Editor
export const StandaloneKitEditorApp: React.FC = () => {
	const kit: ILsdjKit = {
		id: 0,
		key: 'standalone-kit',
		name: 'My Standalone Kit',
		samples: [
			{
				id: 0,
				key: 'sample1',
				name: 'Snare',
				path: '/samples/snare.wav',
				offset: 0,
				length: 500,
				effects: [],
			},
		],
		effects: [],
	};

	return (
		<LsdjStoreProvider initialKit={kit}>
			<div className="app">
				<h1>Standalone Kit Editor</h1>
				<LsdjKitEditor kitKey={kit.key} isExpanded={false} onToggle={() => {}} />
			</div>
		</LsdjStoreProvider>
	);
};

// Example 3: Hybrid App - Can edit ROMs or standalone kits
export const HybridEditorApp: React.FC = () => {
	const [mode, setMode] = useState<'rom' | 'kit'>('rom');
	const [currentRom] = useState<ILsdjRom>({
		id: 0,
		key: 'hybrid-rom',
		name: 'My ROM',
		kits: [
			{
				id: 0,
				key: 'rom-kit1',
				name: 'ROM Kit 1',
				samples: [],
				effects: [],
			},
		],
	});

	const [currentKit] = useState<ILsdjKit>({
		id: 0,
		key: 'hybrid-kit',
		name: 'Standalone Kit',
		samples: [],
		effects: [],
	});

	return (
		<LsdjStoreProvider
			initialRom={mode === 'rom' ? currentRom : undefined}
			initialKit={mode === 'kit' ? currentKit : undefined}
		>
			<div className="app">
				<div className="mode-selector">
					<button onClick={() => setMode('rom')}>Edit ROM</button>
					<button onClick={() => setMode('kit')}>Edit Kit</button>
				</div>

				{mode === 'rom' ? (
					<LsdjRomEditor rom={currentRom} />
				) : (
					<LsdjKitEditor kitKey={currentKit.key} isExpanded={false} onToggle={() => {}} />
				)}
			</div>
		</LsdjStoreProvider>
	);
};
