// GLFWCanvas.tsx
import React, { useEffect, useRef, useState } from 'react';
import { RetroPlugApplication } from './RetroPlugApplication';

interface GLFWCanvasProps {
	width?: number;
	height?: number;
	className?: string;
}

const GLFWCanvas: React.FC<GLFWCanvasProps> = ({}) => {
	const canvasRef = useRef<HTMLCanvasElement>(null);
	const audioContextRef = useRef<AudioContext | null>(null);
	const [app, setApp] = useState<RetroPlugApplication | null>(null);
	const [isLoading, setIsLoading] = useState(true);

	const handleCanvasClick = () => {
		audioContextRef.current?.resume();
	};

	const handleDragOver = (event: React.DragEvent<HTMLCanvasElement>) => {
		event.preventDefault();
	};

	const handleDrop = async (event: React.DragEvent<HTMLCanvasElement>) => {
		console.log('drop');

		event.preventDefault();
		const files = event.dataTransfer.files;
		if (files.length > 0 && app) {
			const file = files[0];

			const data = new Uint8Array(await file.arrayBuffer());
			const buffer = new app.module!.Uint8Buffer(data.byteLength);
			buffer.data().set(data);

			const view = app!.view;
			console.assert(!!view);
			const project = view!.getProject();
			console.log(project);

			project.loadSystem(0x5A8EB011, {
				desc: {
					paths: {
						romPath: '',
						sramPath: '',
						statePath: '',
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
			}, 4294967295);
		}
	};

	useEffect(() => {
		audioContextRef.current = new AudioContext();

		let mounted = true;

		const pendingApp = new RetroPlugApplication();

		pendingApp.load().then(() => {
			if (mounted) {
				setIsLoading(false);
				try {
					pendingApp.setup('#canvas', audioContextRef.current);
				} catch (ex) {
					console.error('Error setting up WASM module:', ex);
				}
				setApp(pendingApp);
			}
		}).catch((err) => {
			console.error('Error loading WASM module:', err);
		});

		return () => {
			mounted = false;

			if (app) {
				app.destroy();
			}

			if (audioContextRef.current) {
				audioContextRef.current.close();
				audioContextRef.current = null;
			}
		};
	}, []);

	return (
		<div className="canvas-container">
			{isLoading && (
				<div className="loading-spinner-overlay">
					<div className="loading-spinner"></div>
					<div className="loading-text">Loading...</div>
				</div>
			)}
			<canvas
				ref={canvasRef}
				id="canvas"
				onClick={handleCanvasClick}
				onDragOver={handleDragOver}
				onDrop={handleDrop}
			/>
		</div>
	);
};

export default GLFWCanvas;
