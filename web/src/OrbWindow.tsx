import React, { useEffect, useRef } from 'react';
import { useRetroPlug } from './contexts/RetroPlugContext';

interface OrbWindowProps {
	name: string;
	width?: number;
	height?: number;
	className?: string;
}

export const OrbWindow: React.FC<OrbWindowProps> = ({ name, width, height, className }) => {
	const { app } = useRetroPlug();
	const canvasRef = useRef<HTMLCanvasElement>(null);

	useEffect(() => {
		if (!canvasRef.current) {
			return;
		}

		canvasRef.current.id = `canvas-${name}`;
		const window = app.createNamedView(name, '#' + canvasRef.current.id);

		return () => {
			window?.requestClose();
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
