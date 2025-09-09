import { createContext, useContext } from 'react';
import * as Comlink from 'comlink';

import { RetroPlugApplication } from '../RetroPlugApplication';
import { Project } from '../wrapper/Project';
import type { FileSystemWorkerAPI } from '../filesystem/FileSystemWorker';

interface RetroPlugContextType {
	canvasId: string | null;
	setCanvasId: (entityId: string | null) => void;
	focusCanvas: () => void;
	fileSystem: Comlink.Remote<FileSystemWorkerAPI>;
	isLoading: boolean;
	isReady: boolean;
	audioContext: AudioContext | null;
	audioContextState: AudioContextState;
	app: RetroPlugApplication;
	project: Project;
}

export const RetroPlugContext = createContext<RetroPlugContextType | undefined>(undefined);

export function useRetroPlug() {
	const context = useContext(RetroPlugContext);
	if (context === undefined) {
		throw new Error('useRetroPlug must be used within an RetroPlugProvider');
	}
	return context;
}
