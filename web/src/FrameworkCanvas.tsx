import React, { useEffect, useRef } from 'react';
import { useRetroPlug } from './contexts/RetroPlugContext';

interface FrameworkCanvasProps {
	width?: number;
	height?: number;
	className?: string;
}

export const FrameworkCanvas: React.FC<FrameworkCanvasProps> = ({}) => {
	const { setCanvasId, audioContextState } = useRetroPlug();

	const canvasRef = useRef<HTMLCanvasElement>(null);
	useEffect(() => {
		if (canvasRef.current) {
			setCanvasId(canvasRef.current.id);
		}
	}, []);

	return (
		<div className="canvas-container">
			{audioContextState !== 'running' && (
				<div className="play-button-overlay">
					<button className="play-button">
						<svg viewBox="0 0 24 24" fill="currentColor">
							<path d="M8 5v14l11-7z"/>
						</svg>
						<span>Click to enable audio</span>
					</button>
				</div>
			)}
			<canvas
				className="canvas-element"
				ref={canvasRef}
				tabIndex={0}
				id="canvas"
			/>
		</div>
	);
};
