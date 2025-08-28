// GLFWCanvas.tsx
import React, { useEffect, useRef, useState } from 'react';
import { RetroPlugApplication } from './RetroPlugApplication';

interface GLFWCanvasProps {
	width?: number;
	height?: number;
	className?: string;
}

const GLFWCanvas: React.FC<GLFWCanvasProps> = ({
	width = 800,
	height = 600,
	className = ''
}) => {
	const canvasRef = useRef<HTMLCanvasElement>(null);
	const audioContextRef = useRef<AudioContext | null>(null);
	const [app, setApp] = useState<RetroPlugApplication | null>(null);
	const [isLoading, setIsLoading] = useState(true);

	useEffect(() => {
		audioContextRef.current = new AudioContext();

		let mounted = true;

		const pendingApp = new RetroPlugApplication();

		pendingApp.load().then(() => {
			if (mounted) {
				pendingApp.setup('canvas', audioContextRef.current!);
				setApp(pendingApp);
			}
		});

		return () => {
			mounted = false;
			if (audioContextRef.current) {
				audioContextRef.current.close();
				audioContextRef.current = null;
			}
		};
	}, []);

	return (
		<div className={`glfw-container ${className}`}>
			<canvas
				ref={canvasRef}
				id="canvas"
				width={width}
				height={height}
				style={{
					display: isLoading ? 'none' : 'block',
					border: '1px solid #ccc'
				}}
			/>
		</div>
	);
};

export default GLFWCanvas;
