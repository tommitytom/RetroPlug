import { createContext, useContext } from 'react';
import { RetroPlugApplication } from '../RetroPlugApplication';

interface RetroPlugContextType {
	canvasId: string | null;
	setCanvasId: (entityId: string | null) => void;
	isLoading: boolean;
	audioContext: AudioContext | null;
	audioContextState: AudioContextState;
	app: RetroPlugApplication|null;
}

export const RetroPlugContext = createContext<RetroPlugContextType | undefined>(undefined);

export function useRetroPlug() {
	const context = useContext(RetroPlugContext);
	if (context === undefined) {
		throw new Error('useRetroPlug must be used within an RetroPlugProvider');
	}
	return context;
}
