import React, { useEffect, useRef } from "react";

import { useRetroPlug } from "./contexts/RetroPlugContext";
import { FrameworkCanvas } from "./FrameworkCanvas";
import { RetroPlugApplication } from "./RetroPlugApplication";
import { type Uint8Buffer } from "./native/RetroPlug";

async function convertFile(app: RetroPlugApplication, file: File): Promise<Uint8Buffer> {
	const romData = new Uint8Array(await file.arrayBuffer());
	const romBuffer = new app.module!.Uint8Buffer(romData.byteLength);
	romBuffer.data().set(romData);
	return romBuffer;
}

async function onDrop(event: DragEvent, app: RetroPlugApplication) {
	const project = app.project;

	const files = event.dataTransfer!.files;
	if (files.length > 1 && app) {
		const rom = await convertFile(app, files[0]);
		const sav = await convertFile(app, files[1]);

		project.loadSystem(
			{
				desc: {
					paths: {
						romPath: "",
						sramPath: "",
						statePath: "",
					},
					settings: {
						includeRom: true,
						gameLink: false,
						reloadRomOnChange: true,
					},
				},
				romBuffer: rom,
				sramBuffer: sav,
				stateBuffer: null,
				stateType: app!.module!.SaveStateType.Sram,
				reset: false,
			},
		);
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
