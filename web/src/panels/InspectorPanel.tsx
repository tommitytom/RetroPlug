import { useEffect, useState } from "react";

import { useProject, useSystem, useSystemMemoryHash } from "../hooks/RetroPlugHooks";
import { Project } from "../wrapper/Project";
import { MemoryType, System } from "../wrapper/System";
import { LSDJ_KIT_COUNT } from "../wrapper/Lsdj";
import { useRetroPlug } from "../contexts/RetroPlugContext";

const LsdjSavInspector: React.FC<{ system: System | null }> = ({ system }) => {
	const [songName, setSongName] = useState("");
	const hash = useSystemMemoryHash(system, MemoryType.Sram);

	useEffect(() => {
		if (!system) return;

		const sav = system.lsdjSav;
		const name = sav.workingProject.getName();
		setSongName(name);
	}, [system, hash, setSongName]);

	return (
		<div>
			{hash}
			{songName}
		</div>
	);
};

interface IIndexedKit {
	id: number;
	name: string;
}

const LsdjRomInspector: React.FC<{ system: System | null }> = ({ system }) => {
	const hash = useSystemMemoryHash(system, MemoryType.Rom);
	const [kits, setKits] = useState<IIndexedKit[]>([]);

	useEffect(() => {
		if (!system) return;

		const rom = system.lsdjRom;
		const kits: IIndexedKit[] = [];

		for (let i = 0; i < LSDJ_KIT_COUNT; ++i) {
			const kit = rom.getKit(i);
			if (kit && kit.isValid) {
				kits.push({ id: i, name: kit.getName() });
			}
		}

		setKits(kits);
	}, [system, hash, setKits]);

	return (
		<div>
			{hash}
			{kits.map((kit) => (
				<div key={`${kit.name}-${kit.id}`}>{kit.name}</div>
			))}
		</div>
	);
};

const LsdjRamInspector: React.FC<{ system: System | null }> = ({ system }) => {
	const { app } = useRetroPlug();
	const hash = useSystemMemoryHash(system, MemoryType.Ram);
	const [pos, setPos] = useState<{ x: number; y: number } | null>(null);

	useEffect(() => {
		if (!system || !app) return;

		const view = app.view;
		const lsdjState = view.getLsdjState(system.id);
		view.delete();
		if (!lsdjState) return;

		const ram = system.getLsdjRam(lsdjState.ramOffsets);
		if (!ram) return;

		setPos({ x: ram.getCursorX(), y: ram.getCursorY() });
	}, [app, system, hash]);

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

const SystemInspector: React.FC<{
	project: Project | null;
	systemIdx: number;
}> = ({ project, systemIdx }) => {
	const system = useSystem(project, systemIdx);

	return (
		<div>
			{system?.romName}
			<LsdjSavInspector system={system} />
			<LsdjRamInspector system={system} />
		</div>
	);
};

export const InspectorPanel: React.FC = () => {
	const project = useProject();
	const [systemCount, setSystemCount] = useState<number>(0);

	useEffect(() => {
		if (!project) {
			setSystemCount(0);
			return;
		}

		setSystemCount(project.systemCount);
	}, [project]);

	return (
		<div>
			{Array.from({ length: systemCount }, (_, idx) => (
				<SystemInspector
					key={`system-${idx}`}
					project={project}
					systemIdx={idx}
				/>
			))}
		</div>
	);
};
