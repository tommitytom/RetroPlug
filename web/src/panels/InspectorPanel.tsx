import { useEffect, useState } from "react";
import { HexEditor } from "hex-editor-react";
import "hex-editor-react/dist/hex-editor.css";

import { useProject, useSystemMemory, useSystemMemoryVersion } from "../hooks/RetroPlugHooks";
import { MemoryType } from "../wrapper/System";
import { type SystemId, toUint8Array } from "../utils/NativeUtil";
import { LsdjRomMemoryEditor } from "../stores/LsdjRom/LsdjRomMemoryEditor";

const LsdjSavInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const [songName, setSongName] = useState("");
	const hash = useSystemMemoryVersion(systemId, MemoryType.Sram);

	useEffect(() => {
		if (!systemId) return;
		//const sav = system.lsdjSav;
		//const name = sav.workingProject.getName();
		//setSongName(name);
	}, [systemId, hash, setSongName]);

	return (
		<div>
			{hash}
			{songName}
		</div>
	);
};



const LsdjRamInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const hash = useSystemMemoryVersion(systemId, MemoryType.Ram);
	const [pos, setPos] = useState<{ x: number; y: number } | null>(null);

	useEffect(() => {
		//if (!system || !app) return;
		//setPos({ x: ram.getCursorX(), y: ram.getCursorY() });
	}, [systemId, hash]);

	return (
		<div>
			{pos && (
				<>
					<div>X: {pos.x}</div>
					<div>Y: {pos.y}</div>
				</>
			)}
		</div>
	);
};

const SystemMemoryInspector: React.FC<{ systemId: SystemId }> = ({ systemId }) => {
	const memory = useSystemMemory(systemId, MemoryType.Ram);

	return (
		<div>
			<HexEditor
				data={memory ? toUint8Array(memory.getBuffer()).slice().buffer : undefined}
			/>
		</div>

	);
};

type InspectorType = 'sav' | 'rom' | 'ram';

const SystemInspector: React.FC<{
	systemId: number;
	inspectorType: InspectorType;
}> = ({ systemId, inspectorType }) => {
	const renderInspector = () => {
		switch (inspectorType) {
			case 'sav':
				return <LsdjSavInspector systemId={systemId} />;
			case 'rom':
				return <LsdjRomMemoryEditor system={systemId} />;
			case 'ram':
				return <LsdjRamInspector systemId={systemId} />;
			default:
				return <SystemMemoryInspector systemId={systemId} />;
		}
	};

	return (
		<div>
			{renderInspector()}
		</div>
	);
};

export const InspectorPanel: React.FC = () => {
	const project = useProject();
	const [systemIds, setSystemIds] = useState<SystemId[]>([]);
	const [selectedSystemId, setSelectedSystemId] = useState<SystemId | null>(null);
	const [selectedInspectorType, setSelectedInspectorType] = useState<InspectorType>('rom');

	useEffect(() => {
		if (!project) {
			setSystemIds([]);
			setSelectedSystemId(null);
			return;
		}

		const ids = project.getSystemIds().sort((a, b) => a - b); // Sort system IDs in ascending order
		setSystemIds(ids);

		// Auto-select the first system if none is selected
		if (ids.length > 0 && (selectedSystemId === null || !ids.includes(selectedSystemId))) {
			setSelectedSystemId(ids[0]);
		}
	}, [project, selectedSystemId]);

	return (
		<div>
			<div className="flex items-center gap-2 px-3 py-2">
				<label htmlFor="inspector-select" className="text-white text-sm font-medium">
					Inspector:
				</label>
				<select
					id="inspector-select"
					value={selectedInspectorType}
					onChange={(e) => setSelectedInspectorType(e.target.value as InspectorType)}
					className="px-3 py-1 bg-gray-700 text-white rounded border border-gray-600 focus:border-blue-500 focus:outline-none text-sm"
				>
					<option value="rom">ROM</option>
					<option value="sav">SAV</option>
					<option value="ram">RAM</option>
				</select>
				{systemIds.length > 1 && (
					<>
						<label htmlFor="system-select" className="text-white text-sm font-medium ml-4">
							Select System:
						</label>
						<select
							id="system-select"
							value={selectedSystemId ?? ''}
							onChange={(e) => setSelectedSystemId(Number(e.target.value))}
							className="px-3 py-1 bg-gray-700 text-white rounded border border-gray-600 focus:border-blue-500 focus:outline-none text-sm"
						>
							{systemIds.map((id) => (
								<option key={id} value={id}>
									System {id}
								</option>
							))}
						</select>
					</>
				)}
			</div>
			{selectedSystemId !== null && (
				<SystemInspector
					key={`system-${selectedSystemId}-${selectedInspectorType}`}
					systemId={selectedSystemId}
					inspectorType={selectedInspectorType}
				/>
			)}
		</div>
	);
};
