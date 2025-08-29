import { type Panel, DockableEditor } from "./components/DockableEditor";
import { SystemPanel } from './panels/SystemPanel';
import { RetroPlugCanvas } from "./RetroPlugCanvas";
import { useRetroPlug } from "./contexts/RetroPlugContext";
import { RetroPlugProvider } from "./contexts/RetroPlugProvider";

function LoadSpinner() {
	const { isLoading } = useRetroPlug();

	return (
		isLoading && (
			<div className="loading-spinner-overlay">
				<div className="loading-spinner"></div>
				<div className="loading-text">Loading...</div>
			</div>
		)
	);
}

const panels: Panel[] = [
	{ id: 'system', title: 'System', content: <SystemPanel /> },
];

const initialLayout = {
	left: { id: 'left', panels: [], activePanel: '', size: 350 },
	center: { id: 'center', panels: ['system'], activePanel: 'system', size: 0 },
	right: { id: 'right', panels: [], activePanel: '', size: 350 },
	bottom: { id: 'bottom', panels: [], activePanel: '', size: 200 },
};

function App() {
	return (
		<RetroPlugProvider>
			<div className="app-container">
				<LoadSpinner />
				<DockableEditor
					panels={panels}
					initialLayout={initialLayout}
					onLayoutChange={(layout) => {
						// You can save the layout to localStorage or send to a server
						//console.log('Layout changed:', layout);
					}}
				/>
				<RetroPlugCanvas />
			</div>
		</RetroPlugProvider>
	);
}

export default App;
