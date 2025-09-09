import React, { ReactNode } from 'react';

import { type Panel, DockableEditor } from './components/DockableEditor';
import { useRetroPlug } from './contexts/RetroPlugContext';
import { RetroPlugProvider } from './contexts/RetroPlugProvider';
import { FileTreePanel } from './panels/FileTreePanel';
import { InspectorPanel } from './panels/InspectorPanel';
import { SystemPanel } from './panels/SystemPanel';

const panels: Panel[] = [
	{ id: 'system', title: 'System', content: <SystemPanel /> },
	{ id: 'inspector', title: 'Inspector', content: <InspectorPanel /> },
	{ id: 'fileTree', title: 'File Tree', content: <FileTreePanel /> },
];

const initialLayout = {
	left: { id: 'left', panels: ['fileTree'], activePanel: 'fileTree', size: 375 },
	center: { id: 'center', panels: ['system'], activePanel: 'system', size: 0 },
	right: { id: 'right', panels: ['inspector'], activePanel: 'inspector', size: 600 },
	bottom: { id: 'bottom', panels: [], activePanel: '', size: 200 },
};

function LoadSpinner() {
	return (
		<div className="loading-spinner-overlay">
			<div className="loading-spinner"></div>
			<div className="loading-text">Loading...</div>
		</div>
	);
}

const LoadWrapper: React.FC<{ children: ReactNode }> = ({ children }) => {
	const { isLoading } = useRetroPlug();

	if (!isLoading) {
		return <>{children}</>;
	} else {
		return <LoadSpinner />;
	}
};

function App() {
	return (
		<RetroPlugProvider>
			<div className="app-container">
				<LoadWrapper>
					<DockableEditor
						panels={panels}
						initialLayout={initialLayout}
						onLayoutChange={(layout) => {
							// You can save the layout to localStorage or send to a server
							//console.log('Layout changed:', layout);
						}}
					/>
				</LoadWrapper>
			</div>
		</RetroPlugProvider>
	);
}

export default App;
