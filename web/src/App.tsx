import React, { ReactNode } from 'react';

import { Layout } from './components/Layout/Layout';
import { useRetroPlug } from './contexts/RetroPlugContext';
import { RetroPlugProvider } from './contexts/RetroPlugProvider';

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
			<LoadWrapper>
				<Layout />
			</LoadWrapper>
		</RetroPlugProvider>
	);
}

export default App;
