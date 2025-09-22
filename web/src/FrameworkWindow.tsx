import React, { useEffect, useRef } from 'react';
import { useRetroPlug } from './contexts/RetroPlugContext';

interface FrameworkWindowProps {
	name: string;
	width?: number;
	height?: number;
	className?: string;
}

export const FrameworkWindow: React.FC<FrameworkWindowProps> = ({ name, width, height, className }) => {
	const { app } = useRetroPlug();
	const canvasRef = useRef<HTMLCanvasElement>(null);

	useEffect(() => {
		if (!canvasRef.current) {
			return;
		}

		canvasRef.current.id = `canvas-${name}`;
		const window = app.createNamedView(name, '#' + canvasRef.current.id);

		return () => {
			window?.delete();
		};
	}, [app, name]);

	return (
		<div className="canvas-container">
			<canvas
				className="canvas-element"
				ref={canvasRef}
				tabIndex={0}
			/>
		</div>
	);
};
