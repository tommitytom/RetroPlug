import { useEffect, useState } from "react";

import { useProject, useSystem, useSystemMemoryHash } from "../hooks/RetroPlugHooks";
import { Project } from "../wrapper/Project";
import { MemoryType, System } from "../wrapper/System";

const LsdjSavInspector: React.FC<{ system: System | null }> = ({ system }) => {
	const [songName, setSongName] = useState("");
	const hash = useSystemMemoryHash(system, MemoryType.Sram);

	useEffect(() => {
		if (!system) return;

		const sav = system.lsdjSav;
		const name = sav.workingProject.getName();
		setSongName(name);
	}, [system, hash]);

	return (
		<div>
			{songName}
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
