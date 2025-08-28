// App.tsx
import React, { useState } from 'react';
import GLFWCanvas from './GLFWCanvas';

function App() {
	const [canvasSize, setCanvasSize] = useState({ width: 800, height: 600 });

	const handleSizeChange = (dimension: 'width' | 'height', value: string) => {
		const size = parseInt(value, 10);
		if (!isNaN(size) && size > 0) {
			setCanvasSize(prev => ({
				...prev,
				[dimension]: size
			}));
		}
	};

	return (
		<div className="App">
			<GLFWCanvas
				width={canvasSize.width}
				height={canvasSize.height}
				className="main-canvas"
			/>
		</div>
	);
}

export default App;