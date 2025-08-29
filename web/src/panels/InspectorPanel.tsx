import { useEffect, useState } from "react";
import { useRetroPlug } from "../contexts/RetroPlugContext";
import { AccessType, MemoryType, System } from "../wrapper/System";
import { useProject } from '../hooks/RetroPlugHooks';

const SystemInspector: React.FC<{ system: System }> = ({ system }) => {
	const { app } = useRetroPlug();
	const [songName, setSongName] = useState("");

	useEffect(() => {
		if (!app || !system) return;

		const systemMemory = system.getMemory(MemoryType.Sram, AccessType.Read);
		const sav = new app.module!.NativeLsdjSav(systemMemory.getBuffer());
		const name = sav.workingProject.getName();
		setSongName(name);
	}, [app, system]);

	return <div>{songName}</div>;
};

export const InspectorPanel: React.FC = () => {
	const project = useProject();
	const [systems, setSystems] = useState<System[]>([]);

	useEffect(() => {
		if (!project) {
			setSystems([]);
			return;
		}

		setSystems(Array.from(project.systems));
	}, [project]);

	return (
		<div>
			{systems.map((system, idx) => (
				<SystemInspector key={`system-${idx}`} system={system} />
			))}
		</div>
	);
};
