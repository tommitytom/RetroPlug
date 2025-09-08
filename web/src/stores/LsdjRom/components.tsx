// components.tsx
import React, { useState } from 'react';
import { useLsdjStore } from './hooks';
import type { IFilterEffect, IGainEffect, ILsdjKit, ILsdjRom, LsdjEffect } from '../../types/LsdjTypes';
import { createFilterEffect, createGainEffect } from './util';
import { LsdjStoreProvider } from './provider';

import '../../styles/teststyles.css';

// ============= Effect Parameter Editor =============
interface LsdjEffectParameterEditorProps {
	kitKey: string;
	sampleKey?: string;
	effectKey: string;
	paramName: string;
	value: number;
}

export const LsdjEffectParameterEditor: React.FC<LsdjEffectParameterEditorProps> = ({
	kitKey,
	sampleKey,
	effectKey,
	paramName,
	value,
}) => {
	const updateKitEffect = useLsdjStore(state => state.updateKitEffect);
	const updateSampleEffect = useLsdjStore(state => state.updateSampleEffect);

	const handleChange = (newValue: number) => {
		if (sampleKey) {
			updateSampleEffect(kitKey, sampleKey, effectKey, { [paramName]: newValue } as any);
		} else {
			updateKitEffect(kitKey, effectKey, { [paramName]: newValue } as any);
		}
	};

	return (
		<div className="parameter-editor">
			<label>
				{paramName}:
				<input
					type="range"
					min="0"
					max={paramName === 'gain' ? 2 : paramName === 'freq' ? 20000 : 100}
					step={0.01}
					value={value}
					onChange={(e) => handleChange(parseFloat(e.target.value))}
				/>
				<span>{value.toFixed(2)}</span>
			</label>
		</div>
	);
};

// ============= Effect Editor =============
interface LsdjEffectEditorProps {
	kitKey: string;
	sampleKey?: string;
	effect: LsdjEffect;
}

export const LsdjEffectEditor: React.FC<LsdjEffectEditorProps> = ({
	kitKey,
	sampleKey,
	effect,
}) => {
	const removeKitEffect = useLsdjStore(state => state.removeKitEffect);
	const removeSampleEffect = useLsdjStore(state => state.removeSampleEffect);

	const handleRemove = () => {
		if (sampleKey) {
			removeSampleEffect(kitKey, sampleKey, effect.key);
		} else {
			removeKitEffect(kitKey, effect.key);
		}
	};

	const renderParameters = () => {
		switch (effect.type) {
			case 'gain':
				return (
					<LsdjEffectParameterEditor
						kitKey={kitKey}
						sampleKey={sampleKey}
						effectKey={effect.key}
						paramName="gain"
						value={(effect as IGainEffect).gain}
					/>
				);
			case 'filter':
				return (
					<>
						<LsdjEffectParameterEditor
							kitKey={kitKey}
							sampleKey={sampleKey}
							effectKey={effect.key}
							paramName="freq"
							value={(effect as IFilterEffect).freq}
						/>
						<LsdjEffectParameterEditor
							kitKey={kitKey}
							sampleKey={sampleKey}
							effectKey={effect.key}
							paramName="q"
							value={(effect as IFilterEffect).q}
						/>
						<LsdjEffectParameterEditor
							kitKey={kitKey}
							sampleKey={sampleKey}
							effectKey={effect.key}
							paramName="feedback"
							value={(effect as IFilterEffect).feedback}
						/>
					</>
				);
		}
	};

	return (
		<div className="effect-editor">
			<div className="effect-header">
				<h4>{effect.type}</h4>
				<button onClick={handleRemove}>Remove</button>
			</div>
			{renderParameters()}
		</div>
	);
};

// ============= Effect List =============
interface LsdjEffectListProps {
	kitKey: string;
	sampleKey?: string;
	title: string;
}

export const LsdjEffectList: React.FC<LsdjEffectListProps> = ({
	kitKey,
	sampleKey,
	title,
}) => {
	const kit = useLsdjStore(state => state.getKit(kitKey));
	const sample = useLsdjStore(state => sampleKey ? state.getSample(kitKey, sampleKey) : undefined);
	const addKitEffect = useLsdjStore(state => state.addKitEffect);
	const addSampleEffect = useLsdjStore(state => state.addSampleEffect);

	const effects = sampleKey ? sample?.effects || [] : kit?.effects || [];

	const handleAddEffect = (type: 'gain' | 'filter') => {
		const newEffect = type === 'gain'
			? createGainEffect()
			: createFilterEffect();

		if (sampleKey) {
			addSampleEffect(kitKey, sampleKey, newEffect);
		} else {
			addKitEffect(kitKey, newEffect);
		}
	};

	return (
		<div className="effect-list">
			<h3>{title}</h3>
			<div className="add-effect-buttons">
				<button onClick={() => handleAddEffect('gain')}>Add Gain</button>
				<button onClick={() => handleAddEffect('filter')}>Add Filter</button>
			</div>
			<div className="effects">
				{effects.map((effect) => (
					<LsdjEffectEditor
						key={effect.key}
						kitKey={kitKey}
						sampleKey={sampleKey}
						effect={effect as LsdjEffect}
					/>
				))}
			</div>
		</div>
	);
};

// ============= Sample Editor =============
interface LsdjSampleEditorProps {
	kitKey: string;
	sampleKey: string;
}

export const LsdjSampleEditor: React.FC<LsdjSampleEditorProps> = ({
	kitKey,
	sampleKey,
}) => {
	const sample = useLsdjStore(state => state.getSample(kitKey, sampleKey));
	const renameSample = useLsdjStore(state => state.renameSample);
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
			<LsdjEffectList
				kitKey={kitKey}
				sampleKey={sampleKey}
				title="Sample Effects"
			/>
		</div>
	);
};

// ============= Kit Editor =============
interface LsdjKitEditorProps {
	kitKey: string;
}

export const LsdjKitEditor: React.FC<LsdjKitEditorProps> = ({ kitKey }) => {
	const kit = useLsdjStore(state => state.getKit(kitKey));
	const renameKit = useLsdjStore(state => state.renameKit);
	const [isEditing, setIsEditing] = useState(false);
	const [tempName, setTempName] = useState(kit?.name || '');

	if (!kit) return null;

	const handleRename = () => {
		renameKit(kitKey, tempName);
		setIsEditing(false);
	};

	return (
		<div className="kit-editor">
			<div className="kit-header">
				{isEditing ? (
					<input
						value={tempName}
						onChange={(e) => setTempName(e.target.value)}
						onBlur={handleRename}
						onKeyPress={(e) => e.key === 'Enter' && handleRename()}
						autoFocus
					/>
				) : (
					<h2 onClick={() => setIsEditing(true)}>{kit.name}</h2>
				)}
			</div>

			<div className="samples">
				<h3>Samples</h3>
				{kit.samples?.map((sample) => (
					<LsdjSampleEditor
						key={sample.id}
						kitKey={kitKey}
						sampleKey={sample.key}
					/>
				))}
			</div>

			<LsdjEffectList
				kitKey={kitKey}
				title="Kit Effects"
			/>
		</div>
	);
};

// ============= ROM Editor =============
interface LsdjRomEditorProps {
	rom: ILsdjRom;
}

export const LsdjRomEditor: React.FC<LsdjRomEditorProps> = ({ rom }) => {
	const selectedKitKey = useLsdjStore(state => state.selectedKitKey);
	const selectKit = useLsdjStore(state => state.selectKit);

	return (
		<div className="rom-editor">
			<h1>ROM: {rom.name}</h1>
			<div className="kit-tabs">
				{rom.kits.map((kit) => (
					<button
						key={kit.key}
						className={selectedKitKey === kit.key ? 'active' : ''}
						onClick={() => selectKit(kit.key)}
					>
						{kit.name}
					</button>
				))}
			</div>
			{selectedKitKey && <LsdjKitEditor kitKey={selectedKitKey} />}
		</div>
	);
};

// ============= App Examples =============
// Example 1: Multiple ROM Editors
export const MultipleRomEditorsApp: React.FC = () => {
	const [roms] = useState<ILsdjRom[]>([
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
				},
			],
		},
		{
			id: 2,
			key: 'rom2',
			name: 'ROM 2',
			kits: [
				{
					id: 0,
					key: 'kit2',
					name: 'Kit 2',
					samples: [],
					effects: [],
				},
			],
		},
	]);

	return (
		<div className="app">
			{roms.map((rom) => (
				<LsdjStoreProvider key={rom.key} initialRom={rom}>
					<LsdjRomEditor rom={rom} />
				</LsdjStoreProvider>
			))}
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
				<LsdjKitEditor kitKey={kit.key} />
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
					<LsdjKitEditor kitKey={currentKit.key} />
				)}
			</div>
		</LsdjStoreProvider>
	);
};
