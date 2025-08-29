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

function App() {
	return (
		<RetroPlugProvider>
			<div className="app-container">
				<LoadSpinner />
				<RetroPlugCanvas />
			</div>
		</RetroPlugProvider>
	);
}

export default App;
