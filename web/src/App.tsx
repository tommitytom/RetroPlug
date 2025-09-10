import React, { ReactNode, useState, useCallback } from 'react';

import { type Panel, DockableEditor, type DockLayout } from './components/DockableEditor';
import { useRetroPlug } from './contexts/RetroPlugContext';
import { RetroPlugProvider } from './contexts/RetroPlugProvider';
import { DocumentProvider } from './contexts/DocumentProvider';
import { FileTreePanel } from './panels/FileTreePanel';
import { InspectorPanel } from './panels/InspectorPanel';
import { SystemPanel } from './panels/SystemPanel';
import { TextDocumentPanel } from './panels/TextDocumentPanel';

const panels: Panel[] = [
	{ id: 'system', title: 'System', content: <SystemPanel /> },
	{ id: 'inspector', title: 'Inspector', content: <InspectorPanel /> },
	{ id: 'fileTree', title: 'File Tree', content: <FileTreePanel /> },
	{ id: 'textEditor', title: 'Text Editor', content: <TextDocumentPanel /> },
];

const initialLayout = {
	left: { id: 'left', panels: ['fileTree'], activePanel: 'fileTree', size: 375 },
	center: { id: 'center', panels: ['system'], activePanel: 'system', size: 0 },
	right: { id: 'right', panels: ['inspector'], activePanel: 'inspector', size: 600 },
	bottom: { id: 'bottom', panels: [], activePanel: '', size: 200 },
};

// Create a context for layout management
const LayoutContext = React.createContext<{
	switchToCenterPanel: (panelId: string) => void;
} | null>(null);

export const useLayoutControl = () => {
	const context = React.useContext(LayoutContext);
	if (!context) {
		throw new Error('useLayoutControl must be used within App component');
	}
	return context;
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
	const [currentLayout, setCurrentLayout] = useState<DockLayout>(initialLayout);

	const switchToCenterPanel = useCallback((panelId: string) => {
		setCurrentLayout(prev => {
			const newLayout = { ...prev };

			// Ensure the panel is in the center zone
			if (!newLayout.center.panels.includes(panelId)) {
				newLayout.center.panels = [...newLayout.center.panels, panelId];
			}

			// Set it as the active panel
			newLayout.center.activePanel = panelId;

			return newLayout;
		});
	}, []);

	const layoutContextValue = {
		switchToCenterPanel
	};

	return (
		<RetroPlugProvider>
			<DocumentProvider>
				<LayoutContext.Provider value={layoutContextValue}>
					<div className="app-container">
						<LoadWrapper>
							<DockableEditor
								panels={panels}
								initialLayout={currentLayout}
								onLayoutChange={(layout) => {
									setCurrentLayout(layout);
								}}
							/>
						</LoadWrapper>
					</div>
				</LayoutContext.Provider>
			</DocumentProvider>
		</RetroPlugProvider>
	);
}

export default App;
