import { createContext, useContext } from 'react';
import { RetroPlugApplication } from '../RetroPlugApplication';
import { Project } from '../wrapper/Project';

interface RetroPlugContextType {
	canvasId: string | null;
	setCanvasId: (entityId: string | null) => void;
	isLoading: boolean;
	isReady: boolean;
	audioContext: AudioContext | null;
	audioContextState: AudioContextState;
	app: RetroPlugApplication|null;
	project: Project | null;
}

export const RetroPlugContext = createContext<RetroPlugContextType | undefined>(undefined);

export function useRetroPlug() {
	const context = useContext(RetroPlugContext);
	if (context === undefined) {
		throw new Error('useRetroPlug must be used within an RetroPlugProvider');
	}
	return context;
}
