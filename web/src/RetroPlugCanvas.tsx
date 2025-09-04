import React, { useEffect, useRef } from "react";

import { useRetroPlug } from "./contexts/RetroPlugContext";
import { FrameworkCanvas } from "./FrameworkCanvas";
import { RetroPlugApplication } from "./RetroPlugApplication";

async function onDrop(event: DragEvent, app: RetroPlugApplication) {
	const project = app.project;

	const files = event.dataTransfer!.files;
	if (files.length > 1 && app) {
		const romData = new Uint8Array(await files[0].arrayBuffer());
		const savData = new Uint8Array(await files[1].arrayBuffer());

		project.addSystem({
			entries: {
				rom: { data: romData },
				sram: { data: savData },
			}
		});
	}
}

export const RetroPlugCanvas: React.FC = () => {
	const containerRef = useRef<HTMLDivElement>(null);
	const { app, audioContextState } = useRetroPlug();

	useEffect(() => {
		if (!app || !containerRef.current) return;

		const handleDragOver = (event: DragEvent) => {
			event.preventDefault();

			// Set cursor based on audio context state
			if (audioContextState !== 'running') {
				event.dataTransfer!.dropEffect = 'none';
			} else {
				event.dataTransfer!.dropEffect = 'copy';
			}
		};

		const handleDrop = async (event: DragEvent) => {
			event.preventDefault();

			// Only allow drop if audio context is running
			if (audioContextState !== 'running') {
				return;
			}

			onDrop(event, app);
		};

		containerRef.current.ondragover = handleDragOver;
		containerRef.current.ondrop = handleDrop;

		return () => {
			if (containerRef.current) {
				containerRef.current.ondragover = null;
				containerRef.current.ondrop = null;
			}
		};
	}, [app, audioContextState]);

	return (
		<div ref={containerRef}>
			<FrameworkCanvas />
		</div>
	);
};
