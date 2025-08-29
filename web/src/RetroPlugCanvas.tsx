import React, { useEffect, useRef } from "react";

import { useRetroPlug } from "./contexts/RetroPlugContext";
import { FrameworkCanvas } from "./FrameworkCanvas";
import { RetroPlugApplication } from "./RetroPlugApplication";

async function onDrop(event: DragEvent, app: RetroPlugApplication) {
	const project = app.view!.getProject()!;
	console.assert(!!project);

	const files = event.dataTransfer!.files;
	if (files.length > 0 && app) {
		const file = files[0];

		const data = new Uint8Array(await file.arrayBuffer());
		const buffer = new app.module!.Uint8Buffer(data.byteLength);
		buffer.data().set(data);

		const system = project!.loadSystem(
			0x5a8eb011,
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
				romBuffer: buffer,
				sramBuffer: null,
				stateBuffer: null,
				stateType: app!.module!.SaveStateType.Sram,
				reset: false,
			},
			4294967295
		);

		if (system) {
			const systemMemory = system.getMemory(
				app.module!.MemoryType.Sram,
				app.module!.AccessType.Read
			);
			const sav = new app.module!.LsdjSav(systemMemory.getBuffer());
			console.log("song valid:", sav.isValid);
		}
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
