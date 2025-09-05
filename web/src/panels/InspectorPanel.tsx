import { useEffect, useState } from "react";
import { HexEditor } from "hex-editor-react";
import "hex-editor-react/dist/hex-editor.css";

import { useProject, useSystemMemory, useSystemMemoryVersion } from "../hooks/RetroPlugHooks";
import { type SystemId } from "../wrapper/Project";
import { MemoryType } from "../wrapper/System";
import { LsdjRomInspector } from "./LsdjRomInspector";
import { toUint8Array } from "../utils/NativeUtil";

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

const SystemInspector: React.FC<{
	systemId: number;
}> = ({ systemId }) => {
	return (
		<div>
			<LsdjSavInspector systemId={systemId} />
			<LsdjRomInspector systemId={systemId} />
			<LsdjRamInspector systemId={systemId} />
		</div>
	);
};

export const InspectorPanel: React.FC = () => {
	const project = useProject();
	const [systemIds, setSystemIds] = useState<SystemId[]>([]);

	useEffect(() => {
		if (!project) {
			setSystemIds([]);
			return;
		}

		setSystemIds(project.getSystemIds());
	}, [project]);

	return (
		<div>
			{systemIds.map((id) => (
				<SystemInspector
					key={`system-${id}`}
					systemId={id}
				/>
			))}
		</div>
	);
};
