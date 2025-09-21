import React, { ReactNode } from 'react';

import { BrowserCompatibility } from './components/BrowserCompatibility';
import { Layout } from './components/Layout/Layout';
import { useRetroPlug } from './contexts/RetroPlugContext';
import { RetroPlugProvider } from './contexts/RetroPlugProvider';
import { ModalProvider } from './contexts/ModalProvider';

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
		<BrowserCompatibility>
			<RetroPlugProvider>
				<LoadWrapper>
					<ModalProvider>
						<Layout />
					</ModalProvider>
				</LoadWrapper>
			</RetroPlugProvider>
		</BrowserCompatibility>
	);
}

export default App;
