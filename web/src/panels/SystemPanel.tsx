import { useEffect, useState } from "react";
import { useDocument } from "../contexts/DocumentContext";
import { useProject, useSystemMemoryVersion } from "../hooks/RetroPlugHooks";
import { RetroPlugCanvas } from "../RetroPlugCanvas"
import { MemoryType } from "../wrapper/System";

export const SystemPanel: React.FC = () => {
	const { markDirty, updateDocument } = useDocument();
	const project = useProject();
	const [systemIds, setSystemIds] = useState<number[]>([]);
	const savVersion = useSystemMemoryVersion(systemIds.length > 0 ? systemIds[0] : null, MemoryType.Sram);

	useEffect(() => {
		const systems = project.getSystemIds();
		setSystemIds(systems);
		updateDocument({ title: project.getProjectName() })
	}, [project]);

	useEffect(() => {
		if (savVersion === null) return;
		markDirty();
	}, [savVersion]);

	return <div className="w-full h-full flex items-center justify-center">
		<RetroPlugCanvas />
	</div>
};
