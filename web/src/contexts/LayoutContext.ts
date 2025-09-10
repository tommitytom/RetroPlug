import { createContext, useContext } from 'react';

interface LayoutContextType {
	switchToPanel: (zoneId: string, panelId: string) => void;
	addPanelToZone: (zoneId: string, panelId: string, makeActive?: boolean) => void;
}

export const LayoutContext = createContext<LayoutContextType | undefined>(undefined);

export function useLayout() {
	const context = useContext(LayoutContext);
	if (context === undefined) {
		throw new Error('useLayout must be used within a LayoutProvider');
	}
	return context;
}
