import React, { ReactNode, useRef, useCallback } from 'react';
import { LayoutContext } from './LayoutContext';

interface LayoutProviderProps {
	children: ReactNode;
}

export interface LayoutRef {
	switchToPanel: (zoneId: string, panelId: string) => void;
	addPanelToZone: (zoneId: string, panelId: string, makeActive?: boolean) => void;
}

export const LayoutProvider: React.FC<LayoutProviderProps> = ({ children }) => {
	const layoutRef = useRef<LayoutRef | null>(null);

	const switchToPanel = useCallback((zoneId: string, panelId: string) => {
		if (layoutRef.current) {
			layoutRef.current.switchToPanel(zoneId, panelId);
		}
	}, []);

	const addPanelToZone = useCallback((zoneId: string, panelId: string, makeActive = true) => {
		if (layoutRef.current) {
			layoutRef.current.addPanelToZone(zoneId, panelId, makeActive);
		}
	}, []);

	const value = {
		switchToPanel,
		addPanelToZone,
	};

	return (
		<LayoutContext.Provider value={value}>
			{React.cloneElement(children as React.ReactElement, { layoutRef })}
		</LayoutContext.Provider>
	);
};
